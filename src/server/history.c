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

#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>		/* strtol */
#include <stdio.h>		/* sprintf */
#include <errno.h>
#include <limits.h>		/* LONG_MAX, LONG_MIN */
#include <sys/uio.h>	/* writev */
#include <libxml/tree.h>
#include "list.h"
#include "log_utils.h"
#include "obix_utils.h"
#include "xml_utils.h"
#include "xml_storage.h"
#include "obix_fcgi.h"
#include "server.h"
#include "tsync.h"
#include "security.h"
#include "errmsg.h"

/*
 * Descriptor for one history log file, whose abstract information
 * (obix:HistoryFileAbstract) is collected in the index file of the
 * current device
 */
typedef struct obix_hist_file {
	/* abstract node in the index DOM subtree */
	xmlNode *abstract;

	/* the creation date */
	char *date;

	/* pathname for history log file */
	char *filepath;

	/* joining obix_hist_dev.files */
	struct list_head list;
} obix_hist_file_t;

/*
 * Descriptor of the history facility for one specific device, which is
 * comprised of a number of log files(each described by obix_hist_file_t
 * as above) and one index file that collects all abstract information
 * for each log file.
 *
 * The index is a complete XML file, however, its xmlDoc node will be
 * detached from the rest of its DOM tree and released, while its root
 * subtree will be reparented to obix_hist.dist.
 *
 * Parallel access to different devices are allowed. For one device,
 * parallel reading are allowed, while writing is excluded from any other
 * reading or writing.
 *
 * Writing is given priority over reading.
 */
typedef struct obix_hist_dev {
	/* total number of records */
	long count;

	/* device's unique ID on obix server */
	char *dev_id;

	/*
	 * The strlen of the facility name
	 *
	 * NOTE: all facilities are organised in an ascending order
	 * based on the length of their name in the hope that the parent
	 * facilities are always inserted before their children, so
	 * that at cleanup the list can be traversed in a reverse manner
	 * to have the children facilities destroyed before their parent
	 * to avoid double free of the children facilities nodes in the
	 * global DOM tree
	 */
	int namelen;

	/* /obix/historyService/histories/dev_id/ */
	xmlChar *href;

	/* index's absolute pathname */
	char *indexpath;

	/* The device's XML object in the global DOM tree */
	xmlNode *node;

	/* The device's index's subtree parented by above node */
	xmlNode *index;

	/* SORTED list of obix_hist_file */
	struct list_head files;

	/* synchroniser among multi threads */
	tsync_t sync;

	/* joining obix_hist.devices */
	struct list_head list;
} obix_hist_dev_t;

typedef int (*obix_hist_func_t) (obix_request_t *request,
								 obix_hist_dev_t *dev, xmlNode *input);

typedef struct obix_hist_ops {
	obix_hist_func_t query;
	obix_hist_func_t append;
} obix_hist_ops_t;

/*
 * Descriptor for the overall history facility
 */
typedef struct obix_hist {
	/* pathname for the ../res/histories/ folder on disk */
	char *dir;

	/* history facility operations */
	obix_hist_ops_t *op;

	/* history facilities for different devices */
	struct list_head devices;

	/* protect devices list */
	pthread_mutex_t mutex;
} obix_hist_t;

obix_hist_t *_history;

#define HISTORIES_DIR			"histories/"
#define HISTORIES_RELHREF		HISTORIES_DIR
#define HIST_INDEX_FILENAME		"index"
#define LOG_FILENAME_SUFFIX		".fragment"

#define HIST_REC_VAL			"value"
#define DEVICE_ID				"dev_id"

#define AOUT_NUMADDED			"numAdded"
#define AOUT_NEWCOUNT			"newCount"
#define AOUT_NEWSTART			"newStart"
#define AOUT_NEWEND				"newEnd"

#define FILTER_LIMIT			"limit"
#define FILTER_START			"start"
#define FILTER_END				"end"
#define FILTER_FORMAT			"format"
#define FILTER_COMPACT			"compact"

#define OBIX_CONTRACT_HIST_ABS	"HistoryFileAbstract"
#define OBIX_CONTRACT_HIST_AOUT	"HistoryAppendOut"

static const char *OBIX_HISTORY_LOBBY = "/obix/historyService/histories/";
static const int OBIX_HISTORY_LOBBY_LEN = 31;

/*
 * Append "\r\n" at the end of a history record so that
 * "</obj>\r\n" can be used as the boundary of it
 */
static const char *HIST_RECORD_SEPARATOR = "\r\n";

/*
 * The index file will be created upon the reception of
 * the get request with a unique device id that this history
 * facility is created for.
 *
 * Initially it will be populated with an index skeleton,
 * then abstract(obix:HistoryFileAbstract) for each log file
 * named after the date when data generated.
 */
static const char *HIST_INDEX_SKELETON =
"<list name=\"index\" href=\"index\" of=\"obix:HistoryFileAbstract\"/>\r\n";

static char *HIST_QUERY_OUT_PREFIX =
"<obj is=\"obix:HistoryQueryOut\">\r\n"
"<int name=\"count\" val=\"%d\"/>\r\n"
"<abstime name=\"start\" val=\"%s\"/>\r\n"
"<abstime name=\"end\" val=\"%s\"/>\r\n"
"<list name=\"data\" of=\"obix:HistoryRecord\">\r\n";

static char *HIST_QUERY_OUT_SUFFIX = "</list>\r\n</obj>\r\n";

static const char *HIST_GET_OUT_SKELETON =
"<str name=\"%s\" href=\"%s\"/>\r\n";

/*
 * Enqueue a new obix_hist_file struct based on its date
 *
 * Return 0 on success, < 0 otherwise
 */
static int __hist_enqueue_file(obix_hist_file_t *new, obix_hist_dev_t *dev)
{
	int res;
	obix_hist_file_t *file;

	list_for_each_entry(file, &dev->files, list) {
		if (timestamp_compare_date(new->date, file->date, &res) < 0) {
			log_error("Failed to compare date strings %s vs %s", new->date,
					  file->date);
			return -1;
		}

		if (res == 0) {
			log_error("Raw data file on the same date %s already exist",
					  new->date);
			return -1;
		}

		if (res < 0) {
			__list_add(&new->list, file->list.prev, &file->list);
			return 0;
		}
	}

	/* List empty or reaching the end of list */
	if (&file->list == &dev->files) {
		list_add_tail(&new->list, &dev->files);
		return 0;
	}

	log_error("Should never reach here");
	return -1;
}

/*
 * Update the value of the val attribute of the specified child
 * in the input contract
 */
static int update_value(xmlNode *input, const char *tag,
						const char *name, const char *value)
{
	xmlNode *node;

	if (!(node = xml_find_child(input, tag, OBIX_ATTR_NAME, name)) ||
		!xmlSetProp(node, BAD_CAST OBIX_ATTR_VAL, BAD_CAST value)) {
		return -1;
	}

	return 0;
}

/*
 * Update the value string of the val attribute of the matching sub-element
 * with OBIX_OBJ_INT tag and the specified name from the input subtree
 */
static int update_count(xmlNode *input, const char *name, long count)
{
	char buf[10 + 1];	/* On 32bit system, 4G has 10 digits */

	sprintf(buf, "%ld", count);

	return update_value(input, OBIX_OBJ_INT, name, buf);
}

static int add_abs_count(obix_hist_file_t *file, int added)
{
	long count = xml_get_child_long(file->abstract, OBIX_OBJ_INT,
									HIST_ABS_COUNT);

	if (count < 0) {
		log_error("Failed to get count from abstract of %s", file->filepath);
		return -1;
	}

	return update_count(file->abstract, HIST_ABS_COUNT, count + added);
}

