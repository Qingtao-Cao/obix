/* *****************************************************************************
 * Copyright (c) 2013-2015 Qingtao Cao
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
 * along with oBIX. If not, see <http://www.gnu.org/licenses/>.
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

#include <string.h>
#include <pthread.h>
#include <time.h>			/* clock_gettime */
#include <errno.h>
#include <libxml/tree.h>
#include "obix_request.h"
#include "xml_storage.h"
#include "log_utils.h"
#include "obix_utils.h"
#include "server.h"
#include "xml_utils.h"
#include "bitmap.h"
#include "watch.h"
#include "ptask.h"
#include "device.h"
#include "errmsg.h"
#include "refcnt.h"
#include "tsync.h"

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

	/* Pointing to the root node of the Watch subsystem */
	xmlNode *node;

	/* Synchronisation facility in multi-thread environment */
	tsync_t sync;
} obix_watch_set_t;

/*
 * Descriptor of the watch object
 */
typedef struct obix_watch {
	/* ID of the current watch object */
	int id;

	/*
	 * Absolute href of the current watch object, which will also
	 * be used to set the HTTP Content-Location header in the
	 * response to pollChanges and pollRefresh requests
	 */
	xmlChar *href;

	/* Pointing to watch object's DOM node */
	xmlNode *node;

	/*
	 * Indicator of whether changes took place on any DOM nodes
	 * monitored by this watch
	 */
	int changed;

	/*
	 * Polling tasks on this watch object. Multiple oBIX clients
	 * may like to share one same watch object.
	 */
	struct list_head tasks;

	/*
	 * The mutex to protect above poll tasks queue when the "write region"
	 * became unusable because of marked as shutdown
	 */
	pthread_mutex_t mutex;

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

	/*
	 * Synchronisation facility to manage the life-cycle of the
	 * watch descriptor itself
	 */
	refcnt_t refcnt;

	/* Synchronisation facility in multi-thread environment */
	tsync_t sync;
} obix_watch_t;

/*
 * Descriptor of a monitored object for a watch object
 */
typedef struct obix_watch_item {
	/* The absolute href of the monitored object */
	xmlChar *href;

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
} obix_watch_item_t;

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
	obix_watch_t *watch;

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
static obix_watch_set_t *watchset;

/* 2^32 = 4,294,967,296, containing 10 digits */
#define WATCH_ID_MAX_BITS	10

/* The default lease time, in millisecond, of a watch object */
#define WATCH_LEASE_DEF		(24*60*60*1000)

/* Template of the watch object URI */
static const char *WATCH_URI_TEMPLATE = "/obix/watchService/%d/watch%d/";
static const xmlChar *WATCH_ID_PREFIX = (xmlChar *)"watch";
static const int WATCH_ID_PREFIX_LEN = 5;

static const xmlChar *WATCH_SERVICE_MAKE = (xmlChar *)"/obix/watchService/make/";

/* The value of the name attribute of the root node of the watchIn contract */
static const char *WATCH_IN_HREFS = "hrefs";

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
static const char *WATCH_PWI = "pollWaitInterval";

/*
 * The minimal waiting period of a long poll request,
 * in milliseconds
 */
static const long WATCH_POLL_INTERVAL_MIN = 100;

static void *poll_thread_task(void *arg);

/*
 * The default number of polling thread created, fallen
 * back on if not specified in configuration file
 */
static const int DEF_THREAD_COUNT = 10;

static void watch_get(obix_watch_t *watch)
{
	refcnt_get(&watch->refcnt);
}

static void watch_put(obix_watch_t *watch)
{
	refcnt_put(&watch->refcnt);
}

/*
 * Return 1 if the given href points to the common operation to create
 * a watch object, 0 otherwise
 */
int is_watch_service_make_href(const xmlChar *href)
{
	return is_str_identical(href, WATCH_SERVICE_MAKE, 1);
}

/*
 * Search for a subnode with the given href in the specified
 * watch object
 *
 * NOTE: Callers must have entered either "read region" or
 * "write region" of the relevant watch object
 */
static xmlNode *__watch_get_node_core(obix_watch_t *watch, const xmlChar *href)
{
	xmlNode *node;

	if (is_str_identical(watch->href, href, 1) == 1) {
		node = watch->node;
	} else {
		node = xmldb_get_node_core(watch->node, href + xmlStrlen(watch->href));
	}

	return node;
}

/**
 * Get the descriptor of a watch object with the specified ID
 */
static obix_watch_t *watch_search_helper(long id)
{
	obix_watch_t *watch;

	if (tsync_reader_entry(&watchset->sync) < 0) {
		return NULL;
	}

	list_for_each_entry(watch, &watchset->watches, list) {
		if (watch->id == id) {
			/* Increase refcnt before returning a reference */
			watch_get(watch);
			tsync_reader_exit(&watchset->sync);
			return watch;
		}
	}

	tsync_reader_exit(&watchset->sync);
	return NULL;
}

/**
 * Get the watch object ID from the given URI
 *
 * Return >=0 on success, -1 on failure
 */
static int watch_get_id(const xmlChar *href)
{
	const xmlChar *start;
	int ret;
	long val;

	if (!href || is_given_type(href, OBIX_WATCH) == 0) {
		return -1;
	}

	/* A watch object URI must be longer than "/obix/watchService/" */
	if (xmlStrlen(href) <= obix_roots[OBIX_WATCH].len) {
		return -1;
	}

	href += obix_roots[OBIX_WATCH].len;

	if (!(start = xmlStrstr(href, WATCH_ID_PREFIX))) {
		return -1;
	}

	start += WATCH_ID_PREFIX_LEN;

	ret = str_to_long((char *)start, &val);

	return (ret == 0) ? val : ret;
}

