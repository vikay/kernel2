#include "globals.h"
#include "errno.h"

#include "util/debug.h"

#include "proc/kthread.h"
#include "proc/kmutex.h"

/*
 * IMPORTANT: Mutexes can _NEVER_ be locked or unlocked from an
 * interrupt context. Mutexes are _ONLY_ lock or unlocked from a
 * thread context.
 */

/* Author : Vaishnavi Dalvi
* Functionality : Initializing the wait queue of mutex using sched_queue_init
* Initializing the holder structure to NULL. This is a thread pointer 
*/

void
kmutex_init(kmutex_t *mtx)
{
	sched_queue_init(&(mtx->km_waitq));
	mtx->km_holder=NULL;        
/* NOT_YET_IMPLEMENTED("PROCS: kmutex_init"); */
}

/*
 * This should block the current thread (by sleeping on the mutex's
 * wait queue) if the mutex is already taken.
 *
 * No thread should ever try to lock a mutex it already has locked.
 */

/* Author : Vaishnavi Dalvi
* Functionality : If holder is not NULL put thread in wait queue.
* else give it the mutex. This queue is not cancellable
*/

void
kmutex_lock(kmutex_t *mtx)
{

/*Lock is not re-entrant, so if process tries to grab the lock again,
it gets blocked, deadlocking on itself */
	  KASSERT(curthr && (curthr != mtx->km_holder));

	if(mtx->km_holder!=NULL)
		sched_sleep_on(&(mtx->km_waitq));	
 	else
		mtx->km_holder=curthr;
	/* NOT_YET_IMPLEMENTED("PROCS: kmutex_lock"); */
}

/*
 * This should do the same as kmutex_lock, but use a cancellable sleep
 * instead.
 */

/* Author : Vaishnavi Dalvi
* Functionality : If holder is NULL give the mutex to thread and return 0.
* else check whether the thread is woken up and the holder is the current thread. If yes return 0. Else put it in cancellable_sleep queue.
* 
*/
int
kmutex_lock_cancellable(kmutex_t *mtx)
{
	KASSERT(curthr && (curthr != mtx->km_holder));
	if(mtx->km_holder == NULL)
	{
		mtx->km_holder=curthr;
		return 0;
	}
	else	
	{
		if(-EINTR==sched_cancellable_sleep_on(&(mtx->km_waitq)) && mtx->km_holder == curthr)
			return 0;
		return sched_cancellable_sleep_on(&(mtx->km_waitq));	
	}
     		/* NOT_YET_IMPLEMENTED("PROCS: kmutex_lock"); */
}

/*
 * If there are any threads waiting to take a lock on the mutex, one
 * should be woken up and given the lock.
 *
 * Note: This should _NOT_ be a blocking operation!
 *
 * Note: Don't forget to add the new owner of the mutex back to the
 * run queue.
 *
 * Note: Make sure that the thread on the head of the mutex's wait
 * queue becomes the new owner of the mutex.
 *
 * @param mtx the mutex to unlock
 */


/* Author : Vaishnavi Dalvi
* Functionality : Wake up the first thread from queue and give it the mutex. Add it to the runnable queue.
*
*/
void
kmutex_unlock(kmutex_t *mtx)
{
	KASSERT(curthr && (curthr == mtx->km_holder));
	
        kthread_t *woken_thread = sched_wakeup_on(&(mtx->km_waitq));
	if(woken_thread != NULL)
	{
	mtx->km_holder=woken_thread;
	/*sched_make_runnable(woken_thread); */
	KASSERT(curthr != mtx->km_holder);	
	}
	else mtx->km_holder=NULL;
	
	/* NOT_YET_IMPLEMENTED("PROCS: kmutex_unlock"); */
}
