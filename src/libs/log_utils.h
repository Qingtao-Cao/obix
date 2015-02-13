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
 * along with oBIX. If not, see <http://www.gnu.org/licenses/>.
 *
 * *****************************************************************************/

#ifndef LOG_UTILS_H_
#define LOG_UTILS_H_

/**
 * This is a prototype of log handler function.
 * @param fmt Message format (used in the same way as with @a printf()).
 */
typedef void (*log_function)(char* fmt, ...);

/**@name Log handlers
 * Contain links to the current log handlers. Normally these links should not
 * be used directly.
 * @{
 */
/**
 * Contains link to the current handler of @a debug log messages. Normally
 * #log_debug() should be used instead.
 */
extern log_function log_debugHandler;
/**
 * Contains link to the current handler of @a warning log messages. Normally
 * #log_warning() should be used instead.
 */
extern log_function log_warningHandler;
/**
 * Contains link to the current handler of @a error log messages. Normally
 * #log_error() should be used instead.
 */
extern log_function log_errorHandler;
/** @} */

/**@name Logging utilities
 * @{*/
// A trick with define is done in order to auto-add filename and line number
// into the log message.

/**
 * Prints @a debug message to the configured output.
 * Automatically adds filename and string number of the place from where the log
 * was written.
 *
 * @param fmt Message format (used in the same way as with @a printf()).
 */
#define log_debug(fmt, ...) (*log_debugHandler)("%s(%d): " fmt, __FILE__, \
 __LINE__, ## __VA_ARGS__)

/**
 * Prints @a warning message to the configured output.
 * Automatically adds filename and string number of the place from where the log
 * was written.
 *
 * @param fmt Message format (used in the same way as with @a printf()).
 */
#define log_warning(fmt, ...) (*log_warningHandler)("%s(%d): " fmt, __FILE__, \
 __LINE__, ## __VA_ARGS__)

/**
 * Prints @a error message to the configured output.
 * Automatically adds filename and string number of the place from where the log
 * was written.
 *
 * @param fmt Message format (used in the same way as with @a printf()).
 */
#define log_error(fmt, ...) (*log_errorHandler)("%s(%d): " fmt, __FILE__, \
 __LINE__, ## __VA_ARGS__)
/**@}*/

/**
 * Defines possible log levels.
 */
typedef enum
{
	/** Debug log level. */
	LOG_LEVEL_DEBUG,
	/** Warning log level. */
	LOG_LEVEL_WARNING,
	/** Error log level. */
	LOG_LEVEL_ERROR,
	/** 'No' log level. */
	LOG_LEVEL_NO
} LOG_LEVEL;

/**
 * Switches library to use @a syslog for handling messages.
 *
 * @param facility Facility tells syslog who issued the message. See
 * documentation of @a syslog for more information.
 */
void log_useSyslog(int facility);

/**
 * Switches library to use @a printf for handling messages.
 */
void log_usePrintf();

/**
 * Sets the minimum priority level of the messages which will be processed.
 *
 * @param level Priority level:
 *              - #LOG_LEVEL_DEBUG - All messages will be printed;
 *              - #LOG_LEVEL_WARNING - Only warning and error messages will be
 *                                     printed;
 *              - #LOG_LEVEL_ERROR - Only error messages are printed;
 *              - #LOG_LEVEL_NO - Nothing is printed at all.
 */
void log_setLevel(LOG_LEVEL level);

#endif /* LOG_UTILS_H_ */
