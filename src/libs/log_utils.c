/* *****************************************************************************
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

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include "log_utils.h"

/** @name Logging to @a stdout
 * @{ */
static void log_debugPrintf(char* fmt, ...);
static void log_warningPrintf(char* fmt, ...);
static void log_errorPrintf(char* fmt, ...);
/** @}
 * @name Logging using @a syslog
 * @{ */
static void log_debugSyslog(char* fmt, ...);
static void log_warningSyslog(char* fmt, ...);
static void log_errorSyslog(char* fmt, ...);
/** @} */

/** @name Log handlers.
 * Used to invoke quickly current logging function.
 * @{ */
log_function log_debugHandler = &log_debugPrintf;
log_function log_warningHandler = &log_warningPrintf;
log_function log_errorHandler = &log_errorPrintf;
/** @} */

/** Defines global log level. */
static int _log_level = LOG_LEVEL_DEBUG;

/** Logging mode. */
static int _use_syslog = 0;

/** Logs debug message using printf. */
static void log_debugPrintf(char* fmt, ...)
{
    printf("DEBUG ");
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

/** Logs warning message using printf. */
static void log_warningPrintf(char* fmt, ...)
{
    printf("WARNING ");
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

/** Logs error message using printf. */
static void log_errorPrintf(char* fmt, ...)
{
    printf("ERROR ");
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

/** Logs debug message using syslog. */
static void log_debugSyslog(char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsyslog(LOG_DEBUG, fmt, args);
    va_end(args);
}

/** Logs warning message using syslog. */
static void log_warningSyslog(char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsyslog(LOG_WARNING, fmt, args);
    va_end(args);
}

/** Logs error message using syslog. */
static void log_errorSyslog(char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsyslog(LOG_ERR, fmt, args);
    va_end(args);
}

/** Does not log anything.
 * Used to ignore messages of some priorities according to the global log level.
 */
static void log_nothing(char* fmt, ...)
{
    // dismiss the log message
}

/** Configures logging system in printf mode according to global log level. */
static void setPrintf()
{
    // drop all log functions
    log_debugHandler = &log_nothing;
    log_warningHandler = &log_nothing;
    log_errorHandler = &log_nothing;
    // set corresponding log functions
    switch(_log_level)
    {
    case LOG_LEVEL_DEBUG:
        log_debugHandler = &log_debugPrintf;
    case LOG_LEVEL_WARNING:
        log_warningHandler = &log_warningPrintf;
    case LOG_LEVEL_ERROR:
        log_errorHandler = &log_errorPrintf;
    default:
		break;
    }
}

/** Configures logging system in syslog mode according to global log level. */
static void setSyslog()
{
    // drop all log functions
    log_debugHandler = &log_nothing;
    log_warningHandler = &log_nothing;
    log_errorHandler = &log_nothing;
    // set corresponding log functions
    switch(_log_level)
    {
    case LOG_LEVEL_DEBUG:
        log_debugHandler = &log_debugSyslog;
    case LOG_LEVEL_WARNING:
        log_warningHandler = &log_warningSyslog;
    case LOG_LEVEL_ERROR:
        log_errorHandler = &log_errorSyslog;
    default:
		break;
    }
}

void log_usePrintf()
{
	_use_syslog = 0;

	/* Close syslog if opened */
	closelog();

	setPrintf();
}

void log_useSyslog(int facility)
{
	_use_syslog = 1;

	openlog(NULL, LOG_NDELAY, facility);

	setSyslog();
}

void log_setLevel(LOG_LEVEL level)
{
	_log_level = level;

	if (_use_syslog == 1) {
		setSyslog();
	} else {
		setPrintf();
	}
}
