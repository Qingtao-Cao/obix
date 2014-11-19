/* *****************************************************************************
 * Copyright (c) 2013-2015 Qingtao Cao [harry.cao@nextdc.com]
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

#include <libgen.h>			/* dirname */
#include <string.h>			/* memset */
#include <stdlib.h>			/* malloc */
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include "xml_storage.h"
#include "obix_utils.h"
#include "device.h"
#include "hash.h"
#include "cache.h"
#include "watch.h"
#include "xml_utils.h"
#include "log_utils.h"
#include "security.h"
#include "errmsg.h"

static const char *SERVER_DB_DEVICES = "/devices/";
static const char *SERVER_DB_DEVICE_META = "meta";
static const char *SERVER_DB_DEVICE_CONTRACT = "device";

static const char *DEVICE_META_CONTRACT =
"<obj of=\"nextdc:device-meta\">\r\n"
"<str name=\"owner_id\" val=\"%s\"/>\r\n"
"<uri val=\"%s\"/>\r\n"
"</obj>";

static const char *DEVICE_OWNER_ID = "owner_id";

typedef struct obix_devices {
	/*
	 * The period to update a device's persistent file on
	 * the hard drive
	 */
	int backup_period;

	/*
	 * The hash table of all devices registered, which are
	 * recognisable by their unique hrefs
	 */
	hash_table_t *tab;

	/* The cache used along with the hash table */
	cache_t *cache;

	/* Pointing to the device descriptor of the device root */
	obix_dev_t *device_root;
} obix_devices_t;

static obix_devices_t *_devices;

/*
 * Return 1 if the given href can point to a potential device,
 * 0 otherwise
 */
int is_device_href(const xmlChar *href)
{
	xmlChar next;

	/*
	 * A valid device href should start with "/obix/deviceRoot", and
	 * the next byte must be either NULL or a slash
	 */
	if (xmlStrncmp(href, BAD_CAST OBIX_DEVICE_ROOT,
				   OBIX_DEVICE_ROOT_LEN - 1) == 0) {
		next = href[OBIX_DEVICE_ROOT_LEN - 1];
		if (next == '\0' || next == '/') {
			return 1;
		}
	}

	return 0;
}

/*
 * Return 1 if the given href points to the Device root,
 * 0 otherwise
 */
int is_device_root_href(const xmlChar *href)
{
	/*
	 * Must be lenient on the trailing slash, since the parameter
	 * may be generated by dirname() which excludes trailing
	 * slash for every token
	 */
	return is_str_identical((const char *)href, OBIX_DEVICE_ROOT);
}

/*
 * Application specific methods to calculate a hash value
 * from the given href. If it starts with the common part of
 * "/obix/deviceRoot/" and longer than it, then skip it over.
 */
static unsigned int device_get_hash(const xmlChar *href,
									const unsigned int tab_size)
{
	int len = xmlStrlen(href);

	if (xmlStrncmp(href, BAD_CAST OBIX_DEVICE_ROOT,
				   OBIX_DEVICE_ROOT_LEN) == 0 &&
		len > OBIX_DEVICE_ROOT_LEN) {
		href += OBIX_DEVICE_ROOT_LEN;
		len -= OBIX_DEVICE_ROOT_LEN;
	}

	return hash_bkdr(href, len, tab_size);
}

static int device_cmp_hash(const xmlChar *href, hash_node_t *node)
{
	obix_dev_t *dev = (obix_dev_t *)(node->item);

	return is_str_identical((const char *)href, (const char *)dev->href);
}

static hash_ops_t device_hash_ops = {
	.get = device_get_hash,
	.cmp = device_cmp_hash
};

/* The meta information of a device */
typedef struct meta_info {
	char *owner_id;
	char *href;
} meta_info_t;

/*
 * Read the owner ID and href information of a device from
 * its meta file
 *
 * Return 0 on success, > 0 for error code
 */
static int device_load_meta(const char *path, meta_info_t *info)
{
	struct stat statbuf;
	xmlDoc *doc;
	xmlNode *root;
	int ret = 0;

	errno = 0;
	if (lstat(path, &statbuf) < 0) {
		return (errno == ENOENT) ? 0 : ERR_DISK_IO;
	}

	if (!(doc = xmlReadFile(path, NULL,
							XML_PARSE_OPTIONS_COMMON | XML_PARSE_NODICT)) ||
		!(root = xmlDocGetRootElement(doc)) ||
		!(info->owner_id = xml_get_child_val(root, OBIX_OBJ_STR,
											 DEVICE_OWNER_ID)) ||
		!(info->href = xml_get_child_val(root, OBIX_OBJ_URI, NULL))) {
		log_error("Unable to parse XML document %s", path);
		ret = ERR_NO_MEM;
	}

	if (doc) {
		xmlFreeDoc(doc);
	}

	/* Doesn't have to clean up info since the caller will do it anyway */
	return ret;
}

/*
 * Load the device persistent file into the oBIX server, in particular,
 * add the device contract into the global DOM tree and have its descriptor
 * created and registered
 *
 * Return 0 on success, > 0 for error code
 */
static int device_load_contract(const char *path, meta_info_t *info)
{
	struct stat statbuf;
	xmlDoc *doc;
	xmlNode *root;
	int ret = 0;

	errno = 0;
	if (lstat(path, &statbuf) < 0) {
		return (errno == ENOENT) ? 0 : ERR_DISK_IO;
	}

	if (!(doc = xmlReadFile(path, NULL,
							XML_PARSE_OPTIONS_COMMON | XML_PARSE_NODICT)) ||
		!(root = xmlDocGetRootElement(doc))) {
		log_error("Unable to parse XML document %s", path);
		ret = ERR_NO_MEM;
	} else {
		ret = device_add(root, BAD_CAST info->href, info->owner_id, 0);
	}

	if (doc) {
		xmlFreeDoc(doc);
	}

	return ret;
}

