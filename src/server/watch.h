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

xmlNode *handlerWatchServiceMake(obix_request_t *request, const xmlChar *href, xmlNode *input);
xmlNode *handlerWatchDelete(obix_request_t *request, const xmlChar *href, xmlNode *input);
xmlNode *handlerWatchAdd(obix_request_t *request, const xmlChar *href, xmlNode *input);
xmlNode *handlerWatchRemove(obix_request_t *request, const xmlChar *href, xmlNode *input);
xmlNode *handlerWatchPollChanges(obix_request_t *request, const xmlChar *href, xmlNode *input);
xmlNode *handlerWatchPollRefresh(obix_request_t *request, const xmlChar *href, xmlNode *input);

int obix_watch_init(const int);
void obix_watch_dispose(void);

void watch_notify_watches(long id, xmlNode *monitored, WATCH_EVT event);

int watch_update_uri(const xmlChar *href, const xmlChar *new);
xmlNode *watch_copy_uri(const xmlChar *href, xml_copy_flags_t flags);
int watch_get_op_id(const xmlChar *href, long *id);

int is_watch_service_make_href(const xmlChar *href);

#endif /* _WATCH_HEADER_ */

