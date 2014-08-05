/* *****************************************************************************
 * Copyright (c) 2013-2014 Qingtao Cao [harry.cao@nextdc.com]
 *
 * This file is part of oBIX.
 *
 * oBIX is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * oBIX is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with oBIX.  If not, see <http://www.gnu.org/licenses/>.
 *
 * *****************************************************************************/
/*
 * A super oBIX watch subsystem which provides fantastic scalability,
 * flexibility and performance. By far it supports the following major
 * features:
 *	. no limitation on the number of watches
 *	. no limitation on the number of objects monitored by one watch
 *	. no limitation on the number of oBIX clients sharing one watch
 *	. multiple watches able to monitor one same object, in particular,
 *	  nested watches installed at different levels in one subtree
 *	. long poll mechanism
 *	. support parallelism and thread safe, specifically, multiple long
 *	  poll threads handling poll tasks simultaneously to yield minimal
 *	  latency.
 *	. recyclable watch IDs, no worries about watch ID counter's overflow
 *	  (by manipulating extensible bitmap nodes)
 */

#include <assert.h>
#include <string.h>
#include <pthread.h>
#include <time.h>			/* clock_gettime */
#include <errno.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include "obix_request.h"
#include "xml_storage.h"
#include "ptask.h"
#include "list.h"
#include "libxml_config.h"
#include "log_utils.h"
#include "obix_utils.h"
#include "server.h"
#include "xml_utils.h"
#include "bitmap.h"
#include "watch.h"

/*
 * Descriptor of all watch objects on the oBIX server
 */
typedef struct watch_set {
	/* The bitmap to get ID for the next watch object, starting from 0 */
	bitmap_t *map;

	/* The daemon to lease idle watch objects */
	Task_Thread *lease_thread;

	/* All watch objects' queue */
	struct list_head watches;

	/* The mutex to protect the whole data structure */
	pthread_mutex_t mutex;
} watch_set_t;


/*
 * Descriptor of the watch object
 */
typedef struct watch {
	/* ID of the current watch object */
	int id;

	/*
	 * Absolute URI of the current watch object, which will also
	 * be used to set the HTTP Content-Location header in the
	 * response to pollChanges and pollRefresh requests
	 */
	char *uri;

	/* Pointing to watch object's DOM node */
	xmlNode *node;

	/*
	 * Indicator of whether changes took place on any DOM nodes
	 * monitored by this watch
	 */
	int changed;

	/*
	 * Indicator of whether any threads(handling Watch.Delete requests or
	 * leasing unattended watches) would like to delete the watch.
	 *
	 * Used to synchronise among deletion threads.
	 */
	int is_shutdown;

	/*
	 * The reference count of current watch object, any thread except
	 * the lease thread that holds a pointer to current watch object
	 * will have its reference count increased by 1.
	 *
	 * Note,
	 * 1. Any watch user must protect itself by calling get_watch(or
	 * the like) before accessing it and put_watch(or the like) once
	 * is done with it, so as to prevent the watch from being deleted
	 * by other thread.
	 */
	int refcnt;

	/*
	 * Polling tasks on this watch object. Multiple oBIX clients
	 * may like to share one same watch object.
	 */
	struct list_head tasks;

	/*
	 * The number of poll tasks for this watch. The changes indicators
	 * in both watch and watch item should not be reset unless for the
	 * last poll task otherwise only the first poll task could harvest
	 * the changes
	 */
	int tasks_count;

	/* The task ID of this watch object in the task queue of lease thread */
	int lease_tid;

	/* Queue of monitored objects */
	struct list_head items;

	/* Joining all watches list */
	struct list_head list;

	/* Waiting queue between the deletion request and polling thread */
	pthread_cond_t wq;

	/*
	 * The mutex to protect the whole data structure
	 *
	 * Note,
	 * 1. If unable to avoid the situation to grab more than one
	 * mutex, ALWAYS grab a watch's mutex first before that of
	 * global backlog or watchset to avoid ABBA type of deadlock
	 */
	pthread_mutex_t mutex;
} watch_t;

/*
 * Descriptor of a monitored object for a watch object
 */
typedef struct watch_item {
	/* The absolute URI/HREF of the monitored object */
	char *uri;

	/* Pointing to the monitored object in the DOM tree */
	xmlNode *node;

	/*
	 * Pointing to the meta node installed by this watch object
	 * as children of the monitored object
	 */
	xmlNode *meta;

	/*
	 * Counter of changes since last watch.longPoll request, if greater
	 * than 1, then the changes may have not been polled/collected in
	 * a timely manner
	 */
	int count;

	/* Joining the queue of watch->items */
	struct list_head list;
} watch_item_t;


/**
 * Descriptor of the backlog of all pending poll tasks
 */
typedef struct poll_backlog {
	/* The number of polling threads */
	int num_threads;

	/* The shutting down flag */
	int is_shutdown;

	/* The fleet of polling threads as consumer of this backlog */
	pthread_t *poll_threads;

	/*
	 * Queue of all pending poll tasks, organized in expiry ascending order
	 * Producer: Watch.PollChanges handler
	 */
	struct list_head list_all;

	/*
	 * Queue of all active poll tasks that should be attended immediately
	 * Producer:
	 *	1. obix_server_write(), when a watched upon DOM node is changed
	 *	2. delete_watch_task(), before a watch could be deleted
	 */
	struct list_head list_active;

	/*
	 * Mutex to protect the whole data structure
	 *
	 * Note,
	 * 1. Since list_active will be attended before list_all,
	 * no extra lock needed to access them in parallel
	 * 2. If this lock becomes a bottleneck, try to draw on
	 * from Linux kernel timer implementation
	 */
	pthread_mutex_t mutex;

	/*
	 * The wait queue where poll threads sleep on when no tasks
	 * need to be attended
	 */
	pthread_cond_t wq;
} poll_backlog_t;

/*
 * Descriptor of a polling task which is handled sometime in the future
 */
typedef struct poll_task {
	/* Accompanied watch object */
	watch_t *watch;

	/*
	 * When this task should be handled in the future.
	 *
	 * Note,
	 * 1. Nanosecond precision rather than second precision
	 * should be adopted or otherwise pthread_cond_timedwait()
	 * could not return precisely but 1 second prematurely
	 */
	struct timespec expiry;

	/* Relevant response object */
	obix_request_t *request;

	/* The obix:watchOut contract returned to requester */
	xmlNode *watch_out;

	/* Joining accompanied watch's tasks queue */
	struct list_head list_watch;

	/* Joining all tasks queue */
	struct list_head list_all;

	/*
	 * Also joining active tasks queue, if any change occurred
	 * on one of DOM nodes monitored by accompanied watch
	 */
	struct list_head list_active;
} poll_task_t;

static poll_backlog_t *backlog;
static watch_set_t *watchset;

/**
 * Error codes used in watch subsystem
 */
enum {
	ERR_NO_WATCHOBJ = 0,
	ERR_NO_WATCHOUT,
	ERR_NO_NULLOBJ,
	ERR_NO_POLLTASK,
	ERR_NO_MEM
};

