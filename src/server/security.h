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

#ifndef _SECURITY_H
#define _SECURITY_H

/*
 * TODO: XML objects on the oBIX server can be categorised into three classes:
 * "devce", "watch" and "history"
 *
 * Each class supports a number of permissions corresponding to the operations
 * supported
 *
 * The user-level Access Control Rules Database should contain the following
 * information:
 *  . a list of initial ID for system resources
 *	. a full list of ID of possible oBIX clients (that are used by the
 *	  Authentication Server for different oBIX clients)
 *	. access control of all client IDs on a particular class of object
 */

extern const char *OBIX_ID_DEVICE;
extern const char *OBIX_ID_WATCH;
extern const char *OBIX_ID_HISTORY;

#define OP_DEVICE_ADD			0x00000001	/* Add a new node into a device contract */
#define	OP_DEVICE_REMOVE		0x00000002	/* Remove a node from a device contract */
#define	OP_DEVICE_DELETE		0x00000004	/* Delete a device contract */

#define	OP_WATCH_CREATE			0x00000100	/* Create a new watch */
#define	OP_WATCH_DELETE			0x00000200	/* Delete a watch */
#define	OP_WATCH_ADD			0x00000400	/* Have a watch monitor a new uri */
#define	OP_WATCH_REMOVE			0x00000800	/* Stop a watch from monitoring a uri */
#define OP_WATCH_POLLCHANGE		0x00001000	/* Poll a watch for changes */
#define OP_WATCH_POLLREFRESH	0x00002000	/* Reset a watch */

#define OP_HISTORY_CREATE		0x00010000	/* Create a new history facility */
#define OP_HISTORY_QUERY		0x00020000	/* Query a history facility */
#define OP_HISTORY_APPEND		0x00040000	/* Append to a history facility */

int se_lookup(const char *subject, const char *object, const int ops);

#endif
