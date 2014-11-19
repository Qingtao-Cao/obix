/* *****************************************************************************
 * Copyright (c) 2013-2015 Qingtao Cao [harry.cao@nextdc.com]
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

#ifndef _ERRMSG_H
#define _ERRMSG_H

/**
 * Descriptor of an error message and relevant error type
 */
typedef struct err_msg {
	const char *type;
	const char *msgs;
} err_msg_t;

extern err_msg_t server_err_msg[];

typedef enum errcode {
	/* Generic error codes */
	ERR_SUCCESS = 0,
	ERR_NO_INPUT,
	ERR_NO_HREF,
	ERR_NO_NAME,
	ERR_NO_REQUESTER_ID,
	ERR_NO_SUCH_URI,
	ERR_NO_MEM,
	ERR_NO_OP_NODE,
	ERR_NO_META_NODE,
	ERR_INVALID_INPUT,
	ERR_INVALID_HREF,
	ERR_INVALID_META,
	ERR_INVALID_ARGUMENT,
	ERR_INVALID_STATE,
	ERR_INVALID_OBJ,
	ERR_TS_COMPARE,
	ERR_TS_OBSOLETE,
	ERR_READONLY_HREF,
	ERR_PERM_DENIED,
	ERR_DISK_IO,

	/* Error codes specific for the Device subsystem */
	ERR_DEVICE_CONFLICT_OWNER,
	ERR_DEVICE_EXISTS,
	ERR_DEVICE_ORPHAN,
	ERR_DEVICE_NO_SUCH_URI,
	ERR_DEVICE_CHILDREN,

	/* Error codes specific for the Watch subsystem */
	ERR_WATCH_UNSUPPORTED_HREF,
	ERR_WATCH_NO_SUCH_URI,

	/* Error codes specific for the History subsystem */
	ERR_HISTORY_DEVID,
	ERR_HISTORY_IO,
	ERR_HISTORY_DATA,
	ERR_HISTORY_EMPTY,

	/* Error codes specific for the Batch subsystem */
	ERR_BATCH_RECURSIVE,
	ERR_BATCH_HISTORY,
	ERR_BATCH_POLLCHANGES
} errcode_t;

#endif
