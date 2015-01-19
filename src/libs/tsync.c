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

#include "tsync.h"

#undef DEBUG_TSYNC

#ifdef DEBUG_TSYNC
#include <assert.h>
#include "log_utils.h"
#include "obix_utils.h"		/* get_tid */
#endif
/*
 * tsync_t is embedded in a hosting data structure, therefore
 * no need to take care of their memory allocation and release
 */
void tsync_init(tsync_t *sync)
{
	sync->being_shutdown = 0;
	sync->readers = sync->writers = 0;
	sync->running_readers = sync->running_writers = 0;

	pthread_mutex_init(&sync->mutex, NULL);
	pthread_cond_init(&sync->rq, NULL);
	pthread_cond_init(&sync->wq, NULL);
	pthread_cond_init(&sync->swq, NULL);
}

void tsync_cleanup(tsync_t *sync)
{
	pthread_cond_destroy(&sync->swq);
	pthread_cond_destroy(&sync->wq);
	pthread_cond_destroy(&sync->rq);
	pthread_mutex_destroy(&sync->mutex);
}

/*
 * Raise the shutdown flag and waiting for the completion of any
 * existing readers or writers
 *
 * NOTE: Only one thread can actually shut down an object while
 * other threads with same intention will end up with object
 * unavailable to avoid segfault caused by double-free
 */
int tsync_shutdown_entry(tsync_t *sync)
{
	pthread_mutex_lock(&sync->mutex);
	if (sync->being_shutdown == 1) {
		pthread_mutex_unlock(&sync->mutex);
		return -1;
	}

	/*
	 * NOTE: must raise the shutdown flag before sleeping so that
	 * the last running writer or reader can wake it up
	 */
	sync->being_shutdown = 1;

	while (sync->readers > 0 || sync->writers > 0) {
#ifdef DEBUG_TSYNC
		log_debug("[%u] Wait for pending writers = %d, readers = %d (%p)",
				  get_tid(), sync->writers, sync->readers, sync);
#endif
		pthread_cond_wait(&sync->swq, &sync->mutex);
	}

	pthread_mutex_unlock(&sync->mutex);
	return 0;
}

/*
 * Reset the shutdown flag and make relevant object available again
 */
void tsync_shutdown_revoke(tsync_t *sync)
{
	pthread_mutex_lock(&sync->mutex);

	if (sync->being_shutdown == 1) {
		sync->being_shutdown = 0;
	}

	pthread_mutex_unlock(&sync->mutex);
}

int tsync_writer_entry(tsync_t *sync)
{
	pthread_mutex_lock(&sync->mutex);

	if (sync->being_shutdown == 1) {
		pthread_mutex_unlock(&sync->mutex);
		return -1;
	}

	sync->writers++;

	while (sync->running_readers > 0 || sync->running_writers > 0) {
#ifdef DEBUG_TSYNC
		log_debug("[%u] New Writer waiting for existing writers = %d, readers = %d (%p)",
				  get_tid(), sync->writers, sync->readers, sync);
#endif
		pthread_cond_wait(&sync->wq, &sync->mutex);
	}

	sync->running_writers++;

#ifdef DEBUG_TSYNC
	log_debug("[%u] Writer entered, writers = %d, readers = %d (%p)",
			  get_tid(), sync->writers, sync->readers, sync);
#endif

	pthread_mutex_unlock(&sync->mutex);
	return 0;
}

void tsync_writer_exit(tsync_t *sync)
{
	pthread_mutex_lock(&sync->mutex);

	sync->writers--;
	sync->running_writers--;

#ifdef DEBUG_TSYNC
	assert(sync->running_writers == 0);
	assert(sync->running_readers == 0);
#endif

	/*
	 * NOTE: Pending writers are given priority over pending readers
	 *
	 * NOTE: only ONE blocked writer is waken up each time, whereas
	 * ALL blocked readers are waken up since they can run concurrently
	 *
	 * NOTE: if there are any pending writers or readers, ignore
	 * the shutdown flag until they are all completed
	 */
	if (sync->writers > 0) {
		pthread_cond_signal(&sync->wq);
	} else if (sync->readers > 0) {
		pthread_cond_broadcast(&sync->rq);
	} else if (sync->being_shutdown == 1 &&
			   sync->readers == 0 &&
			   sync->writers == 0) {
		pthread_cond_signal(&sync->swq);
	}

#ifdef DEBUG_TSYNC
	log_debug("[%u] Writer exited, writers = %d, readers = %d (%p)",
			  get_tid(), sync->writers, sync->readers, sync);
#endif

	pthread_mutex_unlock(&sync->mutex);
}

int tsync_reader_entry(tsync_t *sync)
{
	pthread_mutex_lock(&sync->mutex);

	if (sync->being_shutdown == 1) {
		pthread_mutex_unlock(&sync->mutex);
		return -1;
	}

	sync->readers++;

	while (sync->writers > 0) {
#ifdef DEBUG_TSYNC
		log_debug("[%u] Reader begin sleeping, writers = %d, readers = %d (%p)",
				  get_tid(), sync->writers, sync->readers, sync);
#endif
		pthread_cond_wait(&sync->rq, &sync->mutex);
	}

	sync->running_readers++;

#ifdef DEBUG_TSYNC
	log_debug("[%u] Reader entered, writers = %d, readers = %d, running_readers = %d (%p)",
			  get_tid(), sync->writers, sync->readers, sync->running_readers, sync);
#endif

	pthread_mutex_unlock(&sync->mutex);
	return 0;
}

void tsync_reader_exit(tsync_t *sync)
{
	pthread_mutex_lock(&sync->mutex);

	/*
	 * NOTE: can't be done along with a comparison operation
	 * logically AND/ORed with others since it may not have a
	 * chance to be executed at all!
	 */
	sync->readers--;
	sync->running_readers--;

	if (sync->running_readers == 0 &&
		sync->writers > 0) {
		pthread_cond_signal(&sync->wq);
	}

	if (sync->being_shutdown == 1 &&
		sync->readers == 0 &&
		sync->writers == 0) {
		pthread_cond_signal(&sync->swq);
	}

#ifdef DEBUG_TSYNC
	log_debug("[%u] Reader exited, writers = %d, readers = %d, running_readers = %d (%p)",
			  get_tid(), sync->writers, sync->readers, sync->running_readers, sync);
#endif

	pthread_mutex_unlock(&sync->mutex);
}