/**
 * Get the descriptor of a watch object with the specified URI
 * and increase its reference count
 *
 * Return the watch descriptor on success, NULL otherwise
 */
static obix_watch_t *watch_search(const xmlChar *href)
{
	int id = watch_get_id(href);

	return (id >= 0) ? watch_search_helper(id) : NULL;
}

/**
 * Insert all poll tasks related with one watch into the
 * list_active queue, if they has not been appended there yet
 *
 * NOTE: polling threads only work on backlog->list_active queue or
 * the expired tasks at the beginning of backlog->list_all queue,
 * it's more efficient to add poll tasks to list_active queue
 * than to have them expired and moved to beginning of list_all
 *
 * NOTE: callers have gained exclusive access to relevant watch
 * object by entered its "write region" or marked it as shutdown
 */
static void __watch_notify_tasks(obix_watch_t *watch)
{
	poll_task_t *task;

	if (list_empty(&watch->tasks) == 1) {
		return;
	}

	pthread_mutex_lock(&backlog->mutex);
	list_for_each_entry(task, &watch->tasks, list_watch) {
		if (task->list_active.prev == &task->list_active) {
			list_add_tail(&task->list_active, &backlog->list_active);
		}
	}
	pthread_cond_signal(&backlog->wq);
	pthread_mutex_unlock(&backlog->mutex);
}

/**
 * Notify a watch object of the change event on the given node
 */
void watch_notify_watches(long id, xmlNode *monitored, WATCH_EVT event)
{
	obix_watch_t *watch;
	obix_watch_item_t *item;

	if (!(watch = watch_search_helper(id))) {
		log_warning("Dangling watch meta for watch%d", id);
		return;
	}

	if (tsync_writer_entry(&watch->sync) < 0) {
		log_error("Watch%d being shutdown", id);
		return;
	}

	list_for_each_entry(item, &watch->items, list) {
		if (item->node == monitored) {
			/*
			 * The watch item should not be removed along with the deleted
			 * monitored object so that polling threads and watch.pollRefresh
			 * handler will have a chance to harvest a null object
			 *
			 * Moreover, if the watch.add handler is to monitor the same
			 * device contract later (after being signed up again) the nullified
			 * watch item will get removed so as to make room for the new one
			 */
			if (event == WATCH_EVT_NODE_DELETED) {
				item->node = item->meta = NULL;
			}

			item->count++;
			watch->changed = 1;
			__watch_notify_tasks(watch);

			log_debug("[%u] Notified watch%d of %s", get_tid(),
					  watch->id, item->href);
		}
	}

	tsync_writer_exit(&watch->sync);

	watch_put(watch);
}

static void fill_watch_out(xmlNode *watch_out, xmlNode *child)
{
	if (!child) {
		return;
	}

	/*
	 * Remove the hidden attribute so as to have the watched upon object
	 * properly displayed in the response
	 */
	xmlUnsetProp(child, BAD_CAST OBIX_ATTR_HIDDEN);

	if (!xmlAddChild(watch_out->children, child)) {
		log_error("Failed to add copied node to watchOut contract");
		xmlFreeNode(child);
	}
}

/*
 * Callers should ensure the to-be-deleted watch item has been
 * de-queued from relevant watch object, or not added in the
 * first place
 */
static void watch_item_cleanup(obix_watch_item_t *item)
{
	if (!item) {
		return;
	}

	/*
	 * NOTE: The watch meta node may have been deleted along
	 * with the monitored device contract and device_del()
	 * ensures the de-association with relevant watch item
	 *
	 * NOTE: Callers should have ensured that the watch meta
	 * node already de-associated with the monitored device
	 * contract, which needs to be done in the "write region"
	 * of the monitored device contract
	 */
	if (item->meta) {
		xmlFreeNode(item->meta);
	}

	if (item->href) {
		xmlFree(item->href);
	}

	free(item);
}

static obix_watch_item_t *watch_item_init(int watch_id, const xmlChar *href)
{
	obix_watch_item_t *item;
	char buf[WATCH_ID_MAX_BITS];

	if (!(item = (obix_watch_item_t *)malloc(sizeof(obix_watch_item_t)))) {
		return NULL;
	}

	memset(item, 0, sizeof(obix_watch_item_t));
	sprintf(buf, "%d", watch_id);

	if (!(item->meta = xmlNewNode(NULL, BAD_CAST OBIX_OBJ_META)) ||
		!xmlSetProp(item->meta, BAD_CAST OBIX_META_ATTR_WATCH_ID, BAD_CAST buf) ||
		!(item->href = xmlStrdup(href))) {
		goto failed;
	}

	INIT_LIST_HEAD(&item->list);
	return item;

failed:
	watch_item_cleanup(item);
	return NULL;
}

/*
 * NOTE: Callers should have gained exclusive access to relevant
 * watch object by entered its "write region" or marked it as shutdown
 */
static void __watch_delete_item_core(obix_watch_t *watch,
									 obix_watch_item_t *item)
{
	list_del(&item->list);

	if (item->href && item->meta) {
		if (device_unlink_single_node(item->href, item->meta, 0) > 0) {
			log_error("Failed to unlink watch meta node for %s from watch%d",
					  item->href, watch->id);
		} else {
			log_debug("Watch item for %s deleted from watch%d",
					  item->href, watch->id);
		}
	}

	watch_item_cleanup(item);
}

/**
 * Delete the specified watch item from the watch object
 * and fill in the watchOut contract with relevant result
 */
