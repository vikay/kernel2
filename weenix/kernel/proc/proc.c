#include "kernel.h"
#include "config.h"
#include "globals.h"
#include "errno.h"

#include "util/debug.h"
#include "util/list.h"
#include "util/string.h"
#include "util/printf.h"

#include "proc/kthread.h"
#include "proc/proc.h"
#include "proc/sched.h"
#include "proc/proc.h"

#include "mm/slab.h"
#include "mm/page.h"
#include "mm/mmobj.h"
#include "mm/mm.h"
#include "mm/mman.h"

#include "vm/vmmap.h"

#include "fs/vfs.h"
#include "fs/vfs_syscall.h"
#include "fs/vnode.h"
#include "fs/file.h"

/*	Implemented from point of view of STP and NOT MTP	*/

proc_t *curproc = NULL; /* global */
static slab_allocator_t *proc_allocator = NULL;

static list_t _proc_list;
static proc_t *proc_initproc = NULL; /* Pointer to the init process (PID 1) */

void
proc_init()
{
        list_init(&_proc_list);
        proc_allocator = slab_allocator_create("proc", sizeof(proc_t));
        KASSERT(proc_allocator != NULL);
}

static pid_t next_pid = 0;

/**
 * Returns the next available PID.
 *
 * Note: Where n is the number of running processes, this algorithm is
 * worst case O(n^2). As long as PIDs never wrap around it is O(n).
 *
 * @return the next available PID
 */
static int
_proc_getid()
{
        proc_t *p;
        pid_t pid = next_pid;
        while (1) {
failed:
                list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                        if (p->p_pid == pid) {
                                if ((pid = (pid + 1) % PROC_MAX_COUNT) == next_pid) {
                                        return -1;
                                } else {
                                        goto failed;
                                }
                        }
                } list_iterate_end();
                next_pid = (pid + 1) % PROC_MAX_COUNT;
                return pid;
        }
}

/*
 * The new process, although it isn't really running since it has no
 * threads, should be in the PROC_RUNNING state.
 *
 * Don't forget to set proc_initproc when you create the init
 * process. You will need to be able to reference the init process
 * when reparenting processes to the init process.
 */

/*
 * Author : Aditya Parikh
 * Functionality : allocate slab, assign pid(wrap around ??), increment next_pid,
 * save process name, assign parent_process(idle and others),
 * save global init_proc, assign state running state, 
 * assign status 0, init wait queue, 
 * init. thread list, init. children process list,
 * append into proc_list, appended into parent's child list,
 * assigned new pagedir, returning p_proc_new(instead of null)
 */
proc_t *
proc_create(char *name)
{
	proc_t *p_proc_new;
	dbg(DBG_PROC,"Allocating memory for Process %s\n",name);
	p_proc_new = (proc_t *)slab_obj_alloc(proc_allocator);
	
        p_proc_new->p_pid = next_pid;
	KASSERT(PID_IDLE != p_proc_new->p_pid || list_empty(&_proc_list)); /* pid can only be PID_IDLE if this is the first process */
	KASSERT(PID_INIT != p_proc_new->p_pid || PID_IDLE == curproc->p_pid); /* pid can only be PID_INIT when creating from idle process */

	strcpy(p_proc_new->p_comm, name);
	p_proc_new->p_pagedir = pt_create_pagedir();
	
 	if(next_pid==0)       
		p_proc_new->p_pproc = p_proc_new; /* idle process parent of itself*/
	else
		p_proc_new->p_pproc = curproc;  
	if(next_pid==1)
		proc_initproc = p_proc_new;
        p_proc_new->p_state = PROC_RUNNING;
	p_proc_new->p_status = 0;
	sched_queue_init(&(p_proc_new->p_wait));
	list_init(&(p_proc_new->p_children));
	list_init(&(p_proc_new->p_threads));
	list_link_init(&(p_proc_new->p_child_link));
	list_link_init(&(p_proc_new->p_list_link));
	list_insert_tail(&_proc_list, &(p_proc_new->p_list_link)) ;
	list_insert_tail(&(p_proc_new->p_pproc->p_children), &(p_proc_new->p_child_link)) ;	
	next_pid++;
	dbg(DBG_PROC,"New Process %s Created\n",name);
        return p_proc_new;
}

