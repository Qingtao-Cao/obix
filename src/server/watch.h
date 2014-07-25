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
#include "obix_request.h"

/*
 * Event why relevant watches are notified of
 */
typedef enum {
	/*
	 * The monitored node has been changed, regardless of
	 * whether its val attribute is updated, has more or less
	 * children
	 *
	 * In this case any watches on the monitored node or any
	 * of its ancestors should be notified.
	 */
	WATCH_EVT_NODE_CHANGED = 1,

	/*
	 * The monitored node is deleted
	 *
	 * In this case, any watches in the deleted subtree must
	 * have relevant watch_item_t invalidated.
	 */
	WATCH_EVT_NODE_DELETED = 2
} WATCH_EVT;

xmlNode *handlerWatchServiceMake(obix_request_t *, xmlNode *);
xmlNode *handlerWatchDelete(obix_request_t *, xmlNode *);
xmlNode *handlerWatchAdd(obix_request_t *, xmlNode *);
xmlNode *handlerWatchRemove(obix_request_t *, xmlNode *);
xmlNode *handlerWatchPollChanges(obix_request_t *, xmlNode *);
xmlNode *handlerWatchPollRefresh(obix_request_t *, xmlNode *);

int obix_watch_init(xml_config_t *);
void obix_watch_dispose(void);

void xmldb_notify_watches(xmlNode *node, WATCH_EVT evt);

#endif /* _WATCH_HEADER_ */