/*
 * Append one record into a log file
 *
 * Obviously the IO performance could be further promoted if ALL records
 * contained in one HistoryAppendIn contract could be written altogether
 * instead of separately. However, the adoption of Linux kernel IO cache
 * has greatly mitigated the potential performance loss. Furthermore, the
 * potential improvement will require creating iovecs array for arbitrary
 * number of records, making it not that appealing.
 *
 * Return > 0 on success, < 0 otherwise
 */
static int write_logfile(obix_hist_file_t *file, xmlNode *record)
{
	int fd, ret = -1;
	char *data;
	struct iovec iov[2];

	if (!(data = xml_dump_node(record))) {
		log_error("Failed to dump record content");
		return -1;
	}

	iov[0].iov_base = data;
	iov[0].iov_len = strlen(data);
	iov[1].iov_base = (char *)HIST_RECORD_SEPARATOR;
	iov[1].iov_len = strlen(HIST_RECORD_SEPARATOR);

	if ((fd = open(file->filepath, O_APPEND | O_WRONLY | O_SYNC)) > 0) {
		ret = writev(fd, iov, 2);
		close(fd);
	}

	free(data);
	return ret;
}

/*
 * Read the content of a log file into a buffer
 *
 * Return the buffer address on success, NULL otherwise.
 * If successful, *len will be set as the file size in bytes.
 */
static char *read_logfile(obix_hist_file_t *file, int *len)
{
	int fd;
	char *buf;
	struct stat statbuf;

	*len = 0;

	if (lstat(file->filepath, &statbuf) < 0 ||
		statbuf.st_size == 0 ||
		(fd = open(file->filepath, O_RDONLY)) < 0) {
		return NULL;
	}

	if (!(buf = (char *)malloc(statbuf.st_size + 1))) {
		goto failed;
	}

	if (read(fd, buf, statbuf.st_size) < statbuf.st_size) {
		free(buf);
		buf = NULL;
	} else {
		buf[statbuf.st_size] = '\0';
		*len = statbuf.st_size;
	}

	/* Fall through */

failed:
	close(fd);
	return buf;
}

static void hist_destroy_file(obix_hist_file_t *file)
{
	if (file->date) {
		free(file->date);
	}

	if (file->filepath) {
		free(file->filepath);
	}

	free(file);
}

/*
 * Create a descriptor for one history log file based on its
 * abstract element, which is read from index file during
 * initialization, or newly setup during append operation.
 *
 * Return the address of relevant file descriptor on success,
 * NULL otherwise
 */
static obix_hist_file_t *__hist_create_file(obix_hist_dev_t *dev,
											xmlNode *abstract,
											int newly_created)
{
	obix_hist_file_t *file = NULL;
	struct stat statbuf;

	if (!(file = (obix_hist_file_t *)malloc(sizeof(obix_hist_file_t)))) {
		log_error("Failed to allocae file descriptor");
		return NULL;
	}
	memset(file, 0, sizeof(obix_hist_file_t));

	file->abstract = abstract;
	INIT_LIST_HEAD(&file->list);

	if (!(file->date = xml_get_child_val(abstract, OBIX_OBJ_DATE, HIST_ABS_DATE))) {
		log_error("Failed to get val from node with tag %s, name %s",
				  OBIX_OBJ_DATE, HIST_ABS_DATE);
		goto failed;
	}

	if (link_pathname(&file->filepath, _history->dir, dev->dev_id,
					  file->date, LOG_FILENAME_SUFFIX) < 0) {
		log_error("Not enough memory to allocate absolute pathname for "
				  "log file on %s", file->date);
		goto failed;
	}

	if (newly_created == 1) {
		list_add_tail(&file->list, &dev->files);
		return file;
	}

	/* Sanity check on existing fragment files */
	if (lstat(file->filepath, &statbuf) < 0 ||
		S_ISREG(statbuf.st_mode) == 0 ||
		statbuf.st_size == 0){
		log_error("%s is not a regular file, or is empty", file->filepath);
		goto failed;
	}

	if (__hist_enqueue_file(file, dev) == 0) {
		return file;
	}

	log_error("Failed to enqueue a file descriptor for %s", file->filepath);

	/* Fall through */

failed:
	hist_destroy_file(file);
	return NULL;
}

/*
 * Allocate and setup an abstract node for a new fragment file
 */
static xmlNode *__hist_add_absnode(obix_hist_dev_t *dev, const char *date,
								   const char *start)
{
	xmlNode *node;

	if (!(node = xmldb_copy_sys(HIST_ABS_STUB))) {
		return NULL;
	}

	update_value(node, OBIX_OBJ_DATE, HIST_ABS_DATE, date);
	update_count(node, HIST_ABS_COUNT, 0);
	update_value(node, OBIX_OBJ_ABSTIME, HIST_ABS_START, start);
	update_value(node, OBIX_OBJ_ABSTIME, HIST_ABS_END, start);

	if (xmldb_add_child(dev->index, node, 0, 0) != 0) {
		log_error("Failed to add abstract node on %s into %s",
				  date, dev->href);
		xmlFreeNode(node);
		node = NULL;
	}

	return node;
}

/**
 * Create a new history fragment file and setup relevant backend data
 * structure based on the specified timestamp of its first record
 *
 * Return address of the relevant file descriptor on success,
 * NULL otherwise
 */
static obix_hist_file_t *__hist_create_fragment(obix_hist_dev_t *dev,
												const char *ts)
{
	obix_hist_file_t *file = NULL;
	xmlNode *node;
	char *filepath = NULL, *date = NULL;
	int fd;

	if (!(date = timestamp_get_utc_date(ts)) ||
		link_pathname(&filepath, _history->dir, dev->dev_id,
					  date, LOG_FILENAME_SUFFIX) < 0) {
		goto failed;
	}

	errno = 0;
	fd = creat(filepath, OBIX_FILE_PERM);
	if (fd < 0 && errno != EEXIST) {
		goto failed;
	}
	close(fd);

	if (!(node = __hist_add_absnode(dev, date, ts))) {
		goto abs_failed;
	}

	if (!(file = __hist_create_file(dev, node, 1))) {
		goto desc_failed;
	}

	xml_setup_private(file->abstract, (void *)dev);

	free(date);
	free(filepath);
	return file;

desc_failed:
	xmldb_delete_node(node, 0);

abs_failed:
	unlink(filepath);

failed:
	if (date) {
		free(date);
	}

	if (filepath) {
		free(filepath);
	}

	return file;
}

/*
 * Setup and register a XML node for a device, which bridges
 * the device's index subtree with that of global DOM tree
 *
 * Return the address of relevant XML node on success, NULL
 * otherwise
 *
 * NOTE: If the given href happens to be an ancestor of the
 * href of existing history facility, then its corresponding
 * node would have been established already because of the
 * usage of the CREATE_ANCESTORS_HISTORY option, but the
 * rest of the ancestor's history facility needs to be further
 * established
 */
static xmlNode *__hist_add_devnode(const xmlChar *href)
{
	xmlNode *node;

	if ((node = xmldb_get_node(href)) != NULL) {
		log_debug("Ancestor history facility already created at %s", href);
		return node;
	}

	if (!(node = xmldb_copy_sys(HIST_DEV_STUB))) {
		return NULL;
	}

	if (!xmlSetProp(node, BAD_CAST OBIX_ATTR_HREF, href)) {
		log_error("Failed to set href %s on relevant node", href);
		goto failed;
	}

	/*
	 * The XML nodes for ancestors hrefs may be created if needed,
	 * however, NO parent history facilities will ever be created
	 */
	if (xmldb_put_node(node, href, CREATE_ANCESTORS_HISTORY) != 0) {
		log_error("Failed to add node with href %s into XML database", href);
		goto failed;
	}

	return node;

failed:
	xmlFreeNode(node);
	return NULL;
}

/*
 * Setup and register a XML Node for the index file of a history facility
 *
 * Return the address of relevant XML Node on success, NULL otherwise
 */
