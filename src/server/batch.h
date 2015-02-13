/* *****************************************************************************
 * Copyright (c) 2013-2015 Qingtao Cao
 * Copyright (c) 2014 Tyler Watson
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

#ifndef _HAVE_BATCH_H
#define _HAVE_BATCH_H

#include <libxml/tree.h>
#include "obix_request.h"

xmlNode *handlerBatch(obix_request_t *request, const xmlChar *uri, xmlNode *input);

#endif