static int device_load_files(const char *resdir);

/*
 * Load a child device contract from the given folder
 *
 * Return 0 on success, < 0 on error so as to break from
 * the for_each_file_name loop
 */
static int device_load_child(const char *dir, const char *file, void *arg)
{
	struct stat statbuf;
	char *path;
	int ret = 0;

	if (link_pathname(&path, dir, NULL, file, NULL) < 0) {
		log_error("Failed to assemble absolute path name for %s/%s", dir, file);
		return -1;
	}

	/*
	 * Skip over meta or contract files of the parent device,
	 * return 0 so as to move on to the next children device
	 */
	if (lstat(path, &statbuf) < 0 || S_ISDIR(statbuf.st_mode) == 0) {
		free(path);
		return 0;
	}

	if (device_load_files(path) != 0) {
		log_error("Failed to load child device %s", path);
		ret = -1;
	}

	free(path);
	return ret;
}

/*
 * Load all device persistent files from the hard drive
 *
 * Return 0 on success, > 0 for error code
 */
static int device_load_files(const char *resdir)
{
	meta_info_t info = { NULL, NULL };
	char *meta = NULL, *file = NULL;
	int ret = 0;

	/*
	 * Load the meta and contract files in the current directory first,
	 * then move on to its children to ensure the parent device is setup
	 * before any of its children devices.
	 *
	 * It doesn't matter if the meta or contract file is missing, e.g.
	 * in the top directory of the Device Root where the helper function
	 * doesn't need to load anything
	 */
	if (link_pathname(&meta, resdir, NULL, SERVER_DB_DEVICE_META,
					  XML_FILENAME_SUFFIX) < 0 ||
		link_pathname(&file, resdir, NULL, SERVER_DB_DEVICE_CONTRACT,
					  XML_FILENAME_SUFFIX) < 0) {
		log_error("Failed to assemble pathname for persistent files under %s",
				  resdir);
		ret = ERR_NO_MEM;
		goto failed;
	}

	if ((ret = device_load_meta(meta, &info)) != 0) {
		log_error("Failed to load device meta at %s", meta);
		goto failed;
	}

	if ((ret = device_load_contract(file, &info)) != 0) {
		log_error("Failed to load device contract at %s", file);
		goto failed;
	}

	if (for_each_file_name(resdir, NULL, NULL,
						   device_load_child, NULL) < 0) {
		ret = ERR_DISK_IO;
	}

	/* Fall through */

failed:
	if (info.owner_id) {
		free(info.owner_id);
	}

	if (info.href) {
		free(info.href);
	}

	if (meta) {
		free(meta);
	}

	if (file) {
		free(file);
	}

	return ret;
}

/*
 * Remove the given device's persistent files on the hard drive
 */
static void __device_remove_files(obix_dev_t *dev)
{
	if (dev->meta) {
		unlink(dev->meta);
	}

	if (dev->file) {
		unlink(dev->file);
	}

	if (dev->dir) {
		rmdir(dev->dir);
	}
}

/*
 * Create persistent device files on the hard drive for the given device
 *
 * Return 0 on success, > 0 for error code
 */
static int __device_create_files(obix_dev_t *dev)
{
	int fd, ret = ERR_DISK_IO, errno_saved = 0;

	errno = 0;
	if (mkdir(dev->dir, OBIX_DIR_PERM) < 0) {
		log_error("Failed to mkdir %s because of %s", dev->dir,
				  strerror(errno));
		errno_saved = errno;
		goto failed;
	}

	errno = 0;
	if ((fd = creat(dev->file, OBIX_FILE_PERM)) < 0) {
		log_error("Failed to creat %s because of %s", dev->file,
				  strerror(errno));
		errno_saved = errno;
		goto file_failed;
	}
	close(fd);

	errno = 0;
	if ((fd = creat(dev->meta, OBIX_FILE_PERM)) < 0) {
		log_error("Failed to creat %s because of %s", dev->meta,
				  strerror(errno));
		errno_saved = errno;
		goto meta_failed;
	}
	close(fd);

	return 0;

meta_failed:
	unlink(dev->file);

file_failed:
	rmdir(dev->dir);

failed:
	if (errno_saved == EEXIST) {
		log_error("Device persistent files exist but corrupted, "
				  "manually fixup required");
	}

	if (errno_saved == EDQUOT || errno_saved == ENOMEM ||
		errno_saved == ENOSPC) {
		ret = ERR_NO_MEM;
	}

	return ret;
}

static int __device_write_meta(obix_dev_t *dev)
{
	char *buf;
	int len, ret = 0;

	len = strlen(DEVICE_META_CONTRACT) + strlen(dev->owner_id) +
		  xmlStrlen(dev->href) - 4;
	if (!(buf = (char *)malloc(len + 1))) {
		log_error("Failed to allocate buf to assemble meta file for device %s",
				  dev->href);
		return ERR_NO_MEM;
	}

	len = sprintf(buf, DEVICE_META_CONTRACT, dev->owner_id, dev->href);
	if (xml_write_file(dev->meta, buf, len) < 0) {
		log_error("Failed to write meta file at %s", dev->meta);
		ret = ERR_NO_MEM;

	}

	free(buf);
	return ret;
}