static xmlNode *__hist_add_indexnode(const char *path, xmlNode *parent)
{
	struct stat statbuf;
	xmlDoc *doc;
	xmlNode *root;

	if (lstat(path, &statbuf) < 0 ||
		S_ISREG(statbuf.st_mode) == 0 ||
		statbuf.st_size == 0) {
		log_error("%s is not a valid index file", path);
		return NULL;
	}

	/*
	 * No parser dictionary is used when history index files
	 * are parsed and then inserted into the global DOM tree
	 */
	if (!(doc = xmlReadFile(path, NULL,
							XML_PARSE_OPTIONS_COMMON | XML_PARSE_NODICT))) {
		log_error("Failed to setup XML DOM tree for %s", path);
		return NULL;
	}

	if (!(root = xmlDocGetRootElement(doc))) {
		log_error("Failed to get the root element for %s", path);
		goto failed;
	}

	/*
	 * Index file's href is just "index" since creation, therefore
	 * no need to set it relative once again
	 */
	if (xmldb_add_child(parent, root, 1, 0) != 0) {
		log_error("Failed to add root node from %s into XML database", path);
		/*
		 * Explicitly release the root node on failure now that it
		 * has been unlinked from the original document
		 */
		xmlFreeNode(root);
		root = NULL;
	}

	/* Fall through */

failed:
	xmlFreeDoc(doc);
	return root;
}

static void hist_destroy_dev(obix_hist_dev_t *dev)
{
	obix_hist_file_t *file, *n;

	if (tsync_shutdown_entry(&dev->sync) < 0) {
		return;
	}

	list_for_each_entry_safe(file, n, &dev->files, list) {
		list_del(&file->list);
		hist_destroy_file(file);
	}

	if (dev->href) {
		xmlFree(dev->href);
	}

	if (dev->dev_id) {
		free(dev->dev_id);
	}

	if (dev->indexpath) {
		free(dev->indexpath);
	}

	if (dev->node) {
		xmldb_delete_node(dev->node, 0);
	}

	tsync_cleanup(&dev->sync);

	free(dev);
}

/*
 * Create a history facility for the specified device and
 * initialize it with any existing data on hard drive.
 *
 * Return new history facility's descriptor on success,
 * NULL otherwise.
 *
 * NOTE: The caller should ensure <resdir>/histories/dev_id/index.xml
 * exists and filled in with HIST_INDEX_SKELETON at least
 *
 * NOTE: On success the passed in string parameters are saved in a
 * history facility and should be released upon cleanup, so callers
 * should NOT release them instead. However, on failure callers
 * should release these strings by themselves
 */
static obix_hist_dev_t *__hist_create_dev(char *dev_id, xmlChar *href,
										  char *indexpath, int newly_created)
{
	xmlNode *node;
	xmlChar *is_attr = NULL;
	obix_hist_dev_t *dev, *n;
	obix_hist_file_t *file;

	if (!(dev = (obix_hist_dev_t *)malloc(sizeof(obix_hist_dev_t)))) {
		log_error("Failed to allocae history facility for %s", dev_id);
		return NULL;
	}
	memset(dev, 0, sizeof(obix_hist_dev_t));

	if (!(dev->node = __hist_add_devnode(href)) ||
		!(dev->index = __hist_add_indexnode(indexpath, dev->node))) {
		goto failed;
	}

	/* Save parameters directly into descriptors on success */
	dev->dev_id = dev_id;
	dev->href = href;
	dev->indexpath = indexpath;

	dev->namelen = strlen(dev_id);
	INIT_LIST_HEAD(&dev->list);
	INIT_LIST_HEAD(&dev->files);
	tsync_init(&dev->sync);

	/*
	 * Create descriptor for each fragment file when loading from disk
	 * at startup, whereas newly created facility has no fragments
	 */
	if (newly_created == 0) {
		for (node = dev->index->children; node; node = node->next) {
			if (node->type != XML_ELEMENT_NODE) {
				continue;
			}

			if (is_attr) {
				xmlFree(is_attr);
				is_attr = NULL;		/* avoid double-free */
			}

			if (xmlStrcmp(node->name, BAD_CAST OBIX_OBJ) != 0 ||
				!(is_attr = xmlGetProp(node, BAD_CAST OBIX_ATTR_IS)) ||
				xmlStrcmp(is_attr, BAD_CAST OBIX_CONTRACT_HIST_FILE_ABS) != 0) {
				continue;
			}

			if ((file = __hist_create_file(dev, node, 0)) != NULL) {
				dev->count += xml_get_child_long(file->abstract, OBIX_OBJ_INT,
												 HIST_ABS_COUNT);
			} else {
				log_error("Failed to create descriptor for one fragment file of %s",
						  dev->dev_id);
			}
		}

		if (is_attr) {
			xmlFree(is_attr);
		}
	}

	/*
	 * Enqueue the newly created history facility according to the
	 * length of its name
	 *
	 * History facilities with shorter names are placed ahead of those
	 * with longer names to ensure "descendant" facilities are behind
	 * the "ancestor" ones. At cleanup the queue is accessed in a *reverse*
	 * order to dispose descendants first so as to avoid double free
	 * of them
	 */
	list_for_each_entry(n, &_history->devices, list) {
		if (n->namelen < dev->namelen) {
			continue;
		}

		__list_add(&dev->list, n->list.prev, &n->list);
		break;
	}

	if (&n->list == &_history->devices)	{	/* empty list */
		list_add_tail(&dev->list, &_history->devices);
	}

	/*
	 * Lastly setup the _private pointers for the entire subtree
	 * of the current history facility, although race conditions
	 * may only happen against the abstract for the last fragment
	 * file
	 *
	 * This conforms with the behaviour adopted in the query handler
	 */
	xml_setup_private(dev->node, (void *)dev);

	return dev;

failed:
	if (dev->node) {
		xmldb_delete_node(dev->node, 0);
	}

	free(dev);
	return NULL;
}

/**
 * Flush index DOM tree content into index file on hard drive
 */
static void hist_flush_index(obix_hist_dev_t *dev)
{
	char *data;

	if (!(data = xml_dump_node(dev->index))) {
		log_error("Failed to dump XML subtree of %s", dev->href);
		return;
	}

	if (xml_write_file(dev->indexpath, OPEN_FLAG_SYNC, data,
					   strlen(data)) < 0) {
		log_error("Failed to save %s on hard drive", dev->href);
	}

	free(data);
}

/*
 * Append records from input contract to the given history facility,
 * return the number of records added through the last parameter
 *
 * Return 0 on success, > 0 for error code
 *
 * NOTE: Caller has entered the "write region" of relevant history
 * facility
 */
