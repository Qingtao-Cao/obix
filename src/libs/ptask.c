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

#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include "log_utils.h"
#include "ptask.h"

/** Represents on periodic task and its properties. */
typedef struct _Periodic_Task
{
    int id;
    /** Time when the task should be executed. */
    struct timespec nextScheduledTime;
    /** Execution period. */
    struct timespec period;
    /** How many times the task should be executed. */
    int executeTimes;
    /** Task itself. */
    periodic_task task;
    /** Argument, which should be passed to the task during execution. */
    void* arg;
    /** @name State flags
     * @{ */
	int isCancelled;
	int isExecuting;
    /** @} */
    /** @name Links to neighbor elements in task list.
     * @see _Task_Thread::taskList
    * @{ */
    struct _Periodic_Task* prev;
    struct _Periodic_Task* next;
    /** @} */
}
Periodic_Task;

/** Defines internal attributes of #Task_Thread. */
struct _Task_Thread
{
    /** Used for unique ID generation. */
    int id_gen;
    /** List of scheduled tasks. */
    Periodic_Task* taskList;
    /** Thread handle. */
    pthread_t thread;
    /** Synchronization mutex. */
    pthread_mutex_t taskListMutex;
    /** Condition, which happens when task list is changed. */
    pthread_cond_t taskListUpdated;
    /** Condition, which happens when task has been executed. */
    pthread_cond_t taskExecuted;
};

static void periodicTask_removeFromList(Task_Thread *, Periodic_Task *);

/**@name Utility methods for work with @a timespec structure
 * @{*/
/** Converts milliseconds (represented as @a long) into @a timespec structure.*/
static void timespec_fromMillis(struct timespec* time, long millis)
{
    time->tv_sec = millis / 1000;
    time->tv_nsec = (millis % 1000) * 1000000;
}

/**
 * Adds time from the second argument to the first one.
 * @return  @li @a 1 if the resulting time in @a time1 is greater or equal @a 0;
 *  		@li @a -1 if the result is less than @a 0.
 */
static int timespec_add(struct timespec* time1, const struct timespec* time2)
{
    long newNano = time1->tv_nsec + time2->tv_nsec;
    time1->tv_sec += time2->tv_sec + (newNano / 1000000000);
    time1->tv_nsec = newNano % 1000000000;
    // check negative cases
    if ((time1->tv_nsec < 0) && (time1->tv_sec > 0))
    {
        time1->tv_sec--;
        time1->tv_nsec = 1000000000 + time1->tv_nsec;
    }
    else if ((time1->tv_sec < 0) && (time1->tv_nsec > 0))
    {
        time1->tv_sec++;
        time1->tv_nsec = time1->tv_nsec - 1000000000;
    }

    // return positive value if result is positive and vice versa
    if ((time1->tv_sec >= 0) && (time1->tv_nsec >= 0))
        return 1;
    else
        return -1;
}

/** Copies time from @a source to @a dest. */
static void timespec_copy(struct timespec* dest, const struct timespec* source)
{
    dest->tv_sec = source->tv_sec;
    dest->tv_nsec = source->tv_nsec;
}

/**
 * Compares value of two time structures.
 * @return  @li @a 1  if @a time1 > @a time2;
 * 			@li @a 0  if @a time1 == @a time2;
 * 			@li @a -1 if @a time1 < @a time2.
 */
static int timespec_cmp(struct timespec* time1, struct timespec* time2)
{
    // compare seconds first
    if (time1->tv_sec > time2->tv_sec)
    {
        return 1;
    }
    else if (time1->tv_sec < time2->tv_sec)
    {
        return -1;
    }
    else // time1->tv_sec == time2->tv_sec
    {	// compare also nanoseconds
        if (time1->tv_nsec > time2->tv_nsec)
        {
            return 1;
        }
        else if (time1->tv_nsec < time2->tv_nsec)
        {
            return -1;
        }
        else // time1->tv_nsec == time2->tv_nsec
        {
            return 0;
        }
    }
}
/**@}*/

/**
 * Returns new unique ID for periodic task.
 * @note This should be called only from synchronized context in order to be
 * 		thread safe!
 */
