#include "config.h"
#include "globals.h"

#include "errno.h"

#include "util/init.h"
#include "util/debug.h"
#include "util/list.h"
#include "util/string.h"

#include "proc/kthread.h"
#include "proc/proc.h"
#include "proc/sched.h"

#include "mm/slab.h"
#include "mm/page.h"

kthread_t *curthr; /* global */
static slab_allocator_t *kthread_allocator = NULL;

#ifdef __MTP__
/* Stuff for the reaper daemon, which cleans up dead detached threads */
static proc_t *reapd = NULL;
static kthread_t *reapd_thr = NULL;
static ktqueue_t reapd_waitq;
static list_t kthread_reapd_deadlist; /* Threads to be cleaned */

static void *kthread_reapd_run(int arg1, void *arg2);
#endif

void
kthread_init()
{
        kthread_allocator = slab_allocator_create("kthread", sizeof(kthread_t));
        KASSERT(NULL != kthread_allocator);
}

/**
 * Allocates a new kernel stack.
 *
 * @return a newly allocated stack, or NULL if there is not enough
 * memory available
 */
static char *
alloc_stack(void)
{
        /* extra page for "magic" data */
        char *kstack;
        int npages = 1 + (DEFAULT_STACK_SIZE >> PAGE_SHIFT);
        kstack = (char *)page_alloc_n(npages);

        return kstack;
}

/**
 * Frees a stack allocated with alloc_stack.
 *
 * @param stack the stack to free
 */
static void
free_stack(char *stack)
{
	page_free_n(stack, 1 + (DEFAULT_STACK_SIZE >> PAGE_SHIFT));
}

/*
 * Allocate a new stack with the alloc_stack function. The size of the
 * stack is DEFAULT_STACK_SIZE.
 *
 * Don't forget to initialize the thread context with the
 * context_setup function. The context should have the same pagetable
 * pointer as the process.
 */

/*
 * Author : ADITYA JAIN
 * Functionality :
 * Allocates a slab to thread called new_thread
 * allocate a stack to this new thread.
 * initialize the thread context with the
 * context_setup function. The context should have the same pagetable
 * pointer as the process.
 */

kthread_t *
kthread_create(struct proc *p, kthread_func_t func, long arg1, void *arg2)
{
	KASSERT(NULL != p); /* should have associated process */
	kthread_t *new_thread;
       	new_thread= (kthread_t*) slab_obj_alloc(kthread_allocator);
         
        char *new_stack=alloc_stack();
	new_thread->kt_kstack=new_stack;
	  

        new_thread->kt_proc = p;        
       
	new_thread->kt_wchan = NULL;   
	
	context_setup(&(new_thread->kt_ctx),func,arg1,arg2,new_stack,DEFAULT_STACK_SIZE,p->p_pagedir);
       
	list_link_init(&new_thread->kt_qlink);
	list_link_init(&new_thread->kt_plink);

	if(list_empty(&p->p_threads)==1)
       	{
		list_init(&p->p_threads);
       	}
	
	list_insert_tail(&p->p_threads,&new_thread->kt_plink);
	dbg(DBG_PROC,"Created Thread for Process %s\n",p->p_comm);
       	
       
       return new_thread;
   
}


void
kthread_destroy(kthread_t *t)
{
        KASSERT(t && t->kt_kstack);
	free_stack(t->kt_kstack);
        if (list_link_is_linked(&t->kt_plink))
                list_remove(&t->kt_plink);
        slab_obj_free(kthread_allocator, t);
	dbg(DBG_PROC,"Destroyed Thread for Process %s\n",t->kt_proc->p_comm);
}

/*
 * If the thread to be cancelled is the current thread, this is
 * equivalent to calling kthread_exit. Otherwise, the thread is
 * sleeping and we need to set the cancelled and retval fields of the
 * thread.
 *
 * If the thread's sleep is cancellable, cancelling the thread should
 * wake it up from sleep.
 *
 * If the thread's sleep is not cancellable, we do nothing else here.
 */