static int __hist_append_dev(obix_hist_dev_t *dev, xmlNode *input, int *added)
{
	obix_hist_file_t *file;
	xmlNode *list, *record;
	char *ts = NULL, *latest_ts = NULL;
	int count = 0, all_count = 0;
	int res, new_day = 0, ret = 0;

	*added = 0;

	if(!(list = xml_find_child(input, OBIX_OBJ_LIST,
							   OBIX_ATTR_NAME, HIST_AIN_DATA))) {
		return ERR_INVALID_INPUT;
	}

	/* Get the timestamp of the latest history record */
	if (list_empty(&dev->files) == 1) {
		file = NULL;
		latest_ts = strdup(HIST_TS_INIT);
	} else {
		file = list_last_entry(&dev->files, obix_hist_file_t, list);
		latest_ts = xml_get_child_val(file->abstract, OBIX_OBJ_ABSTIME,
									  HIST_ABS_END);
	}

	if (!latest_ts) {
		return ERR_NO_MEM;
	}

	/*
	 * Records would have to be treated separately, not only to
	 * examine their timestamp's sanity, but more importantly, to
	 * create new log file for a new date when needed
	 */
	for (record = list->children; record; record = record->next) {
		if (record->type != XML_ELEMENT_NODE) {
			continue;
		}

		/*
		 * Free strings of previous record. Due to the use of continue,
		 * they can't be released at the end of each loop
		 */
		if (ts) {
			free(ts);
		}

		if (!(ts = xml_get_child_val(record, OBIX_OBJ_ABSTIME, HIST_REC_TS)) ||
			timestamp_compare(ts, latest_ts, &res, &new_day) < 0) {
			ret = ERR_TS_COMPARE;
			continue;
		}

		/*
		 * Newly added history records MUST not include a timestamp
		 * older than or equal to the latest one
		 */
		if (res <= 0) {
			log_debug("ts: %s VS latest_ts: %s", ts, latest_ts);
			ret = ERR_TS_OBSOLETE;
			continue;
		}

		/* Create a new fragment file for the new date */
		if (new_day == 1) {
			if (count > 0) {
				add_abs_count(file, count);
				count = 0;		/* Reset counter for the new log file */
			}

			if (!(file = __hist_create_fragment(dev, ts))) {
				ret = ERR_HISTORY_IO;
				goto failed;
			}
		}

		if (write_logfile(file, record) < 0) {
			ret = ERR_HISTORY_IO;
			goto failed;
		}

		update_value(file->abstract, OBIX_OBJ_ABSTIME, HIST_ABS_END, ts);

		/* Update latest_ts */
		free(latest_ts);
		if (!(latest_ts = strdup(ts))) {
			ret = ERR_NO_MEM;
			goto failed;
		}

		count++;
		all_count++;
	}

	/*
	 * Don't mix up count and all_count, the former is about the number of
	 * records written to a single log file and gets reset to zero when it
	 * comes to a new log file, while the latter is about the total number
	 * of records successfully written into current device.
	 */
	if (count > 0) {
		add_abs_count(file, count);
	}

	if (all_count > 0) {
		dev->count += all_count;
		hist_flush_index(dev);
		*added = all_count;
	}

	/* TODO:
	 * How to handle the case when only partial records are successfully
	 * written and the rest are failed due to errors such as -4, -5 and -6?
	 *
	 * In this case, (all_count > 0 && ret < 0)
	 */

	/* Fall through */

failed:
	if (latest_ts) {
		free(latest_ts);
	}

	if (ts) {
		free(ts);
	}

	return ret;
}

/**
 * Append records from input contract to history log files
 *
 * Return 0 on success, > 0 for error code
 */
static int hist_append_dev(obix_request_t *request,
						   obix_hist_dev_t *dev, xmlNode *input)
{
	char *start = NULL, *end = NULL;
	obix_hist_file_t *first, *last;
	xmlNode *aout = NULL;
	char *data;
	long count;
	int added, ret;

	if (tsync_writer_entry(&dev->sync) < 0) {
		log_error("History facility %s shouldn't have been marked as "
				  "being shutdown", dev->dev_id);
		return ERR_INVALID_STATE;
	}

	if ((ret = __hist_append_dev(dev, input, &added)) > 0) {
		tsync_writer_exit(&dev->sync);
		return ret;
	}

	/* dev->files is ensured not empty */
	first = list_first_entry(&dev->files, obix_hist_file_t, list);
	last = list_last_entry(&dev->files, obix_hist_file_t, list);
	count = dev->count;

	start = xml_get_child_val(first->abstract, OBIX_OBJ_ABSTIME, HIST_ABS_START);
	end = xml_get_child_val(last->abstract,	OBIX_OBJ_ABSTIME, HIST_ABS_END);

	tsync_writer_exit(&dev->sync);

	/* Allocate and setup a HistoryAppendOut contract */

	if (!(aout = xmldb_copy_sys(HIST_AOUT_STUB))) {
		ret = ERR_NO_MEM;
		goto failed;
	}

	update_count(aout, AOUT_NUMADDED, added);
	update_count(aout, AOUT_NEWCOUNT, count);

	if (start) {
		update_value(aout, OBIX_OBJ_ABSTIME, AOUT_NEWSTART, start);
	}

	if (end) {
		update_value(aout, OBIX_OBJ_ABSTIME, AOUT_NEWEND, end);
	}

	if (!(data = xml_dump_node(aout))) {
		ret = ERR_NO_MEM;
		goto failed;
	}

	if (obix_request_create_append_response_item(request, data,
												 strlen(data), 0) < 0) {
		free(data);
		ret = ERR_NO_MEM;
	} else {
		ret = 0;	/* Success */
	}

	/* Fall through */

failed:
	if (start) {
		free(start);
	}

	if (end) {
		free(end);
	}

	if (aout) {
		xmlFreeNode(aout);
	}

	return ret;
}

/*
 * We are going to parse the log file all by ourselves instead of employing
 * DOM tree for sake of efficiency. To this end the format of markups of a
 * record and timestamp tag have to be crystal-clearly defined, which should
 * be in accordance with those provided in HistoryAppendIn contract and the
 * outcome of xml_dump_node(called by write_logfile).
 *
 * NOTE: "\r\n" is appended for a history record before writing into the
 * raw history data file thus "</obj>\r\n" can be used as the boundary of it.
 * Another prerequisite is it is not used anywhere inside of a history record,
 * which is true in current implementation.
 */
static const char *RECORD_START = "<obj is=\"obix:HistoryRecord\">";
static const char *RECORD_END = "</obj>\r\n";
static const char *TS_VAL_START = "<abstime name=\"timestamp\" val=\"";
static const char *TS_VAL_END = "\"";

/*
 * Parse the content of a log file pointed to by data, no more than
 * limit number of records within specified time range of [start, end]
 * will be returned.
 *
 * Return a memory region that only contains desirable records, NULL
 * on errors. The original memory region will be downsized and no extra
 * memory needs to be allocated ever.
 *
 * On return, limit will be set as the number of satisfactory records,
 * and *end_ts points to the timestamp of the last record while *start_ts
 * points to that of the first record if required to set.
 *
 * Note,
 * 1. Callers need not / should not free timestamp pointers since they are
 * pointing to static memory buffers.
 */
