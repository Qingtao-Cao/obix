/******************************************************************************
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
 * along with oBIX. If not, see <http://www.gnu.org/licenses/>.
 *
 ******************************************************************************/

#ifndef _OBIX_FCGI_H
#define _OBIX_FCGI_H

#include "obix_request.h"

typedef enum fcgi_env {
	FCGI_ENV_REQUEST_URI = 0,
	FCGI_ENV_REQUEST_METHOD,
	FCGI_ENV_REMOTE_PORT,
	FCGI_ENV_REMOTE_ADDR,
	FCGI_ENV_REQUESTER_ID
} fcgi_env_t;

char *obix_fcgi_get_requester_id(obix_request_t *request);

#endif