static time_t __device_get_file_ts(obix_dev_t *dev)
{
	struct stat statbuf;

	return (lstat(dev->file, &statbuf) == 0) ? statbuf.st_mtime : 0;
}

/*
 * Copy the subtree of a device, EXCLUDING any of its
 * children devices
 */
static xmlNode *__device_copy_no_child(obix_dev_t *parent, const xmlNode *src)
{
	xmlNode *copy_src, *copy_node, *node;

	if (!(copy_src = xmlCopyNode((xmlNode *)src, 2))) {
		return NULL;
	}

	for (node = src->children; node; node = node->next) {
		if (node->_private != parent ||
			!(copy_node = __device_copy_no_child(parent, node))) {
			continue;
		}

		if (!xmlAddChild(copy_src, copy_node)) {
			xmlFreeNode(copy_node);
			xmlFreeNode(copy_src);
			return NULL;
		}
	}

	return copy_src;
}

/*
 * Dump the content of a device into a buffer, EXCLUDING any of
 * its children devices
 *
 * NOTE: this is NOT a recursive function, since any child device
 * contracts should not be saved into parent's persistent file
 */
static char *__device_dump_device(obix_dev_t *dev)
{
	xmlNode *copy;
	char *buf = NULL;

	/* No children devices */
	if (list_empty(&dev->children) == 1) {
		return xml_dump_node(dev->node);
	}

	/*
	 * Otherwise excluding all its children devices
	 * by making another copy of it
	 */
	if ((copy = __device_copy_no_child(dev, dev->node)) != NULL) {
		buf = xml_dump_node(copy);
		xmlFreeNode(copy);
	}

	return buf;
}

static int __device_write_file(obix_dev_t *dev, time_t now)
{
	char *buf;
	int ret = 0;

	if (!(buf = __device_dump_device(dev))) {
		log_error("Failed to dump content from device of %s", dev->href);
		return ERR_NO_MEM;
	}

	if (xml_write_file(dev->file, buf, strlen(buf)) < 0) {
		log_error("Failed to write device file at %s", dev->file);
		ret = ERR_NO_MEM;
	}

	/*
	 * Doesn't matter if now == 0 when the persistent file is initially
	 * created and it will get updated by the first write into the device
	 */
	dev->mtime = now;

	free(buf);
	return ret;
}

/*
 * Save the current snapshot of the device contract onto the hard drive
 *
 * For sake of performance and efficiency, it's not desirable and not
 * necessary at all to save every single change of the device into its
 * persistent file on the hard drive, especially when it's being updated
 * very frequently. Instead, backup should be done when the entire
 * device contract may have been properly updated by client via one
 * batch object
 *
 * Return 0 on success, > 0 for error code
 */
int device_write_file(obix_dev_t *dev)
{
	time_t now;
	int ret = 0;

	if (tsync_writer_entry(&dev->sync) < 0) {
		return ERR_INVALID_STATE;
	}

	if ((now = time(NULL)) >= dev->mtime + _devices->backup_period) {
		ret = __device_write_file(dev, now);
	}

	tsync_writer_exit(&dev->sync);

	return ret;
}

/*
 * De-associate the given device descriptor from its social network
 */
static void __device_unlink(obix_dev_t *dev)
{
	hash_del(_devices->tab, dev->href);
	cache_invalidate(_devices->cache, dev->href);

	list_del(&dev->siblings);
	dev->parent = NULL;
}

/*
 * Entirely wipe out any data structure related with the given device
 * except its descriptor
 *
 * NOTE: caller must have entered the "write region" of the parent device
 */
static void __device_purge(obix_dev_t *dev, int sign_off)
{
	/* Delete its XML node from the global DOM tree */
	xmldb_delete_node(dev->node, 0);

	/*
	 * Remove its persistent files and folder on the hard-drive when
	 * the device is signed off. However, they are perserved during
	 * normal server shutdown
	 */
	if (sign_off == 1) {
		__device_remove_files(dev);
	}

	/* De-associate from its parent device's network */
	__device_unlink(dev);
}

/*
 * Delete the specified device descriptor
 *
 * NOTE: Callers must take care of following operations before
 * deleting a device descriptor:
 *	1. unlink and delete relevant node from the global DOM tree;
 *	2. de-associate with parent device descriptor;
 *	3. remove from Devices hash table and cache.
 * They are supposed to be completed within the "write region"
 * of its parent device
 */
static void device_dispose(obix_dev_t *dev)
{
	if (!dev) {
		return;
	}

	if (list_empty(&dev->children) == 0) {
		log_warning("Device of %s still have descendant descriptors, "
					"memory leaks on them ensue!", dev->href);
	}

	if (dev->href) {
		free(dev->href);
	}

	if (dev->owner_id) {
		free(dev->owner_id);
	}

	if (dev->dir) {
		free(dev->dir);
	}

	if (dev->file) {
		free(dev->file);
	}

	if (dev->meta) {
		free(dev->meta);
	}

	if (dev->ref) {
		xmlFreeNode(dev->ref);
	}

	tsync_cleanup(&dev->sync);

	free(dev);
}

static int device_setup_mapping(xmlNode **element, void *arg1, void *arg2)
{
	if (!*element) {
		return -1;
	}

	(*element)->_private = arg1;

	return 0;
}

/*
 * Allocate and initialise a brand-new device descriptor for the node
 * that is going to be added into the global DOM tree
 */
