/* *****************************************************************************
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
