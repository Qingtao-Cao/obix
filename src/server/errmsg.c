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

#include "errmsg.h"
#include "obix_utils.h"

err_msg_t server_err_msg[] = {
	/* Generic error codes */
	[ERR_NO_INPUT] = {
		.type = OBIX_CONTRACT_ERR_UNSUPPORTED,
		.msgs = "Missing input device contract"
	},
	[ERR_NO_HREF] = {
		.type = OBIX_CONTRACT_ERR_UNSUPPORTED,
		.msgs = "Provided input has no href attribute"
	},
	[ERR_NO_NAME] = {
		.type = OBIX_CONTRACT_ERR_UNSUPPORTED,
		.msgs = "Provided input has no name attribute"
	},
	[ERR_NO_REQUESTER_ID] = {
		.type = OBIX_CONTRACT_ERR_UNSUPPORTED,
		.msgs = "Relevant request has no REQUESTER_ID environment"
	},
	[ERR_NO_SUCH_URI] = {
		.type = OBIX_CONTRACT_ERR_BAD_URI,
		.msgs = "Requested URI could not be found on the server"
	},
	[ERR_NO_MEM] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "Insufficient memory on the server"
	},
	[ERR_NO_OP_NODE] = {
		.type = OBIX_CONTRACT_ERR_UNSUPPORTED,
		.msgs = "Requested URI is not an operation node"
	},
	[ERR_NO_META_NODE] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "Failed to retrieve meta node from relevant node"
	},
	[ERR_INVALID_INPUT] = {
		.type = OBIX_CONTRACT_ERR_UNSUPPORTED,
		.msgs = "Provided input contract is malformed"
	},
	[ERR_INVALID_HREF] = {
		.type = OBIX_CONTRACT_ERR_BAD_URI,
		.msgs = "Provided href is invalid"
	},
	[ERR_INVALID_META] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "Failed to retrieve required attribute from the meta node"
	},
	[ERR_INVALID_ARGUMENT] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "Unknown server error: invalid argument"
	},
	[ERR_INVALID_STATE] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "The requested device/facility is being shutting down, abort write attempt"
	},
	[ERR_INVALID_OBJ] = {
		.type = OBIX_CONTRACT_ERR_UNSUPPORTED,
		.msgs = "Provided input contains invalid object model"
	},
	[ERR_TS_COMPARE] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "Failed to compare timestamps. Malformed? "
				"use ts2utc testcase to check timestamp sanity"
	},
	[ERR_TS_OBSOLETE] = {
		.type = OBIX_CONTRACT_ERR_UNSUPPORTED,
		.msgs = "Data list contains records with timestamp older than or "
				"equal to that of the last record"
	},
	[ERR_READONLY_HREF] = {
		.type = OBIX_CONTRACT_ERR_BAD_URI,
		.msgs = "Provided href is read-only"
	},
	[ERR_PERM_DENIED] = {
		.type = OBIX_CONTRACT_ERR_UNSUPPORTED,
		.msgs = "The requested operation is not permitted"
	},
	[ERR_DISK_IO] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "Disk I/O failed"
	},

	/* Error codes specific for the Device subsystem */
	[ERR_DEVICE_CONFLICT_OWNER] = {
		.type = OBIX_CONTRACT_ERR_PERMISSION,
		.msgs = "Another client already registered a device at the same href"
	},
	[ERR_DEVICE_ORPHAN] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "Unknown server error: orphan device"
	},
	[ERR_DEVICE_NO_SUCH_URI] = {
		.type = OBIX_CONTRACT_ERR_BAD_URI,
		.msgs = "Provided href doesn't point to a valid device"
	},
	[ERR_DEVICE_CHILDREN] = {
		.type = OBIX_CONTRACT_ERR_UNSUPPORTED,
		.msgs = "Device contains children"
	},

	/* Error codes specific for the Watch subsystem */
	[ERR_WATCH_UNSUPPORTED_HREF] = {
		.type = OBIX_CONTRACT_ERR_UNSUPPORTED,
		.msgs = "Provided href can't be watched upon"
	},
	[ERR_WATCH_NO_SUCH_URI] = {
		.type = OBIX_CONTRACT_ERR_BAD_URI,
		.msgs = "Provided href is not monitored by relevant watch object"
	},

	/* Error codes specific for the History subsystem */
	[ERR_HISTORY_DEVID] = {
		.type = OBIX_CONTRACT_ERR_UNSUPPORTED,
		.msgs = "Failed to get history device ID from request"
	},
	[ERR_HISTORY_IO] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "I/O error while performing history request"
	},
	[ERR_HISTORY_DATA] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "Data in relevant history facility is corrupted"
	},
	[ERR_HISTORY_EMPTY] = {
		.type = OBIX_CONTRACT_ERR_UNSUPPORTED,
		.msgs = "No data in relevant history facility at all"
	},

	/* Error codes specific for the Batch subsystem */
	[ERR_BATCH_RECURSIVE] = {
		.type = OBIX_CONTRACT_ERR_UNSUPPORTED,
		.msgs = "Recursive batch commands not supported"
	},
	[ERR_BATCH_HISTORY] = {
		.type = OBIX_CONTRACT_ERR_UNSUPPORTED,
		.msgs = "No history related requests via batch supported, "
				"please request them through normal POST method directly"
	},
	[ERR_BATCH_POLLCHANGES] = {
		.type = OBIX_CONTRACT_ERR_UNSUPPORTED,
		.msgs = "No watch.pollChanges requests via batch supported, "
				"please request them through normal POST method directly"
	}
};