static obix_dev_t *device_init(xmlNode *node, const xmlChar *href,
							   const char *dir, const char *requester_id)
{
	obix_dev_t *dev;

	if (!(dev = (obix_dev_t *)malloc(sizeof(obix_dev_t)))) {
		log_error("Failed to allocate a device descriptor");
		return NULL;
	}
	memset(dev, 0, sizeof(obix_dev_t));

	INIT_LIST_HEAD(&dev->children);
	INIT_LIST_HEAD(&dev->siblings);

	tsync_init(&dev->sync);

	if (!(dev->href = xmlStrdup(href)) ||			/* could be static */
		!(dev->dir = strdup(dir)) ||
		!(dev->owner_id = strdup(requester_id)) ||
		!(dev->ref = xml_create_ref_node(node, href))) {
		log_error("No memory to setup the device descriptor of %s", href);
		goto failed;
	}

	/* No meta and device persistent files for the Device Root */
	if (is_device_root_href(href) == 0) {
		if (link_pathname(&dev->meta, dir, NULL, SERVER_DB_DEVICE_META,
						  XML_FILENAME_SUFFIX) < 0 ||
			link_pathname(&dev->file, dir, NULL, SERVER_DB_DEVICE_CONTRACT,
						  XML_FILENAME_SUFFIX) < 0) {
			log_error("No memory to setup the device descriptor of %s", href);
			goto failed;
		}
	}

	/*
	 * Setup the dual mappings from device node and its descriptor
	 *
	 * NOTE: every single node in the device subtree should have their
	 * _private pointer initialised, sometimes other types of nodes than
	 * elements may be accessed
	 */
	dev->node = node;
	xml_for_each_node_type(node, 0, device_setup_mapping, dev, NULL);

	return dev;

failed:
	device_dispose(dev);
	return NULL;
}

void obix_devices_dispose(void)
{
	if (!_devices) {
		return;
	}

	if (_devices->device_root) {
		/*
		 * Recursively deleting all remaining registered devices
		 * when the server is shutting down to prevent memory leaks.
		 * The persistent device facilities on hard drive will help
		 * server recover at next start-up
		 */
		device_del(_devices->device_root, OBIX_ID_DEVICE, 0);
	}

	if (_devices->cache) {
		cache_dispose(_devices->cache);
	}

	if (_devices->tab) {
		hash_destroy_table(_devices->tab);
	}

	free(_devices);
	_devices = NULL;

	log_debug("The Device subsystem disposed");
}

int obix_devices_init(const char *resdir, const int table_size,
					  const int cache_size, const int backup_period)
{
	xmlNode *root;
	char *dir;

	if (link_pathname(&dir, resdir, NULL, SERVER_DB_DEVICES, NULL) < 0) {
		log_error("Failed to assemble pathname for persistent device database");
		return ERR_NO_MEM;
	}

	if (!(_devices = (obix_devices_t *)malloc(sizeof(obix_devices_t)))) {
		log_error("Failed to allocate descriptor for the Device subsystem");
		goto failed;
	}
	memset(_devices, 0, sizeof(obix_devices_t));

	_devices->backup_period = backup_period;

	if (!(_devices->tab = hash_init_table(table_size, &device_hash_ops))) {
		log_error("Failed to allocate hash table for the Device subsystem");
		goto failed;
	}

	if (!(_devices->cache = cache_init(cache_size))) {
		log_error("Failed to allocate cache for the Device subsystem");
		goto failed;
	}

	if (!(root = xmldb_get_node(BAD_CAST OBIX_DEVICE_ROOT)) ||
		!(_devices->device_root = device_init(root, BAD_CAST OBIX_DEVICE_ROOT,
											  dir, OBIX_ID_DEVICE))) {
		log_error("Failed to setup a device descriptor for the Device Root");
		goto failed;
	}

	if (device_load_files(dir) != 0) {
		log_error("Failed to load device persistent files from %s", dir);
		goto failed;
	}

	free(dir);

	log_debug("The Device subsystem initialised");
	return 0;

failed:
	free(dir);
	obix_devices_dispose();

	return ERR_NO_MEM;
}

/*
 * Get a node from a device subtree
 */
xmlNode *device_get_node(obix_dev_t *dev, const xmlChar *href)
{
	xmlNode *node = NULL;

	if (!dev || !href) {
		return NULL;
	}

	/* href corresponds to the root node of the device */
	if (is_str_identical((const char *)href,
						 (const char *)dev->href) == 1) {
		return dev->node;
	}

	/* or a descendant node within the device */
	if (tsync_reader_entry(&dev->sync) < 0) {
		return NULL;
	}

	node = xmldb_get_node_legacy(dev->node, href + xmlStrlen(dev->href));
	tsync_reader_exit(&dev->sync);

	/*
	 * Cache up subnodes' mapping to parent device's descriptor would be
	 * useful only when the server is not busy and the client is using
	 * simple device contract (such as the example_adaptor). However, this
	 * normally won't be the case since oBIX clients can fastly update
	 * their device contracts, as a result, the cache is flushed quickly
	 */
	return node;
}

xmlChar *device_node_path(obix_dev_t *dev, xmlNode *node)
{
	xmlChar *href;

	if (!dev || !node) {
		return NULL;
	}

	if (dev->node == node) {
		return xmlStrdup(dev->href);
	}

	if (tsync_reader_entry(&dev->sync) < 0) {
		return NULL;
	}

	href = xmldb_node_path_legacy(node, dev->node, dev->href);
	tsync_reader_exit(&dev->sync);

	return href;
}

/*
 * Copy the node from the given device. Basically it's similar to
 * xml_copy_r() but takes extra care of coming across "read region"
 * of the parent and child devices
 */