static char *parse_log(char *data,
					   char *start, char *end,
					   int *limit,
					   char **start_ts, char **end_ts,
					   int *len_data)
{
	char *n;			/* Iterator among data */
	char *p;			/* Start of a record */
	char *w;			/* Where desirable record should be moved to */
	char *ts;
	int r, len, ret;
	int use_val_prev = 0;	/* The val_prev instead of val TS should be used */

	/*
	 * Timestamp of each record would have to be copied and made a standalone
	 * string so as to be compared with the specified [start, end] range.
	 * To this end, use static buffers for sake of efficiency.
	 *
	 * In fact, log_start will eventually hold the timestamp of the first
	 * record in current HistoryQueryOut contract and val or val_prev will
	 * contain that of the very last record for the current log file, depending
	 * on wether val exceeds the specified end timestamp.
	 */
	static char val[HIST_REC_TS_MAX_LEN + 1];
	static char val_prev[HIST_REC_TS_MAX_LEN + 1];
	static char log_start[HIST_REC_TS_MAX_LEN + 1];
	int is_start_set;

	r = 0;
	is_start_set = 0;
	w = n = data;
	for (p = strstr(n, RECORD_START); p; p = strstr(n, RECORD_START)) {
		if (!(ts = strstr(p, TS_VAL_START))) {	/* Start of timestamp element */
			log_error("No timestamp markup \"%s\" in current record",
						TS_VAL_START);
			goto failed;
		}

		ts += strlen(TS_VAL_START);		/* Start of timestamp value */

		if (!(n = strstr(ts, TS_VAL_END))) {	/* End of timestamp value */
			log_error("No timestamp markup \"%s\" in current record",
						TS_VAL_END);
			goto failed;
		}

		memcpy(val_prev, val, HIST_REC_TS_MAX_LEN + 1);

		len = n - ts;
		strncpy(val, ts, len);
		*(val + len) = '\0';

		/*
		 * If start or end timestamps are NULL, then all records in current
		 * log file are satisfactory so the chores to compare timestamps
		 * can be safely spared. Read consecutive amount of records since
		 * the beginning of log file until enough of them are found.
		 */
		if (start && end) {
			ret = timestamp_has_common(val, val, start, end);
			if (ret == -2) {
				/* Error */
				continue;
			} else if (ret == -3) {
				/*
				 * TS of current record is later than [start, end], since
				 * records are in date ascending order, no needs to search
				 * any more
				 */
				use_val_prev = 1;
				break;
			} else if (ret == -4) {
				/*
				 * TS of current record is earlier than [start, end], keep
				 * searching among the rest of log file
				 */
				continue;
			}
		}

		if (!(n = strstr(n, RECORD_END))) {
			log_error("No %s markup in current record\n", RECORD_END);
			goto failed;
		}

		n += strlen(RECORD_END);	/* Next byte after the end of current record */

		/* Move the desirable record to earlier part of data buf */
		if (w == p) {
			w = n;
		} else {
			while (p < n) {
				*w++ = *p++;
			}
		}

		/*
		 * Keep track of timestamp of the very first record as the
		 * "start" timestamp of current HistoryQueryOut contract
		 * only when it has not been set up yet
		 */
		if (is_start_set == 0 && start_ts) {
			strcpy(log_start, val);
			is_start_set = 1;
		}

		if (++r == *limit) {
			break;
		}
	}

	if (start_ts) {
		if (!(*start_ts = strdup(log_start))) {
			goto failed;
		}
	}

	if (*end_ts) {
		free(*end_ts);
	}

	*end_ts = (use_val_prev == 1) ? strdup(val_prev) : strdup(val);
	if (!*end_ts) {
		goto failed;
	}

	*limit = r;
	*w = '\0';

	*len_data = w - data;		/* strlen */

	return realloc(data, w - data + 1);		/* Preserve NULL terminator */

failed:
	free(data);
	return NULL;
}

/*
 * Query records from device's history facilities
 *
 * Return 0 on success, > 0 on errors
 */
static int __hist_query_dev(obix_request_t *request,
							obix_hist_dev_t *dev, xmlNode *input)
{
	long limit;									/* the number of records wanted */
	char *start = NULL, *end = NULL;			/* start/end TS specified in input  */
	char *d_oldest = NULL, *d_latest = NULL;	/* oldest/latest ts for the device */
	char *f_oldest = NULL, *f_latest = NULL;	/* oldest/latest ts for a log file */
	char *start_ts = NULL, *end_ts = NULL;		/* start/end TS in HistoryQueryOut */
	long n;										/* maximal num of records to read */
	int r = 0;									/* actual num of records read */
	int count;									/* num of records read from a log file */
	int ret = ERR_NO_MEM;						/* default error code */
	int len, res;
	int start_unspecified = 0;
	int end_unspecified = 0;
	obix_hist_file_t *file, *first, *last;
	response_item_t *item;
	char *data;

	if (list_empty(&dev->files) == 1) {
		return ERR_HISTORY_EMPTY;
	}

	first = list_first_entry(&dev->files, obix_hist_file_t, list);
	last = list_last_entry(&dev->files, obix_hist_file_t, list);

	d_oldest = xml_get_child_val(first->abstract, OBIX_OBJ_ABSTIME,
								 HIST_ABS_START);
	d_latest = xml_get_child_val(last->abstract, OBIX_OBJ_ABSTIME,
								 HIST_ABS_END);

	if (!d_oldest || !d_latest) {
		ret = ERR_HISTORY_DATA;
		goto failed;
	}

	/* All start and end TS and limit are allowable to be omitted */
	if (!(start = xml_get_child_val(input, OBIX_OBJ_ABSTIME, FILTER_START))) {
		start_unspecified = 1;
		if (!(start = strdup(d_oldest))) {
			goto failed;
		}
	}

	if (!(end = xml_get_child_val(input, OBIX_OBJ_ABSTIME, FILTER_END))) {
		end_unspecified = 1;
		if (!(end = strdup(d_latest))) {
			goto failed;
		}
	}

	if ((limit = xml_get_child_long(input, OBIX_OBJ_INT, FILTER_LIMIT)) == 0) {
		/*
		 * If the number of records wanted equals to zero, then return
		 * the timestamps for the very first and last records of the
		 * current device
		 */
		goto no_matching_data;
	}

	/*
	 * If not specified or explicity set as a negative value, then
	 * fetch all available records for the current device
	 */
	n = (limit < 0 || limit > dev->count) ? dev->count : limit;

	res = timestamp_has_common(start, end, d_oldest, d_latest);
	if (res == -2) {
		ret = ERR_TS_COMPARE;
		goto failed;
	} else if (res == -3 || res == -4) {
		/*
		 * Before return a HistoryQueryOut contract with an empty data
		 * list, it is desirable to unset start or end timestamp if they
		 * are not specified in the first place
		 */
		if (start_unspecified == 1) {
			free(start);
			if (!(start = strdup(HIST_AIN_TS_UND))) {
				goto failed;
			}
		}

		if (end_unspecified == 1) {
			free(end);
			if (!(end = strdup(HIST_AIN_TS_UND))) {
				goto failed;
			}
		}

		goto no_matching_data;
	}

	if (timestamp_find_common(&start, &end, d_oldest, d_latest) < 0) {
		ret = ERR_TS_COMPARE;
		goto failed;
	}

	list_for_each_entry(file, &dev->files, list) {
		/*
		 * Due to the use of continue, f_oldest and f_latest
		 * of the previous log file should be released before
		 * recording those of the current log file
		 */
		if (f_oldest) {
			free(f_oldest);
		}

		if (f_latest) {
			free(f_latest);
		}

		f_oldest = xml_get_child_val(file->abstract, OBIX_OBJ_ABSTIME,
									 HIST_ABS_START);
		f_latest = xml_get_child_val(file->abstract, OBIX_OBJ_ABSTIME,
									 HIST_ABS_END);
		count = xml_get_child_long(file->abstract, OBIX_OBJ_INT,
								   HIST_ABS_COUNT);

		res = timestamp_has_common(start, end, f_oldest, f_latest);
		if (res == -2) {
			/* Error */
			ret = ERR_TS_COMPARE;
			goto flush_response;
		} else if (res == -3) {
			/*
			 * Records in current log file are earlier than the specified
			 * range, keep looking for among the rest of log files
			 */
			continue;
		} else if (res == -4) {
			/*
			 * Records in current log file are later than the specified
			 * range, since log file descriptors are sorted in date ascending
			 * order, no need to check following log files any more. Done.
			 */
			break;
		} else {
			/* Content of current log file needs to be returned */
			if (!(data = read_logfile(file, &len))) {
				ret = ERR_HISTORY_IO;
				goto flush_response;
			}

			/*
			 * If both f_oldest and f_latest locates within [start, end] and
			 * the number of records in current log file is no more than that
			 * of requested then the whole log file should be returned.
			 *
			 * Otherwise, only the satisfactory part of current log file needs
			 * to be identified and returned.
			 *
			 * However, there is one more optimization we can make. For the
			 * case of the whole records in current log files satisfy timestamps
			 * requirement but it contains more number of records than desirable,
			 * consecutive number of records since the start of the log file
			 * should be directly returned without having to compare each of
			 * their  timestamp any more.
			 */
			if (timestamp_has_common(f_oldest, f_oldest, start, end) == 1 &&
				timestamp_has_common(f_latest, f_latest, start, end) == 1) {
				if (count <= n) {
					/*
					 * The whole content of current log file is desirable
					 * and already loaded in data. Now just update start_ts
					 * and end_ts if needed.
					 */
					if (!start_ts) {
						if (!(start_ts = strdup(f_oldest))) {
							goto flush_response;
						}
					}

					if (end_ts) {
						free(end_ts);
					}

					if (!(end_ts = strdup(f_latest))) {
						goto flush_response;
					}
				} else {
					count = n;
					if (!(data = parse_log(data, NULL, NULL, &count,
											(!start_ts) ? &start_ts : NULL,
											&end_ts, &len))) {
						ret = ERR_HISTORY_DATA;
						goto flush_response;
					}
				}
			} else {	/* Records' timestamp would have to be compared */
				count = n;
				if (!(data = parse_log(data, start, end, &count,
										(!start_ts) ? &start_ts : NULL,
										&end_ts, &len))) {
					ret = ERR_HISTORY_DATA;
					goto flush_response;
				}
			}

			if (!(item = obix_request_create_response_item(data, len, 0))) {
				free(data);
				goto flush_response;
			}

			obix_request_append_response_item(request, item);

			/* Count the number of records read, finish if reaching the limit */
			r += count;
			if ((n -= count) == 0) {
				break;
			}
		} /* else */
	} /* list_for_each_entry */

no_matching_data:
	/* Add HistoryQueryOut contract header */
	len = strlen(HIST_QUERY_OUT_PREFIX) + HIST_FLT_VAL_MAX_BITS +
			HIST_REC_TS_MAX_LEN * 2 - 6;

	if (!(data = (char *)malloc(len + 1))) {
		goto flush_response;
	}

	/*
	 * Pay attention that the length of a response item must be
	 * decided by the actual number of bytes printed instead of those
	 * allocated, since the latter is deliberately spacious enough to
	 * to accommodate the maximum count number.
	 *
	 * Otherwise the client side will complain connection is closed
	 * by server before all claimed number of bytes could be read.
	 */
	len = sprintf(data, HIST_QUERY_OUT_PREFIX, r,
					((start_ts != NULL) ? start_ts : start),
					((end_ts != NULL) ? end_ts : end));

	if (!(item = obix_request_create_response_item(data, len, 0))) {
		free(data);
		goto flush_response;
	}

	obix_request_add_response_item(request, item);

	/* Tail HistoryQueryOut contract footer */
	if (!(item = obix_request_create_response_item(HIST_QUERY_OUT_SUFFIX,
										strlen(HIST_QUERY_OUT_SUFFIX), 1))) {
		goto flush_response;
	}

	obix_request_append_response_item(request, item);

	ret = 0;

	/* Fall through */

flush_response:
	/*
	 * On error, only the response items that may have been inserted
	 * into response list need to be wiped out so as to make room for
	 * the error contract, while the [request, response] pair must be
	 * preserved until error contract is sent out
	 */
	if (ret > 0) {
		obix_request_destroy_response_items(request);
	}

failed:
	if (start) {
		free(start);
	}

	if (end) {
		free(end);
	}

	if (d_oldest) {
		free(d_oldest);
	}

	if (d_latest) {
		free(d_latest);
	}

	if (f_oldest) {
		free(f_oldest);
	}

	if (f_latest) {
		free(f_latest);
	}

	if (start_ts) {
		free(start_ts);
	}

	if (end_ts) {
		free(end_ts);
	}

	return ret;
}

