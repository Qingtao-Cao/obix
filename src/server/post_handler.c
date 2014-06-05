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

#include "post_handler.h"
#include "server.h"
#include "batch.h"
#include "watch.h"
#include "history.h"

/** Array of all available post handlers. */
static const obix_server_postHandler POST_HANDLER[] = {
	&handlerError,				//0 default handler which returns error
	&handlerWatchServiceMake,	//1 watchService.make
	&handlerWatchAdd,			//2 Watch.add
	&handlerWatchRemove,		//3 Watch.remove
	&handlerWatchPollChanges,	//4 Watch.pollChanges
	&handlerWatchPollRefresh,	//5 Watch.pollRefresh
	&handlerWatchDelete,		//6 Watch.delete
	&handlerSignUp,				//7 signUp
	&handlerBatch,				//8 Batch
	&handlerHistoryGet,			//9 History.Get
	&handlerHistoryQuery,		//10 History.Query
	&handlerHistoryAppend		//11 History.Append
};

/** Amount of available post handlers. */
static const int POST_HANDLERS_COUNT = 12;

obix_server_postHandler obix_server_getPostHandler(int id)
{
	obix_server_postHandler handler;

	if ((id < 0) || (id >= POST_HANDLERS_COUNT)) {
		handler = POST_HANDLER[0];
	} else {
		handler = POST_HANDLER[id];
	}

	return handler;
}
