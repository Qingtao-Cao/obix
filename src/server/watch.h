/* *****************************************************************************
 * Copyright (c) 2013-2014 Qingtao Cao [harry.cao@nextdc.com]
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