/*
Author: Aditya Jain
*If the thread is curthr, then directly thread_exit
*else sched_cancel. Schde_cancel description says that it will set the retval and cancelled field. check sched.c
*/
void
kthread_cancel(kthread_t *kthr, void *retval)   
{
	        
	KASSERT(NULL != kthr); /* should have thread */
	if(kthr == curthr)
		kthread_exit(retval);
	else 
	{
	  if(kthr->kt_state==KT_SLEEP_CANCELLABLE )
	  {
			
	  	sched_cancel(kthr);	
	  }
	  else
		kthr->kt_cancelled=1;
	  kthr->kt_retval=retval;
	}
	dbg(DBG_PROC,"Cancelled Thread for Process %s\n",kthr->kt_proc->p_comm);
	/*NOT_YET_IMPLEMENTED("PROCS: kthread_cancel"); */


}

/*
 * You need to set the thread's retval field, set its state to
 * KT_EXITED, and alert the current process that a thread is exiting
 * via proc_thread_exited.
 *
 * It may seem unneccessary to push the work of cleaning up the thread
 * over to the process. However, if you implement MTP, a thread
 * exiting does not necessarily mean that the process needs to be
 * cleaned up.
 */

/* Author : Aditya Jain
 * Functionality : Setting the retval and state as mentioned above and calling proc_thread_exited
 */
void
kthread_exit(void *retval)
{
 	
	KASSERT(!curthr->kt_wchan); /* queue should be empty */
        KASSERT(!curthr->kt_qlink.l_next && !curthr->kt_qlink.l_prev); /* queue should be empty */
        KASSERT(curthr->kt_proc == curproc);
	
	curthr->kt_retval = retval;
	curthr->kt_state = KT_EXITED; 
	proc_thread_exited(retval);

      /* NOT_YET_IMPLEMENTED("PROCS: kthread_exit"); */
}

/*
 * The new thread will need its own context and stack. Think carefully
 * about which fields should be copied and which fields should be
 * freshly initialized.
 *
 * You do not need to worry about this until VM.
 */
kthread_t *
kthread_clone(kthread_t *thr)
{
        
	/*kthread_t new_clonethread;
	new_clonethread=(kthread_t*) slab_obj_alloc(kthread_allocator);
       	dbg_print("101\n");  
       	char *new_stack=alloc_stack();
	 context_t *kthr_context;
        context_setup(kthr_context,func,arg1,arg2,new_stack,DEFAULT_STACK_SIZE,p->p_pagedir);
		
	dbg_print("102\n");  
	new_clonethread->kt_proc=thr->kt_proc; */
       
	NOT_YET_IMPLEMENTED("VM: kthread_clone");
        return NULL;
}

/*
 * The following functions will be useful if you choose to implement
 * multiple kernel threads per process. This is strongly discouraged
 * unless your weenix is perfect.
 */
#ifdef __MTP__
int
kthread_detach(kthread_t *kthr)
{
        NOT_YET_IMPLEMENTED("MTP: kthread_detach");
        return 0;
}

int
kthread_join(kthread_t *kthr, void **retval)
{
        NOT_YET_IMPLEMENTED("MTP: kthread_join");
        return 0;
}

/* ------------------------------------------------------------------ */
/* -------------------------- REAPER DAEMON ------------------------- */
/* ------------------------------------------------------------------ */
static __attribute__((unused)) void
kthread_reapd_init()
{
        NOT_YET_IMPLEMENTED("MTP: kthread_reapd_init");
}
init_func(kthread_reapd_init);
init_depends(sched_init);

void
kthread_reapd_shutdown()
{
        NOT_YET_IMPLEMENTED("MTP: kthread_reapd_shutdown");
}

static void *
kthread_reapd_run(int arg1, void *arg2)
{
        NOT_YET_IMPLEMENTED("MTP: kthread_reapd_run");
        return (void *) 0;
}
#endif