static int generateId(Task_Thread* thread)
{
    return thread->id_gen++;
}

/**@name Utility methods for work with #Periodic_Task structure
 * @{*/
/**
 * Sets next execution time for the task, which will be used to
 * decide its position in the tasklist.
 * Insert new tasks into tasklist, or re-sort tasklist once
 * existing tasks got updated.
 *
 * @note This should be called while taskListMutex is held.
 */
static void periodicTask_generateNextExecTime(Task_Thread *thread,
                                              Periodic_Task *ptask,
                                              int newly_created)
{
    Periodic_Task *prev = NULL, *next = NULL;

    /*
     * Next execution time is generated by adding period to last time
     * when the task is supposed to be executed.
     */
    timespec_add(&(ptask->nextScheduledTime), &(ptask->period));

    /*
     * Arrange the tasklist so that tasks are strictly sorted in
     * ascending order of the next execution time.
     *
     * For the newly added task, the start of tasklist is searched from.
     * For executed or reset task, however, since its next execution time
     * is bumped later, it will always be moved back along the tasklist.
     */
    if (newly_created == 1) {
        prev = NULL;
        next = thread->taskList;
    } else {
        prev = ptask->prev;
        next = ptask->next;
        /* Optimization of the case when there is only one item in queue */
        if ((!prev) && (!next))
            return;
        periodicTask_removeFromList(thread, ptask);
    }

    while (next && (timespec_cmp(&(ptask->nextScheduledTime),
                                 &(next->nextScheduledTime)) == 1)) {
        prev = next;
        next = next->next;
    }

    if (prev)
        prev->next = ptask;
    else
        thread->taskList = ptask;
    ptask->prev = prev;

    if (next)
        next->prev = ptask;
    ptask->next = next;
}

/** Updates task execution period. */
static void periodicTask_setPeriod(Periodic_Task* ptask,
                                   long period,
                                   int executeTimes)
{
    timespec_fromMillis(&(ptask->period), period);
    ptask->executeTimes = executeTimes;
}

/**
 * Sets next execution time for the task as (current time + execution period).
 * Current thread's tasklist will be re-sorted.
 * @note This should be called while taskListMutex is held.
 */
static void periodicTask_resetExecTime(Task_Thread *thread,
                                       Periodic_Task *ptask,
                                       int newly_created)
{
    // set next execution time = current time + period
    clock_gettime(CLOCK_REALTIME, &(ptask->nextScheduledTime));
    periodicTask_generateNextExecTime(thread, ptask, newly_created);
}

/**
 * Creates new instance of periodic task based on provided parameters.
 * Inserts it into current thread's tasklist based on when its next
 * execution time is going to be triggered.
 * @return @a NULL if there is not enough memory.
 * @note This should be called while taskListMutex is held.
 */
static Periodic_Task *periodicTask_create(
    Task_Thread *thread,
    periodic_task task,
    void *arg,
    long period,
    int executeTimes)
{
    Periodic_Task *ptask = (Periodic_Task *)malloc(sizeof(Periodic_Task));
    if (!ptask) {
        log_error("Unable to create new periodic task: Not enough memory.");
        return NULL;
    }

    ptask->task = task;
    ptask->arg = arg;
    ptask->id = generateId(thread);
    ptask->isCancelled = 0;
    ptask->isExecuting = 0;
    ptask->prev = LIST_POISON1;
    ptask->next = LIST_POISON2;

    periodicTask_setPeriod(ptask, period, executeTimes);
    periodicTask_resetExecTime(thread, ptask, 1);

    return ptask;
}

/** Releases memory allocated for periodic task. */
static void periodicTask_free(Periodic_Task* ptask)
{
    free(ptask);
}

/** Removes periodic task from scheduled task list (_Task_Thread::taskList). */
static void periodicTask_removeFromList(Task_Thread* thread,
                                        Periodic_Task* ptask)
{
    if (ptask->prev != NULL)
    {
        ptask->prev->next = ptask->next;
    }
    else
    {	// removed task from the top of the list
        thread->taskList = ptask->next;
    }

    if (ptask->next != NULL)
    {
        ptask->next->prev = ptask->prev;
    }

    ptask->prev = LIST_POISON1;
    ptask->next = LIST_POISON2;
}