/*
 * Handle history.query requests on relevant history facility
 *
 * Return 0 on success, > 0 for error code
 */
static int hist_query_dev(obix_request_t *request,
						  obix_hist_dev_t *dev, xmlNode *input)
{
	int ret;

	if (tsync_reader_entry(&dev->sync) < 0) {
		log_error("History facility %s shouldn't have been marked as "
				  "being shutdown", dev->dev_id);
		return ERR_INVALID_STATE;
	}

	ret = __hist_query_dev(request, dev, input);

	tsync_reader_exit(&dev->sync);
	return ret;
}

static obix_hist_ops_t obix_hist_operations = {
	.query = hist_query_dev,
	.append = hist_append_dev,
};

void obix_hist_dispose(void)
{
	obix_hist_dev_t *dev, *n;

	/*
	 * Just in case oBIX server gets shutdown before
	 * history facility is ever initialized
	 */
	if (!_history) {
		return;
	}

	pthread_mutex_lock(&_history->mutex);
	/*
	 * IMPORTANT!
	 * If any history facilities are parent to others, they are assured
	 * to be created before any of their children. Therefore on exit
	 * any children facilities should be disposed before their parent
	 * so as to avoid double-free
	 *
	 * To this end, the queue is traversed in a REVERSE order
	 */
	list_for_each_entry_safe_reverse(dev, n, &_history->devices, list) {
		list_del(&dev->list);
		hist_destroy_dev(dev);
	}
	pthread_mutex_unlock(&_history->mutex);

	free(_history->dir);

	pthread_mutex_destroy(&_history->mutex);

	free(_history);
	_history = NULL;

	log_debug("The History subsystem disposed");
}

/*
 * All history irrelevant but possibly existing sub folders
 * under the histories/ folder are enumerated here, they are
 * skipped over during initialisation
 *
 * NOTE: there is no need to list regular files that may exist
 * under histhories/ folder since they are ignored by default
 */
static const char *skipped_dirs[] = {
	"lost+found",
	NULL
};

static int is_skipped_dir(const char *dir)
{
	int i;

	for (i = 0; skipped_dirs[i]; i++) {
		if (strcmp(dir, skipped_dirs[i]) == 0) {
			return 1;
		}
	}

	/* Not on the black-list */
	return 0;
}

static int hist_get_href(const char *token, void *arg1, void *arg2)
{
	char **href = (char **)arg1;

	if (!*href) {
		return ((*href = strdup(token)) != NULL) ? 0 : -1;
	}

	/*
	 * Concatenate existing string with the new token, delimited by
	 * an '/' character
	 */
	if (!(*href = realloc(*href, strlen(*href) + strlen(token) + 2)) ||
		!(*href = strcat(*href, STR_DELIMITER_SLASH)) ||
		!(*href = strcat(*href, token))) {
		return -1;
	}

	return 0;
}

/*
 * Create a history facility based on disk files in the current sub folder
 *
 * Return 0 on success, -1 otherwise
 */
static int hist_load_dev(const char *parent_dir, const char *subdir,
						 void *arg)	/* ignored */
{
	struct stat statbuf;
	char *dev_id, *href, *subhref, *indexpath, *path;
	int ret = -1;

	dev_id = href = subhref = indexpath = NULL;

	if (link_pathname(&path, parent_dir, subdir, NULL, NULL) < 0) {
		log_error("Failed to assemble pathname for %s", subdir);
		return -1;
	}

	/* Skip over non-folder files, buggy or irrelevant sub folders */
	if (lstat(path, &statbuf) < 0 ||
		S_ISDIR(statbuf.st_mode) == 0 ||
		is_skipped_dir(subdir) == 1) {
		free(path);
		log_debug("Skipping history irrelevant file: %s", subdir);
		return 0;
	}
	free(path);

	if (for_each_str_token(STR_DELIMITER_DOT, subdir,
						   hist_get_href, &subhref, NULL) < 0) {
		log_error("Failed to convert %s into href format", subdir);
		return -1;
    }

	if (!(dev_id = strdup(subdir)) ||
		link_pathname(&indexpath, parent_dir, dev_id, HIST_INDEX_FILENAME,
					  XML_FILENAME_SUFFIX) < 0 ||
		link_pathname(&href, OBIX_HISTORY_LOBBY, NULL, subhref, NULL) < 0) {
		log_error("Failed to allocate meta information for %s", subdir);
		goto failed;
	}

	if (__hist_create_dev(dev_id, (xmlChar *)href, indexpath, 0) != NULL) {
		/*
		 * On success the address of name, href and index file's pathname
		 * are all saved in the device descriptor and these strings are
		 * released along with it during clean-up
		 */
		free(subhref);
		return 0;
	}

	log_error("Failed to setup history facility for %s", subdir);

	/* Fall through */

failed:
	if (subhref) {
		free(subhref);
	}

	if (href) {
		free(href);
	}

	if (dev_id) {
		free(dev_id);
	}

	if (indexpath) {
		free(indexpath);
	}

	return ret;
}

