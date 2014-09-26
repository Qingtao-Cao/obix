/* *****************************************************************************
 * Copyright (c) 2013-2014 Qingtao Cao [harry.cao@nextdc.com]
 * Copyright (c) 2009 Andrey Litvinov
 *
 * This file is part of oBIX
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

#ifndef PTASK_H_
#define PTASK_H_

/*
 * Non-NULL pointers are likely to trigger segfault
 * under normal circumstances, used to verify that
 * nobody uses non-initialized pointers
 */
#ifndef LIST_POISON1
#define LIST_POISON1 ((void *) 0x00100100)
#endif

#ifndef LIST_POISON2
#define LIST_POISON2 ((void *) 0x00200200)
#endif

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
 */
int ptask_dispose(Task_Thread* thread, int wait);

/**
 * Schedules new task for execution.
 */
int ptask_schedule(Task_Thread* thread,
                   periodic_task task,
                   void* arg,
                   long period,
                   int executeTimes);

/**
 * Sets new execution period for the specified task.
 */
int ptask_reschedule(Task_Thread* thread,
                     int taskId,
                     long period,
                     int executeTimes,
                     int add);

/**
 * Checks whether the task with provided id is scheduled for execution in the
 * thread.
 */
int ptask_isScheduled(Task_Thread* thread, int taskId);

/**
 * Resets time until the next execution of the specified task.
 * The next execution time will be current time + @a period provided when the
 * task was scheduled. If the @a period needs to be changed than use
 * #ptask_reschedule() instead.
 */
int ptask_reset(Task_Thread* thread, int taskId);

/**
 * Removes task from the scheduled list.
 */
int ptask_cancel(Task_Thread* thread, int taskId, int wait);

/*
 * Descriptor of a particular worker thread and its payload
 */
typedef struct obix_task {
	/* Indicator whether initialised already */
	int initialised;

	/* the worker thread's descriptor */
	Task_Thread *t;

	/* task ID */
	int id;

	/* payload of this task */
	periodic_task func;

	/* pointing back to the owner structure */
	void *args;

	/* in millisecond */
	long period;

	/* maximum execution times */
	int times;
} obix_task_t;

int obix_schedule_task(obix_task_t *task);
void obix_cancel_task(obix_task_t *task);
void obix_destroy_task(obix_task_t *task);
int obix_setup_task(obix_task_t *task, Task_Thread *t, periodic_task func,
					void *args, long period, int times);
int obix_reset_task(obix_task_t *task);

#endif /* PTASK_H_ */
