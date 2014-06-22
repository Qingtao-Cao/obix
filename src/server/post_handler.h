/* *****************************************************************************
 * Copyright (c) 2014 Tyler Watson <tyler.watson@nextdc.com>
 * Copyright (c) 2013-2014 Qingtao Cao [harry.cao@nextdc.com]
 * Copyright (c) 2009 Andrey Litvinov
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

#ifndef POST_HANDLER_H_
#define POST_HANDLER_H_

#include <libxml/tree.h>
#include "obix_request.h"

/**
 * Prototype of a POST Handler function.
 *
 * @param response Response object, which should be used to send operation
 *					results.
 * @param input Parsed request input.
 */
typedef xmlNode *
(*obix_server_postHandler)(obix_request_t *, xmlNode *);

/**
 * Returns handler with specified id.
 * Never returns @a NULL. If there is no handler with specified id, then it
 * returns a handler, which sends error message to the user.
 */
obix_server_postHandler obix_server_getPostHandler(int id);

#endif /* POST_HANDLER_H_ */
