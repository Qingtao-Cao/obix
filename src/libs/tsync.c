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

#include "tsync.h"

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
 */
void tsync_shutdown(tsync_t *sync)
{
	pthread_mutex_lock(&sync->mutex);
	if (sync->being_shutdown == 1) {
		pthread_mutex_unlock(&sync->mutex);
		return;
	}

	sync->being_shutdown = 1;

	while (sync->readers > 0 || sync->writers > 0) {
		pthread_cond_wait(&sync->swq, &sync->mutex);
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
		pthread_cond_wait(&sync->wq, &sync->mutex);
	}

	sync->running_writers++;

	pthread_mutex_unlock(&sync->mutex);
	return 0;
}

void tsync_writer_exit(tsync_t *sync)
{
	pthread_mutex_lock(&sync->mutex);

	sync->running_writers--;
	sync->writers--;

	if (sync->writers > 0) {
		pthread_cond_signal(&sync->wq);
	} else if (sync->readers > 0) {
		pthread_cond_signal(&sync->rq);
	} else if (sync->being_shutdown == 1) {
		pthread_cond_signal(&sync->swq);
	}

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
		pthread_cond_wait(&sync->rq, &sync->mutex);
	}

	sync->running_readers++;

	pthread_mutex_unlock(&sync->mutex);
	return 0;
}

void tsync_reader_exit(tsync_t *sync)
{
	pthread_mutex_lock(&sync->mutex);

	sync->readers--;
	sync->running_readers--;

	if (sync->running_readers == 0 && sync->writers > 0) {
		pthread_cond_signal(&sync->wq);
	}

	if (sync->being_shutdown == 1 && sync->readers == 0 && sync->writers == 0) {
		pthread_cond_signal(&sync->swq);
	}

	pthread_mutex_unlock(&sync->mutex);
}
