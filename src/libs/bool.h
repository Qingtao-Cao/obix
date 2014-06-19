/* *****************************************************************************
 * Copyright (c) 2013-2014 Qingtao Cao [harry.cao@nextdc.com]
 * Copyright (c) 2009 Andrey Litvinov
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

#ifndef BOOL_H_
#define BOOL_H_

#include <stdbool.h>

/** Boolean data type which is so natural for all programmers. */
typedef int BOOL;

#ifndef true
#define true 1
#endif

#ifndef TRUE
/** That's @a true. */
#define TRUE 1
#endif

#ifndef FALSE
/** This is @a false. */
#define FALSE 0
#endif

#endif /* BOOL_H_ */
