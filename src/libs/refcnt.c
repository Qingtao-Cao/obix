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

#include "refcnt.h"

void refcnt_init(refcnt_t *refcnt)
{
	refcnt->count = 0;

	pthread_mutex_init(&refcnt->mutex, NULL);
	pthread_cond_init(&refcnt->wq, NULL);
}

void refcnt_cleanup(refcnt_t *refcnt)
{
	pthread_cond_destroy(&refcnt->wq);
	pthread_mutex_destroy(&refcnt->mutex);
}

void refcnt_get(refcnt_t *refcnt)
{
	pthread_mutex_lock(&refcnt->mutex);
	refcnt->count++;
	pthread_mutex_unlock(&refcnt->mutex);
}

void refcnt_put(refcnt_t *refcnt)
{
	pthread_mutex_lock(&refcnt->mutex);

	if (--refcnt->count == 0) {
		pthread_cond_signal(&refcnt->wq);
	}

	pthread_mutex_unlock(&refcnt->mutex);
}

void refcnt_sync(refcnt_t *refcnt)
{
	pthread_mutex_lock(&refcnt->mutex);

	while (refcnt->count > 0) {
		pthread_cond_wait(&refcnt->wq, &refcnt->mutex);
	}

	pthread_mutex_unlock(&refcnt->mutex);
}

int refcnt_read(refcnt_t *refcnt)
{
	int count;

	pthread_mutex_lock(&refcnt->mutex);
	count = refcnt->count;
	pthread_mutex_unlock(&refcnt->mutex);

	return count;
}
