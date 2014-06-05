/* *****************************************************************************
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
/** @file
 * Periodic Task - tool for asynchronous task execution.
 *
 * Periodic Task utility can be used to schedule some function(s) to
 * be invoked periodically in a separate thread. A function can scheduled to be
 * invoked either defined number of times or indefinite (until it is canceled).
 *
 * @section ptask-usage Usage
 *
 * The following piece of code will schedule a function @a foo() to be
 * executed every second in a separate thread:
 *
 * @code
 * // prints greetings
 * int foo(void* args)
 * {
 *     printf(Greetings from %s!, (char*) args);
 * }
 *
 * ...
 *
 * // initialize a new thread
 * Task_Thread* thread = ptask_init();
 * if (thread == NULL)
 * {
 *     // initialization failed
 *     return -1;
 * }
 *
 * long period = 1000; 	// interval between executions in milliseconds
 * int timesToExecute = EXECUTE_INDEFINITE;	// how many times to execute
 * char* arg = (char*) malloc(7); // It will be passed to the foo() as argument
 * strcpy(arg, "Andrey");
 *
 * // schedule foo() method to be executed indefinitely once is a second
 * int taskId = ptask_schedule(thread, &foo, arg, period, timesToExecute);
 * @endcode
 *
 * @note @a foo() function should match #periodic_task prototype.
 * @note The variable which is passed to the #ptask_schedule() function as
 *       argument for @a foo() shouldn't be locally defined. Otherwise it can
 *       appear that it doesn't exist when the @a foo() is executed.
 *
 * The returned task id can be then used to change execution period, or cancel
 * the task.
 *
 * One task thread can be used to schedule several tasks, but scheduled
 * functions must be quick enough in order not to block other tasks to be
 * executed in time.
 *
 * At the end of application all initialized task thread should be freed using
 * #ptask_dispose().
 *
 * @author Andrey Litvinov
 */

#ifndef PTASK_H_
#define PTASK_H_

#include <stdbool.h>
#include "bool.h"

/*
 * Non-NULL pointers are likely to trigger segfault
 * under normal circumstances, used to verify that
 * nobody uses non-initialized pointers
 */
#define LIST_POISON1 ((void *) 0x00100100)
#define LIST_POISON2 ((void *) 0x00200200)


/**
 * Specifies that the task should be executed indefinite number of times
 * (until #ptask_cancel() is called).
 */
#define EXECUTE_INDEFINITE -1

/**
 * Prototype of the function which can be scheduled.
 *
 * @param arg Argument which is passed to the function when it is invoked.
 */
typedef void (*periodic_task)(void* arg);

/**
 * Represents a separate thread which can be used to schedule tasks.
 */
typedef struct _Task_Thread Task_Thread;

/**
 * Creates new instance of #Task_Thread.
 *
 * @return Pointer to the new instance of #Task_Thread, or @a NULL if some
 *         error occurred.
 */
Task_Thread* ptask_init();

/**
 * Releases resources allocated for the provided #Task_Thread instance.
 * All scheduled tasks are canceled.
 *
 * @param thread Pointer to the #Task_Thread to be freed.
 * @param wait   If #true than the method will block and wait until specified
 *               thread is really disposed. Otherwise, method will only schedule
 *               asynchronous disposing of the thread.
 * @return @a 0 on success, negative error code otherwise.
 */
int ptask_dispose(Task_Thread* thread, bool wait);

/**
 * Schedules new task for execution.
 *
 * @param thread Thread in which the task will be executed.
 * @param task Task which should be scheduled.
 * @param arg Argument which will be passed to the task function each time when
 *            it is invoked.
 * @param period Time interval in milliseconds, which defines how often the
 *               task will be executed.
 * @param executeTimes Defines how many times (min. 1) the task should be
 *                     executed. If #EXECUTE_INDEFINITE is provided than the
 *                     task is executed until #ptask_cancel() with
 *                     corresponding task ID is called.
 * @return @li >0 - ID of the scheduled task.
 *         @li <0 - Error code.
 */
int ptask_schedule(Task_Thread* thread,
                   periodic_task task,
                   void* arg,
                   long period,
                   int executeTimes);

/**
 * Sets new execution period for the specified task.
 *
 * @param thread Thread in which the task is scheduled.
 * @param taskId Id of the scheduled task.
 * @param period New time interval in milliseconds (or time which will be added
 *               to the current task period).
 * @param executeTimes Defines how many times (min. 1) the task should be
 *                     executed. If #EXECUTE_INDEFINITE is provided than the
 *                     task is executed until #ptask_cancel() is called with
 *                     corresponding task ID.
 * @param add Defines whether time provided in @a period argument will be used
 *            as new execution period, or will be added to the current one.
 * @note When @a add is set to #true, @a period will be also added to the next
 *       execution time, but when @a add is #false the next execution will be
 *       (current time + @a period).
 * @return @a 0 on success, negative error code otherwise.
 */
int ptask_reschedule(Task_Thread* thread,
                     int taskId,
                     long period,
                     int executeTimes,
                     bool add);

/**
 * Checks whether the task with provided id is scheduled for execution in the
 * thread.
 *
 * @param thread Thread where the task should be searched for.
 * @param taskId Task id which is searched for.
 * @return #true if the task with specified @a taskId is scheduled,
 *         #false otherwise.
 */
bool ptask_isScheduled(Task_Thread* thread, int taskId);

/**
 * Resets time until the next execution of the specified task.
 * The next execution time will be current time + @a period provided when the
 * task was scheduled. If the @a period needs to be changed than use
 * #ptask_reschedule() instead.
 *
 * @param thread Thread where the task is scheduled.
 * @param taskId Id of the task whose execution time should be reset.
 * @return @a 0 on success, negative error code otherwise.
 */
int ptask_reset(Task_Thread* thread, int taskId);

/**
 * Removes task from the scheduled list.
 *
 * @param thread Thread in which task is scheduled.
 * @param taskId ID of the task to be removed.
 * @param wait When task is being executed it can be canceled only after
 *             execution is completed. This parameter defines whether the
 *             function should wait until the task is really canceled, or it can
 *             just mark the task as canceled, which guarantees that the task
 *             will be removed as soon as the current execution is completed.
 *             In case when this function is called while the task is not
 *             executed @a wait argument makes no difference.
 * @return @li @a 0 on success;
 *         @li @a -1 if task with provided ID is not found.
 */
int ptask_cancel(Task_Thread* thread, int taskId, bool wait);

#endif /* PTASK_H_ */