static xmlNode *__device_copy_node(obix_dev_t *parent, const xmlNode *src,
								   xml_copy_exclude_flags_t flag, int depth)
{
	obix_dev_t *child;
	xmlNode *node, *copy_src = NULL, *copy_node = NULL;
	int ret = ERR_NO_MEM;

	/*
	 * If moving out of the parent device's bourndary and into that of
	 * the child device, come across different "read regions" accordingly
	 */
	if ((child = src->_private) != parent) {
		tsync_reader_exit(&parent->sync);

		if (tsync_reader_entry(&child->sync) < 0) {
			tsync_reader_entry(&parent->sync);
			return NULL;
		}
	}

	/*
	 * If hidden, meta or comment nodes are not explicitly required,
	 * they may be skipped over according to the flag
	 */
	if (depth > 0) {
		if (((flag & XML_COPY_EXCLUDE_HIDDEN) > 0 &&
			 xml_is_hidden(src) == 1) ||
			((flag & XML_COPY_EXCLUDE_META) > 0 &&
			 xmlStrcmp(src->name, BAD_CAST OBIX_OBJ_META) == 0) ||
			((flag & XML_COPY_EXCLUDE_COMMENTS) > 0 &&
			 src->type == XML_COMMENT_NODE)) {
			ret = 0;
			goto out;
		}
	}

	if (!(copy_src = xmlCopyNode((xmlNode *)src, 2))) {
		log_error("Failed to copy a node");
		goto out;
	}

	for (node = src->children; node; node = node->next) {
		if (!(copy_node = __device_copy_node(child, node, flag, ++depth))) {
			/*
			 * The current child may have been deliberatly excluded,
			 * move on to the next one
			 */
			continue;
		}

		if (!xmlAddChild(copy_src, copy_node)) {
			log_error("Failed to organise a node's copy from device %s",
					  parent->href);
			xmlFreeNode(copy_node);
			goto out;
		}
	}

	ret = 0;

	/* Fall through */

out:
	if (child != parent) {
		tsync_reader_exit(&child->sync);

		/*
		 * There isn't much what we can do if failed to re-enter
		 * the "read region" of the parent device
		 */
		tsync_reader_entry(&parent->sync);
	}

	if (ret > 0 && copy_src) {
		xmlFreeNode(copy_src);
		copy_src = NULL;
	}

	return copy_src;
}

/*
 * Copy a node from its host device
 *
 * Return the node's pointer on success, NULL otherwise
 */
xmlNode *device_copy_node(const xmlNode *src, xml_copy_exclude_flags_t flag)
{
	obix_dev_t *dev;
	xmlNode *node;

	if (!src) {
		return NULL;
	}

	if (!(dev = src->_private)) {
		log_error("Broken mapping from device nodes to its descriptor!");
		return NULL;
	}

	if (tsync_reader_entry(&dev->sync) < 0) {
		return NULL;
	}

	node = __device_copy_node(dev, src, flag, 0);

	tsync_reader_exit(&dev->sync);

	return node;
}

/*
 * Get the host device descriptor for the given href
 *
 * Return NULL if it does not exist
 *
 * Search the hash table on a cache miss. If found,
 * also have the cache udpated
 */
obix_dev_t *device_search(const xmlChar *href)
{
	obix_dev_t *dev = NULL;

	if (is_device_href(href) == 0) {
		return NULL;
	}

	if (is_device_root_href(href) == 1) {
		return _devices->device_root;
	}

	if (!(dev = (obix_dev_t *)cache_search(_devices->cache, href))) {
		if ((dev = (obix_dev_t *)hash_search(_devices->tab, href)) != NULL) {
			cache_update(_devices->cache, dev->href, dev);
		}
	}

	return dev;
}

/*
 * Search for the closest or direct parent device that hosts
 * the given href
 *
 * NOTE: the device may not necessarily actually host a node
 * corresponding to the given href. Callers need to invoke
 * device_get_node to further find out whether relevant node
 * in the device's subtree exists or not
 */
obix_dev_t *device_search_parent(const xmlChar *href)
{
	obix_dev_t *dev = NULL;
	xmlChar *parent = NULL;

	if (is_device_href(href) == 0 || is_device_root_href(href) == 1) {
		return NULL;
	}

	if (!(parent = xmlStrdup(href)) ||
		!(parent = (xmlChar *)dirname((char *)parent))) {
		return NULL;
	}

	while (!(dev = device_search(parent))) {
		if (!(parent = (xmlChar *)dirname((char *)parent))) {
			/* Won't have to nullify dev since it is assured to be NULL */
			break;
		}
	}

	if (parent) {
		xmlFree(parent);
	}

	return dev;
}

/*
 * Setup the required social network for the given device descriptor
 *
 * Return 0 on success, > 0 for error codes
 */
static int __device_link(obix_dev_t *child, obix_dev_t *parent)
{
	if (hash_add(_devices->tab, child->href, child) != 0) {
		return ERR_NO_MEM;
	}

	cache_update(_devices->cache, child->href, child);

	list_add_tail(&child->siblings, &parent->children);
	child->parent = parent;
	return 0;
}

/*
 * Delete the specified device. If needed recursively delete its
 * children first, which however, should be done only when the server
 * is shutting down or otherwise this will leave client side dangling
 * Device structures
 *
 * Return 0 on success, > 0 for error code
 */