/*
 * Initialise the history subsystem
 *
 * Return 0 on success, > 0 for error code
 */
int obix_hist_init(const char *resdir)
{
	if (_history) {
		return 0;
	}

	if (!(_history = (obix_hist_t *)malloc(sizeof(obix_hist_t)))) {
		log_error("Failed to alloc a history descriptor");
		return ERR_NO_MEM;
	}
	memset(_history, 0, sizeof(obix_hist_t));

	if (link_pathname(&_history->dir, resdir, HISTORIES_DIR, NULL, NULL) < 0) {
		log_error("Failed to init history: not enough memory");
		free(_history);
		_history = NULL;
		return ERR_NO_MEM;
	}

	_history->op = &obix_hist_operations;
	INIT_LIST_HEAD(&_history->devices);
	pthread_mutex_init(&_history->mutex, NULL);

	if (for_each_file_name(_history->dir, NULL, NULL,	/* all possible names */
						   hist_load_dev, NULL) < 0) {
		log_error("Failed to setup history facilities from %s",
				  _history->dir);
		obix_hist_dispose();
		return ERR_NO_MEM;
	}

	log_debug("The History subsystem initialised");
	return 0;
}

/*
 * Try to find a history facility for the specified device.
 *
 * Return its obix_hist_dev_t address on success, NULL otherwise.
 */
static obix_hist_dev_t *hist_find_device(const char *dev_id)
{
	obix_hist_dev_t *dev;

	pthread_mutex_lock(&_history->mutex);
	list_for_each_entry(dev, &_history->devices, list) {
		/* The whole string needs to be exactly the same */
		if (is_str_identical((xmlChar *)dev->dev_id,
							 (xmlChar *)dev_id, 0) == 1) {
			pthread_mutex_unlock(&_history->mutex);
			return dev;
		}
	}
	pthread_mutex_unlock(&_history->mutex);

	return NULL;
}

static int get_dev_id_helper(const char *token, void *arg1, void *arg2)
{
	char **dev_id = (char **)arg1;

	if (!*dev_id) {
		return ((*dev_id = strdup(token)) != NULL) ? 0 : -1;
	}

	/*
	 * Concatenate existing string with the new token, delimited by
	 * an '.' character
	 */
	if (!(*dev_id = realloc(*dev_id, strlen(*dev_id) + strlen(token) + 2)) ||
		!(*dev_id = strcat(*dev_id, STR_DELIMITER_DOT)) ||
		!(*dev_id = strcat(*dev_id, token))) {
		return -1;
	}

	return 0;
}

/*
 * Get and duplicate the device ID string from the given URI,
 * which is in below format:
 *
 *	/obix/historyServices/histories/X1/DHXX/BCMXX/CBXX/op_name
 *
 * Where X1 stands for the data center name, DHXX is the name of
 * data hall name(of a data center), BCMXX is the name of a BCM
 * device and lastly, CBXX is the name of a CB device.
 *
 * There could be extra hierarchies between DHXX and BCMXX therefore
 * no assumption should be made and it is this function's responsibility
 * to convert them into a new string from slash delimiter to dot
 * delimiter.
 *
 * Return 0 on success, > 0 for error code
 *
 * Note,
 * 1. Callers should free the returned device ID string after use.
 */
static int hist_get_dev_id(const char *uri, const char *op_name, char **dev_id)
{
	char *str, *end;
	int len = OBIX_HISTORY_LOBBY_LEN;
	int ret;

	*dev_id = NULL;

	/*
	 * Skip history lobby uri if present, which is the case
	 * for the append and query handlers
	 */
	if (strncmp(uri, OBIX_HISTORY_LOBBY, len) == 0) {
		uri += len;
	}

	if (op_name != NULL) {
		if (!(end = strstr(uri, op_name))) {
			return ERR_HISTORY_DEVID;
		}
		len = end - uri - 1;	/* Minus the "/" preceding op_name */
	} else {
		end = (char *)uri + strlen(uri);
		len = (slash_followed(uri) == 1) ? (end - uri - 1) : (end - uri);
	}

	if (!(str = strndup(uri, len))) {
		return ERR_NO_MEM;
	}

	ret = for_each_str_token(STR_DELIMITER_SLASH, str,
							 get_dev_id_helper, dev_id, NULL);
	free(str);

	return (ret < 0) ? ERR_NO_MEM : 0;
}

static xmlNode *handlerHistoryHelper(obix_request_t *request,
									 const xmlChar *uri,
									 xmlNode *input,
									 const char *op_name)
{
	obix_hist_dev_t *dev;
	char *dev_id;
	int ret = ERR_NO_MEM;

	/* Find the device to operate on */
	if ((ret = hist_get_dev_id((char *)uri, op_name, &dev_id)) != 0) {
		goto failed;
	}

	dev = hist_find_device(dev_id);
	free(dev_id);
	if (!dev) {
		ret = ERR_NO_SUCH_URI;
		goto failed;
	}

	/* Invoke handler in response to request */
	if (strcmp(op_name, HIST_OP_APPEND) == 0) {
		ret = _history->op->append(request, dev, input);
	} else if (strcmp(op_name, HIST_OP_QUERY) == 0) {
		ret = _history->op->query(request, dev, input);
	} else {
		ret = ERR_NO_SUCH_URI;
	}

	/* Add XML Header */
	if (ret == 0 && obix_request_add_response_xml_header(request) == 0) {
		request->is_history = 1;
		obix_request_send_response(request);
		return NULL;	/* Success */
	}

	/* Fall through */

failed:
	log_error("%s : %s", uri, server_err_msg[ret].msgs);

	return obix_server_generate_error(uri, server_err_msg[ret].type,
									  op_name, server_err_msg[ret].msgs);
}

xmlNode *handlerHistoryAppend(obix_request_t *request, const xmlChar *uri,
							  xmlNode *input)
{
	return handlerHistoryHelper(request, uri, input, HIST_OP_APPEND);
}

xmlNode *handlerHistoryQuery(obix_request_t *request, const xmlChar *uri,
							 xmlNode *input)
{
	return handlerHistoryHelper(request, uri, input, HIST_OP_QUERY);
}

/*
 * Create and setup a folder with a skeleton index file for
 * a brand-new history facility
 *
 * Return 0 on success, > 0 for error
 */
static int __hist_create_backend(const char *devdir, const char *indexpath)
{
	int ret = ERR_DISK_IO;

	errno = 0;
	if (mkdir(devdir, OBIX_DIR_PERM) < 0) {
		log_error("Failed to mkdir %s because of %s", devdir,
				  strerror(errno));
		if (errno == EDQUOT || errno == ENOMEM || errno == ENOSPC) {
			ret = ERR_NO_MEM;
		}

		return ret;
	}

	errno = 0;
	if (creat(indexpath, OBIX_FILE_PERM) < 0) {
		log_error("Failed to creat %s because of %s", indexpath,
				  strerror(errno));
		if (errno == EDQUOT || errno == ENOMEM || errno == ENOSPC) {
			ret = ERR_NO_MEM;
		}

		goto failed;
	}

	if (xml_write_file(indexpath, OPEN_FLAG_SYNC, HIST_INDEX_SKELETON,
					   strlen(HIST_INDEX_SKELETON)) < 0) {
		goto write_failed;
	}

	return 0;

write_failed:
	unlink(indexpath);

failed:
	rmdir(devdir);
	return ret;
}