static err_msg_t watch_err_msg[] = {
	[ERR_NO_WATCHOBJ] = {
		.type = OBIX_CONTRACT_ERR_BAD_URI,
		.msgs = "No watch available at this URI"
	},
	[ERR_NO_WATCHOUT] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "Failed to allocate a watchOut object"
	},
	[ERR_NO_NULLOBJ] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "Failed to allocate a NULL object"
	},
	[ERR_NO_POLLTASK] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "Failed to allocate a polling task"
	},
	[ERR_NO_MEM] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "Failed to allocate a watch object"
	}
};

/* 2^32 = 4,294,967,296, containing 10 digits */
#define WATCH_ID_MAX_BITS	10

/* The default lease time, in millisecond, of a watch object */
#define WATCH_LEASE_DEF		(24*60*60*1000)

/* Template of the watch object URI */
static const char *WATCH_URI_TEMPLATE = "/obix/watchService/%d/watch%d/";
static const char *WATCH_URI_PREFIX = "/obix/watchService/";
static const char *WATCH_ID_PREFIX = "watch";

/*
 * The number of watch objects within one parent folder, used to
 * help setup a simple 2-level hierarchy under watchService so as to
 * help cut down the overhead of finding an existing watch object.
 *
 * Suppose this value is 64, then all watches will be organized
 * under watch service in the following manner:
 *
 *		watchService/0/watch0 ~ watch63
 *		watchService/1/watch64 ~ watch127
 *		watchService/2/watch128 ~ watch191
 *		...
 */
static const int MAX_WATCHES_PER_FOLDER = 64;

/* Names used in watch contract definition */
static const char *WATCH_MIN = "min";
static const char *WATCH_MAX = "max";
static const char *WATCH_LEASE = "lease";

/*
 * Patterns used by xmlXPathEval to identify a number of
 * matching nodes in a given DOM tree
 */

/*
 * Any watch meta children of the given node
 *
 * Note,
 * 1. Only the subtree of the current node instead of the global
 * DOM tree should be searched upon
 */
static const char *XP_WATCH_METAS = "./meta[@watch_id]";

/*
 * Any ancestor node including itself that have at least one
 * watch meta installed
 */
static const char *XP_WATCH_ANCESTOR_OR_SELF =
"./ancestor-or-self::*[child::meta[@watch_id]]";

/*
 * Any descendant node including itself that have at least one
 * watch meta installed
 */
static const char *XP_WATCH_DESCENDANT_OR_SELF =
"./descendant-or-self::*[child::meta[@watch_id]]";

/*
 * Any children contained in this hierarchy:
 *
 *	<obj is="obix:WatchIn">
 *		<list names="hrefs">
 *
 * Note,
 * 1. Since the client will provide a WatchIn document,
 * the search should start from the root of this document
 */
static const char *XP_WATCH_IN =
"/obj[@is='obix:WatchIn']/list[@names='hrefs']/*";

/*
 * The lease node of the current watch object, as in
 *
 *	<reltime name="lease"/>
 *
 * Note,
 * 1. Since the watch object is in the global DOM tree and
 * context->node has been set as the watch object, the xpath
 * expression should use a relative format
 */
static const char *XP_WATCH_LEASE =
"./reltime[@name='lease']";

/*
 * The minimal poll wait interval setting of the current watch object
 */
static const char *XP_WATCH_PWI_MIN =
"./obj[@name='pollWaitInterval']/reltime[@name='min']";

/*
 * The maximal poll wait interval setting of the current watch object
 */
static const char *XP_WATCH_PWI_MAX =
"./obj[@name='pollWaitInterval']/reltime[@name='max']";

/*
 * Predict to match the val attribute from the poll-thread-count setting,
 * which is the number of server threads spawned for non-polling tasks
 */
static const char *XP_POLL_THREAD_COUNT = "/config/poll-thread-count/@val";

static void *poll_thread_task(void *arg);

/**
 * Find a watch item that monitors the object with the specified
 * URI in the given watch object
 *
 * Return the watch item pointer on success, NULL if not found.
 *
 * Note,
 * 1. Callers should have grabbed watch->mutex
 */
static watch_item_t *__get_watch_item(watch_t *watch, const char *uri)
{
	watch_item_t *item;

	list_for_each_entry(item, &watch->items, list) {
		if (str_is_identical(item->uri, uri) == 0) {
			return item;
		}
	}

	return NULL;
}

/**
 * Find a watch item that monitors the object with the specified
 * URI or its parent in the given watch object.
 *
 * Return the watch item pointer on success, NULL if not found.
 *
 * Note,
 * 1. Callers should have grabbed watch->mutex
 */
static watch_item_t *__get_watch_item_or_parent(watch_t *watch, const char *uri)
{
	watch_item_t *item;

	list_for_each_entry(item, &watch->items, list) {
		if (str_is_identical(item->uri, uri) == 0 ||
			strncmp(item->uri, uri, strlen(item->uri)) == 0) {
			return item;
		}
	}

	return NULL;
}

static watch_item_t *get_watch_item_or_parent(watch_t *watch, const char *uri)
{
	watch_item_t *item;

	pthread_mutex_lock(&watch->mutex);
	item = __get_watch_item_or_parent(watch, uri);
	pthread_mutex_unlock(&watch->mutex);

	return item;
}

/**
 * Get the descriptor of a watch object with the specified ID
 */
static watch_t *get_watch_helper(long id)
{
	watch_t *watch;

	pthread_mutex_lock(&watchset->mutex);
	list_for_each_entry(watch, &watchset->watches, list) {
		if (watch->id == id) {
			/* Increase refcnt before returning a reference */
			watch->refcnt++;

#ifdef DEBUG_REFCNT
			log_debug("[%u] Watch%d refcnt increased to %d",
					  get_tid(), watch->id, watch->refcnt);
#endif

			pthread_mutex_unlock(&watchset->mutex);
			return watch;
		}
	}

	pthread_mutex_unlock(&watchset->mutex);
	return NULL;
}

static void __put_watch(watch_t *);
static void put_watch(watch_t *);

/**
 * Insert all poll tasks related with one watch into the
 * list_active queue, if they has not been appended there yet
 *
 * Note,
 * 1. Polling threads only work on backlog->list_active queue or
 * the expired tasks at the beginning of backlog->list_all queue,
 * it's more efficient to add poll tasks to list_active queue
 * than to have them expired and moved to beginning of list_all.
 * 2. Callers should hold watch->mutex
 */
static void __notify_watch_tasks(watch_t *watch)
{
	poll_task_t *task;

	if (list_empty(&watch->tasks) == 0) {
		pthread_mutex_lock(&backlog->mutex);
		list_for_each_entry(task, &watch->tasks, list_watch) {
			if (task->list_active.prev == &task->list_active) {
				list_add_tail(&task->list_active, &backlog->list_active);
			}
		}
		pthread_cond_signal(&backlog->wq);
		pthread_mutex_unlock(&backlog->mutex);
	}
}