static void watch_delete_item(obix_watch_t *watch, const xmlChar *href,
							  xmlNode *watch_out)
{
	obix_watch_item_t *item;
	xmlNode *node;
	int ret = ERR_WATCH_NO_MONITORED_URI;

	if (tsync_writer_entry(&watch->sync) < 0) {
		ret = ERR_INVALID_STATE;
		goto failed;
	}

	list_for_each_entry(item, &watch->items, list) {
		if (is_str_identical(item->href, href, 1) == 1) {
			__watch_delete_item_core(watch, item);
			ret = 0;
			break;
		}
	}

	tsync_writer_exit(&watch->sync);

	/* Fall through */

failed:
	node = (ret == 0) ? obix_obj_null(href) :
						obix_server_generate_error(href, server_err_msg[ret].type,
										"Watch.delete", server_err_msg[ret].msgs);

	fill_watch_out(watch_out, node);
}

/**
 * Find a watch item that monitors the object with the specified
 * URI or its parent in the given watch object.
 *
 * Return the watch item pointer on success, NULL if not found.
 */
static obix_watch_item_t *__get_watch_item_or_parent(obix_watch_t *watch,
													 const xmlChar *href)
{
	obix_watch_item_t *item, *n;

	list_for_each_entry_safe(item, n, &watch->items, list) {
		if (is_str_identical(item->href, href, 1) == 1) {
			/*
			 * If the watch item has been nullified due to the removal
			 * of monitored device contract, delete it so that a new
			 * watch item can be created to monitor the same device again
			 */
			if (!item->meta && !item->node) {
				__watch_delete_item_core(watch, item);
				item = NULL;
			}

			return item;
		}

		if (xmlStrncmp(item->href, href, xmlStrlen(item->href)) == 0) {
			/* Monitoring its parent href */
			return item;
		}
	}

	return NULL;
}

/**
 * Create a watch item for the given watch object to monitor the object
 * as specified by the current sub-node of an obix:watchIn contract,
 * also fill in the watchOut contract with the content of the monitored
 * object
 *
 * NOTE: only device contracts are allowed to be monitored by watches
 */
static void watch_create_item(obix_watch_t *watch, const xmlChar *href,
							  xmlNode *watch_out)
{
	xmlNode *copy = NULL;
	obix_watch_item_t *item = NULL, *existed = NULL;
	int ret = 0;

	if (!(copy = device_copy_uri(href, EXCLUDE_META))) {
		ret = ERR_DEVICE_NO_SUCH_URI;
		goto failed;
	}

	if (!xmlSetProp(copy, BAD_CAST OBIX_ATTR_HREF, href)) {
		ret = ERR_NO_MEM;
		goto failed;
	}

	if (!(item = watch_item_init(watch->id, href))) {
		log_error("Failed to allocate a new obix_watch_item_t for %s", href);
		ret = ERR_NO_MEM;
		goto failed;
	}

	/*
	 * "find + insert" should be done atomically to avoid races when
	 * multiple threads trying to add a watch item on the same node
	 * simultaneously
	 */
	if (tsync_writer_entry(&watch->sync) < 0) {
		ret = ERR_INVALID_STATE;
		goto failed;
	}

	if (!(existed = __get_watch_item_or_parent(watch, href))) {
		/*
		 * No need to backup the monitored device's persistent file
		 * with the addition of a watch meta node
		 */
		if ((ret = device_link_single_node(href, item->meta, &item->node, 0)) > 0) {
			tsync_writer_exit(&watch->sync);
			log_error("Failed to add watch meta node under %s", href);
			goto failed;
		}

		list_add_tail(&item->list, &watch->items);
	}

	tsync_writer_exit(&watch->sync);

	if (existed) {
		log_debug("watch%d already monitoring %s or its parent",
				  watch->id, href);
		watch_item_cleanup(item);
	}

	/* Fall through */

failed:
	if (ret > 0) {
		log_error("%s : %s", (href) ? href : (xmlChar *)"(Not available)",
				  server_err_msg[ret].msgs);

		if (item) {
			watch_item_cleanup(item);
		}

		if (copy) {
			xmlFreeNode(copy);
		}

		copy = obix_server_generate_error(href, server_err_msg[ret].type,
									  "Watch.add", server_err_msg[ret].msgs);
	}

	fill_watch_out(watch_out, copy);
}

/**
 * Get the value of the val attribute of a obix:reltime node
 */
static long get_time_helper(xmlNode *node)
{
	long time = 0;
	xmlChar *val;

	if ((val = xmlGetProp(node, BAD_CAST OBIX_ATTR_VAL)) != NULL) {
		if (obix_reltime_to_long((const char *)val, &time) != 0) {
			time = 0;
		}

		xmlFree(val);
	}

	return time;
}

/**
 * Get time relevant settings of the given watch object.
 *
 * NOTE: a watch's lease time should solely be determined by
 * oBIX server's watch subsystem without any involvement of
 * its user, and it will be reset whenever the watch has been
 * accessed with a valid parameter.
 */
static long get_time(xmlNode *watch_node, const char *name)
{
	xmlNode *lease, *pwi, *max, *min;
	long time = 0;

	if (strcmp(name, WATCH_LEASE) == 0) {
		if ((lease = xml_find_child(watch_node, OBIX_OBJ_RELTIME,
									OBIX_ATTR_HREF, WATCH_LEASE)) != NULL) {
			if ((time = get_time_helper(lease)) <= 0) {
				time = WATCH_LEASE_DEF;
			}
		}
	} else if (strcmp(name, WATCH_MAX) == 0) {
		if ((pwi = xml_find_child(watch_node, OBIX_OBJ,
								  OBIX_ATTR_HREF, WATCH_PWI)) != NULL &&
			(max = xml_find_child(pwi, OBIX_OBJ_RELTIME,
								  OBIX_ATTR_HREF, WATCH_MAX)) != NULL) {
			time = get_time_helper(max);
		}
	} else if (strcmp(name, WATCH_MIN) == 0) {
		if ((pwi = xml_find_child(watch_node, OBIX_OBJ,
								  OBIX_ATTR_HREF, WATCH_PWI)) != NULL &&
			(min = xml_find_child(pwi, OBIX_OBJ_RELTIME,
								  OBIX_ATTR_HREF, WATCH_MIN)) != NULL) {
			time = get_time_helper(min);
		}
	}

	return time;
}