/**
 * Cleans up as much as the process as can be done from within the
 * process. This involves:
 *    - Closing all open files (VFS)
 *    - Cleaning up VM mappings (VM)
 *    - Waking up its parent if it is waiting
 *    - Reparenting any children to the init process
 *    - Setting its status and state appropriately
 *
 * The parent will finish destroying the process within do_waitpid (make
 * sure you understand why it cannot be done here). Until the parent
 * finishes destroying it, the process is informally called a 'zombie'
 * process.
 *
 * This is also where any children of the current process should be
 * reparented to the init process (unless, of course, the current
 * process is the init process. However, the init process should not
 * have any children at the time it exits).
 *
 * Note: You do _NOT_ have to special case the idle process. It should
 * never exit this way.
 *
 * @param status the status to exit the process with
 */

/* Author : Aditya Parikh 
 * Functionality : 
 * reparented child processes of current process to init process,
 * assigned status and state to current process,
 * woken up parent process
 */
void
proc_cleanup(int status)
{
	list_link_t *link;
	proc_t *proc_child;
	KASSERT(NULL != proc_initproc); /* should have an "init" process */
	KASSERT(1 <= curproc->p_pid); /* this process should not be idle process */
	KASSERT(NULL != curproc->p_pproc); /* this process should have parent process */
	dbg(DBG_PROC,"Starting Cleaning up Process %s\n",curproc->p_comm);
	for (link = curproc->p_children.l_next;link != &(curproc->p_children) && curproc->p_pid!=PID_INIT;)
	{
		proc_child = list_item(link, proc_t, p_child_link);
		list_link_t *remove_link = link;
		link = link->l_next;
		list_remove(remove_link); 
		list_insert_tail(&(proc_initproc->p_children), &(proc_child->p_child_link)) ;
		dbg(DBG_PROC,"Reparenting Child Process %s to Init Process",proc_child->p_comm);	
	}
	
	curproc->p_state = PROC_DEAD;
	curproc->p_status = status;
	dbg(DBG_PROC,"Cleaning up Process %s Complete. Waking up Parent Process %s\n",curproc->p_comm,curproc->p_pproc->p_comm);
	if(!sched_queue_empty(&curproc->p_pproc->p_wait))
	{	
		sched_wakeup_on(&(curproc->p_pproc->p_wait));
		sched_switch();
	}
}

/*
 * This has nothing to do with signals and kill(1).
 *
 * Calling this on the current process is equivalent to calling
 * do_exit().
 *
 * In Weenix, this is only called from proc_kill_all.
 */

/* Author : Aditya Parikh
 * Functionality :
 * if calling proc = current proc then called do_exit
 * else for that proc : cancelled all threads, as STP no join
 */
void
proc_kill(proc_t *p, int status)
{
	if(p==curproc)
		do_exit(status);
	else	
	{
		list_link_t *link;
		kthread_t *child_thread;
		p->p_status = status;
		for (link = p->p_threads.l_next;link != &(p->p_threads);)	
		{
			child_thread = list_item(link, kthread_t, kt_plink);	
			link = link->l_next;	
			kthread_cancel(child_thread, (void*)&status); /* not sure about retval */
		}
	}
}

/*
 * Remember, proc_kill on the current process will _NOT_ return.
 * Don't kill direct children of the idle process.
 *
 * In Weenix, this is only called by sys_halt.
 */

/* Author : Aditya Parikh
 * Functionality : 
 * kills all procs
 * except idle and init process(do nothing for both)
 */
void
proc_kill_all()
{
        list_link_t *link;
	proc_t *target_proc;
	dbg(DBG_PROC,"Killing all process which are alive.\n");
	for (link = _proc_list.l_next;link != &(_proc_list);)
	{
		target_proc = list_item(link, proc_t, p_list_link);
		link = link->l_next;
		if(target_proc->p_pproc->p_pid!= PID_IDLE && target_proc!=curproc)
		{
			proc_kill(target_proc, 0); /* not sure about status = 0 */
		}
	}
	if(target_proc->p_pproc->p_pid!= PID_IDLE)	
		proc_kill(curproc, 0);
}

proc_t *
proc_lookup(int pid)
{
        proc_t *p;
        list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                if (p->p_pid == pid) {
                        return p;
                }
        } list_iterate_end();
        return NULL;
}

list_t *
proc_list()
{
        return &_proc_list;
}

/*
 * This function is only called from kthread_exit.
 *
 * Unless you are implementing MTP, this just means that the process
 * needs to be cleaned up and a new thread needs to be scheduled to
 * run. If you are implementing MTP, a single thread exiting does not
 * necessarily mean that the process should be exited.
 */

