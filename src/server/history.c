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
#include <sys/uio.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include "list.h"
#include "log_utils.h"
#include "obix_utils.h"
#include "xml_utils.h"
#include "xml_storage.h"
#include "obix_request.h"
#include "server.h"

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
	char *devhref;

	/* index's absolute pathname */
	char *indexpath;

	/* The device's XML object in the global DOM tree */
	xmlNode *node;

	/* The device's index's subtree parented by above node */
	xmlNode *index;

	/* SORTED list of obix_hist_file */
	struct list_head files;

	/* protect status bits and counter */
	pthread_mutex_t mutex;

	/* readers' and writers' waiting queues */
	pthread_cond_t rq, wq;

	/* all writers cnt */
	int writers;

	/* running readers cnt */
	int readers;

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
#define INDEX_FILENAME			"index"

#define INDEX_FILENAME_SUFFIX	".xml"
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
static const char *INDEX_SKELETON =
"<list name=\"index\" href=\"index\" of=\"obix:HistoryFileAbstract\"/>\r\n";

static char *QUERY_OUT_PREFIX =
"<obj is=\"obix:HistoryQueryOut\">\r\n"
"<int name=\"count\" val=\"%d\"/>\r\n"
"<abstime name=\"start\" val=\"%s\"/>\r\n"
"<abstime name=\"end\" val=\"%s\"/>\r\n"
"<list name=\"data\" of=\"obix:HistoryRecord\">\r\n";

static char *QUERY_OUT_SUFFIX = "</list>\r\n</obj>\r\n";

static const char *GET_OUT_SKELETON =
"<str name=\"%s\" href=\"%s\"/>\r\n";

/**
 * Error codes generated by history facility
 *
 * If there is no strong need to expose to oBIX clients
 * the detailed reason of a potential oBIX server error,
 * cut down the number of error codes by using generalized
 * error code such as ERR_NO_MEM
 */
enum {
	ERR_NO_MEM = 1,			/* 0 stands for success */
	ERR_NO_DEV,
	ERR_INVALID_URI_PREFIX,
	ERR_INVALID_URI_SUFFIX,
	ERR_ILLEGAL_OP,
	ERR_NO_WRITTEN,
	ERR_NO_LATEST_TS,
	ERR_NO_DATA,
	ERR_COMPARE_TS,
	ERR_OBSOLETE_TS,
	ERR_CREATE_LOGFILE,
	ERR_APPEND_LOGFILE,
	ERR_READ_LOGFILE,
	ERR_PARSE_LOGFILE,
	ERR_EMPTY_DEV,
	ERR_NO_DEVID,
	ERR_WRITE_INDEX,
	ERR_NO_PERM,
	ERR_NO_DEV_ID,
	ERR_CORRUPT_DEV
};

static err_msg_t hist_err_msg[] = {
	[ERR_NO_MEM] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "Not enough memory"
	},
	[ERR_INVALID_URI_PREFIX] = {
		.type = OBIX_CONTRACT_ERR_BAD_URI,
		.msgs = "Invalid URI not preceded by history lobby's href"
	},
	[ERR_INVALID_URI_SUFFIX] = {
		.type = OBIX_CONTRACT_ERR_BAD_URI,
		.msgs = "Invalid URI not followed by history's operation name"
	},
	[ERR_NO_DEV] = {
		.type = OBIX_CONTRACT_ERR_BAD_URI,
		.msgs = "No history facility available for this device"
	},
	[ERR_ILLEGAL_OP] = {
		.type = OBIX_CONTRACT_ERR_BAD_URI,
		.msgs = "Requested operation not supported yet"
	},
	[ERR_NO_WRITTEN] = {
		.type = OBIX_CONTRACT_ERR_UNSUPPORTED,
		.msgs = "No records written, probably due to no records "
				"or timestamp issue"
	},
	[ERR_NO_LATEST_TS] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "Failed to get the end timestamp from the latest log file"
	},
	[ERR_NO_DATA] = {
		.type = OBIX_CONTRACT_ERR_UNSUPPORTED,
		.msgs = "Illegal HistoryAppendIn contract without data list as child"
	},
	[ERR_COMPARE_TS] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "Failed to compare timestamps. Malformed? "
				"use ts2utc testcase to check timestamp sanity"
	},
	[ERR_OBSOLETE_TS] = {
		.type = OBIX_CONTRACT_ERR_UNSUPPORTED,
		.msgs = "Data list contains records with timestamp older than or "
				"equal to that of the last record"
	},
	[ERR_CREATE_LOGFILE] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "Failed to create a new log file"
	},
	[ERR_APPEND_LOGFILE] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "Failed to append records to the log file"
	},
	[ERR_READ_LOGFILE] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "Failed to read from a log file"
	},
	[ERR_PARSE_LOGFILE] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "Failed to parse the content of a log file"
	},
	[ERR_EMPTY_DEV] = {
		.type = OBIX_CONTRACT_ERR_UNSUPPORTED,
		.msgs = "The device's history facility is empty"
	},
	[ERR_NO_DEVID] = {
		.type = OBIX_CONTRACT_ERR_UNSUPPORTED,
		.msgs = "Failed to get device ID from request"
	},
	[ERR_WRITE_INDEX] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "Failed to write into history index file for the device"
	},
	[ERR_NO_PERM] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "Failed to create history folder or index file for the device"
	},
	[ERR_NO_DEV_ID] = {
		.type = OBIX_CONTRACT_ERR_BAD_URI,
		.msgs = "Failed to get device ID from the requested href"
	},
	[ERR_CORRUPT_DEV] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "Corrupted index file of current device"
	}
};

