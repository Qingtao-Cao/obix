/* *****************************************************************************
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
 * along with oBIX.  If not, see <http://www.gnu.org/licenses/>.
 *
 * *****************************************************************************/

#ifndef _HISTORY_H_
#define _HISTORY_H_

#include <libxml/tree.h>
#include "obix_request.h"

int obix_hist_init(const char *);
void obix_hist_dispose(void);

xmlNode *handlerHistoryGet(obix_request_t *, xmlNode *);
xmlNode *handlerHistoryAppend(obix_request_t *, xmlNode *);
xmlNode *handlerHistoryQuery(obix_request_t *, xmlNode *);

int is_history_requests(const char *uri);

#endif	/* _HISTORY_H_ */
