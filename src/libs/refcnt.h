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

#ifndef _REFCNT_H
#define _REFCNT_H

#include <pthread.h>

typedef struct refcnt {
	int count;
	pthread_mutex_t mutex;
	pthread_cond_t wq;
} refcnt_t;

void refcnt_init(refcnt_t *refcnt);
void refcnt_cleanup(refcnt_t *refcnt);
void refcnt_get(refcnt_t *refcnt);
void refcnt_put(refcnt_t *refcnt);
void refcnt_sync(refcnt_t *refcnt);
int refcnt_read(refcnt_t *refcnt);

#endif