/**
 * Notify a watch object of the change event on the given node
 */
static void xmldb_notify_watch(xmlNode *meta, xmlNode *parent, WATCH_EVT event)
{
	xmlChar *uri;
	watch_t *watch;
	watch_item_t *item;
	long id;

	assert(meta && parent);

	/* Sanity check of a watch meta */
	if (xmlStrcmp(meta->name, BAD_CAST OBIX_OBJ_META) != 0) {
		log_error("Not a watch meta");
		return;
	}

	if ((id = xml_get_long(meta, OBIX_META_ATTR_WATCH_ID)) < 0) {
		log_error("Failed to get watch ID from relevant watch meta");
		return;
	}

	if (!(watch = get_watch_helper(id))) {
		log_warning("No watch descriptor found for watch%d", id);
		return;
	}

	if (!(uri = xmldb_node_path(parent))) {
		log_warning("Failed to get absolute URI of the monitored node");
		return;
	}

	/*
	 * Find and change a matching watch_item_t should be done
	 * in an atomic manner
	 */
	pthread_mutex_lock(&watch->mutex);
	if (!(item = __get_watch_item(watch, (const char *)uri))) {
		log_warning("%s has not watched upon %s", watch->uri, uri);
	} else {
		if (event == WATCH_EVT_NODE_DELETED) {
			item->node = item->meta = NULL;
		}

		item->count++;
		watch->changed = 1;
		__notify_watch_tasks(watch);

		log_debug("[%u] Notified watch%d of %s", get_tid(),
				  watch->id, item->uri);
	}

	/* Decrease the watch reference count now that it has been dealt with */
	__put_watch(watch);
	pthread_mutex_unlock(&watch->mutex);

	xmlFree(uri);
}

static void xmldb_notify_watch_wrapper(xmlNode *meta, void *arg1, void *arg2)
{
	xmlNode *parent = (xmlNode *)arg1;
	WATCH_EVT event = *(WATCH_EVT *)arg2;

	xmldb_notify_watch(meta, parent, event);
}

/**
 * Notify all watches that may have been monitoring
 * the same given node
 */
static void xmldb_notify_watches_helper(xmlNode *targeted, void *arg1, void *arg2)
{
	/* arg1 is ignored */

	xml_xpath_for_each_item(targeted, XP_WATCH_METAS,
							xmldb_notify_watch_wrapper, targeted, arg2);
}

/**
 * Notify relevant watches of what had happened
 */
void xmldb_notify_watches(xmlNode *node, WATCH_EVT event)
{
	const char *xp = NULL;

	switch (event) {
	case WATCH_EVT_NODE_CHANGED:
		xp = XP_WATCH_ANCESTOR_OR_SELF;
		break;
	case WATCH_EVT_NODE_DELETED:
		xp = XP_WATCH_DESCENDANT_OR_SELF;
		break;
	default:
		log_error("Failed to notify watches due to bad event");
		break;
	}

	if (xp) {
		xml_xpath_for_each_item(node, xp, xmldb_notify_watches_helper,
								NULL, (void *)&event);
	}
}

xmlNode *xmldb_put_watch_meta(xmlNode *node, int watch_id)
{
	xmlNode *meta;
	char buf[WATCH_ID_MAX_BITS];

	sprintf(buf, "%d", watch_id);

	/* Already existing */
	if ((meta = xml_find_child(node, OBIX_OBJ_META,
							   OBIX_META_ATTR_WATCH_ID, buf)) != NULL) {
		return meta;
	}

	if (!(meta = xmlNewDocNode(_storage, NULL, BAD_CAST OBIX_OBJ_META, NULL))) {
		return NULL;
	}

	if (xmlSetProp(meta, BAD_CAST OBIX_META_ATTR_WATCH_ID,
				   BAD_CAST buf) == NULL ||
		xmlAddChild(node, meta) == NULL) {
		xmlFreeNode(meta);
		meta = NULL;
	}

	return meta;
}

/*
 * Delete a watch meta node and release the watch item descriptor
 *
 * Note,
 * 1. Callers should have held watch->mutex.
 */
static void __delete_watch_item(watch_item_t *item)
{
	/*
	 * The watch meta node may have been deleted along with
	 * the monitored object
	 */
	if (item->meta) {
		xmldb_delete_node(item->meta, 0);
	}

	free(item->uri);
	free(item);
}

/**
 * Delete the specified watch item from the watch object
 */
static void delete_watch_item(xmlNode *node, void *arg1, void *arg2)
{
	watch_t *watch = (watch_t *)arg1;	/* arg2 is ignored */
	watch_item_t *item;
	char *uri;

	assert(node && watch);

	if (!(uri = (char *)xmlGetProp(node, BAD_CAST OBIX_ATTR_VAL))) {
		log_error("The current sub-node of watchIn contract doesn't contain "
				  "a valid val attribute");
		return;
	}

	pthread_mutex_lock(&watch->mutex);
	list_for_each_entry(item, &watch->items, list) {
		if (str_is_identical(item->uri, uri) == 0) {
			list_del(&item->list);
			__delete_watch_item(item);
			log_debug("Item for %s deleted from watch%d", uri, watch->id);
			break;
		}
	}
	pthread_mutex_unlock(&watch->mutex);

	free(uri);
}

static watch_item_t *create_watch_item(watch_t *watch, const char *uri)
{
	watch_item_t *item;
	xmlNode *node;

	assert(watch && uri);

	if ((item = get_watch_item_or_parent(watch, uri)) != NULL) {
		log_debug("watch%d already monitoring %s or its parent",
				  watch->id, uri);
		return item;
	}

	/*
	 * Before creating watch item and relevant meta object, check sanity
	 * of the watched upon object in the DOM tree which should not be
	 * an operation node
	 */
	if (!(node = xmldb_get_node(BAD_CAST uri))) {
		log_error("The watched upon object of %s doesn't exist", uri);
		return NULL;
	}

	if (xmlStrcmp(node->name, BAD_CAST OBIX_OBJ_OP) == 0) {
		log_error("Unable to watch upon an operation node");
		return NULL;
	}

	if (!(item = (watch_item_t *)malloc(sizeof(watch_item_t)))) {
		log_error("Failed to allocate watch_item_t");
		return NULL;
	}
	memset(item, 0, sizeof(watch_item_t));

	if (!(item->uri = (char *)malloc(strlen(uri) + 1))) {
		log_error("Failed to duplicate uri %s", uri);
		goto failed;
	}
	strcpy(item->uri, uri);

	item->node = node;
	INIT_LIST_HEAD(&item->list);

	if (!(item->meta = xmldb_put_watch_meta(item->node, watch->id))) {
		log_error("Failed to install a meta node under %s for watch %d",
					uri, watch->id);
		goto dom_failed;
	}

	pthread_mutex_lock(&watch->mutex);
	list_add_tail(&item->list, &watch->items);
	pthread_mutex_unlock(&watch->mutex);

	log_debug("Item for %s created for watch%d", uri, watch->id);

	return item;

dom_failed:
	free(item->uri);

failed:
	free(item);
	return NULL;
}

