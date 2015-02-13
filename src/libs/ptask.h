/* *****************************************************************************
 * Copyright (c) 2013-2015 Qingtao Cao
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

#include <pthread.h>
#include <time.h>
/*
 * Prototype of the function which can be scheduled.
 */
typedef void (*periodic_task)(void *arg);

/*
 * Descriptor of a piece of work to be run by a worker thread
 */
typedef struct _Periodic_Task {
	/* The unique ID of current task */
	int id;

	/* The execution period and the next execution time */
	struct timespec period;
	struct timespec nextScheduledTime;

	/* The number of times this task should be run */
	int executeTimes;

	/* The workload and required parameter */
	periodic_task task;
	void *arg;

	/* Flag whether this task is marked as shutdown and being executed */
	int isCancelled;
	int isExecuting;

	/* Join the task queue of a worker thread */
	struct _Periodic_Task* prev;
	struct _Periodic_Task* next;
} Periodic_Task;

/*
 * Descriptor of a worker thread that executes a list of tasks
 * in threadCycle()
 */
typedef struct _Task_Thread {
	/* Seed for task ID */
	int id_gen;

	/* Thread ID */
	pthread_t thread;

	/* The queue of tasks and the mutex to protect it */
	Periodic_Task *taskList;
	pthread_mutex_t taskListMutex;

	/*
	 * The wait queue to sleep on for any changes on the task list
	 *
	 * Used by the main loop of the worker thread and notified by
	 * user whenever a new task is added or removed from the list
	 */
	pthread_cond_t taskListUpdated;

	/*
	 * The wait queue to sleep on until the current task is completed
	 *
	 * Used by the user thread who would like to delete a task which
	 * is being execution and notified by the worker thread when it's
	 * done with it
	 */
	pthread_cond_t taskExecuted;
} Task_Thread;

/*
 * Non-NULL pointers are likely to trigger segfault under normal
 * circumstances, used to verify that nobody uses non-initialized pointers
 *
 * NOTE: will become redundant once task queue is organised by
 * list_head
 */
#ifndef LIST_POISON1
#define LIST_POISON1 ((void *)0x00100100)
#endif

#ifndef LIST_POISON2
#define LIST_POISON2 ((void *)0x00200200)
#endif

#define EXECUTE_INDEFINITE		(-1)

Task_Thread *ptask_init(void);
int ptask_schedule(Task_Thread *thread, periodic_task task, void *arg,
				   long period, int executeTimes);
int ptask_reschedule(Task_Thread *thread, int taskId, long period,
					 int executeTimes, int add);
int ptask_reset(Task_Thread *thread, int taskId);
int ptask_cancel(Task_Thread *thread, int taskId, int wait);
int ptask_dispose(Task_Thread *thread, int wait);

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