/* Author : Aditya Parikh
 * Functionality : cleaned up current process and switched to another thread.
 * Assuming no MTP
 */
void
proc_thread_exited(void *retval)
{
	kthread_t *kill_thread;
	list_link_t *link ;

	for(link = curproc->p_threads.l_next;link!=&(curproc->p_threads);)
	{
		kill_thread = list_item(link , kthread_t , kt_plink);
		link=link->l_next;
		if(kill_thread != curthr && kill_thread->kt_state == KT_EXITED)
		{	
			kthread_destroy(kill_thread);
		}
	}
	
	if(curproc->p_threads.l_next->l_next == &(curproc->p_threads))
	{
		proc_cleanup(1);
		sched_switch();
	}
	
	else if(curproc->p_threads.l_next->l_next != &(curproc->p_threads))
	{	sched_switch();	
		proc_thread_exited(retval);
	}
	
}

/* If pid is -1 dispose of one of the exited children of the current
 * process and return its exit status in the status argument, or if
 * all children of this process are still running, then this function
 * blocks on its own p_wait queue until one exits.
 *
 * If pid is greater than 0 and the given pid is a child of the
 * current process then wait for the given pid to exit and dispose
 * of it.
 *
 * If the current process has no children, or the given pid is not
 * a child of the current process return -ECHILD.
 *
 * Pids other than -1 and positive numbers are not supported.
 * Options other than 0 are not supported.
 */

/* Author : Aditya Parikh
 * Functionality : 
 * IF pid=-1 disposed ANY ONE 'exited' child and returned its status IF atleast one 'exited' 
 * IF pid>0 disposed required child and returned its status IF 'exited' 
 * IF pid=-1 and no children then set errno = ECHILD
 * IF pid>0 and required pid not found then set errno = ECHILD
 * IF pid=-1 OR >0 and children are running then made process 'wait'(cancellable)
 * IF pid<-1 OR pid=0 OR options!=0 return NULL
 * after 'wait' called do_waitpid again and return whatever it returns.
 * for disposed freed process memory and removed child from proc_list and children_list
 */

pid_t
 do_waitpid(pid_t pid, int options, int *status)
{
	list_link_t *link;
	list_link_t *link1;
	proc_t *proc_child;
	pid_t rpid = -1;
        int no_child = 0;
	int req_proc_found_running = 0;
	int req_proc_found = 0;
	
	for (link = curproc->p_children.l_next;link != &(curproc->p_children);)
	{
		no_child++;	
		proc_child = list_item(link, proc_t, p_child_link);
		list_link_t *remove_link = link;		
		link = link->l_next;
		
		if(pid>0 && proc_child->p_pid==pid)
			req_proc_found = 1;
		if(pid==-1 || req_proc_found == 1)
		{
			if(proc_child->p_state == PROC_DEAD)
			{
				rpid = proc_child->p_pid;
				KASSERT(NULL != proc_child); /* the process should not be NULL */
				KASSERT(-1 == pid || rpid == pid); /* should be able to find the process */
				KASSERT(NULL != proc_child->p_pagedir); /* this process should have pagedir */
				/*status = proc_child->p_status;*/
				if(list_link_is_linked(remove_link))
					list_remove(remove_link);
				if(list_link_is_linked(&(proc_child->p_list_link)))
					list_remove(&(proc_child->p_list_link));
	
				while(proc_child->p_threads.l_next != &proc_child->p_threads)
				{
					kthread_t *thread_kill = list_item(proc_child->p_threads.l_next, kthread_t , kt_plink);
					KASSERT(KT_EXITED == thread_kill->kt_state); /* thr points to a thread to be destroyed */ 
					list_remove(&(thread_kill->kt_plink));
					kthread_destroy(thread_kill);

				}
				dbg(DBG_PROC,"Freeing memory for process %s\n",proc_child->p_comm);
				slab_obj_free(proc_allocator, proc_child);
				return rpid;
			}
			else if(pid>0)
			{
				req_proc_found_running = 1;
				break;
			}
		}
	}
	if((no_child==0 && pid==-1) || (pid>0 && req_proc_found == 0))
	{
		return -ECHILD;
	}
	else if((pid==-1 && no_child!=0) || (pid>0 && req_proc_found_running==1))
	{
		dbg(DBG_PROC,"Sleeping process %s for child process to terminate\n",curproc->p_comm);
		sched_sleep_on(&curproc->p_wait);
		dbg(DBG_PROC,"Process %s Woke up\n",curproc->p_comm);
		return do_waitpid(pid, options, status);
	}		
        return 0;
}

