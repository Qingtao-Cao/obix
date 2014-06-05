/* *****************************************************************************
 * Copyright (c) 2014 Tyler Watson <tyler.watson@nextdc.com>
 * Copyright (c) 2013-2014 Qingtao Cao [harry.cao@nextdc.com]
 * Copyright (c) 2009 Andrey Litvinov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 * *****************************************************************************/

#ifndef POST_HANDLER_H_
#define POST_HANDLER_H_

#include <libxml/tree.h>
#include "response.h"

/**
 * Prototype of a POST Handler function.
 *
 * @param response Response object, which should be used to send operation
 *					results.
 * @param URI Requested URI.
 * @param input Parsed request input.
 */
typedef xmlNode *
(*obix_server_postHandler)(response_t *, const char *, xmlNode *);

/**
 * Returns handler with specified id.
 * Never returns @a NULL. If there is no handler with specified id, then it
 * returns a handler, which sends error message to the user.
 */
obix_server_postHandler obix_server_getPostHandler(int id);

#endif /* POST_HANDLER_H_ */