xmlNode *handlerHistoryGet(obix_request_t *request, const xmlChar *uri,
						   xmlNode *input)
{
	obix_hist_dev_t *dev;
	char *href, *subhref, *dev_id, *requester_id, *devdir, *indexpath, *data;
	int len, ret = ERR_NO_MEM;

	href = subhref = dev_id = requester_id = devdir = indexpath = data = NULL;

	if (!(requester_id = obix_fcgi_get_requester_id(request))) {
		ret = ERR_NO_REQUESTER_ID;
		goto failed;
	}

	if (!(subhref = xml_get_child_val(input, OBIX_OBJ_STR, DEVICE_ID))) {
		ret = ERR_INVALID_INPUT;
		goto failed;
	}

	if (hist_get_dev_id(subhref, NULL, &dev_id) != 0) {
		ret = ERR_HISTORY_DEVID;
		goto failed;
	}

	if (link_pathname(&devdir, _history->dir, dev_id, NULL, NULL) < 0 ||
		link_pathname(&indexpath, devdir, NULL, HIST_INDEX_FILENAME,
					  XML_FILENAME_SUFFIX) < 0 ||
		link_pathname(&href, OBIX_HISTORY_LOBBY, NULL, subhref, NULL) < 0) {
		ret = ERR_NO_MEM;
		goto failed;
	}

	len = strlen(HIST_GET_OUT_SKELETON) + strlen(dev_id) + strlen(href) - 4;
	if (!(data = (char *)malloc(len + 1))) {
		ret = ERR_NO_MEM;
		goto failed;
	}
	len = sprintf(data, HIST_GET_OUT_SKELETON, dev_id, href);

	/* "find + create" should be done atomically to avoid races */
	pthread_mutex_lock(&_history->mutex);
	list_for_each_entry(dev, &_history->devices, list) {
		if (is_str_identical((xmlChar *)dev->dev_id,
							 (xmlChar *)dev_id, 0) == 1) {
			pthread_mutex_unlock(&_history->mutex);
			/*
			 * Release strings that would have been referenced in
			 * a facility descriptor and nullify their pointers to
			 * avoid double-free in case of error
			 */
			free(dev_id);
			free(href);
			free(indexpath);
			dev_id = href = indexpath = NULL;
			goto existed;
		}
	}

	/* Create the history facility upon request */

	if (se_lookup(requester_id, OBIX_ID_HISTORY, OP_HISTORY_CREATE) == 0) {
		pthread_mutex_unlock(&_history->mutex);
		ret = ERR_PERM_DENIED;
		goto failed;
	}

	if ((ret = __hist_create_backend(devdir, indexpath)) > 0) {
		pthread_mutex_unlock(&_history->mutex);
		goto failed;
	}

	if (!(dev = __hist_create_dev(dev_id, (xmlChar *)href, indexpath, 1))) {
		pthread_mutex_unlock(&_history->mutex);
		ret = ERR_NO_MEM;
		goto failed;
	}

	ret = 0;			/* history facility created successfully */

	pthread_mutex_unlock(&_history->mutex);

	/* Device created, fall through */

existed:
	if (obix_request_add_response_xml_header(request) == 0 &&
		obix_request_create_append_response_item(request, data, len, 0) == 0) {
		free(subhref);
		free(requester_id);
		free(devdir);

		request->response_uri = xmlStrdup(dev->href);
		request->is_history = 1;
		obix_request_send_response(request);

		return NULL;	/* Success */
	}

	/*
	 * Failed to create response item won't have the newly created
	 * history facility discarded
	 */

failed:
	if (data) {
		free(data);
	}

	if (subhref) {
		free(subhref);
	}

	if (requester_id) {
		free(requester_id);
	}

	if (devdir) {
		free(devdir);
	}

	if (ret > 0) {
		if (href) {
			free(href);
		}

		if (dev_id) {
			free(dev_id);
		}

		if (indexpath) {
			free(indexpath);
		}
	}

	log_error("%s : %s", uri, server_err_msg[ret].msgs);

	/*
	 * On error, wipe out all potentially added response items
	 * to make room for the error contract
	 */
	obix_request_destroy_response_items(request);

	return obix_server_generate_error(uri, server_err_msg[ret].type,
									  "History.Get", server_err_msg[ret].msgs);
}

/*
 * Copy the given subtree in history facilities. Basically it's similar
 * to xml_copy_r() but takes extra care to come across "read regions"
 * of different history facilities
 */
static xmlNode *__hist_copy_node(obix_hist_dev_t *previous, const xmlNode *src,
								 xml_copy_flags_t flags, int depth)
{
	obix_hist_dev_t *current = (obix_hist_dev_t *)src->_private;
	xmlNode *node, *copy_src = NULL, *copy_node = NULL;
	int ret = ERR_NO_MEM;

	/*
	 * Come across the boundary of different "read regions" if needed
	 */
	if (previous && previous != current) {
		tsync_reader_exit(&previous->sync);
	}

	if (current && current != previous) {
		if (tsync_reader_entry(&current->sync) < 0) {
			if (previous) {
				tsync_reader_entry(&previous->sync);
			}
			return NULL;
		}
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
		if (!(copy_node = __hist_copy_node(current, node, flags, ++depth))) {
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
	if (current && current != previous) {
		tsync_reader_exit(&current->sync);
	}

	if (previous && previous != current) {
		tsync_reader_entry(&previous->sync);
	}

	if (ret > 0 && copy_src) {
		xmlFreeNode(copy_src);
		copy_src = NULL;
	}

	return copy_src;
}

/*
 * Find the "youngest" or "smallest" history facility that hosts
 * the given href.
 *
 * Take advantage of the fact that history facility descriptors
 * are organised in the ascending order of the length of their
 * names, so search the queue from tail to head until find the
 * first history facility whose href prefixes the given href
 *
 * NOTE: since history facilities are not removable, there is no
 * need to manipulate a refcnt_t to manage its life cycle and there
 * is no risk to split the search operation and further access
 * attempts on a history facility descriptor
 */
static obix_hist_dev_t *hist_search(const xmlChar *href)
{
	obix_hist_dev_t *dev;

	if (is_given_type(href, OBIX_HISTORY) == 0) {
		return NULL;
	}

	pthread_mutex_lock(&_history->mutex);
	list_for_each_entry_reverse(dev, &_history->devices, list) {
		if (xmlStrstr(href, dev->href) != NULL) {
			pthread_mutex_unlock(&_history->mutex);
			return dev;
		}
	}
	pthread_mutex_unlock(&_history->mutex);

	return NULL;
}

/*
 * Get a subnode in the given history facility contract
 */
static xmlNode *__hist_get_node_core(obix_hist_dev_t *dev, const xmlChar *href)
{
	xmlNode *node;

	if (is_str_identical(href, dev->href, 1) == 1) {
		node = dev->node;
	} else {
		node = xmldb_get_node_core(dev->node, href + xmlStrlen(dev->href));
    }

	return node;
}

/*
 * Copy a subtree at the given href, which may belong to a particular
 * history facility or a high-level href that contains a number of
 * history facilities
 *
 * Return the copy's pointer on success, NULL otherwise
 */
xmlNode *hist_copy_uri(const xmlChar *href, xml_copy_flags_t flag)
{
	obix_hist_dev_t *dev;
	xmlNode *node, *copy = NULL;

	if (!(dev = hist_search(href))) {
		/*
		 * href points to some common history facilities that have
		 * no descriptors created
		 */
		node = xmldb_get_node(href);
	} else {
		/* Won't fail since history facilities are not removable */
		tsync_reader_entry(&dev->sync);
		node = __hist_get_node_core(dev, href);
	}

	if (node) {
		copy = __hist_copy_node(dev, node, flag, 0);
	}

	if (dev) {
		tsync_reader_exit(&dev->sync);
	}

	return copy;
}

