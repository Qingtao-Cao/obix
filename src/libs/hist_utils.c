/* *****************************************************************************
 * Copyright (c) 2013-2014 Qingtao Cao [harry.cao@nextdc.com]
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

const char *HIST_REC_TS = "timestamp";
const char *HIST_AIN_DATA = "data";
const char *HIST_AIN_TS_UND = "UNSPECIFIED";

const char *HIST_OP_APPEND = "append";
const char *HIST_OP_QUERY = "query";

const char *HIST_APPEND_IN_PREFIX =
"<obj href=\"obix:HistoryAppendIn\">\r\n"
"<list name=\"data\" of=\"obix:HistoryRecord\">\r\n"
"<obj is=\"obix:HistoryRecord\">\r\n"
"<abstime name=\"timestamp\" val=\"%s\"/>\r\n";

const char *HIST_APPEND_IN_SUFFIX = "</obj>\r\n</list>\r\n</obj>";

const char *HIST_APPEND_IN_CONTENT = "<real name=\"%s\" val=\"%f\"/>\r\n";

const char *HIST_FLT_PREFIX = "<obj href=\"obix:HistoryFilter\">\r\n";

const char *HIST_FLT_LIMIT_TEMPLATE =
"<int name=\"limit\" val=\"%d\" />\r\n";

const char *HIST_FLT_START_TEMPLATE =
"<abstime name=\"start\" val=\"%s\" />\r\n";

const char *HIST_FLT_END_TEMPLATE =
"<abstime name=\"end\" val=\"%s\" />\r\n";

const char *HIST_FLT_FMT_TEMPLATE =
"<str name=\"format\" val=\"%s\" />\r\n";

const char *HIST_FLT_CMPT_TEMPLATE =
"<bool name=\"compact\" val=\"%s\" />\r\n";

const char *HIST_FLT_SUFFIX = "\r\n</obj>";

/*
 * Timestamps are in "yyyy-mm-ddThh:mm:ss" format which has
 * 19 bytes without the NULL terminator.
 */
const char *HIST_REC_TS_FORMAT = "%4d-%.2d-%.2dT%.2d:%.2d:%.2d";