int device_del(obix_dev_t *child, const char *requester_id, int sign_off)
{
	obix_dev_t *parent, *dev, *n;

	if (sign_off == 1) {
		if (list_empty(&child->children) == 0) {
			log_error("Unable to delete Device %s due to existing children",
					  child->href);
			return ERR_DEVICE_CHILDREN;
		}
	} else {	/* Delete the device during server shutdown */
		list_for_each_entry_safe(dev, n, &child->children, siblings) {
			if (device_del(dev, requester_id, 0) != 0) {
				log_error("Failed to delete device of %s", dev->href);
				/*
				 * Keep on going because unable to recovery since some children
				 * may have been deleted
				 */
			}
		}
	}

	/* The Device Root has no parent and can't be signed off*/
	if (child == _devices->device_root) {
		__device_purge(child, 0);
		device_dispose(child);
		return 0;
	}

	if (!(parent = child->parent)) {
		log_error("Broken device descriptor of %s without parent", child->href);
		return ERR_DEVICE_ORPHAN;
	}

	if (se_lookup(requester_id, parent->owner_id, OP_DEVICE_REMOVE) == 0) {
		log_error("\"%s\" permission denied to remove child device of %s "
				  "from parent device owned by \"%s\"",
				  requester_id, child->href, parent->owner_id);
		return ERR_PERM_DENIED;
	}

	if (se_lookup(requester_id, child->owner_id, OP_DEVICE_DELETE) == 0) {
		log_error("\"%s\" permission denied to delete device of %s "
				  "owned by \"%s\"",
				  requester_id, child->href, child->owner_id);
		return ERR_PERM_DENIED;
	}

	xmldb_notify_watches(child->node, WATCH_EVT_NODE_DELETED);

	/*
	 * Signal that the device is shutting down to ensure no further
	 * reader nor writer. Upon return any existing readers and writers
	 * are ensured completed and exited
	 */
	tsync_shutdown(&child->sync);

	if (tsync_writer_entry(&parent->sync) < 0) {
		log_error("Device %s is being shutting down, abort write attempt",
				  parent->href);
		return ERR_INVALID_STATE;
	}

	__device_purge(child, sign_off);

	tsync_writer_exit(&parent->sync);

	xmldb_notify_watches(parent->node, WATCH_EVT_NODE_CHANGED);

	device_dispose(child);
	return 0;
}

/*
 * Add the given node into the global DOM tree, also setup its device
 * descriptor and the link with that of the parent device
 *
 * Return 0 on success, > 0 for error code
 */
int device_add(xmlNode *input, const xmlChar *href, const char *requester_id,
			   int sign_up)		/* == 0 if loadded from persistent files */
{
	obix_dev_t *parent, *child = NULL, *dev;
	xmlNode *mount_point;
	xmlChar *mount_point_href = NULL, *name = NULL;
	char *dir;
	int ret = 0;

	if (!(parent = device_search_parent(href))) {
		log_error("Failed to find the parent device for %s", href);
		return ERR_DEVICE_NO_SUCH_URI;
	}

	/*
	 * Devices are expected to have unique names in the territory
	 * of one oBIX server so that their persistent files can differ
	 * from each other
	 */
	if (!(name = xmlGetProp(input, BAD_CAST OBIX_ATTR_NAME))) {
		log_error("Failed to get the new device's name at %s", href);
		return ERR_NO_NAME;
	}

	if (link_pathname(&dir, parent->dir, NULL, (const char *)name, NULL) < 0) {
		log_error("Failed to assemble pathname for persistent device files");
		ret = ERR_NO_MEM;
		goto failed;
	}

	if (!(mount_point_href = xmlStrdup(href)) ||
		!(mount_point_href = (xmlChar *)dirname((char *)mount_point_href))) {
		ret = ERR_NO_MEM;
		goto failed;
	}

	if (!(mount_point = device_get_node(parent, mount_point_href))) {
		log_error("Failed to get the mount point for new device at %s", href);
		ret = ERR_NO_SUCH_URI;
		goto failed;
	}

	if (se_lookup(requester_id, parent->owner_id, OP_DEVICE_ADD) == 0) {
		log_error("Permission denied to sign up new device of %s", href);
		ret = ERR_PERM_DENIED;
		goto failed;
	}

	if (!(child = device_init(input, href, dir, requester_id))) {
		log_error("Failed to allocate device descriptor for %s", href);
		ret = ERR_NO_MEM;
		goto failed;
	}

	if (tsync_writer_entry(&parent->sync) < 0) {
		log_error("Device %s is being shutting down, abort write attempt",
				  parent->href);
		ret = ERR_INVALID_STATE;
		goto failed;
	}

	/*
	 * Re-check if the client has registered the same device already
	 * since multiple signUp requests may be handled in parallel
	 */
	list_for_each_entry(dev, &parent->children, siblings) {
		if (xmlStrcmp(dev->href, href) == 0) {
			tsync_writer_exit(&parent->sync);
			log_debug("Device of %s already signed up by %s",
					  href, dev->owner_id);
			ret = (strcmp(dev->owner_id, requester_id) == 0) ?
						ERR_DEVICE_EXISTS : ERR_DEVICE_CONFLICT_OWNER;
			goto failed;
		}
	}

	/*
	 * NOTE: hrefs in persistent device files are relative already
	 */
	if ((ret = xmldb_add_child(mount_point, input, 1, sign_up)) != 0) {
		tsync_writer_exit(&parent->sync);
		log_error("Failed to add device of %s into global DOM tree", href);
		goto failed;
	}

	if ((ret = __device_link(child, parent)) != 0) {
		log_error("Failed to add device of %s into parent network", href);
		goto link_failed;
	}

	/*
	 * Only create persistent files for newly signed up devices,
	 * on failure they are signed off
	 */
	if (sign_up == 1) {
		if ((ret = __device_create_files(child)) != 0 ||
			(ret = __device_write_meta(child)) != 0 ||
			(ret = __device_write_file(child, 0)) != 0) {
			log_error("Failed to setup device persistent files for %s", href);
			goto disk_failed;
		}
	} else {
		child->mtime = __device_get_file_ts(child);
	}

	tsync_writer_exit(&parent->sync);

	/*
	 * NOTE: notifying ancestors of the newly added device would have to
	 * be placed outside of above "write region". Since xmldb_node_path
	 * is invoked in the "read region" of the monitored node to get its
	 * href
	 *
	 * Deadlock ensues if the monitored node is within the parent device
	 */
	if (sign_up == 1) {
		xmldb_notify_watches(mount_point, WATCH_EVT_NODE_CHANGED);
	}

	/* Fall through */

disk_failed:
	if (ret > 0) {
		__device_remove_files(child);
		__device_unlink(child);
	}

link_failed:
	if (ret > 0) {
		xmlUnlinkNode(input);
		tsync_writer_exit(&parent->sync);
	}

failed:
	if (name) {
		xmlFree(name);
	}

	if (mount_point_href) {
		xmlFree(mount_point_href);
	}

	if (dir) {
		free(dir);
	}

	if (ret > 0 && child) {
		device_dispose(child);
	}

	/*
	 * If the device has been registered by the same client before, regard
	 * the current attempt as success
	 */
	if (ret == ERR_DEVICE_EXISTS) {
		ret = 0;
	}

	return ret;
}