/**
 * Populate the obix:watchOut contract with the watched upon object.
 *
 * Return 0 on success, < 0 otherwise.
 *
 * Note:
 * 1. All copied nodes under watchOut/list will be released along with
 * the whole watchOut contract once its content have been sent back
 * to client. Since the copies are orphans, releasing them won't impact
 * the global DOM tree at all.
 *
 * 2. Callers should have held relevant watch->mutex.
 *
 * 3. Set href in watchOut contract absolute so as to better indicate
 * the monitored object.
 */
static int fill_watch_out(xmlNode *watch_out, watch_item_t *item)
{
	xmlNode *node;

	assert(watch_out && item);

	if (!(node = xmldb_copy_node(item->node, XML_COPY_EXCLUDE_META))) {
		log_error("Failed to copy node at %s into watchOut contract",
				  item->uri);
		return -1;
	}

	if (xmlSetProp(node, BAD_CAST OBIX_ATTR_HREF, BAD_CAST item->uri) == NULL) {
		log_error("Failed to set absolute href in watchOut contract");
		goto failed;
	}

	/*
	 * Remove the hidden attribute so as to have the watched upon object
	 * properly displayed in the response
	 */
	xmlUnsetProp(node, BAD_CAST OBIX_ATTR_HIDDEN);

	/*
	 * All monitored object will be added as children of the list object
	 * in the watchOut contract
	 */
	if (xmlAddChild(watch_out->children, node) == NULL) {
		log_error("Failed to add copied node at %s to watchOut contract",
				  item->uri);
	} else {
		return 0;
	}

	/* Fall through */

failed:
	xmlFreeNode(node);
	return -1;
}

/**
 * Create a watch item for the given watch object to monitor the object
 * as specified by the current sub-node of an obix:watchIn contract,
 * also fill in the watchOut contract with the content of the monitored
 * object.
 */
static void create_watch_item_wrapper(xmlNode *node, void *arg1, void *arg2)
{
	watch_t *watch = (watch_t *)arg1;
	xmlNode *watch_out = (xmlNode *)arg2;
	watch_item_t *item;
	xmlChar *uri;

	if (!(uri = xmlGetProp(node, BAD_CAST OBIX_ATTR_VAL))) {
		log_error("The current sub-node of watchIn contract doesn't contain "
				  "a valid val attribute");
		return;
	}

	if (!(item = create_watch_item(watch, (const char *)uri))) {
		log_error("Failed to create watch_item_t and meta tag for %s", uri);
		xmlFree(uri);
		return;
	}
	xmlFree(uri);

	fill_watch_out(watch_out, item);
}

/**
 * Get the value of the val attribute of a obix:reltime node
 */
static void get_time_helper(xmlNode *node, void *arg1, void *arg2)
{
	long *time = (long *)arg1;	/* arg2 is ignored */
	xmlChar *val;
	int error;

	if (xmlStrcmp(node->name, BAD_CAST OBIX_OBJ_RELTIME) != 0 ||
		!(val = xmlGetProp(node, BAD_CAST OBIX_ATTR_VAL))) {
		return;
	}

	error = obix_reltime_parseToLong((const char *)val, time);
	xmlFree(val);

	if (error != 0) {
		*time = 0;
	}
}

/**
 * Get time relevant settings of the given watch object.
 *
 * Note,
 * 1. A watch's lease time should solely be determined by
 * oBIX server's watch subsystem without any involvement of
 * its user, and it will be reset whenever the watch has been
 * accessed with a valid parameter.
 */
static void get_time(xmlNode *watch_node, const char *name, long *time)
{
	/*
	 * Just in case no matching node found or error occurs
	 */
	*time = 0;

	if (strcmp(name, WATCH_MIN) == 0) {
		xml_xpath_for_each_item(watch_node, XP_WATCH_PWI_MIN,
								get_time_helper, time, NULL);
	} else if (strcmp(name, WATCH_MAX) == 0) {
		xml_xpath_for_each_item(watch_node, XP_WATCH_PWI_MAX,
								get_time_helper, time, NULL);
	} else if (strcmp(name, WATCH_LEASE) == 0) {
		xml_xpath_for_each_item(watch_node, XP_WATCH_LEASE,
								get_time_helper, time, NULL);

		if (*time == 0) {
			*time = WATCH_LEASE_DEF;
		}
	}
}

/**
 * Delete an already dequeued watch object
 *
 * If the watch's reference count is not 0, other threads are accessing
 * it as well, the current thread(either handling Watch.Delete
 * requests or the lease thread) needs to wait until others are done
 * with the watch before it can be safely removed.
 *
 * Since long poll mechanism has not been stipulated by oBIX spec at all,
 * it should not impose side-effect on the attempt to delete existing
 * watch. To this end, polling threads are waken up to handle poll tasks
 * associated with the to-be-deleted watch as soon as possible.
 */
static void delete_watch_helper(watch_t *watch)
{
	watch_item_t *item, *n;

	pthread_mutex_lock(&watch->mutex);
	while (watch->refcnt > 0) {
#ifdef DEBUG_REFCNT
		log_debug("[%u] Needs to wait until Watch%d refcnt(%d) dropped to 0",
				  get_tid(), watch->id, watch->refcnt);
#endif

		__notify_watch_tasks(watch);
		pthread_cond_wait(&watch->wq, &watch->mutex);
	}

	/* Free every watch item descriptor and relevant meta tag */
	list_for_each_entry_safe(item, n, &watch->items, list) {
		list_del(&item->list);
		__delete_watch_item(item);
	}
	pthread_mutex_unlock(&watch->mutex);

	xmldb_delete_node(watch->node, DOM_DELETE_EMPTY_PARENT);

	pthread_mutex_destroy(&watch->mutex);
	pthread_cond_destroy(&watch->wq);

	bitmap_put_id(watchset->map, watch->id);

	free(watch->uri);
	free(watch);
}

/**
 * The payload of watch lease tasks run by the lease thread
 *
 * If there is any thread that has started handling Watch.Delete
 * request, have THAT thread delete the watch on behalf of us.
 * Return immediately so that THAT thread could return from
 * ptask_cancel(..., TRUE) as soon as possible. BTW, this watch
 * have been dequeued by THAT thread already.
 *
 * Note,
 * 1. Thanks to the fact that threads handling Watch.Delete requests
 * will wait for relevant lease tasks to be finished in the first place,
 * it's safe for the latter to hold a pointer to relevant watch object.
 */
static void delete_watch_task(void *arg)
{
	watch_t *watch = (watch_t *)arg;

	pthread_mutex_lock(&watch->mutex);
	if (watch->is_shutdown == 1) {
		pthread_mutex_unlock(&watch->mutex);
		return;
	}
	pthread_mutex_unlock(&watch->mutex);

	/* Otherwise, delete the watch by ourselves */
	pthread_mutex_lock(&watchset->mutex);
	list_del(&watch->list);
	pthread_mutex_unlock(&watchset->mutex);

	delete_watch_helper(watch);
}