/*
 * Cancel all threads, join with them, and exit from the current
 * thread.
 *
 * @param status the exit status of the process
 */

/* Author : Aditya Parikh
 * Functionality : 
 * cancelled all threads, as STP no join
 * exited from current thread
 */
void
do_exit(int status)
{
	list_link_t *link;
	kthread_t *child_thread;

	for (link = curproc->p_threads.l_next;link != &(curproc->p_threads);)	
	{
		child_thread = list_item(link, kthread_t, kt_plink);
		link = link->l_next;
		if(child_thread!=curthr)		
			kthread_cancel(child_thread, 0);
	}
	kthread_exit((void*)1);
}

size_t
proc_info(const void *arg, char *buf, size_t osize)
{
        const proc_t *p = (proc_t *) arg;
        size_t size = osize;
        proc_t *child;

        KASSERT(NULL != p);
        KASSERT(NULL != buf);

        iprintf(&buf, &size, "pid:          %i\n", p->p_pid);
        iprintf(&buf, &size, "name:         %s\n", p->p_comm);
        if (NULL != p->p_pproc) {
                iprintf(&buf, &size, "parent:       %i (%s)\n",
                        p->p_pproc->p_pid, p->p_pproc->p_comm);
        } else {
                iprintf(&buf, &size, "parent:       -\n");
        }

#ifdef __MTP__
        int count = 0;
        kthread_t *kthr;
        list_iterate_begin(&p->p_threads, kthr, kthread_t, kt_plink) {
                ++count;
        } list_iterate_end();
        iprintf(&buf, &size, "thread count: %i\n", count);
#endif

        if (list_empty(&p->p_children)) {
                iprintf(&buf, &size, "children:     -\n");
        } else {
                iprintf(&buf, &size, "children:\n");
        }
        list_iterate_begin(&p->p_children, child, proc_t, p_child_link) {
                iprintf(&buf, &size, "     %i (%s)\n", child->p_pid, child->p_comm);
        } list_iterate_end();

        iprintf(&buf, &size, "status:       %i\n", p->p_status);
        iprintf(&buf, &size, "state:        %i\n", p->p_state);

#ifdef __VFS__
#ifdef __GETCWD__
        if (NULL != p->p_cwd) {
                char cwd[256];
                lookup_dirpath(p->p_cwd, cwd, sizeof(cwd));
                iprintf(&buf, &size, "cwd:          %-s\n", cwd);
        } else {
                iprintf(&buf, &size, "cwd:          -\n");
        }
#endif /* __GETCWD__ */
#endif

#ifdef __VM__
        iprintf(&buf, &size, "start brk:    0x%p\n", p->p_start_brk);
        iprintf(&buf, &size, "brk:          0x%p\n", p->p_brk);
#endif

        return size;
}

size_t
proc_list_info(const void *arg, char *buf, size_t osize)
{
        size_t size = osize;
        proc_t *p;

        KASSERT(NULL == arg);
        KASSERT(NULL != buf);

#if defined(__VFS__) && defined(__GETCWD__)
        iprintf(&buf, &size, "%5s %-13s %-18s %-s\n", "PID", "NAME", "PARENT", "CWD");
#else
        iprintf(&buf, &size, "%5s %-13s %-s\n", "PID", "NAME", "PARENT");
#endif

        list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                char parent[64];
                if (NULL != p->p_pproc) {
                        snprintf(parent, sizeof(parent),
                                 "%3i (%s)", p->p_pproc->p_pid, p->p_pproc->p_comm);
                } else {
                        snprintf(parent, sizeof(parent), "  -");
                }

#if defined(__VFS__) && defined(__GETCWD__)
                if (NULL != p->p_cwd) {
                        char cwd[256];
                        lookup_dirpath(p->p_cwd, cwd, sizeof(cwd));
                        iprintf(&buf, &size, " %3i  %-13s %-18s %-s\n",
                                p->p_pid, p->p_comm, parent, cwd);
                } else {
                        iprintf(&buf, &size, " %3i  %-13s %-18s -\n",
                                p->p_pid, p->p_comm, parent);
                }
#else
                iprintf(&buf, &size, " %3i  %-13s %-s\n",
                        p->p_pid, p->p_comm, parent);
#endif
        } list_iterate_end();
        return size;
}