/*
 * Update the val attribute on the given device node and
 * notify relevant watch objects if the val attribute is
 * first set or changed
 *
 * Return 0 on success, > 0 for error code
 */
int device_update_node(xmlNode *target, const xmlChar *new)
{
	obix_dev_t *dev;
	xmlChar *old = NULL;
	int changed = 0, ret = 0;

	if (!(dev = target->_private)) {
		log_error("Broken mapping from device nodes to its descriptor!");
		return ERR_DEVICE_ORPHAN;
	}

	if (tsync_writer_entry(&dev->sync) < 0) {
		log_error("Device %s is being shutting down, abort write attempt",
				  dev->href);
		return ERR_INVALID_STATE;
	}

	if (!(old = xmlGetProp(target, BAD_CAST OBIX_ATTR_VAL)) ||
		xmlStrcmp(old, new) != 0) {
		if (!xmlSetProp(target, BAD_CAST OBIX_ATTR_VAL, new)) {
			log_error("Failed to set the val attribute within %s", dev->href);
			ret = ERR_NO_MEM;
		} else {
			changed = 1;
		}
	}

	tsync_writer_exit(&dev->sync);

	if (changed == 1) {
		xmldb_notify_watches(target, WATCH_EVT_NODE_CHANGED);
	}

	if (old) {
		xmlFree(old);
	}

	return ret;
}

/*
 * Add a new child node under the given parent node and backup
 * the latest device contract onto the hard drive.
 *
 * It's important to setup the newly added node's _private pointer
 * properly
 *
 * Return 0 on success, > 0 for error code
 */
int device_add_node(xmlNode *parent, xmlNode *child)
{
	obix_dev_t *dev;
	int ret = 0;

	if (!parent || !child) {
		return ERR_INVALID_ARGUMENT;
	}

	if (!(dev = parent->_private)) {
		log_error("Broken mapping from device nodes to its descriptor!");
		return ERR_DEVICE_ORPHAN;
	}

	if (tsync_writer_entry(&dev->sync) < 0) {
		return ERR_INVALID_STATE;
	}

	if (!xmlAddChild(parent, child)) {
		ret = ERR_NO_MEM;
	} else {
		__device_write_file(dev, 0);
		child->_private = dev;
	}

	tsync_writer_exit(&dev->sync);

	return ret;
}

/*
 * Delete the given node from the global DOM tree and backup
 * the latest device contract onto the hard drive
 *
 * Return 0 on success, > 0 for error code
 */
int device_delete_node(xmlNode *node)
{
	obix_dev_t *dev;
	xmlNode *parent;

	if (!node || !(parent = node->parent)) {
		log_error("Orphan node without parent!");
		return ERR_INVALID_ARGUMENT;
	}

	if (!(dev = parent->_private)) {
		log_error("Broken mapping from device nodes to its descriptor!");
		return ERR_DEVICE_ORPHAN;
	}

	if (tsync_writer_entry(&dev->sync) < 0) {
		log_error("Device %s is being shutting down, abort write attempt",
				  dev->href);
		return ERR_INVALID_STATE;
	}

	xml_delete_node(node);

	__device_write_file(dev, 0);

	tsync_writer_exit(&dev->sync);

	return 0;
}

/*
 * Read the "op" meta node in a device contract
 *
 * Return 0 on success, > 0 for error code
 */
int device_get_op_id(const xmlNode *node, long *id)
{
	obix_dev_t *dev;
	int ret = 0;

	if (!(dev = node->_private)) {
		log_error("Broken mapping from device nodes to its descriptor!");
		return ERR_DEVICE_ORPHAN;
	}

	if (tsync_reader_entry(&dev->sync) < 0) {
		return ERR_INVALID_STATE;
	}

	ret = xmldb_get_op_id_legacy(node, id);
	tsync_reader_exit(&dev->sync);

	return ret;
}