static watch_t *create_watch(void)
{
	watch_t *watch;
	long lease;

	if (!(watch = (watch_t *)malloc(sizeof(watch_t)))) {
		log_error("Failed to allocate watch_t");
		return NULL;
	}
	memset(watch, 0, sizeof(watch_t));

	if ((watch->id = bitmap_get_id(watchset->map)) < 0) {
		log_error("Failed to get an ID for current watch object");
		goto failed;
	}

	if (!(watch->uri = (char *)malloc(strlen(WATCH_URI_TEMPLATE) +
									  (WATCH_ID_MAX_BITS - 2) * 2 + 1))) {
		log_error("Failed to allocate URI string for a watch object");
		goto failed;
	}
	sprintf(watch->uri, WATCH_URI_TEMPLATE,
			watch->id / MAX_WATCHES_PER_FOLDER, watch->id);

	if (!(watch->node = xmldb_copy_uri(OBIX_SYS_WATCH_STUB))) {
		log_error("Failed to copy a watch contract from the DOM tree");
		goto copy_node_failed;
	}

	if ((xmlSetProp(watch->node, BAD_CAST OBIX_ATTR_HREF,
					BAD_CAST watch->uri) == NULL)) {
		log_error("Failed to set watch's href as %s", watch->uri);
		goto dom_failed;
	}

	/*
	 * Automatically create any missing ancestors for watch objects,
	 * since there are a 2-level hierarchy for watch service and
	 * their direct parent nodes may not exist yet
	 */
	if (xmldb_put_node(watch->node, DOM_CREATE_ANCESTORS) != 0) {
		log_error("Failed to register %s node to DOM tree", watch->uri);
		goto dom_failed;
	}

	get_time(watch->node, WATCH_LEASE, &lease);
	INIT_LIST_HEAD(&watch->tasks);
	INIT_LIST_HEAD(&watch->items);
	INIT_LIST_HEAD(&watch->list);
	pthread_mutex_init(&watch->mutex, NULL);
	pthread_cond_init(&watch->wq, NULL);

	watch->lease_tid = ptask_schedule(watchset->lease_thread,
									  delete_watch_task,
									  watch, lease, 1);
	if (watch->lease_tid < 0) {
		log_error("Failed to register a lease task for %s", watch->uri);
		delete_watch_helper(watch);
		return NULL;
	}

	/* Finally, enlist the new watch into global list */
	pthread_mutex_lock(&watchset->mutex);
	list_add_tail(&watch->list, &watchset->watches);
	pthread_mutex_unlock(&watchset->mutex);

	return watch;

dom_failed:
	xmlFreeNode(watch->node);

copy_node_failed:
	free(watch->uri);

failed:
	if (watch->id >= 0) {
		bitmap_put_id(watchset->map, watch->id);
	}

	free(watch);
	return NULL;
}

static void reset_lease_time(watch_t *watch)
{
	if (watch->lease_tid > 0) {
		ptask_reset(watchset->lease_thread, watch->lease_tid);
	}
}

/**
 * Get the ID of the given watch object
 *
 * Return >=0 on success, -1 on failure
 */
static int get_watch_id(const char *uri)
{
	int len = strlen(WATCH_URI_PREFIX);
	char *start;

	assert(uri);

	if (strncmp(uri, WATCH_URI_PREFIX, len) != 0) {
		return -1;
	}
	uri += len;

	if (!(start = strstr(uri, WATCH_ID_PREFIX))) {
		return -1;
	}
	start += strlen(WATCH_ID_PREFIX);

	return str_to_long(start);
}

/**
 * Get the descriptor of a watch object with the specified URI
 * and increase its reference count
 *
 * Return the watch descriptor on success, NULL otherwise
 */
static watch_t *get_watch(const char *uri)
{
	int id = get_watch_id(uri);

	return (id >= 0) ? get_watch_helper(id) : NULL;
}

/**
 * Decrease the watches reference count and signal potential
 * deletion thread
 *
 * Note,
 * 1. Callers should hold watch->mutex
 */
static void __put_watch(watch_t *watch)
{
	if (--watch->refcnt == 0) {
		pthread_cond_signal(&watch->wq);
	}

#ifdef DEBUG_REFCNT
	log_debug("[%u] Watch%d refcnt decreased to %d",
			  get_tid(), watch->id, watch->refcnt);
#endif
}

/**
 * Decrease the watches reference count and signal potential
 * deletion thread
 */
static void put_watch(watch_t *watch)
{
	pthread_mutex_lock(&watch->mutex);
	__put_watch(watch);
	pthread_mutex_unlock(&watch->mutex);
}


/**
 * Get the descriptor of a watch object with the specified URI
 * and remove it from the global watchset->watches queue.
 *
 * Return the watch descriptor on success, NULL otherwise
 *
 * Note,
 * 1, The find and delete operations must be executed altogether
 * in an atomic manner in response to delete requests, otherwise
 * race conditions among deleting thread ensue
 */
static watch_t *dequeue_watch(const char *uri)
{
	watch_t *watch, *n;
	long id;

	assert(uri);

	if ((id = get_watch_id(uri)) < 0) {
        log_error("Failed to get watch ID from %s", uri);
		return NULL;
	}

	pthread_mutex_lock(&watchset->mutex);
	list_for_each_entry_safe(watch, n, &watchset->watches, list) {
		if (watch->id == id) {
			list_del(&watch->list);
			pthread_mutex_unlock(&watchset->mutex);

			pthread_mutex_lock(&watch->mutex);
			watch->is_shutdown = 1;
			pthread_mutex_unlock(&watch->mutex);

			return watch;
		}
	}

	pthread_mutex_unlock(&watchset->mutex);
	return NULL;
}

/**
 * Get the very first task from the global poll task queue, if it exists
 *
 * Note,
 * 1. Callers should hold backlog->mutex
 */
static poll_task_t *get_first_task(poll_backlog_t *bl)
{
	return (list_empty(&bl->list_all) == 1) ?
			NULL : list_first_entry(&bl->list_all, poll_task_t, list_all);
}

/**
 * Get the very first task from the active poll task queue, if it exists
 *
 * Note,
 * 1. Callers should hold backlog->mutex
 */
static poll_task_t *get_first_task_active(poll_backlog_t *bl)
{
	return (list_empty(&bl->list_active) == 1) ?
			NULL : list_first_entry(&bl->list_active, poll_task_t, list_active);
}

/**
 * Get the first expired poll task from backlog->list_all queue.
 *
 * Return NULL if the first item in the queue has not expired yet.
 * Also return the expiry of the first item in the queue if needed.
 */
static poll_task_t *get_expired_task(poll_backlog_t *bl, struct timespec *ts)
{
	poll_task_t *task;
	struct timespec now;

	if (ts) {
		ts->tv_sec = 0;
		ts->tv_nsec = 0;
	}

	if (clock_gettime(CLOCK_REALTIME, &now) < 0 ||
		!(task = get_first_task(bl))) {
		return NULL;
	}

	if (ts) {
		ts->tv_sec = task->expiry.tv_sec;
		ts->tv_nsec = task->expiry.tv_nsec;
	}

	return (timespec_compare(&task->expiry, &now) <= 0) ? task : NULL;
}

