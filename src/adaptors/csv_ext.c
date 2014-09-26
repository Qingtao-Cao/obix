/* *****************************************************************************
 * Copyright (c) 2014 Qingtao Cao [harry.cao@nextdc.com]
 *
 * This file is part of obix-adaptors
 *
 * obix-adaptors is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * obix-adaptors is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with obix-adaptors. If not, see <http://www.gnu.org/licenses/>.
 *
 * *****************************************************************************/

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "obix_client.h"
#include "log_utils.h"
#include "csv_ext.h"

/*
 * Create a CSV record descriptor for an desirable record
 * and append it to relevant queue.
 */
int csv_add_record(csv_state_t *csv, const void *data)
{
	csv_record_t *record;

	assert(csv && data);

	if (!(record = (csv_record_t *)malloc(sizeof(csv_record_t)))) {
		return OBIX_ERR_NO_MEMORY;
	}

	record->data = data;
	INIT_LIST_HEAD(&record->list);

	list_add_tail(&record->list, &csv->wanted);

	return OBIX_SUCCESS;
}

/* Destroy all CSV record descriptors */
void csv_destroy_records(csv_state_t *csv)
{
	csv_record_t *record, *n;

	list_for_each_entry_safe(record, n, &csv->wanted, list) {
		list_del(&record->list);
		free(record);
	}
}

static void csv_destroy_file(csv_file_t *file)
{
	if (file->path) {
		free(file->path);
	}

	free(file);
}

/* Destroy all CSV file descriptors */
void csv_destroy_files(csv_state_t *csv)
{
	csv_file_t *file, *n;

	list_for_each_entry_safe(file, n, &csv->files, list) {
		list_del(&file->list);
		csv_destroy_file(file);
	}
}

/* Destroy the specified CSV folder descriptor */
void csv_destroy_csv(csv_state_t *csv)
{
	if (!csv) {
		return;
	}

	if (csv->buf) {
		free(csv->buf);
	}

	csv_free(&csv->parser);

	csv_destroy_records(csv);
	csv_destroy_files(csv);

	free(csv);
}

/*
 * Allocate and intialise a CSV file's descriptor
 *
 * Return the address of the new descriptor on success,
 * NULL otherwise
 */
csv_state_t *csv_setup_csv(csv_ops_t *op, int options, unsigned char delim)
{
	csv_state_t *csv;

	assert(op);

	if (!(csv = (csv_state_t *)malloc(sizeof(csv_state_t)))) {
		return NULL;
	}
	memset(csv, 0, sizeof(csv_state_t));

	if (csv_init(&csv->parser, options) != 0) {
		free(csv);
		csv = NULL;
	} else {
		csv->op = op;
		csv->arg = csv;
		INIT_LIST_HEAD(&csv->wanted);
		INIT_LIST_HEAD(&csv->files);
		csv_set_delim(&csv->parser, delim);
	}

	return csv;
}

/*
 * Read the given CSV file and parse its content by the user-provided
 * callbacks. In particular, selectively pick up values of those wanted
 * records into relevant descriptors
 */
int csv_read_file(csv_state_t *csv, csv_file_t *file)
{
	int fd, ret, len;

	assert(csv && file && file->path);

	if (file->size == 0) {
		log_warning("A race codition with BMS, new file created "
					"but not written completely yet : %s", file->path);
		return OBIX_ERR_BAD_CONNECTION_HW;
	}

	if ((fd = open(file->path, O_RDONLY)) < 0) {
		log_error("Failed to open csv file at %s", file->path);
		return OBIX_ERR_BAD_CONNECTION_HW;
	}

	if (!(csv->buf = (char *)realloc(csv->buf, file->size + 1))) {
		log_error("Failed to allocate buffer of %d bytes to "
				  "read csv file at %s", file->size, file->path);
		close(fd);
		return OBIX_ERR_NO_MEMORY;
	}

	ret = read(fd, csv->buf, file->size);
	close(fd);

	if (ret != file->size) {
		log_error("Failed to read from %s", file->path);
		return OBIX_ERR_BAD_CONNECTION_HW;
	}

	csv->buf[file->size] = '\0';

	if (csv->op->p) {
		len = (csv->op->p)(csv->buf, file->size);
	} else {
		len = file->size;
	}

	if (csv_parse(&csv->parser, csv->buf, len,
				  csv->op->cb1, csv->op->cb2, csv->arg) != len) {
		log_error("Failed to parse csv file: %s",
				  csv_strerror(csv_error(&csv->parser)));
	}

	csv_fini(&csv->parser, NULL, NULL, csv->arg);

	return OBIX_SUCCESS;
}

/*
 * Create a descriptor for a CSV file and enqueue it based on its
 * latest modification time
 *
 * Return OBIX_SUCCESS on success, < 0 on errors
 */
int csv_setup_file(const char *dir, const char *path, void *arg)
{
	struct list_head *head = (struct list_head *)arg;
	csv_file_t *file, *n;
	struct stat statbuf;
	int ret;

	if (!(file = (csv_file_t *)malloc(sizeof(csv_file_t)))) {
		log_error("Failed to malloc csv file desc");
		return OBIX_ERR_NO_MEMORY;
	}
	memset(file, 0, sizeof(csv_file_t));

	if (link_pathname(&file->path, dir, NULL, path, NULL) < 0) {
		log_error("Failed to stitch csv file path for %s", path);
		ret = OBIX_ERR_NO_MEMORY;
		goto failed;
	}

	INIT_LIST_HEAD(&file->list);

	if (lstat(file->path, &statbuf) < 0) {
		log_error("Failed to get statistics of %s", file->path);
		ret = OBIX_ERR_BAD_CONNECTION_HW;
		goto failed;
	}

	file->size = statbuf.st_size;
	file->mtime = statbuf.st_mtime;

	list_for_each_entry(n, head, list) {
		if (n->mtime < file->mtime) {
			continue;
		}

		__list_add(&file->list, n->list.prev, &n->list);
		return OBIX_SUCCESS;
	}

	/* List empty or reaching the end of it */
	if (&n->list == head) {
		list_add_tail(&file->list, head);
		return OBIX_SUCCESS;
	}

	log_error("Should never reach here!");
	return OBIX_ERR_NO_MEMORY;

failed:
	csv_destroy_file(file);
	return ret;
}
