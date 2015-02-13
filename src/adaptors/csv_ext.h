/* *****************************************************************************
 * Copyright (c) 2013-2015 Qingtao Cao
 *
 * This file is part of obix.
 *
 * obix is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * obix is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with obix. If not, see <http://www.gnu.org/licenses/>.
 *
 * *****************************************************************************/

#ifndef _CSV_HEADER_
#define _CSV_HEADER_

#include <csv.h>
#include <time.h>
#include "list.h"

typedef size_t (*prep)(void *, size_t);
typedef void (*for_one_field)(void *, size_t, void *);
typedef void (*for_one_record)(int, void *);

/*
 * Callbacks invoked by CSV parser
 *
 * Users should provide the POLICY to manipulate CSV data, while
 * this file implements the MECHANISM to enforce that POLICY.
 */
typedef struct csv_ops {
	/* The callback invoked before parsing the content */
	prep p;

	/* The callback invoked after an entire field has been read */
	for_one_field cb1;

	/* The callback invoked when the end of a record is encountered */
	for_one_record cb2;
} csv_ops_t;

/*
 * Descriptor for a generic CSV record
 *
 * Users should provide their own, specific CSV record descriptor
 * and should also populate the csv_file_t.wanted queue with references
 * to the CSV file
 */
typedef struct csv_record {
	/* Reference to user-specific CSV record descriptor */
	const void *data;

	/* To join the csv_file_t.wanted queue */
	struct list_head list;
} csv_record_t;

/* Descriptor for a CSV file */
typedef struct csv_file {
	/* The absolute path of the CSV file on the hard-drive */
	char *path;

	/* The size of the current CSV file */
	int size;

	/* The timestamp of the most recently modification */
	time_t mtime;

	/* To join csv_folder_t.files list */
	struct list_head list;
} csv_file_t;

/*
 * Descriptor for a CSV wrapper, including a CSV parser, state machine
 * and relevant parameters, the list of CSV files to be processed and
 * the list of records interested by the user application
 */
typedef struct csv_state {
	/* The CSV Parser */
	struct csv_parser parser;

	/* The buffer containing all data in one CSV file */
	char *buf;

	/* The argument passed to callbacks */
	void *arg;

	/* The delimiter used in the CSV file */
	unsigned char delim;

	/*
	 * Callbacks invoked by CSV Parser. Users need to specify
	 * how they would like to manipulate CSV records
	 */
	csv_ops_t *op;

	/* The list of CSV records needed by user */
	struct list_head wanted;

	/* The list of CSV files, in mtime ascending order */
	struct list_head files;

	/* The number of fields of a record */
	int fields_count;

	/*
	 * Pointing to the user-defined data pointed to by
	 * one of wanted records
	 */
	void *matching;
} csv_state_t;

csv_state_t *csv_setup_csv(csv_ops_t *, int, unsigned char);

int csv_add_record(csv_state_t *, const void *);

int csv_read_file(csv_state_t *, csv_file_t *);

void csv_destroy_csv(csv_state_t *);

void csv_destroy_files(csv_state_t *);

void csv_destroy_records(csv_state_t *);

int csv_setup_file(const char *dir, const char *path, void *arg);

#endif /* _CSV_HEADER_ */