/**
 * Executes scheduled task and schedules it for next execution if needed.
 * @note This should be called while taskListMutex is held.
 */
static void periodicTask_execute(Task_Thread* thread, Periodic_Task* ptask)
{
    ptask->isExecuting = 1;

    /* Release mutex, because execution may take considerable time */
    pthread_mutex_unlock(&(thread->taskListMutex));
    (ptask->task)(ptask->arg);
    pthread_mutex_lock(&(thread->taskListMutex));

    /*
     * Check whether this task is about to be cancelled. If yes, then
     * delete it and notify ptask_cancel of the completion and removal
     * of it.
     *
     * Note: ptask_cancel has already removed the current ptask from
     * thread's tasklist already!
     */
    if (ptask->isCancelled == 1) {
        periodicTask_free(ptask);
        pthread_cond_signal(&(thread->taskExecuted));
        return;
    }

    ptask->isExecuting = 0;

    /* Check whether we need to schedule next execution time */
    if (ptask->executeTimes != EXECUTE_INDEFINITE) {
        ptask->executeTimes--;
        if (ptask->executeTimes == 0) {
            /* The task has already been executed required times */
            periodicTask_removeFromList(thread, ptask);
            periodicTask_free(ptask);
            return;
        }
    }

    /* Caculate the next execution time, re-arrange the tasklist if needed */
    periodicTask_generateNextExecTime(thread, ptask, 0);
}

/** Returns task with provided id, or @a NULL if no such task found. */
static Periodic_Task* periodicTask_get(Task_Thread* thread, int id)
{
    Periodic_Task* ptask = thread->taskList;
    while((ptask != NULL) && (ptask->id != id))
    {
        ptask = ptask->next;
    }
    return ptask;
}

/** Returns task with the smallest scheduled time.
 *
 * Thanks to the fact that tasklist is strictly sorted based on the
 * ascending order to tasks' next execution time, the very first item
 * should always be returned.
 *
 * @return @a NULL if there are no scheduled tasks.
 */
static Periodic_Task* periodicTask_getClosest(Task_Thread* thread)
{
    return thread->taskList;
}

/** Releases memory allocated by whole tasks in the task list. */
static void periodicTask_deleteRecursive(Periodic_Task* ptask)
{
    if (ptask == NULL)
    {
        return;
    }

    periodicTask_deleteRecursive(ptask->next);

    periodicTask_free(ptask);
}
/** @} */

/** Main working cycle which executed tasks. */
static void* threadCycle(void* arg)
{
    Task_Thread* thread = (Task_Thread*) arg;
    Periodic_Task* ptask = NULL;
    int waitState;

    log_debug("Periodic Task thread is started...");

    pthread_mutex_lock(&(thread->taskListMutex));

    /*
     * Endless cycle to wait and process the periodic tasks in the queue.
     * The thread is stopped by a special task of stopTask.
     */
    while (1)
    {
        /*
         * Find the closest task
         * Wait by loop in case of spurious wakeup
         */
        while ((ptask = periodicTask_getClosest(thread)) == NULL)
            pthread_cond_wait(&(thread->taskListUpdated),
                              &(thread->taskListMutex));

        /* Wait for the time when task should be executed */
        waitState = pthread_cond_timedwait(&(thread->taskListUpdated),
                                           &(thread->taskListMutex),
                                           &(ptask->nextScheduledTime));

        /* Check why we stopped waiting */
        switch (waitState)
        {
        case 0:
            /*
             * condition _taskListUpdated has been signaled, which means that
             * a new task has been added into the queue. So we need to check
             * once again for the closest task.
             */
            break;
        case ETIMEDOUT:
            /*
             * Nothing happened when we waited for the task to be executed
             * so let's execute it now.
             *
             * Fall through.
             */
        case EINVAL:
            /*
             * The selected closest task has a next execution time in the past,
             * which is just a normal case with a one-shot task.
             */
            periodicTask_execute(thread, ptask);
            break;
        default:
            log_warning("Periodic Task thread: pthread_cond_timedwait() "
                        "returned unknown result: %d", waitState);
            break;
        }
    }

    /* this line actually is never reached, see stopTask */
    return NULL;
}

