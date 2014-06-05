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

#ifndef _HISTORY_H_
#define _HISTORY_H_

#include <libxml/tree.h>
#include "response.h"

int obix_hist_init(const char *);
void obix_hist_dispose(void);

xmlNode *handlerHistoryGet(response_t *, const char *, xmlNode *);
xmlNode *handlerHistoryAppend(response_t *, const char *, xmlNode *);
xmlNode *handlerHistoryQuery(response_t *, const char *, xmlNode *);

int is_history_requests(const char *uri);

#endif	/* _HISTORY_H_ */