/**
 * Reply an already dequeued poll task and free it in the end.
 * The accompanied [request, response] will also be deleted
 * once response is sent out.
 *
 * Note,
 * 1. This is a time-consuming function callers should not
 * hold any mutex during invocation.
 */
static void do_and_free_task(poll_task_t *task)
{
	obix_server_reply_object(task->request, task->watch_out);
	free(task);
}

/**
 * Dispose polling threads.
 *
 */
static void poll_backlog_dispose(poll_backlog_t *bl)
{
	poll_task_t *task, *n;
	int i;

	/*
	 * Raise the shutting down flag and wake up poll threads that
	 * are being blocked for any outstanding poll tasks.
	 *
	 * Although only one thread can grab the backlog->mutex at any
	 * one time, it will release the mutex before calling pthread_exit()
	 * so that other threads will have a chance to obtain the mutex
	 * and exit eventually.
	 */
	if (bl->poll_threads) {
		pthread_mutex_lock(&bl->mutex);
		bl->is_shutdown = 1;
		pthread_cond_broadcast(&bl->wq);
		pthread_mutex_unlock(&bl->mutex);

		for (i = 0; i < bl->num_threads; i++) {
			if (bl->poll_threads[i] == 0) {
				continue;
			}

			if (pthread_join(bl->poll_threads[i], NULL) != 0) {
				log_warning("Failed to join thread%d and it could be "
							"left zombie", i);
			}
		}

		free(bl->poll_threads);
	}

	/*
	 * No dangling poll tasks should ever exist after all
	 * watches have been removed already. Delete them if
	 * they are there. Since all poll threads have been
	 * terminated, no mutex is ever needed any more.
	 */
	if (list_empty(&bl->list_all) == 0) {
		log_warning("Dangling poll tasks found (Shouldn't happen!)");
		list_for_each_entry_safe(task, n, &bl->list_all, list_all) {
			list_del(&task->list_all);
			do_and_free_task(task);
		}
	}

	pthread_mutex_destroy(&bl->mutex);
	pthread_cond_destroy(&bl->wq);

	free(bl);
}

/**
 * Create and initialized a poll backlog descriptor
 *
 * Return its address on success, NULL otherwise
 */
static poll_backlog_t *poll_backlog_init(int num)
{
	poll_backlog_t *bl;
	int i;

	assert(num > 0);

	if (!(bl = (poll_backlog_t *)malloc(sizeof(poll_backlog_t)))) {
		log_error("Failed to allocate poll_backlog_t");
		return NULL;
	}
	memset(bl, 0, sizeof(poll_backlog_t));

	bl->num_threads = num;
	INIT_LIST_HEAD(&bl->list_all);
	INIT_LIST_HEAD(&bl->list_active);
	pthread_mutex_init(&bl->mutex, NULL);
	pthread_cond_init(&bl->wq, NULL);

	bl->poll_threads = (pthread_t *)malloc(sizeof(pthread_t) * num);
	if (!bl->poll_threads) {
		log_error("Failed to allocate polling threads pointer array");
		goto failed;
	}

	/*
	 * Fork a fleet of poll threads at starts-up, which will sleep
	 * on backlog->wq until any poll tasks need to be attended
	 */
	for (i = 0; i < num; i++) {
		if (pthread_create(&bl->poll_threads[i], NULL,
						   poll_thread_task, bl) != 0) {
			log_error("Failed to create a polling thread");
			goto failed;
		}
	}

	return bl;

failed:
	poll_backlog_dispose(bl);
	return NULL;
}

/**
 * Dequeue and remove every watch object and then free the
 * whole watch set.
 *
 * Deleting a watch will also have relevant poll tasks
 * processed, dequeued and released as well.
 */
static void watch_set_dispose(watch_set_t *set)
{
	watch_t *watch, *n;

	pthread_mutex_lock(&set->mutex);
	list_for_each_entry_safe(watch, n, &set->watches, list) {
		list_del(&watch->list);

		/*
		 * Unlease watchset->mutex while waiting to have a watch
		 * finally deleted, which will notify and wait for poll
		 * threads to send back relevant responses
		 */
		pthread_mutex_unlock(&set->mutex);
		if (watch->lease_tid) {
			ptask_cancel(watchset->lease_thread, watch->lease_tid, TRUE);
		}
		delete_watch_helper(watch);
		pthread_mutex_lock(&set->mutex);
	}
	pthread_mutex_unlock(&set->mutex);

	if (set->lease_thread) {
		ptask_dispose(set->lease_thread, TRUE);
	}

	pthread_mutex_destroy(&set->mutex);

	bitmap_dispose(set->map);

	free(set);
}

/**
 * Create and initialized a watch set descriptor, return its
 * address on success, return NULL on failure
 */
static watch_set_t *watch_set_init(void)
{
	watch_set_t *ws;

	if (!(ws = (watch_set_t *)malloc(sizeof(watch_set_t)))) {
		log_error("Failed to allocate watch_set_t");
		return NULL;
	}
	memset(ws, 0, sizeof(watch_set_t));

	INIT_LIST_HEAD(&ws->watches);
	pthread_mutex_init(&ws->mutex, NULL);

	if (!(ws->map = bitmap_init())) {
		log_error("Failed to create bitmaps");
		goto failed;
	}

	if (!(ws->lease_thread = ptask_init())) {
		log_error("Failed to create the lease thread");
		goto failed;
	}

	return ws;

failed:
	watch_set_dispose(ws);
	return NULL;
}

/**
 * Dispose the whole watch subsystem
 *
 * Note,
 * The backlog cleanup function should be called AFTER that of
 * the watch set, so that all watches and potential poll tasks
 * for each watch will have a chance to be processed and removed
 */
void obix_watch_dispose(void)
{
	if (watchset) {
		watch_set_dispose(watchset);
		watchset = NULL;
	}

	if (backlog) {
		poll_backlog_dispose(backlog);
		backlog = NULL;
	}

	log_debug("Watch subsystem is disposed");
}


/**
 * Initialize the watch subsystem
 *
 * Return 0 on success, < 0 on failures
 */
int obix_watch_init(xml_config_t *config)
{
	int num;

	if (watchset || backlog) {
		log_error("The watch subsystem may have been initialized already");
		return 0;
	}

	num = xml_parse_threads(config, XP_POLL_THREAD_COUNT);

	if (!(watchset = watch_set_init()) ||
		!(backlog = poll_backlog_init(num))) {
		log_error("Failed to initialized watch subsystem");
		obix_watch_dispose();
		return -1;
	}

	log_debug("Watch subsystem is initialized");

	return 0;
}