/** A special task, which is scheduled in order to stop Task_Thread. */
static void stopTask(void* arg)
{
    Task_Thread* thread = (Task_Thread*) arg;
    // delete all tasks recursively
    pthread_mutex_lock(&(thread->taskListMutex));
    periodicTask_deleteRecursive(thread->taskList);
    thread->taskList = NULL;
    pthread_mutex_unlock(&(thread->taskListMutex));

    // stop the thread
    pthread_mutex_destroy(&(thread->taskListMutex));
    pthread_cond_destroy(&(thread->taskListUpdated));
    pthread_cond_destroy(&(thread->taskExecuted));
    free(thread);
    log_debug("Periodic Task thread is stopped.");
    pthread_exit(NULL);
}

int ptask_schedule(Task_Thread* thread,
                   periodic_task task,
                   void* arg,
                   long period,
                   int executeTimes)
{
    if ((executeTimes <= 0) && (executeTimes != EXECUTE_INDEFINITE))
        return -1;

    pthread_mutex_lock(&(thread->taskListMutex));
    Periodic_Task* ptask =
        periodicTask_create(thread, task, arg, period, executeTimes);

    // task list is updated, notify taskThread
    pthread_cond_signal(&(thread->taskListUpdated));
    pthread_mutex_unlock(&(thread->taskListMutex));

    return (ptask == NULL) ? -1 : ptask->id;
}

int ptask_reschedule(Task_Thread* thread,
                     int taskId,
                     long period,
                     int executeTimes,
                     int add)
{
    if (executeTimes == 0)
    {	// executeTimes can't be 0, but can be -1 (EXECUTE_INDEFINITE).
        return -1;
    }

    pthread_mutex_lock(&(thread->taskListMutex));

    Periodic_Task* ptask = periodicTask_get(thread, taskId);
    if (ptask == NULL)
    {	// task is not found
        pthread_mutex_unlock(&(thread->taskListMutex));
        return -1;
    }

    if (add)
    {
        // add provided time to the period and next execution time
        struct timespec addTime, temp;
        timespec_fromMillis(&addTime, period);
        timespec_copy(&temp, &(ptask->period));
        if (timespec_add(&temp, &addTime) < 0)
        {	// resulting new period would be negative -> error
            pthread_mutex_unlock(&(thread->taskListMutex));
            return -1;
        }

        timespec_copy(&(ptask->period), &temp);
        timespec_add(&(ptask->nextScheduledTime), &addTime);
        ptask->executeTimes = executeTimes;
    }
    else // add == false
    {
        if (period < 0)
        {
            // period should not be negative
            pthread_mutex_unlock(&(thread->taskListMutex));
            return -1;
        }
        // set provided time as a new period and reschedule execution time
        periodicTask_setPeriod(ptask, period, executeTimes);
        periodicTask_resetExecTime(thread, ptask, 0);
    }

    // task list is updated, notify taskThread
    pthread_cond_signal(&(thread->taskListUpdated));
    pthread_mutex_unlock(&(thread->taskListMutex));
    return 0;
}

int ptask_isScheduled(Task_Thread* thread, int taskId)
{
	return (periodicTask_get(thread, taskId) != NULL) ? 1 : 0;
}

/*
 * Reset the next execution time based on current time
 * of the specified task
 */
int ptask_reset(Task_Thread* thread, int taskId)
{
    pthread_mutex_lock(&(thread->taskListMutex));

    Periodic_Task* ptask = periodicTask_get(thread, taskId);
    if (ptask == NULL)
    {	// task is not found
        pthread_mutex_unlock(&(thread->taskListMutex));
        return -1;
    }

    periodicTask_resetExecTime(thread, ptask, 0);

    // task list is updated, notify taskThread
    pthread_cond_signal(&(thread->taskListUpdated));
    pthread_mutex_unlock(&(thread->taskListMutex));

    return 0;
}

/*
 * Cancel(dequeue and free) the specified task from tasklist.
 * If the task is being executed, then whether to wait for
 * its completion will be decided by the wait parameter.
 *
 * NOTE: the wait parameter must be 0 under any of following
 * cases, otherwise deadlock emerges!
 * 1. when the caller is currently holding any mutex;
 * 2. the executed task is trying to cancel itself
 */
