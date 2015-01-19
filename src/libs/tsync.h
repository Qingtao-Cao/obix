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
 * "tsync" stands for "threads synchronisation", it provides a possible
 * implementation of the Readers-Writers model in a multi-thread environment
 * with the following features:
 *	. multi readers can co-exist;
 *	. a running writer excludes any other writer or reader;
 *	. if there is any existing writer, in particular, any waiting writer
 *	  due to running readers, no more readers are allowed;
 *	. synchronised shutdown.
 *
 * However, persistent in-coming writers will starve readers. Therefore
 * this implementation suits for cases when the number of readers outweighs
 * that of writers and writting operations are not time-consuming.
 */

#ifndef _TSYNC_H
#define _TSYNC_H

#include <pthread.h>

/*
 * tsync structure should be embedded in another hosting structure
 */
typedef struct tsync {
	/* Indicator whether a shutdown begins */
	int being_shutdown;

	/*
	 * The number of readers and writers respectively,
	 * regardless of their status
	 */
	int readers, writers;

	/* The number of running readers and writers respectively */
	int running_readers, running_writers;

	/* The wait queues for readers and writers respectively */
	pthread_cond_t rq, wq;

	/* The wait queue for the shutdown thread */
	pthread_cond_t swq;

	/* The lock to protect the whole structure */
	pthread_mutex_t mutex;
} tsync_t;

void tsync_init(tsync_t *sync);
void tsync_cleanup(tsync_t *sync);
int tsync_writer_entry(tsync_t *sync);
void tsync_writer_exit(tsync_t *sync);
int tsync_reader_entry(tsync_t *sync);
void tsync_reader_exit(tsync_t *sync);
int tsync_shutdown_entry(tsync_t *sync);
void tsync_shutdown_revoke(tsync_t *sync);
#endif