/*
 * Release a watch descriptor once it has been marked as
 * being shutdown
 */
static void __watch_dispose(obix_watch_t *watch)
{
	obix_watch_item_t *item, *n;

	if (!watch) {
		return;
	}

	/*
	 * Wait for the completion of any other users on the watch object,
	 * such as relevant poll tasks
	 */
	refcnt_sync(&watch->refcnt);

	list_for_each_entry_safe(item, n, &watch->items, list) {
		__watch_delete_item_core(watch, item);
	}

	if (watch->href) {
		xmlFree(watch->href);
	}

	if (watch->id >= 0) {
		bitmap_put_id(watchset->map, watch->id);
	}

	tsync_cleanup(&watch->sync);
	refcnt_cleanup(&watch->refcnt);
	pthread_mutex_destroy(&watch->mutex);

	free(watch);
}

/**
 * The payload of watch lease tasks run by the lease thread
 *
 * If there is any thread that has started handling Watch.Delete
 * request, have THAT thread delete the watch on behalf of us.
 * Return immediately so that THAT thread could return from
 * ptask_cancel(..., wait = 1) as soon as possible. BTW, this watch
 * have been dequeued by THAT thread already.
 *
 * NOTE: thanks to the fact that threads handling Watch.Delete requests
 * will wait for relevant lease tasks to be finished in the first place,
 * it's safe for the latter to hold a pointer to relevant watch object.
 */
static void delete_watch_task(void *arg)
{
	obix_watch_t *watch = (obix_watch_t *)arg;

	if (tsync_shutdown_entry(&watch->sync) < 0) {
		return;
	}

	if (tsync_writer_entry(&watchset->sync) == 0) {
		xmldb_delete_node(watch->node, DELETE_EMPTY_ANCESTORS_WATCH);
		list_del(&watch->list);
		tsync_writer_exit(&watchset->sync);
	}

	__watch_dispose(watch);
}

