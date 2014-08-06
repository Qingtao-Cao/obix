/* *****************************************************************************
 * Copyright (c) 2014 NEXTDC Paul Gampe [paul.gampe@nextdc.com]
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

#ifndef _MY_LIST_H
#define _MY_LIST_H

#define fd_set LIBC_fd_set
#define dev_t LIBC_dev_t
#define nlink_t LIBC_nlink_t
#define timer_t LIBC_timer_t
#define loff_t LIBC_loff_t
#define u_int64_t LIBC_u_int64_t
#define int64_t LIBC_int64_t
#define blkcnt_t LIBC_blkcnt_t
/**
 *  * container_of - cast a member of a structure out to the containing structure
 *  * @ptr:        the pointer to the member.
 *  * @type:       the type of the container struct this is embedded in.
 *  * @member:     the name of the member within the struct.
 *  *
 *  */
#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (type *)( (char *)__mptr - offsetof(type,member) );})

#undef bool
#undef false
#undef true
#include <linux/list.h>
#undef blkcnt_t
#undef int64_t
#undef u_int64_t
#undef loff_t
#undef timer_t
#undef nlink_t
#undef dev_t
#undef fd_set

#endif