int ptask_cancel(Task_Thread* thread, int taskId, int wait)
{
    pthread_mutex_lock(&(thread->taskListMutex));

    Periodic_Task* ptask = periodicTask_get(thread, taskId);
    if (ptask == NULL) {
        pthread_mutex_unlock(&(thread->taskListMutex));
        return -1;
    }

    /*
     * Dequeue the task in the first place, and notify the task
     * thread its tasklist has been changed
     */
    periodicTask_removeFromList(thread, ptask);
    pthread_cond_signal(&(thread->taskListUpdated));

    if (ptask->isExecuting) {
        /*
         * Cannot free the task now that it is under execution.
         * Instead raise the isCancelled flag so that it would
         * be freed on completion.
         */
        ptask->isCancelled = 1;
        if (wait == 1) {
            pthread_cond_wait(&(thread->taskExecuted),
                              &(thread->taskListMutex));
        }
    } else {
        periodicTask_free(ptask);
    }

    pthread_mutex_unlock(&(thread->taskListMutex));
    return 0;
}

Task_Thread* ptask_init()
{
    Task_Thread* thread = (Task_Thread*) malloc(sizeof(Task_Thread));
    if (!thread) {
        log_error("Unable to start task thread: Not enough memory");
        return NULL;
    }

	pthread_mutex_init(&thread->taskListMutex, NULL);
	pthread_cond_init(&thread->taskListUpdated, NULL);
	pthread_cond_init(&thread->taskExecuted, NULL);

    thread->id_gen = 1;
    thread->taskList = NULL;

	if (pthread_create(&(thread->thread), NULL, &threadCycle, thread) != 0) {
        log_error("Unable to start a new thread");
		pthread_mutex_destroy(&thread->taskListMutex);
		pthread_cond_destroy(&thread->taskListUpdated);
		pthread_cond_destroy(&thread->taskExecuted);
        free(thread);

		thread = NULL;
    }

    return thread;
}

int ptask_dispose(Task_Thread* thread, int wait)
{
	int error;

	if (!thread) {
		return -1;
	}

	if ((error = ptask_schedule(thread, &stopTask, thread, 0, 1)) <= 0) {
		return error;
	}

    if (wait == 1) {
        return pthread_join(thread->thread, NULL);
	}

	return 0;
}

/*
 * Dequeue the specific task/job from the queue
 */
void obix_cancel_task(obix_task_t *task)
{
	if (task->initialised == 1 && task->t) {
		ptask_cancel(task->t, task->id, 1);
	}
}

int obix_schedule_task(obix_task_t *task)
{
	if (task->initialised == 0 || !task->t) {
		return -1;
	}

	task->id = ptask_schedule(task->t, task->func, task->args,
							  task->period, task->times);

	return (task->id < 0) ? -1 : 0;
}

/*
 * Add a special task to terminate and release the entire thread
 */
void obix_destroy_task(obix_task_t *task)
{
	if (task->initialised == 1 && task->t) {
		ptask_dispose(task->t, 1);
	}
}

/*
 * Reset the next execution time from the current moment on
 */
int obix_reset_task(obix_task_t *task)
{
	if (task->initialised == 0 || task->id < 0) {
		return -1;
	}

	return ptask_reset(task->t, task->id);
}

/*
 * Initialise a task descriptor, specifying its payload function and
 * other attributes such as argument, running period etc.
 *
 * Also create a worker thread if not provided. Callers can make use
 * of pointers of existing worker threads to run more than one task.
 */
int obix_setup_task(obix_task_t *task, Task_Thread *t, periodic_task func,
					void *args, long period, int times)
{
	if (task->initialised == 1) {
		return 0;
	}

	if (!t) {
		if (!(task->t = ptask_init())) {
			log_error("Failed to fork worker threads");
			return -1;
		}
	} else {
		task->t = t;
	}

	task->id = 0;		/* not scheduled yet */
	task->func = func;
	task->args = args;
	task->period = period;
	task->times = times;

	task->initialised = 1;
	return 0;
}