static void reset_lease_time(obix_watch_t *watch)
{
	if (watch->lease_tid > 0) {
		ptask_reset(watchset->lease_thread, watch->lease_tid);
	}
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

	if (num <= 0) {
		num = DEF_THREAD_COUNT;
	}

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

static int watch_del(const xmlChar *href)
{
	obix_watch_t *watch;

	if (!(watch = watch_search(href))) {
		return ERR_WATCH_NO_SUCH_URI;
	}

	/*
	 * Mark the watch as being shutdown so as to synchronise with other
	 * threads accessing it and prevent further access
	 *
	 * If some other threads may have started to delete the same watch
	 * object, such as another thread serving a watch.delete request, or
	 * the lease thread, have THAT thread taken care ofdeletion and exit
	 * gracefully
	 */
	if (tsync_shutdown_entry(&watch->sync) < 0) {
		watch_put(watch);
		return 0;
	}

	/*
	 * Cancel relevant lease task
	 *
	 * NOTE: wait == 1 so as to wait for the lease thread getting
	 * rid of relevant task for the watch object
	 */
	if (watch->lease_tid) {
		ptask_cancel(watchset->lease_thread, watch->lease_tid, 1);
	}

	/*
	 * Wake up any pending poll tasks on the watch object and wait
	 * until they are all processed
	 */
	__watch_notify_tasks(watch);

	if (tsync_writer_entry(&watchset->sync) == 0) {
		xmldb_delete_node(watch->node, DELETE_EMPTY_ANCESTORS_WATCH);
		list_del(&watch->list);
		tsync_writer_exit(&watchset->sync);
	}

	watch_put(watch);

	__watch_dispose(watch);
	return 0;
}


/*
 * Release a watch set descriptor once it has been marked as
 * being shutdown
 */
static void watch_set_cleanup(obix_watch_set_t *set)
{
	if (!set) {
		return;
	}

	tsync_cleanup(&set->sync);

	if (set->lease_thread) {
		ptask_dispose(set->lease_thread, 1);
	}

	if (set->map) {
		bitmap_dispose(set->map);
	}

	free(set);
}

/**
 * Dequeue and remove every watch object and then free the
 * whole watch set.
 *
 * Deleting a watch will also have relevant poll tasks
 * processed, dequeued and released as well.
 */
static void watch_set_dispose(obix_watch_set_t *set)
{
	obix_watch_t *watch, *n;

	if (tsync_shutdown_entry(&set->sync) < 0) {
		return;
	}

	list_for_each_entry_safe(watch, n, &set->watches, list) {
		list_del(&watch->list);
		watch_del(watch->href);
	}

	watch_set_cleanup(set);
}

/**
 * Create and initialized a watch set descriptor, return its
 * address on success, return NULL on failure
 */
static obix_watch_set_t *watch_set_init(void)
{
	obix_watch_set_t *set;

	if (!(set = (obix_watch_set_t *)malloc(sizeof(obix_watch_set_t)))) {
		log_error("Failed to allocate obix_watch_set_t");
		return NULL;
	}
	memset(set, 0, sizeof(obix_watch_set_t));

	if (!(set->node = xmldb_get_node(obix_roots[OBIX_WATCH].root))) {
		log_error("Failed to find the root node of the Watch subsystem");
		goto failed;
	}

	tsync_init(&set->sync);
	INIT_LIST_HEAD(&set->watches);

	if (!(set->map = bitmap_init())) {
		log_error("Failed to create bitmaps");
		goto failed;
	}

	if (!(set->lease_thread = ptask_init())) {
		log_error("Failed to create the lease thread");
		goto failed;
	}

	xml_setup_private(set->node, (void *)set);

	return set;

failed:
	watch_set_cleanup(set);
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

	log_debug("The Watch subsystem disposed");
}


/**
 * Initialize the watch subsystem
 *
 * Return 0 on success, > 0 for error code
 */
int obix_watch_init(const int poll_threads)
{
	if (watchset || backlog) {
		return 0;
	}

	if (!(watchset = watch_set_init()) ||
		!(backlog = poll_backlog_init(poll_threads))) {
		log_error("Failed to initialized watch subsystem");
		obix_watch_dispose();
		return ERR_NO_MEM;
	}

	log_debug("The Watch subsystem is initialised");

	return 0;
}

static obix_watch_t *watch_create(void)
{
	obix_watch_t *watch;
	long lease;

	if (!(watch = (obix_watch_t *)malloc(sizeof(obix_watch_t)))) {
		log_error("Failed to allocate obix_watch_t");
		return NULL;
	}
	memset(watch, 0, sizeof(obix_watch_t));

	if ((watch->id = bitmap_get_id(watchset->map)) < 0) {
		log_error("Failed to get an ID for current watch object");
		goto failed;
	}

	if (!(watch->href = (xmlChar *)malloc(strlen(WATCH_URI_TEMPLATE) +
									  (WATCH_ID_MAX_BITS - 2) * 2 + 1))) {
		log_error("Failed to allocate URI string for a watch object");
		goto failed;
	}
	sprintf((char *)watch->href, WATCH_URI_TEMPLATE,
			watch->id / MAX_WATCHES_PER_FOLDER, watch->id);

	if (!(watch->node = xmldb_copy_sys(WATCH_STUB)) ||
		!xmlSetProp(watch->node, BAD_CAST OBIX_ATTR_HREF, watch->href)) {
		log_error("Failed to copy a xmlNode for %s", watch->href);
		goto node_failed;
	}

	INIT_LIST_HEAD(&watch->tasks);
	INIT_LIST_HEAD(&watch->items);
	INIT_LIST_HEAD(&watch->list);
	pthread_mutex_init(&watch->mutex, NULL);
	tsync_init(&watch->sync);
	refcnt_init(&watch->refcnt);

	lease = get_time(watch->node, WATCH_LEASE);
	watch->lease_tid = ptask_schedule(watchset->lease_thread,
									  delete_watch_task, watch, lease, 1);
	if (watch->lease_tid < 0) {
		log_error("Failed to register a lease task for %s", watch->href);
		goto lease_failed;
	}

	if (tsync_reader_entry(&watchset->sync) < 0) {
		log_error("The watch subsystem is shutting down");
		goto ws_failed;
	}

	/*
	 * Automatically create the missing parent for watch objects
	 * to setup a 2-level hierarchy
	 */
	if (xmldb_put_node(watch->node, watch->href,
					   CREATE_ANCESTORS_WATCH) > 0) {
		tsync_reader_exit(&watchset->sync);
		log_error("Failed to register %s node to DOM tree", watch->href);
		goto ws_failed;
	}

	/*
	 * The private pointer of the added parent node should point
	 * to the descriptor of the entire Watch subsystem in stead
	 * of any particular watch objects underneath it
	 */
	watch->node->parent->_private = watchset;

	list_add_tail(&watch->list, &watchset->watches);

	tsync_reader_exit(&watchset->sync);

	xml_setup_private(watch->node, (void *)watch);

	watch_get(watch);
	return watch;

ws_failed:
	ptask_cancel(watchset->lease_thread, watch->lease_tid, 1);

lease_failed:
	pthread_mutex_destroy(&watch->mutex);
	tsync_cleanup(&watch->sync);
	refcnt_cleanup(&watch->refcnt);

node_failed:
	if (watch->node) {
		xmlFreeNode(watch->node);
	}

	xmlFree(watch->href);

failed:
	if (watch->id >= 0) {
		bitmap_put_id(watchset->map, watch->id);
	}

	free(watch);
	return NULL;
}

xmlNode *handlerWatchServiceMake(obix_request_t *request, const xmlChar *href,
								 xmlNode *input)
{
	xmlNode *node = NULL;
	obix_watch_t *watch;
	int ret;

	if (!(watch = watch_create())) {
		ret = ERR_NO_MEM;
		goto failed;
	}

	/*
	 * Another thread may try to delete the created watch object since
	 * it is publicly available. However, the deletion thread will be
	 * blocked until its refcnt drops to 0 (by this thread) after marking
	 * it as being shutdown, which will make this thread exit directly
	 */

	if (tsync_reader_entry(&watch->sync) < 0) {
		watch_put(watch);
		ret = ERR_INVALID_STATE;
		goto failed;
	}

	if ((node = xmldb_copy_node(watch->node, EXCLUDE_META)) != NULL) {
		if (!xmlSetProp(node, BAD_CAST OBIX_ATTR_HREF, watch->href)) {
			xmlFreeNode(node);
			node = NULL;
		}
	}

	tsync_reader_exit(&watch->sync);

	request->response_uri = xmlStrdup(watch->href);

	watch_put(watch);
	return node;

failed:
	log_error("%s : %s", href, server_err_msg[ret].msgs);
	return obix_server_generate_error(href, server_err_msg[ret].type,
									  "Watch.make", server_err_msg[ret].msgs);
}

/**
 * Delete the specified watch from system.
 */
xmlNode *handlerWatchDelete(obix_request_t *request, const xmlChar *href,
							xmlNode *input)
{
	xmlNode *node;
	int ret;

	if ((ret = watch_del(href)) == 0) {
		node = obix_obj_null(href);
	} else {
		node = obix_server_generate_error(href, server_err_msg[ret].type,
									"Watch.delete", server_err_msg[ret].msgs);
	}

	return node;
}

static xmlNode *watch_item_helper(obix_request_t *request,
								  const xmlChar *href, xmlNode *input,
								  int add)		/* 1 for Watch.add */
{
	obix_watch_t *watch;
	xmlNode *watch_out = NULL, *list, *item;
	xmlChar *is_attr = NULL, *target_href;
	int ret = 0;

	if (!request || !input ||
		!(is_attr = xmlGetProp(input, BAD_CAST OBIX_ATTR_IS)) ||
		xmlStrcmp(is_attr, BAD_CAST OBIX_CONTRACT_WATCH_IN) != 0 ||
		!(list = xml_find_child(input, OBIX_OBJ_LIST, OBIX_ATTR_NAME,
								WATCH_IN_HREFS))) {
		ret = ERR_INVALID_INPUT;
		goto failed;
	}

	if (!(watch_out = xmldb_copy_sys(WATCH_OUT_STUB))) {
		ret = ERR_NO_MEM;
		goto failed;
	}

	if (!(watch = watch_search(href))) {
		ret = ERR_WATCH_NO_SUCH_URI;
		goto failed;
	}

	reset_lease_time(watch);

	for (item = list->children; item; item = item->next) {
		if (item->type != XML_ELEMENT_NODE ||
			xmlStrcmp(item->name, BAD_CAST OBIX_OBJ_URI) != 0 ||
			!(target_href = xmlGetProp(item, BAD_CAST OBIX_ATTR_VAL))) {
			continue;
		}

		if (add == 1) {
			watch_create_item(watch, target_href, watch_out);
		} else {
			watch_delete_item(watch, target_href, watch_out);
		}

		if (target_href) {
			xmlFree(target_href);
		}
	}

	watch_put(watch);

	/* Fall through */

failed:
	if (is_attr) {
		xmlFree(is_attr);
	}

	if (ret > 0) {
		log_error("%s : %s", href, server_err_msg[ret].msgs);

		if (watch_out) {
			xmlFreeNode(watch_out);
		}

		watch_out = obix_server_generate_error(href, server_err_msg[ret].type,
									((add == 1) ? "Watch.add" : "Watch.remove"),
									server_err_msg[ret].msgs);
	}

	return watch_out;
}

xmlNode *handlerWatchAdd(obix_request_t *request, const xmlChar *href,
						 xmlNode *input)
{
	return watch_item_helper(request, href, input, 1);
}

xmlNode *handlerWatchRemove(obix_request_t *request, const xmlChar *href,
							xmlNode *input)
{
	return watch_item_helper(request, href, input, 0);
}

/**
 * Create a poll task for the specified watch object
 *
 * Return 0 on success, > 0 for error code
 */
static int watch_create_poll_task(obix_watch_t *watch, long expiry,	/* milliseconds */
								  obix_request_t *request, xmlNode *watch_out)
{
	poll_task_t *task, *pos;

	if (!(task = (poll_task_t *)malloc(sizeof(poll_task_t)))) {
		log_error("Failed to create a poll_task_t");
		return ERR_NO_MEM;
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
	task->expiry.tv_sec += expiry / 1000;
	task->expiry.tv_nsec += (expiry % 1000) * 1000;

	task->request = request;
	task->watch_out = watch_out;

	INIT_LIST_HEAD(&task->list_all);
	INIT_LIST_HEAD(&task->list_active);
	INIT_LIST_HEAD(&task->list_watch);

	/* Associate the poll task with its watch altogether */
	if (tsync_writer_entry(&watch->sync) < 0) {
		free(task);
		return ERR_INVALID_STATE;
	}

	list_add_tail(&task->list_watch, &watch->tasks);
	watch->tasks_count++;
	tsync_writer_exit(&watch->sync);

	task->watch = watch;

	/*
	 * Insert the poll task into the global poll task queue which is
	 * organized in strict expiry ascending order of each task
	 */
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
 */
static void __watch_harvest_changes(obix_watch_t *watch, xmlNode *watch_out,
									int include_all)	/* 1 for pollRefresh */
{
	obix_watch_item_t *item;
	xmlNode *node;

	if (watch->changed == 0 && include_all == 0) {
		return;
	}

	list_for_each_entry(item, &watch->items, list) {
		if (item->count == 0 && include_all == 0) {
			continue;
		}

		if (item->count > 1) {
			log_warning("No pending pollChanges requests to return changes "
						"earlier, or polling threads not fast enough.");
			log_warning("Changes counter %d", item->count);
		}

		if (include_all == 1 ||	watch->tasks_count <= 1) {
			item->count = 0;
		}

		/* The monitored node may have been signed off */
		if (item->node &&
			(node = device_copy_uri(item->href, EXCLUDE_META))) {
			xmlSetProp(node, BAD_CAST OBIX_ATTR_HREF, item->href);
		} else {
			node = obix_obj_null(item->href);
		}

		if (!node) {
			node = obix_server_generate_error(item->href,
										server_err_msg[ERR_NO_MEM].type,
										"Watch.PollChange",
										server_err_msg[ERR_NO_MEM].msgs);
		}

		fill_watch_out(watch_out, node);

		log_debug("[%u] Harvested %s", get_tid(), item->href);
	}

	if (include_all == 1 || watch->tasks_count <= 1) {
		watch->changed = 0;
	}
}

static xmlNode *watch_poll_helper(obix_request_t *request,
								  const xmlChar *href,
								  int include_all)	/* 1 for pollRefresh */
{
	obix_watch_t *watch;
	xmlNode *watch_out = NULL;
	long wait_min, wait_max, delay = 0;
	int ret = 0;

	if (!request) {
		ret = ERR_INVALID_ARGUMENT;
		goto failed;
	}

	if (!(watch_out = xmldb_copy_sys(WATCH_OUT_STUB))) {
		ret = ERR_NO_MEM;
		goto failed;
	}

	if (!(watch = watch_search(href))) {
		ret = ERR_WATCH_NO_SUCH_URI;
		goto failed;
	}

	reset_lease_time(watch);

	if (tsync_reader_entry(&watch->sync) < 0) {
		ret = ERR_INVALID_STATE;
		goto out;
	}

	if (watch->changed == 1 || include_all == 1) {
		__watch_harvest_changes(watch, watch_out, include_all);
		tsync_reader_exit(&watch->sync);
		goto out;
	}

	wait_max = get_time(watch->node, WATCH_MAX);
	wait_min = get_time(watch->node, WATCH_MIN);

	tsync_reader_exit(&watch->sync);

	if (wait_max > wait_min) {
		delay = wait_max;
	} else if (wait_min > 0) {
		delay = wait_min;
	} else {
		/*
		 * Enforce long poll mechanism for sake of performance if
		 * not enabled by user in the first place
		 */
		delay = WATCH_POLL_INTERVAL_MIN;
	}

	if ((ret = watch_create_poll_task(watch, delay, request, watch_out)) == 0) {
		/*
		 * The polling thread will take care of sending back watchOut
		 * contract when either the poll task expires or changes occur
		 * and release it finally
		 *
		 * NOTE: don't decrease the watch's refernence count since it
		 * should be remained added 1 for each outstanding poll tasks
		 */
		return NULL;
	}

	/* Fall through */

out:
	watch_put(watch);

failed:
	if (ret > 0) {
		log_error("%s : %s", href, server_err_msg[ret].msgs);

		if (watch_out) {
			xmlFreeNode(watch_out);
		}

		watch_out = obix_server_generate_error(href, server_err_msg[ret].type,
						((include_all == 1) ? "Watch.refresh" : "Watch.poll"),
						server_err_msg[ret].msgs);
	}

	return watch_out;
}

xmlNode *handlerWatchPollChanges(obix_request_t *request,
								 const xmlChar *href, xmlNode *input)
{
	/* input is ignored */

	return watch_poll_helper(request, href, 0);
}

xmlNode *handlerWatchPollRefresh(obix_request_t *request,
								 const xmlChar *href, xmlNode *input)
{
	/* input is ignored */

	return watch_poll_helper(request, href, 1);
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
	obix_watch_t *watch;
	int ret;

	if (!(watch = task->watch)) {
		log_warning("Relevant watch of current poll task was deleted! "
					"(Shouldn't happen!)");
		do_and_free_task(task);
		return;
	}

	if ((ret = tsync_reader_entry(&watch->sync)) < 0) {
		log_debug("Watch%d has been marked as shutdown", watch->id);
	}

	__watch_harvest_changes(watch, task->watch_out, 0);

	if (ret == 0) {
		tsync_reader_exit(&watch->sync);
	}

	/*
	 * Use a separate mutex to protect watch->tasks queue when the
	 * critical regions are no longer usable after marked as shutdown
	 */
	if ((ret = tsync_writer_entry(&watch->sync)) < 0) {
		log_debug("Watch%d has been marked as shutdown", watch->id);
		pthread_mutex_lock(&watch->mutex);
	}

	list_del(&task->list_watch);
	watch->tasks_count--;

	if (ret < 0) {
		pthread_mutex_unlock(&watch->mutex);
	} else {
		tsync_writer_exit(&watch->sync);
	}

	/*
	 * Decrease the reference count on the watch object now that
	 * it has been dealt with
	 */
	watch_put(watch);

	task->watch = NULL;

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

int watch_update_uri(const xmlChar *href, const xmlChar *new)
{
	obix_watch_t *watch;
	xmlNode *node;
	xmlChar *old = NULL;
	int ret = 0;

	if (!(watch = watch_search(href))) {
		return ERR_WATCH_NO_SUCH_URI;
	}

	if (tsync_writer_entry(&watch->sync) < 0) {
		ret = ERR_INVALID_STATE;
		goto failed;
	}

	if (!(node = __watch_get_node_core(watch, href))) {
		tsync_writer_exit(&watch->sync);
		ret = ERR_NO_SUCH_URI;
		goto failed;
	}

	if (!(old = xmlGetProp(node, BAD_CAST OBIX_ATTR_VAL))) {
		tsync_writer_exit(&watch->sync);
		ret = ERR_READONLY_HREF;
		goto failed;
	}

	if (xmlStrcmp(old, new) != 0 &&
		!xmlSetProp(node, BAD_CAST OBIX_ATTR_VAL, new)) {
		tsync_writer_exit(&watch->sync);
		ret = ERR_NO_MEM;
		goto failed;
	}

	tsync_writer_exit(&watch->sync);

	/* Fall through */

failed:
	if (old) {
		xmlFree(old);
	}

	watch_put(watch);
	return ret;
}

/*
 * Get a subnode under the root node of the watch subsystem,
 * also return a value indicating its relative depth under it
 */
static xmlNode *__watch_set_get_node_core(const xmlChar *href, int *level)
{
	xmlNode *node;
	const xmlChar *root_href = obix_roots[OBIX_WATCH].root;

	if (is_str_identical(href, root_href, 1) == 1) {
		node = watchset->node;
		*level = 0;
	} else {
		node = xmldb_get_node_core(watchset->node, href + xmlStrlen(root_href));
		*level = (node) ? 1 : -1;
	}

	return node;
}

/*
 * Copy the subtree of the given node which may have a number of
 * watch objects involved
 */
static xmlNode *__watch_set_copy_node(const xmlNode *src,
									  xml_copy_flags_t flags,
									  int level, int depth)
{
	obix_watch_t *watch = NULL;
	xmlNode *node, *copy_src = NULL, *copy_node = NULL;
	int ret = ERR_NO_MEM;

	if (level == 0 || level == 1 ||
		(level == 2 && src->_private == (void *)watchset)) {
		/*
		 * Already in the read region of watchset descriptor, do nothing
		 *
		 * NOTE: the <meta op/> node under "/obix/watchService/make" are in
		 * level 2 as well
		 */
	} else if (level == 2 && src->_private != NULL &&
			   src->_private != (void *)watchset) {
		/*
		 * Come across read regions from watset into a watch object
		 */
		tsync_reader_exit(&watchset->sync);

		watch = (obix_watch_t *)src->_private;
		watch_get(watch);

		if (tsync_reader_entry(&watch->sync) < 0) {
			watch_put(watch);
			return NULL;
		}
	} else {
		/*
		 * level > 2, still in the current watch object, do nothing
		 */
	}

	/*
	 * If hidden, meta or comment nodes are not explicitly required,
	 * they may be skipped over according to the flag
	 */
	if (depth > 0) {
		if (((flags & EXCLUDE_HIDDEN) > 0 && xml_is_hidden(src) == 1) ||
			((flags & EXCLUDE_META) > 0 &&
			 xmlStrcmp(src->name, BAD_CAST OBIX_OBJ_META) == 0) ||
			((flags & EXCLUDE_COMMENTS) > 0 && src->type == XML_COMMENT_NODE)) {
			ret = 0;
			goto out;
		}
	}

	if (!(copy_src = xmlCopyNode((xmlNode *)src, 2))) {
		log_error("Failed to copy a node");
		goto out;
	}

	for (node = src->children; node; node = node->next) {
		if (!(copy_node = __watch_set_copy_node(node, flags, ++level, ++depth))) {
			/*
			 * The current child may have been deliberatly excluded,
			 * move on to the next one
			 */
			continue;
		}

		if (!xmlAddChild(copy_src, copy_node)) {
			xmlFreeNode(copy_node);
			goto out;
		}
	}

	ret = 0;

	/* Fall through */

out:
	if (level == 2 && src->_private != NULL &&
		src->_private != (void *)watchset) {
		tsync_reader_exit(&watch->sync);
		watch_put(watch);

		tsync_reader_entry(&watchset->sync);
	}

	if (ret > 0 && copy_src) {
		xmlFreeNode(copy_src);
		copy_src = NULL;
	}

	return copy_src;
}

static xmlNode *watch_set_copy_uri(const xmlChar *href, xml_copy_flags_t flags)
{
	xmlNode *node, *copy = NULL;
	int level;

	/*
	 * href points to some common watch facilities such as
	 * "/obix/watchService[/%d/]" or something not existing
	 * so the watchset descriptor is used
	 */
	if (tsync_reader_entry(&watchset->sync) < 0) {
		return NULL;
	}

	if ((node = __watch_set_get_node_core(href, &level)) != NULL) {
		copy = __watch_set_copy_node(node, flags, level, 0);
	}

	tsync_reader_exit(&watchset->sync);
	return copy;
}

static xmlNode *watch_obj_copy_uri(obix_watch_t *watch, const xmlChar *href,
								   xml_copy_flags_t flags)
{
	xmlNode *node, *copy = NULL;

	if (tsync_reader_entry(&watch->sync) < 0) {
		return NULL;
	}

	if ((node = __watch_get_node_core(watch, href)) != NULL) {
		/*
		 * NOTE: watch objects are all "siblings" to each other (although
		 * they are organised in a 2-level hierarchy) so there is no worries
		 * to come across boundaries of different watch objects
		 */
		copy = xmldb_copy_node(node, flags);
	}

	tsync_reader_exit(&watch->sync);

	watch_put(watch);
	return copy;
}

xmlNode *watch_copy_uri(const xmlChar *href, xml_copy_flags_t flags)
{
	obix_watch_t *watch;

	return ((watch = watch_search(href)) != NULL) ?
			watch_obj_copy_uri(watch, href, flags) : watch_set_copy_uri(href, flags);
}

/*
 * Read value of the "op" attribute in the meta node
 * in watch object
 *
 * Return 0 on success, > 0 for error code
 */
int watch_get_op_id(const xmlChar *href, long *id)
{
	obix_watch_t *watch;
	xmlNode *node;
	int ret = ERR_INVALID_STATE;

	if (!(watch = watch_search(href))) {
		return ERR_WATCH_NO_SUCH_URI;
	}

	if (tsync_reader_entry(&watch->sync) == 0) {
		if ((node = __watch_get_node_core(watch, href)) != NULL) {
			ret = xmldb_get_op_id_core(node, id);
		} else {
			ret = 0;
		}

		tsync_reader_exit(&watch->sync);
	}

	watch_put(watch);
	return ret;
}