xmlNode *handlerWatchServiceMake(obix_request_t *request, xmlNode *input)
{
	xmlNode *node;
	watch_t *watch;
	int ret;

	if(!(watch = create_watch())) {
		ret = ERR_NO_MEM;
		goto failed;
	}

	/*
	 * Get a copy of the watch object which will be released once
	 * response sent back to clients. If copying failed, then the
	 * content of a static fatal error will be responded.
	 */
	if ((node = xmldb_copy_node(watch->node, XML_COPY_EXCLUDE_META)) != NULL) {
		if (xmlSetProp(node, BAD_CAST OBIX_ATTR_HREF, BAD_CAST watch->uri) == NULL) {
			xmlFreeNode(node);
			node = NULL;
		}
	}

	/*
	 * Fill in the HTTP Content-Location header with the href
	 * of the newly created watch object
	 */
	request->response_uri = strdup(watch->uri);

	return node;

failed:
	log_error("%s", watch_err_msg[ret].msgs);

	return obix_server_generate_error(request->request_decoded_uri, watch_err_msg[ret].type,
				"WatchService", watch_err_msg[ret].msgs);
}

/**
 * Delete the specified watch from system.
 *
 * Note,
 * 1. Return a Nil object unconditionally, even if the watch
 * object may have been deleted by other deleting thread
 */
xmlNode *handlerWatchDelete(obix_request_t *request, xmlNode *input)
{
	watch_t *watch;

	if ((watch = dequeue_watch(request->request_decoded_uri)) != NULL) {
		/* Cancel relevant lease task */
		if (watch->lease_tid) {
			ptask_cancel(watchset->lease_thread, watch->lease_tid, TRUE);
		}

		delete_watch_helper(watch);
	}

	return obix_obj_null();
}

static xmlNode *watch_item_helper(obix_request_t *request,
								  const char *uri,
								  xmlNode *input,
								  int add)		/* 1 for Watch.add */
{
	watch_t *watch;
	xmlNode *watch_out;
	int ret;

	assert(request && uri && input);

	if (!(watch = get_watch(uri))) {
		ret = ERR_NO_WATCHOBJ;
		goto out;
	}

	reset_lease_time(watch);

	if (add == 1) {
		if (!(watch_out = xmldb_copy_sys(OBIX_SYS_WATCH_OUT_STUB))) {
			ret = ERR_NO_WATCHOUT;
			goto failed;
		}
	} else {
		if (!(watch_out = obix_obj_null())) {
			ret = ERR_NO_NULLOBJ;
			goto failed;
		}
	}

	if (add == 1) {
		xml_xpath_for_each_item(input, XP_WATCH_IN,
								create_watch_item_wrapper, watch, watch_out);
	} else {
		xml_xpath_for_each_item(input, XP_WATCH_IN,
								delete_watch_item, watch, NULL);
	}

	put_watch(watch);
	return watch_out;

failed:
	put_watch(watch);

out:
	log_error("%s", watch_err_msg[ret].msgs);

	return obix_server_generate_error(uri, watch_err_msg[ret].type,
				((add == 1) ? "Watch.add" : "Watch.remove"),
				watch_err_msg[ret].msgs);
}

xmlNode *handlerWatchAdd(obix_request_t *request, xmlNode *input)
{
	return watch_item_helper(request, request->request_decoded_uri, input, 1);
}

xmlNode *handlerWatchRemove(obix_request_t *request, xmlNode *input)
{
	return watch_item_helper(request, request->request_decoded_uri, input, 0);
}

/**
 * Create a poll task for the specified watch object and insert
 * it into the global poll task queue which is organized in
 * strict expiry ascending order
 */
static int create_poll_task(watch_t *watch, long expiry,
							obix_request_t *request, xmlNode *watch_out)
{
	poll_task_t *task, *pos;

	assert(expiry > 0);

	if (!(task = (poll_task_t *)malloc(sizeof(poll_task_t)))) {
		log_error("Failed to create a poll_task_t");
		return -1;
	}
	memset(task, 0, sizeof(poll_task_t));

	/*
	 * Raise the no_reply flag in accompanied response, so that
	 * oBIX server's POST handler could differentiate the long poll
	 * requests from erroneous situations that failed to create a
	 * reply object
	 */
	request->no_reply = 1;

	clock_gettime(CLOCK_REALTIME, &task->expiry); /* since now on */
	task->expiry.tv_sec += expiry / 1000;		/* in milliseconds */
	task->expiry.tv_nsec += (expiry % 1000) * 1000;

	task->request = request;
	task->watch_out = watch_out;
	INIT_LIST_HEAD(&task->list_all);
	INIT_LIST_HEAD(&task->list_active);
	INIT_LIST_HEAD(&task->list_watch);

	/* Associate the poll task with its watch altogether */
	task->watch = watch;
	pthread_mutex_lock(&watch->mutex);
	list_add_tail(&task->list_watch, &watch->tasks);
	watch->tasks_count++;
	pthread_mutex_unlock(&watch->mutex);

	pthread_mutex_lock(&backlog->mutex);
	if (list_empty(&backlog->list_all) == 1) {
		list_add(&task->list_all, &backlog->list_all);
	} else {
		/*
		 * Manage the backlog->list_all queue in strict
		 * expiry ascending order.
		 *
		 * Therefore, insert before current item if its expiry is
		 * greater than that of the new task. However, if the new
		 * task's expiry equals to that of the current one, the new
		 * task should be placed after the current one - that's fair
		 * to follow the FIFO convention in this case.
		 */
		list_for_each_entry(pos, &backlog->list_all, list_all) {
			if (timespec_compare(&pos->expiry, &task->expiry) == 1) {
				__list_add(&task->list_all, pos->list_all.prev, &pos->list_all);
				break;
			}
		}

		/*
		 * Reaching the end of the queue, no existing item has
		 * a greater expiry, append the new task as tail
		 */
		if (&pos->list_all == &backlog->list_all) {
			list_add_tail(&task->list_all, &backlog->list_all);
		}
	}

	/*
	 * Wake up polling threads so as to re-sleep with a smaller
	 * expiry if the newly added becomes the very first item
	 */
	if (list_first_entry(&backlog->list_all, poll_task_t, list_all) == task) {
		pthread_cond_signal(&backlog->wq);
	}

	pthread_mutex_unlock(&backlog->mutex);

	return 0;
}

/**
 * Collect any changes that have taken place since the last
 * watch.PollRefresh request. Nullify any existing changes
 * upon watch.PollRefresh request or when there is no poll
 * task at all or only one poll task in watch->tasks queue
 * (so that all poll tasks on this watch are able to harvest
 * changes).
 *
 * Note,
 * 1. Callers should hold watch->mutex before invocation.
 */
static void harvest_changes(watch_t *watch, xmlNode *watch_out,
							int include_all)	/* 1 for pollRefresh */
{
	watch_item_t *item;

	if (watch->changed == 0 && include_all == 0) {
		return;
	}

	list_for_each_entry(item, &watch->items, list) {
		if (item->count == 0 && include_all == 0) {
			continue;
		}

		if (item->count > 1) {
			log_warning("Polling threads not running fast enough, "
						"current changes counter %d", item->count);
		}

		if (include_all == 1 ||	watch->tasks_count <= 1) {
			item->count = 0;
		}

		/*
		 * Ignore the return value, since the watchOut contract
		 * would have to be sent back to client anyway
		 */
		fill_watch_out(watch_out, item);

		log_debug("[%u] Harvested %s", get_tid(), item->uri);
	}

	if (include_all == 1 || watch->tasks_count <= 1) {
		watch->changed = 0;
	}
}

