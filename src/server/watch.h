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

#ifndef _WATCH_HEADER_
#define _WATCH_HEADER_

#include <libxml/tree.h>
#include "response.h"

xmlNode *handlerWatchServiceMake(response_t *, const char *, xmlNode *);
xmlNode *handlerWatchDelete(response_t *, const char *, xmlNode *);
xmlNode *handlerWatchAdd(response_t *, const char *, xmlNode *);
xmlNode *handlerWatchRemove(response_t *, const char *, xmlNode *);
xmlNode *handlerWatchPollChanges(response_t *, const char *, xmlNode *);
xmlNode *handlerWatchPollRefresh(response_t *, const char *, xmlNode *);

int obix_watch_init(xml_config_t *);
void obix_watch_dispose(void);

void xmldb_notify_watches(xmlNode *node);

#endif /* _WATCH_HEADER_ */