/**
 * Write the index file. The whole index file will be re-written
 *
 * Return > 0 on success, < 0 otherwise
 */
static int write_index(const char *path, const char *data, int size)
{
	struct iovec iov[2];
	int fd;
	int ret = -1;

	iov[0].iov_base = (char *)XML_HEADER;
	iov[0].iov_len = strlen(XML_HEADER);
	iov[1].iov_base = (char *)data;
	iov[1].iov_len = size;

	if ((fd = open(path, O_RDWR | O_TRUNC)) >= 0) {
		ret = writev(fd, iov, 2);
		close(fd);
	}

	return ret;
}

/*
 * Enqueue a new obix_hist_file struct based on its date
 *
 * Note,
 * 1. Callers must have held obix_hist_dev_t.mutex
 */
static int __file_add_sorted(obix_hist_file_t *new, obix_hist_dev_t *dev)
{
	int res;
	obix_hist_file_t *file;

	list_for_each_entry(file, &dev->files, list) {
		if (timestamp_compare_date(new->date, file->date, &res) < 0) {
			log_error("Failed to compare date strings %s vs %s",
						new->date, file->date);
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

	log_error("Should never reach here.");
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
 * Allocate and initialize a new HistoryAppendOut node
 *
 * Note,
 * 1. Callers should release the node once done with it
 */
static xmlNode *create_aout(obix_hist_dev_t *dev, int added)
{
	xmlNode *aout;
	char *start = NULL, *end = NULL;
	obix_hist_file_t *first, *last;
	long count;

	if (!(aout = xmldb_copy_sys(OBIX_SYS_AOUT_STUB))) {
		return NULL;
	}

	pthread_mutex_lock(&dev->mutex);
	first = list_entry(dev->files.next, obix_hist_file_t, list);
	last = list_entry(dev->files.prev, obix_hist_file_t, list);
	count = dev->count;
	pthread_mutex_unlock(&dev->mutex);

	start = xml_get_child_val(first->abstract, OBIX_OBJ_ABSTIME, HIST_ABS_START);
	end = xml_get_child_val(last->abstract,	OBIX_OBJ_ABSTIME, HIST_ABS_END);

	update_count(aout, AOUT_NUMADDED, added);
	update_count(aout, AOUT_NEWCOUNT, count);

	if (start) {
		update_value(aout, OBIX_OBJ_ABSTIME, AOUT_NEWSTART, start);
		free(start);
	}

	if (end) {
		update_value(aout, OBIX_OBJ_ABSTIME, AOUT_NEWEND, end);
		free(end);
	}

	return aout;
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

	if ((fd = open(file->filepath, O_APPEND | O_WRONLY)) > 0) {
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
static obix_hist_file_t *hist_create_file(obix_hist_dev_t *dev,
										  xmlNode *abstract,
										  int newly_created)
{
	obix_hist_file_t *file = NULL;
	struct stat statbuf;

	if (!dev || !abstract) {
		log_error("Illegal parameters provided to create a hist file descriptor");
		return NULL;
	}

	if (!(file = (obix_hist_file_t *)malloc(sizeof(obix_hist_file_t)))) {
		log_error("Failed to allocae file descriptor");
		return NULL;
	}
	memset(file, 0, sizeof(obix_hist_file_t));

	file->abstract = abstract;
	INIT_LIST_HEAD(&file->list);

	if (!(file->date = xml_get_child_val(abstract, OBIX_OBJ_DATE, HIST_ABS_DATE))) {
		log_error("Failed to get val from node with tag %s, name %s within "
				  "below abstract:\n%s", OBIX_OBJ_DATE, HIST_ABS_DATE,
				  xml_dump_node(abstract));
		goto failed;
	}

	if (link_pathname(&file->filepath, _history->dir, dev->dev_id,
					  file->date, LOG_FILENAME_SUFFIX) < 0) {
		log_error("Not enough memory to allocate absolute pathname for "
					"log file on %s", file->date);
		goto failed;
	}

	/* Validate the sanity of relevant log file */
	if (lstat(file->filepath, &statbuf) < 0) {
		log_error("lstat on %s failed", file->filepath);
		goto failed;
	}

	if (S_ISREG(statbuf.st_mode) == 0 ||
		(statbuf.st_size == 0 && newly_created == 0)){
		log_error("%s is not a regular file, or is empty", file->filepath);
		goto failed;
	}

	/* Lastly, enqueue file descriptor based on its date */
	pthread_mutex_lock(&dev->mutex);
	if (newly_created == 1) {
		list_add_tail(&file->list, &dev->files);
	} else {
		if (__file_add_sorted(file, dev) < 0) {
			pthread_mutex_unlock(&dev->mutex);
			log_error("Failed to enqueue a file descriptor for %s",
						file->filepath);
			goto failed;
		}
	}
	pthread_mutex_unlock(&dev->mutex);

	return file;

failed:
	hist_destroy_file(file);
	return NULL;
}

/*
 * Allocate and initialize a new abstract contract
 */
static xmlNode *create_absnode(obix_hist_dev_t *dev, const char *date,
							   const char *start, const char *end)
{
	xmlNode *node;

	if (!(node = xmldb_copy_sys(OBIX_SYS_ABS_STUB))) {
		log_error("Failed to copy from %s", OBIX_SYS_ABS_STUB);
		return NULL;
	}

	update_value(node, OBIX_OBJ_DATE, HIST_ABS_DATE, date);
	update_count(node, HIST_ABS_COUNT, 0);
	update_value(node, OBIX_OBJ_ABSTIME, HIST_ABS_START, start);
	update_value(node, OBIX_OBJ_ABSTIME, HIST_ABS_END, end);

	if (!xmldb_add_child(dev->index, node, 0, 0)) {
		log_error("Failed to add abstract node on %s into %s",
				  date, dev->devhref);
		xmlFreeNode(node);
		node = NULL;
	}

	return node;
}

/**
 * Create a new history log file and setup relevant backend data
 * structure based on the specified timestamp of its first record
 *
 * Return address of the relevant file descriptor on success,
 * NULL otherwise
 */
static obix_hist_file_t *create_file_helper(obix_hist_dev_t *dev,
											const char *ts)
{
	obix_hist_file_t *file = NULL;
	xmlNode *node;
	char *filepath, *date;
	int fd;

	if (!(date = timestamp_get_utc_date(ts)) ||
		link_pathname(&filepath, _history->dir, dev->dev_id,
					  date, LOG_FILENAME_SUFFIX) < 0) {
		goto failed;
	}

	errno = 0;
	fd = creat(filepath, 0644);
	if (fd < 0 && errno != EEXIST) {
		goto failed;
	}
	close(fd);

	if (!(node = create_absnode(dev, date, ts, ts))) {
		goto failed;
	}

	file = hist_create_file(dev, node, 1);

	/* Fall through */

failed:
	if (date) {
		free(date);
	}

	if (filepath) {
		free(filepath);
	}

	return file;
}

static void hist_create_file_wrapper(xmlNode *node, obix_hist_dev_t *dev)
{
	obix_hist_file_t *file;

	if (!(file = hist_create_file(dev, node, 0))) {
		log_error("Failed to create obix_hist_file_t for one data file of %s",
					dev->dev_id);
		return;
	}

	dev->count += xml_get_child_long(file->abstract, OBIX_OBJ_INT,
									 HIST_ABS_COUNT);
}

/*
 * Setup and register a XML node for a device, which bridges
 * the device's index subtree with that of global DOM tree
 *
 * Return the address of relevant XML node on success, NULL
 * otherwise
 *
 * NOTE: If the given devhref happens to be an ancestor of the
 * href of existing history facility, then its corresponding
 * node would have been established already because of the
 * usage of the DOM_CREATE_ANCESTORS option.
 */
static xmlNode *create_devnode(const char *devhref)
{
	xmlNode *node;

	if ((node = xmldb_get_node((xmlChar *)devhref)) != NULL) {
		log_debug("Ancestor history facility already created at %s", devhref);
		return node;
	}

	if (!(node = xmldb_copy_sys(OBIX_SYS_HIST_DEV_STUB))) {
		log_error("Failed to get a %s object", OBIX_SYS_HIST_DEV_STUB);
		return NULL;
	}

	if (!xmlSetProp(node, BAD_CAST OBIX_ATTR_HREF, BAD_CAST devhref)) {
		log_error("Failed to set href %s on relevant node", devhref);
		goto failed;
	}

	/*
	 * NO DOM_CHECK_SANITY is used since the node with the
	 * given href has been assured not existing
	 *
	 * Also, only the XML nodes for ancestor hrefs may be created
	 * if needed, however, NO parent history facilities will ever
	 * be created because no descriptors nor index files would have
	 * been created.
	 */
	if (xmldb_put_node(node, DOM_CREATE_ANCESTORS_HIST) != 0) {
		log_error("Failed to add node with href %s into XML database", devhref);
		goto failed;
	}

	return node;

failed:
	xmlFreeNode(node);
	return NULL;
}

/*
 * Setup and register a XML Node for the index file of a device
 *
 * Return the address of relevant XML Node on success, NULL otherwise
 */
static xmlNode *create_indexnode(const char *path, xmlNode *parent)
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
	if (!xmldb_add_child(parent, root, 1, 0)) {
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

	pthread_mutex_lock(&dev->mutex);
	list_for_each_entry_safe(file, n, &dev->files, list) {
		list_del(&file->list);
		hist_destroy_file(file);
	}
	pthread_mutex_unlock(&dev->mutex);

	if (dev->devhref) {
		free(dev->devhref);
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

	pthread_mutex_destroy(&dev->mutex);
	pthread_cond_destroy(&dev->rq);
	pthread_cond_destroy(&dev->wq);

	free(dev);
}

static int get_href_helper(const char *token, void *arg1, void *arg2)
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
 * Create a history facility for the specified device and
 * initialize it with any existing data on hard drive.
 *
 * Return new history facility's descriptor on success,
 * NULL otherwise.
 *
 * Note:
 * 1. The caller should ensure <resdir>/histories/dev_id/index.xml
 * exists and filled in with INDEX_SKELETON at least.
 */
static obix_hist_dev_t *hist_create_dev(const char *dev_id,
										const char *indexpath)
{
	xmlNode *node;
	xmlChar *is_attr = NULL;
	obix_hist_dev_t *dev, *n;
	char *href = NULL;
	int ret;

	if (!dev_id || !indexpath) {
		log_error("Illegal parameter provided to create a hist device descriptor");
		return NULL;
	}

	if (!(dev = (obix_hist_dev_t *)malloc(sizeof(obix_hist_dev_t)))) {
		log_error("Failed to allocae history facility for %s", dev_id);
		return NULL;
	}
	memset(dev, 0, sizeof(obix_hist_dev_t));

	INIT_LIST_HEAD(&dev->list);
	INIT_LIST_HEAD(&dev->files);
	pthread_mutex_init(&dev->mutex, NULL);
	pthread_cond_init(&dev->rq, NULL);
	pthread_cond_init(&dev->wq, NULL);

	if (!(dev->dev_id = strdup(dev_id))) {
		log_error("Failed to allocate string for %s", dev_id);
		goto failed;
	}
	dev->namelen = strlen(dev->dev_id);

	if (!(dev->indexpath = strdup(indexpath))) {
		log_error("Failed to allocate string for %s", indexpath);
		goto failed;
	}

	if (for_each_str_token(STR_DELIMITER_DOT, dev_id,
						   get_href_helper, &href, NULL) < 0) {
		log_error("Failed to convert %s into href format", dev_id);
		goto failed;
	}

	ret = link_pathname(&dev->devhref, OBIX_HISTORY_LOBBY, href,
						NULL, NULL);
	free(href);
	if (ret < 0) {
		log_error("Failed to allocate device's href name for %s", dev_id);
		goto failed;
	}

	if (!(dev->node = create_devnode(dev->devhref))) {
		goto failed;
	}

	if (!(dev->index = create_indexnode(dev->indexpath, dev->node))) {
		goto failed;
	}

	/*
	 * Lastly, once all DOM infrastructure properly setup and linked
	 * altogether, create descriptors for each log file which cache
	 * pointers to their abstract contract in the global DOM tree.
	 */
	for (node = dev->index->children; node; node = node->next) {
		if (node->type != XML_ELEMENT_NODE) {
			continue;
		}

		if (is_attr) {
			xmlFree(is_attr);
		}

		if (xmlStrcmp(node->name, BAD_CAST OBIX_OBJ) != 0 ||
			!(is_attr = xmlGetProp(node, BAD_CAST OBIX_ATTR_IS)) ||
			xmlStrcmp(is_attr, BAD_CAST OBIX_CONTRACT_HIST_FILE_ABS) != 0) {
			continue;
		}

		hist_create_file_wrapper(node, dev);
	}

	if (is_attr) {
		xmlFree(is_attr);
	}

	pthread_mutex_lock(&_history->mutex);
	list_for_each_entry(n, &_history->devices, list) {
		/*
		 * Considering that tons of history facilities names are
		 * of the same length, if the name of the newly created
		 * has a same length as the current one in the list,
		 * insert the new one before it so as to finish the loop
		 * quickly
		 */
		if (n->namelen < dev->namelen) {
			continue;
		}

		__list_add(&dev->list, n->list.prev, &n->list);
		break;
	}

	if (&n->list == &_history->devices)	{	/* empty list */
		list_add_tail(&dev->list, &_history->devices);
	}
	pthread_mutex_unlock(&_history->mutex);

	return dev;

failed:
	hist_destroy_dev(dev);
	return NULL;
}

/**
 * Flush index DOM tree content into index file on hard drive
 */
static void hist_flush_index(obix_hist_dev_t *dev)
{
	char *data;

	if (!(data = xml_dump_node(dev->index))) {
		log_error("Failed to dump XML subtree of %s", dev->devhref);
		return;
	}

	if (write_index(dev->indexpath, data, strlen(data)) < 0) {
		log_error("Failed to save %s on hard drive", dev->devhref);
	}

	free(data);
}

/*
 * Append records from input contract to history log files
 *
 * Return the number of records appended, < 0 on failures
 */
static int hist_append_dev_helper(obix_hist_dev_t *dev, xmlNode *input)
{
	obix_hist_file_t *file;
	xmlNode *list, *record;
	char *ts = NULL, *latest_ts = NULL;
	int count = 0, all_count = 0;
	int ret = ERR_NO_WRITTEN * -1;
	int res, new_day;

	/* Get the latest history log file */
	pthread_mutex_lock(&dev->mutex);
	if (list_empty(&dev->files) == 1) {
		pthread_mutex_unlock(&dev->mutex);
		file = NULL;

		if (!(latest_ts = strdup(HIST_TS_INIT))) {
			ret = ERR_NO_MEM * -1;
			goto failed;
		}
	} else {
		file = list_entry(dev->files.prev, obix_hist_file_t, list);
		pthread_mutex_unlock(&dev->mutex);

		if (!(latest_ts = xml_get_child_val(file->abstract, OBIX_OBJ_ABSTIME,
											HIST_ABS_END))) {
			ret = ERR_NO_LATEST_TS * -1;
			goto failed;
		}
	}

	if(!(list = xml_find_child(input, OBIX_OBJ_LIST,
							   OBIX_ATTR_NAME, HIST_AIN_DATA))) {
		ret = ERR_NO_DATA * -1;
		goto failed;
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
			ret = ERR_COMPARE_TS * -1;
			continue;
		}

		/*
		 * Newly added history records MUST not include a timestamp
		 * older than or equal to the latest one
		 */
		if (res <= 0) {
			ret = ERR_OBSOLETE_TS * -1;
			continue;
		}

		/* Create a new log file for the new date */
		if (new_day == 1) {
			if (count > 0) {
				add_abs_count(file, count);
				count = 0;		/* Reset counter for the new log file */
			}

			if (!(file = create_file_helper(dev, ts))) {
				ret = ERR_CREATE_LOGFILE * -1;
				goto failed;
			}
		}

		if (write_logfile(file, record) < 0) {
			ret = ERR_APPEND_LOGFILE * -1;
			goto failed;
		}

		update_value(file->abstract, OBIX_OBJ_ABSTIME, HIST_ABS_END, ts);

		/* Update latest_ts */
		free(latest_ts);
		if (!(latest_ts = strdup(ts))) {
			ret = ERR_NO_MEM * -1;
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
		ret = all_count;
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
 * Return 0 on success, > 0 on errors
 */
static int hist_append_dev(obix_request_t *request,
						   obix_hist_dev_t *dev, xmlNode *input)
{
	xmlNode *aout;
	char *data;
	int added;
	int ret = ERR_NO_MEM;

	pthread_mutex_lock(&dev->mutex);
	dev->writers++;	/* all writers count */
	while ((dev->readers > 0) || (dev->writers > 1)) {
		pthread_cond_wait(&dev->wq, &dev->mutex);
	}
	pthread_mutex_unlock(&dev->mutex);

	if ((added = hist_append_dev_helper(dev, input)) < 0) {
		ret = added * -1;
		goto out;
	}

	if (!(aout = create_aout(dev, added))) {
		goto out;
	}

	data = xml_dump_node(aout);
	xmlFreeNode(aout);

	if (!data) {
		goto out;
	}

	if (obix_request_create_append_response_item(request, data, strlen(data), 0) < 0) {
		free(data);
		goto out;
	}

	ret = 0;	/* Success */

	/* Fall through */

out:
	pthread_mutex_lock(&dev->mutex);
	if (--dev->writers > 0)
		pthread_cond_signal(&dev->wq);
	else	/* regardless of whether any readers are waiting */
		pthread_cond_signal(&dev->rq);
	pthread_mutex_unlock(&dev->mutex);

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
static int hist_query_dev_helper(obix_request_t *request,
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

	pthread_mutex_lock(&dev->mutex);
	if (list_empty(&dev->files) == 1) {
		pthread_mutex_unlock(&dev->mutex);
		return ERR_EMPTY_DEV;
	}

	first = list_entry(dev->files.next, obix_hist_file_t, list);
	last = list_entry(dev->files.prev, obix_hist_file_t, list);
	pthread_mutex_unlock(&dev->mutex);

	d_oldest = xml_get_child_val(first->abstract, OBIX_OBJ_ABSTIME,
								 HIST_ABS_START);
	d_latest = xml_get_child_val(last->abstract, OBIX_OBJ_ABSTIME,
								 HIST_ABS_END);

	if (!d_oldest || !d_latest) {
		ret = ERR_CORRUPT_DEV;
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
		ret = ERR_COMPARE_TS;
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
		ret = ERR_COMPARE_TS;
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
			ret = ERR_COMPARE_TS;
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
				ret = ERR_READ_LOGFILE;
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
						ret = ERR_PARSE_LOGFILE;
						goto flush_response;
					}
				}
			} else {	/* Records' timestamp would have to be compared */
				count = n;
				if (!(data = parse_log(data, start, end, &count,
										(!start_ts) ? &start_ts : NULL,
										&end_ts, &len))) {
					ret = ERR_PARSE_LOGFILE;
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
	len = strlen(QUERY_OUT_PREFIX) + HIST_FLT_VAL_MAX_BITS +
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
	len = sprintf(data, QUERY_OUT_PREFIX, r,
					((start_ts != NULL) ? start_ts : start),
					((end_ts != NULL) ? end_ts : end));

	if (!(item = obix_request_create_response_item(data, len, 0))) {
		free(data);
		goto flush_response;
	}

	obix_request_add_response_item(request, item);

	/* Tail HistoryQueryOut contract footer */
	if (!(item = obix_request_create_response_item(QUERY_OUT_SUFFIX,
													strlen(QUERY_OUT_SUFFIX),
													1))) {
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
 * Return 0 on success, > 0 on errors
 */
static int hist_query_dev(obix_request_t *request,
						  obix_hist_dev_t *dev, xmlNode *input)
{
	int ret;

	pthread_mutex_lock(&dev->mutex);
	/* Readers give way to any running/waiting writer */
	while (dev->writers > 0) {
		pthread_cond_wait(&dev->rq, &dev->mutex);
	}
	dev->readers++;	/* running readers count */
	pthread_mutex_unlock(&dev->mutex);

	ret = hist_query_dev_helper(request, dev, input);

	pthread_mutex_lock(&dev->mutex);
	if ((--dev->readers == 0) && (dev->writers > 0)) {
		pthread_cond_signal(&dev->wq);
	}
	pthread_mutex_unlock(&dev->mutex);

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
}

static int hist_create_dev_wrapper(const char *parent_dir, const char *subdir,
								   void *arg)	/* ignored */
{
	obix_hist_dev_t *dev;
	char *indexpath;
	int ret = 0;

	if (link_pathname(&indexpath, parent_dir, subdir,
					  INDEX_FILENAME, INDEX_FILENAME_SUFFIX) < 0) {
		log_error("Failed to allocate index file name for %s", subdir);
		return -1;
	}

	dev = hist_create_dev(subdir, indexpath);
	free(indexpath);

	if (!dev) {
		log_error("Failed to setup history facility for %s", subdir);
		ret = -1;
	}

	return ret;
}

int obix_hist_init(const char *resdir)
{
	int ret;

	if (_history) {
		log_error("History facility already initialized");
		return -1;
	}

	if (!(_history = (obix_hist_t *)malloc(sizeof(obix_hist_t)))) {
		log_error("Failed to alloc a history descriptor");
		return -1;
	}
	memset(_history, 0, sizeof(obix_hist_t));

	if (link_pathname(&_history->dir, resdir, HISTORIES_DIR, NULL, NULL) < 0) {
		log_error("Failed to init history: not enough memory");
		free(_history);
		_history = NULL;
		return -1;
	}

	_history->op = &obix_hist_operations;
	INIT_LIST_HEAD(&_history->devices);
	pthread_mutex_init(&_history->mutex, NULL);

	if ((ret = for_each_file_name(_history->dir,
								  NULL, NULL,	/* all possible names */
								  hist_create_dev_wrapper,
								  NULL)) < 0) {
		log_error("Failed to setup history facilities from %s",
				  _history->dir);
		obix_hist_dispose();
	} else {
		log_debug("History facility initialized!");
	}

	return ret;
}

/*
 * Try to find a history facility for the specified device.
 *
 * Return its obix_hist_dev_t address on success, NULL otherwise.
 */
static obix_hist_dev_t *find_device(const char *dev_id)
{
	obix_hist_dev_t *dev;

	pthread_mutex_lock(&_history->mutex);
	list_for_each_entry(dev, &_history->devices, list) {
		/* The whole string needs to be exactly the same */
		if (strcmp(dev->dev_id, dev_id) == 0) {
			pthread_mutex_unlock(&_history->mutex);
			return dev;
		}
	}
	pthread_mutex_unlock(&_history->mutex);

	return NULL;
}

/*
 * Setup a history facility for the specified device ID
 *
 * Return relevant device descriptor on success, NULL otherwise
 */
static int create_dev_helper(const char *dev_id,
							 obix_hist_dev_t **dev)
{
	char *devdir, *indexpath;
	int ret = ERR_NO_MEM;

	*dev = NULL;

	if (link_pathname(&devdir, _history->dir, dev_id, NULL, NULL) < 0 ||
		link_pathname(&indexpath, devdir, NULL, INDEX_FILENAME,
					  INDEX_FILENAME_SUFFIX) < 0) {
		goto failed;
	}

	if (mkdir(devdir, 0755) < 0) {
		ret = ERR_NO_PERM;
		goto failed;
	}

	if (creat(indexpath, 0644) < 0) {
		ret = ERR_NO_PERM;
		goto creat_failed;
	}

	if (write_index(indexpath, INDEX_SKELETON, strlen(INDEX_SKELETON)) > 0) {
		if ((*dev = hist_create_dev(dev_id, indexpath)) != NULL) {
			ret = 0;
		}
	} else {
		unlink(indexpath);
		ret = ERR_WRITE_INDEX;
	}

	/* Fall through */

creat_failed:
	if (!*dev) {				/* Preserve device dir on success */
		rmdir(devdir);
	}

failed:
	if (devdir) {
		free(devdir);
	}

	if (indexpath) {
		free(indexpath);
	}

	return ret;
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
 * Return 0 on success, > 0 otherwise
 *
 * Note,
 * 1. Callers should free the returned device ID string after use.
 */
static int get_dev_id(const char *uri, const char *op_name, char **dev_id)
{
	char *str, *end;
	int len = strlen(OBIX_HISTORY_LOBBY);
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
			return ERR_INVALID_URI_SUFFIX;
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
									 const char *overrideUri,
									 xmlNode *input,
									 const char *op_name)
{
	obix_hist_dev_t *dev;
	char *dev_id;
	const char *uri;
	int ret = ERR_NO_MEM;

	uri = (overrideUri != NULL) ? overrideUri : request->request_decoded_uri;

	/* Find the device to operate on */
	if ((ret = get_dev_id(uri, op_name, &dev_id)) != 0) {
		ret = ERR_NO_DEV_ID;
		goto failed;
	}

	dev = find_device(dev_id);
	free(dev_id);
	if (!dev) {
		ret = ERR_NO_DEV;
		goto failed;
	}

	/* Invoke handler in response to request */
	if (strcmp(op_name, HIST_OP_APPEND) == 0) {
		ret = _history->op->append(request, dev, input);
	} else if (strcmp(op_name, HIST_OP_QUERY) == 0) {
		ret = _history->op->query(request, dev, input);
	} else {
		ret = ERR_ILLEGAL_OP;
	}

	if (ret > 0) {
		goto failed;
	}

	/* Add XML Header */
	if (obix_request_add_response_xml_header(request) < 0) {
		goto failed;
	}

	request->is_history = 1;
	obix_request_send_response(request);

	return NULL;	/* Success */

failed:
	log_error("%s : %s", uri, hist_err_msg[ret].msgs);

	return obix_server_generate_error(uri, hist_err_msg[ret].type,
									  op_name, hist_err_msg[ret].msgs);
}

xmlNode *handlerHistoryAppend(obix_request_t *request, const char *overrideUri,
							  xmlNode *input)
{
	return handlerHistoryHelper(request, overrideUri, input, HIST_OP_APPEND);
}

xmlNode *handlerHistoryQuery(obix_request_t *request, const char *overrideUri,
							 xmlNode *input)
{
	return handlerHistoryHelper(request, overrideUri, input, HIST_OP_QUERY);
}

xmlNode *handlerHistoryGet(obix_request_t *request, const char *overrideUri,
						   xmlNode *input)
{
	obix_hist_dev_t *dev;
	char *href, *dev_id = NULL, *data;
	const char *uri;
	int len = strlen(OBIX_HISTORY_LOBBY), ret = ERR_NO_MEM;

	uri = (overrideUri != NULL) ? overrideUri : request->request_decoded_uri;

	if (!(href = xml_get_child_val(input, OBIX_OBJ_STR, DEVICE_ID))) {
		ret = ERR_NO_DEVID;
		goto failed;
	}

	if (get_dev_id(href, NULL, &dev_id) != 0) {
		ret = ERR_NO_DEVID;
		goto failed;
	}

	if (!(dev = find_device(dev_id))) {
		if (is_privileged_mode(request) == 0) {
			ret = ERR_NO_DEV;
			goto failed;
		}

		if ((ret = create_dev_helper(dev_id, &dev)) > 0) {
			goto failed;
		}
	}

	len = strlen(GET_OUT_SKELETON) + strlen(dev_id) + strlen(dev->devhref) - 4;

	if (!(data = (char *)malloc(len + 1))) {
		goto failed;
	}

	len = sprintf(data, GET_OUT_SKELETON, dev_id, dev->devhref);

	if (obix_request_add_response_xml_header(request) < 0 ||
		obix_request_create_append_response_item(request, data, len, 0) < 0) {
		free(data);
		goto failed;
	}

	free(href);
	free(dev_id);

	/*
	 * Fill in the HTTP Content-Location header with the href
	 * of the relevant history facility
	 */
	request->response_uri = strdup(dev->devhref);

	request->is_history = 1;
	obix_request_send_response(request);

	return NULL;	/* Success */

failed:
	if (href) {
		free(href);
	}

	if (dev_id) {
		free(dev_id);
	}

	/*
	 * On error, wipe out all potentially added response items
	 * to make room for the error contract
	 */
	obix_request_destroy_response_items(request);

	log_error("%s : %s", uri, hist_err_msg[ret].msgs);

	return obix_server_generate_error(uri, hist_err_msg[ret].type,
									  "History.Get", hist_err_msg[ret].msgs);
}