static xmlNode *watch_poll_helper(obix_request_t *request, const char *uri,
								  int include_all)	/* 1 for pollRefresh */
{
	watch_t *watch;
	xmlNode *watch_out;
	long wait_min, wait_max, delay = 0;
	int ret;

	assert(request && uri);

	if (!(watch = get_watch(uri))) {
		ret = ERR_NO_WATCHOBJ;
		goto out;
	}

	reset_lease_time(watch);

	if (!(watch_out = xmldb_copy_sys(OBIX_SYS_WATCH_OUT_STUB))) {
		ret = ERR_NO_WATCHOUT;
		goto failed;
	}

	pthread_mutex_lock(&watch->mutex);
	if (watch->changed == 1 || include_all == 1) {
		harvest_changes(watch, watch_out, include_all);
		__put_watch(watch);
		pthread_mutex_unlock(&watch->mutex);
		return watch_out;
	}
	pthread_mutex_unlock(&watch->mutex);

	get_time(watch->node, WATCH_MAX, &wait_max);
	get_time(watch->node, WATCH_MIN, &wait_min);

	if (wait_max > 0) {
		delay = wait_max;
	} else if (wait_min > 0) {
		delay = wait_min;
	}

	if (delay > 0) {
		if (create_poll_task(watch, delay, request, watch_out) < 0) {
			xmlFreeNode(watch_out);
			ret = ERR_NO_POLLTASK;
			goto failed;
		}

		/*
		 * The polling thread will take care of sending back watchOut
		 * contract when either the poll task expires or changes occur
		 * and release it finally
		 *
		 * Note,
		 * 1. Each poll task will increase the watch's reference count
		 * by 1, therefore it won't drop to 0 until all poll tasks have
		 * been properly handled
		 */
		return NULL;
	}

	put_watch(watch);
	return watch_out;

failed:
	put_watch(watch);

out:
	log_error("%s", watch_err_msg[ret].msgs);

	return obix_server_generate_error(uri, watch_err_msg[ret].type,
				((include_all == 1) ? "Watch.refresh" : "Watch.poll"),
				watch_err_msg[ret].msgs);
}

xmlNode *handlerWatchPollChanges(obix_request_t *request, xmlNode *input)
{
	/* input is ignored */

	return watch_poll_helper(request, request->request_decoded_uri, 0);
}

xmlNode *handlerWatchPollRefresh(obix_request_t *request, xmlNode *input)
{
	/* input is ignored */

	return watch_poll_helper(request, request->request_decoded_uri, 1);
}

/**
 * Handle one poll task that has been dequeued from backlog
 *
 * Note,
 * 1. Callers should UNLOCK backlog->mutex before invocation
 * but re-grab it once this function returns
 */
static void poll_thread_task_helper(poll_task_t *task)
{
	watch_t *watch = task->watch;

	if (!(watch = task->watch)) {
		log_warning("Relevant watch of current poll task was deleted! "
					"(Shouldn't happen!)");
		do_and_free_task(task);
		return;
	}

	pthread_mutex_lock(&watch->mutex);
	if (watch->changed == 1) {
		harvest_changes(watch, task->watch_out, 0);
	}

	/* Dequeue the poll task from relevant watch object */
	task->watch = NULL;
	list_del(&task->list_watch);
	watch->tasks_count--;

	/*
	 * Once changed items are collected in watchOut contract, poll
	 * threads won't touch the watch's descriptor nor its DOM node
	 * any more, therefore it's safe to wake up potential deletion
	 * threads to wipe the entire watch out.
	 */
	__put_watch(watch);
	pthread_mutex_unlock(&watch->mutex);

	/*
	 * Finally, send out the task's watch_out as response.
	 */
	do_and_free_task(task);
}

/**
 * Payload of polling threads
 */
static void *poll_thread_task(void *arg)
{
	poll_backlog_t *bl = (poll_backlog_t *)arg;
	poll_task_t *task;
	struct timespec closest_expiry;

	for (;;) {
		pthread_mutex_lock(&bl->mutex);

retry:
		if (bl->is_shutdown == 1) {
			pthread_mutex_unlock(&bl->mutex);
			log_debug("[%u] Exiting as the shutting down flag is raised",
					  get_tid());
			pthread_exit(NULL);
		}

		/*
		 * Wait until the global poll tasks queue is not empty
		 * while the shutting down flag is not raised
		 */
		while (list_empty(&bl->list_all) == 1 &&
			   bl->is_shutdown == 0) {
			pthread_cond_wait(&bl->wq, &bl->mutex);
		}

		/*
		 * Wait until the first poll task is expired or any poll tasks
		 * needs to be attended for changes already taken place
		 */
		while (list_empty(&bl->list_active) == 1 &&
			   get_expired_task(bl, &closest_expiry) == NULL &&
			   bl->is_shutdown == 0) {
			pthread_cond_timedwait(&bl->wq, &bl->mutex, &closest_expiry);

			/*
			 * Other threads may have consumed all poll tasks during the
			 * sleep of THIS thread. Therefore must re-start from beginning
			 * if this is the case
			 */
			if (list_empty(&bl->list_all) == 1) {
				goto retry;
			}
		}

		/*
		 * Now there are outstanding, positive poll tasks that should be
		 * attended, handle active tasks before those expired ones.
		 *
		 * Note,
		 * 1, We should not use list_for_each_entry_safe macro but try to
		 * get the very first task from relevant queue, because relevant
		 * mutex would be dropped during time-consuming jobs and other
		 * polling threads WILL dequeue the task next to the current task
		 * that THIS thread is working on, therefore breaching the context
		 * of list_for_each_entry_safe.
		 * 2. Even if the shutting down flag may have been raised, keep
		 * processing each outstanding poll tasks so as to have relevant
		 * clients unblocked. Moreover, watch set disposing thread will
		 * wait for the completion of all poll tasks of a watch before
		 * have that watch deleted.
		 */
		while ((task = get_first_task_active(bl)) != NULL ||
			   (task = get_expired_task(bl, NULL)) != NULL) {
			/*
			 * Dequeue current task so that other poll threads
			 * won't have a chance to work on it at all.
			 */
			list_del(&task->list_active);
			list_del(&task->list_all);

			/*
			 * Release the mutex while engaging time-consuming
			 * jobs but re-grab it before working on remaining
			 * outstanding tasks
			 */
			pthread_mutex_unlock(&bl->mutex);
			poll_thread_task_helper(task);
			pthread_mutex_lock(&bl->mutex);
		}

		pthread_mutex_unlock(&bl->mutex);
	} /* for */

	return NULL;	/* Should never reach here */
}
