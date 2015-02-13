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

#include <string.h>
#include "log_utils.h"
#include "security.h"

/*
 * Under-construction
 *
 * Implemented the Security Engine (method) to consult the user-defined
 * Access Control Rules Database (policy) whether the Subject is permitted
 * to perform the required operations on the Object
 *
 * The Access Control Rules Database is neutral to the oBIX server and thus
 * should be built into binary separately, while the oBIX server should load
 * and interpret Rules Database binary at start-up and setup relevant data
 * structures that will further be consulted in various operations
 *
 * If needed, Subject and Object ID strings can be converted into integers
 * so as to be manipulated more easily
 */

/*
 * Pre-defined, initial ID for various infrastructure
 * on the oBIX server
 */
const char *OBIX_ID_DEVICE = "SERVER:DEVICE";
const char *OBIX_ID_WATCH = "SERVER:WATCH";
const char *OBIX_ID_HISTORY = "SERVER:HISTORY";

/*
 * A placeholder for real implementation, so far it is implemented as a
 * black-list. In the future it should consult the loaded policy binary
 * whether the subject is permitted to operate on the given object
 *
 * Return 1 if the Subject is permitted to perform the required operation
 * on the Object, 0 if denied
 */
int se_lookup(const char *subject, const char *object, const int ops)
{
	if (!subject || !object) {
		return 0;
	}

	/* oBIX server is allowed to commit all kinds of removal */
	if (strcmp(subject, OBIX_ID_DEVICE) == 0 ||
		strcmp(subject, OBIX_ID_WATCH) == 0 ||
		strcmp(subject, OBIX_ID_HISTORY) == 0) {
		return 1;
	}

	/*
	 * oBIX clients are allowed to signOff devices from the Device Root,
	 * otherwise they must be the owner of the parent device
	 */
	if ((ops & OP_DEVICE_REMOVE) > 0) {
		if (strcmp(object, OBIX_ID_DEVICE) == 0) {
			return 1;
		} else {
			return (strcmp(subject, object) == 0) ? 1 : 0;
		}
	}

	/* Only the owner can delete a XML object */
	if ((ops & OP_DEVICE_DELETE) > 0 ||	(ops & OP_WATCH_DELETE) > 0) {
		return (strcmp(subject, object) == 0) ? 1 : 0;
	}

	return 1;
}