xmlNode *device_dump_ref(void)
{
	xmlNode *root = xmlDocGetRootElement(_storage);
	xmlNode *node, *copy, *ref, *ref_copy;
	hash_head_t *head;
	hash_node_t *hn;
	int i;

	if (!(node = xmldb_get_node_legacy(root, BAD_CAST OBIX_DEVICES)) ||
		!(copy = xmldb_copy_node_legacy(node, 0))) {
		log_error("Failed to copy from %s", OBIX_DEVICES);
		return NULL;
	}

	xmlUnsetProp(copy, BAD_CAST OBIX_ATTR_HIDDEN);
	xmlSetProp(copy, BAD_CAST OBIX_ATTR_HREF, BAD_CAST OBIX_DEVICES);

	for (i = 0; i < _devices->tab->size; i++) {
		head = &_devices->tab->table[i];

		tsync_reader_entry(&head->sync);

		list_for_each_entry(hn, &head->head, list) {
			ref = ((obix_dev_t *)(hn->item))->ref;
			if (!(ref_copy = xmlCopyNode(ref, 1)) ||
				!xmlAddChild(copy, ref_copy)) {
				tsync_reader_exit(&head->sync);
				goto failed;
			}
		}

		tsync_reader_exit(&head->sync);
	}

	return copy;

failed:
	if (ref_copy) {
		xmlFreeNode(ref_copy);
	}

	xmlFreeNode(copy);
	return NULL;
}

#ifdef DEBUG
xmlNode *device_dump(void)
{
	hash_table_t *tab = _devices->tab;
	hash_head_t *head;
	hash_node_t *node;
	xmlNode *dump = NULL, *list = NULL, *item = NULL;
	int i, ret = 0;
	char buf[32];

	if (!(dump = xmlNewNode(NULL, BAD_CAST OBIX_OBJ_LIST))) {
		ret = ERR_NO_MEM;
		goto failed;
	}

	/* Being lazy on checking return value for debug functions */
	sprintf(buf, "%d", tab->size);
	xmlSetProp(dump, BAD_CAST OBIX_ATTR_NAME, BAD_CAST "Device Hash Table");
	xmlSetProp(dump, BAD_CAST "size", BAD_CAST buf);
	xmlSetProp(dump, BAD_CAST OBIX_ATTR_OF, BAD_CAST "obix:list");

	for (i = 0; i < tab->size; i++) {
		head = &tab->table[i];

		if (!(list = xmlNewNode(NULL, BAD_CAST OBIX_OBJ_LIST))) {
			ret = ERR_NO_MEM;
			break;
		}

		sprintf(buf, "Queue %d", i);
		xmlSetProp(list, BAD_CAST OBIX_ATTR_NAME, BAD_CAST buf);
		xmlSetProp(list, BAD_CAST OBIX_ATTR_OF, BAD_CAST "obix:uri");

		tsync_reader_entry(&head->sync);

		sprintf(buf, "%d", head->count);
		xmlSetProp(list, BAD_CAST "len", BAD_CAST buf);

		list_for_each_entry(node, &head->head, list) {
			if (!(item = xmlNewNode(NULL, BAD_CAST OBIX_OBJ_URI)) ||
				!xmlSetProp(item, BAD_CAST OBIX_ATTR_VAL,
							((obix_dev_t *)node->item)->href) ||
				!xmlAddChild(list, item)) {
				ret = ERR_NO_MEM;
				break;
			}
		}

		tsync_reader_exit(&head->sync);

		if (ret > 0 || !xmlAddChild(dump, list)) {
			ret = ERR_NO_MEM;
			break;
		}
	}

	/* Fall through */

failed:
	if (ret > 0) {
		if (item) {
			xmlFreeNode(item);
		}

		if (list) {
			xmlFreeNode(list);
		}

		if (dump) {
			xmlFreeNode(dump);
		}

		dump = xmldb_fatal_error();
	}

	return dump;
}

xmlNode *device_cache_dump(void)
{
	cache_t *cache = _devices->cache;
	xmlNode *dump = NULL, *list = NULL, *item = NULL;
	const xmlChar *href;
	char buf[32];
	int ret = 0, i;

	if (!(dump = xmlNewNode(NULL, BAD_CAST OBIX_OBJ)) ||
		!(list = xmlNewNode(NULL, BAD_CAST OBIX_OBJ_LIST)) ||
		!xmlAddChild(dump, list)) {
		ret = ERR_NO_MEM;
		goto failed;
	}

	xmlSetProp(dump, BAD_CAST OBIX_ATTR_NAME, BAD_CAST "Device Cache");
	xmlSetProp(list, BAD_CAST OBIX_ATTR_NAME, BAD_CAST "Cache slots");
	xmlSetProp(list, BAD_CAST OBIX_ATTR_OF, BAD_CAST "obix:uri");

	pthread_mutex_lock(&cache->mutex);

	sprintf(buf, "%ld", cache->hit);
	xmlSetProp(dump, BAD_CAST "hit", BAD_CAST buf);

	sprintf(buf, "%ld", cache->miss);
	xmlSetProp(dump, BAD_CAST "miss", BAD_CAST buf);

	for (i = 0; i < cache->len; i++) {
		href = (cache->items[i].href) ? cache->items[i].href : BAD_CAST "NULL";
		if (!(item = xmlNewNode(NULL, BAD_CAST OBIX_OBJ_URI)) ||
			!xmlSetProp(item, BAD_CAST OBIX_ATTR_VAL, href) ||
			!xmlAddChild(list, item)) {
			ret = ERR_NO_MEM;
			break;
		}
	}

	pthread_mutex_unlock(&cache->mutex);

failed:
	if (ret > 0) {
		if (list) {
			xmlFreeNode(list);
		}

		if (dump) {
			xmlFreeNode(dump);
		}

		dump = xmldb_fatal_error();

	}

	return dump;
}
#endif
