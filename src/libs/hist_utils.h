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

#ifndef _HIST_UTILS_H_
#define _HIST_UTILS_H_

/*
 * The part of history facility that is also required by the client side
 */

extern const char *HIST_OP_APPEND;
extern const char *HIST_OP_QUERY;
extern const char *HIST_REC_TS;
extern const char *HIST_AIN_DATA;
extern const char *HIST_AIN_TS_UND;

extern const char *HIST_APPEND_IN_PREFIX;
extern const char *HIST_APPEND_IN_CONTENT;
extern const char *HIST_APPEND_IN_SUFFIX;

extern const char *HIST_REC_TS_FORMAT;

/*
 * A timestamp string in HIST_REC_TS_FORMAT consists of
 * 19 bytes
 */
#define HIST_REC_TS_MAX_LEN		19

/*
 * Reserve 10 bits for a physical value
 */
#define HIST_REC_VAL_MAX_LEN	10

extern const char *HIST_FLT_PREFIX;
extern const char *HIST_FLT_LIMIT_TEMPLATE;
extern const char *HIST_FLT_START_TEMPLATE;
extern const char *HIST_FLT_END_TEMPLATE;
extern const char *HIST_FLT_FMT_TEMPLATE;
extern const char *HIST_FLT_CMPT_TEMPLATE;
extern const char *HIST_FLT_SUFFIX;

/*
 * There would be 315,360,000 records if one oBIX adapter
 * generates one record on each second over 10 years.
 */
#define HIST_FLT_VAL_MAX_BITS	9

/* The number of decimal fraction desirable */
#define FLOAT_FRACT_MAX_BITS    8

/*
 * The maximum string length of a converted 32bit float value,
 * which consists of 1 byte for sign, 38 bytes for integral part,
 * 1 byte for dot, and extra bytes for fractional part.
 */
#define FLOAT_MAX_BITS    (1 + 38 + 1 + FLOAT_FRACT_MAX_BITS)

#define HIST_BOOL_TRUE		"true"
#define HIST_BOOL_FALSE		"false"
#define HIST_BOOL_MAX_LEN	5

#define HIST_TS_INIT			"1970-01-01T0:0:0"

#endif
