#include "types.h"
#include "globals.h"
#include "kernel.h"

#include "util/gdb.h"
#include "util/init.h"
#include "util/debug.h"
#include "util/string.h"
#include "util/printf.h"

#include "mm/mm.h"
#include "mm/page.h"
#include "mm/pagetable.h"
#include "mm/pframe.h"

#include "vm/vmmap.h"
#include "vm/shadow.h"
#include "vm/anon.h"

#include "main/acpi.h"
#include "main/apic.h"
#include "main/interrupt.h"
#include "main/cpuid.h"
#include "main/gdt.h"

#include "proc/sched.h"
#include "proc/proc.h"
#include "proc/kthread.h"

#include "drivers/dev.h"
#include "drivers/blockdev.h"
#include "drivers/tty/virtterm.h"

#include "api/exec.h"
#include "api/syscall.h"

#include "fs/vfs.h"
#include "fs/vnode.h"
#include "fs/vfs_syscall.h"
#include "fs/fcntl.h"
#include "fs/stat.h"

#include "test/kshell/kshell.h"
#define CREATE_RES 0
#define DESTROY_RES 0
#define DEADLOCK 0
#define PRO_CON 0
#define OTHER 0
kmutex_t mutex;
kmutex_t pcmutex;
kmutex_t mutex1;
kmutex_t mutex2;

char p[20];
int filled = 0;
int pro = 0;
int con = 0;

GDB_DEFINE_HOOK(boot)
GDB_DEFINE_HOOK(initialized)
GDB_DEFINE_HOOK(shutdown)

static void      *bootstrap(int arg1, void *arg2);
static void      *idleproc_run(int arg1, void *arg2);
static kthread_t *initproc_create(void);
static void      *initproc_run(int arg1, void *arg2);
static void       hard_shutdown(void);

/*static void *thread1(int arg1, void *arg2);
static void *thread2(int arg1, void * arg2);
static void *thread3(int arg1, void *arg2);
static void *thread4(int arg1, void * arg2);*/

static context_t bootstrap_context;
void test1(){}
void test2(){}
/**
 * This is the first real C function ever called. It performs a lot of
 * hardware-specific initialization, then creates a pseudo-context to
 * execute the bootstrap function in.
 */
void
kmain()
{
	GDB_CALL_HOOK(boot);

        dbg_init();
        dbgq(DBG_CORE, "Kernel binary:\n");
  
        dbgq(DBG_CORE, "  text: 0x%p-0x%p\n", &kernel_start_text, &kernel_end_text);
        dbgq(DBG_CORE, "  data: 0x%p-0x%p\n", &kernel_start_data, &kernel_end_data);
        dbgq(DBG_CORE, "  bss:  0x%p-0x%p\n", &kernel_start_bss, &kernel_end_bss);

        page_init();

        pt_init();
        slab_init();
        pframe_init();

        acpi_init();
        apic_init();
        intr_init();

        gdt_init();
	/* initialize slab allocators */
#ifdef __VM__
        anon_init();
        shadow_init();
#endif
	vmmap_init();
        proc_init();
        kthread_init();
	
#ifdef __DRIVERS__
        bytedev_init();
        blockdev_init();
#endif

        void *bstack = page_alloc();
        pagedir_t *bpdir = pt_get();
        KASSERT(NULL != bstack && "Ran out of memory while booting.");
	context_setup(&bootstrap_context, bootstrap, 0, NULL, bstack, PAGE_SIZE, bpdir);
	context_make_active(&bootstrap_context);
	panic("\nReturned to kmain()!!!\n");
}

/**
 * This function is called from kmain, however it is not running in a
 * thread context yet. It should create the idle process which will
 * start executing idleproc_run() in a real thread context.  To start
 * executing in the new process's context call context_make_active(),
 * passing in the appropriate context. This function should _NOT_
 * return.
 *
 * Note: Don't forget to set curproc and curthr appropriately.
 *
 * @param arg1 the first argument (unused)
 * @param arg2 the second argument (unused)
 */

/* Author : Aditya Jain
* Description : created a idle processs.
* create a thread for the idle process and then set the current thread and current process.
* Called the idleproc_run() using context_make_active()
*/
static void *
bootstrap(int arg1, void *arg2)
{
	proc_t *proc_idle;        
	pt_template_init();
        proc_idle = proc_create("Idle Process");
	kthread_t* thread_idle=kthread_create(proc_idle, idleproc_run, arg1, arg2);
	curproc=proc_idle;
	KASSERT(NULL != curproc); /* make sure that the "idle" process has been created successfully */
	KASSERT(PID_IDLE == curproc->p_pid); /* make sure that what has been created is the "idle" process */
	curthr=thread_idle;
	KASSERT(NULL != curthr); /* make sure that the thread for the "idle" process has been created successfully */
	context_make_active(&(thread_idle->kt_ctx));
	panic("weenix returned to bootstrap()!!! BAD!!!\n");
	return NULL;
}

/**
 * Once we're inside of idleproc_run(), we are executing in the context of the
 * first process-- a real context, so we can finally begin running
 * meaningful code.
 *
 * This is the body of process 0. It should initialize all that we didn't
 * already initialize in kmain(), launch the init process (initproc_run),
 * wait for the init process to exit, then halt the machine.
 *
 * @param arg1 the first argument (unused)
 * @param arg2 the second argument (unused)
 */

static void *
idleproc_run(int arg1, void *arg2)
{	
	int status;
        pid_t child;

        /* create init proc */
        kthread_t *initthr = initproc_create();

        init_call_all();
        GDB_CALL_HOOK(initialized);
	
	/* Create other kernel threads (in order) */

#ifdef __VFS__
        /* Once you have VFS remember to set the current working directory
         * of the idle and init processes */

        /* Here you need to make the null, zero, and tty devices using mknod */
        /* You can't do this until you have VFS, check the include/drivers/dev.h
         * file for macros with the device ID's you will need to pass to mknod */
#endif

        /* Finally, enable interrupts (we want to make sure interrupts
         * are enabled AFTER all drivers are initialized) */
        intr_enable();

        /* Run initproc */
	
        sched_make_runnable(initthr);
	
        /* Now wait for it */
        child = do_waitpid(-1, 0, &status);

	KASSERT(PID_INIT == child);
	
#ifdef __MTP__
        kthread_reapd_shutdown();
#endif


#ifdef __VFS__
        /* Shutdown the vfs: */
        dbg_print("weenix: vfs shutdown...\n");
        vput(curproc->p_cwd);
        if (vfs_shutdown())
                panic("vfs shutdown FAILED!!\n");

#endif

        /* Shutdown the pframe system */
#ifdef __S5FS__
        pframe_shutdown();
#endif

        dbg_print("\nweenix: halted cleanly!\n");
        GDB_CALL_HOOK(shutdown);
        hard_shutdown();
        return NULL;
}

/**
 * This function, called by the idle process (within 'idleproc_run'), creates the
 * process commonly refered to as the "init" process, which should have PID 1.
 *
 * The init process should contain a thread which begins execution in
 * initproc_run().
 *
 * @return a pointer to a newly created thread which will execute
 * initproc_run when it begins executing
 */

/*
 * AUTHOR : ADITYA JAIN
 * DESCRIPTION : CREATED INIT PROCESS.
 * MADE A THREAD for init Process
 * called init_proc_run using context_make_active 
 * RETURNED THE THREAD MADE
*/
static kthread_t *
initproc_create(void)
{
	proc_t *proc_init= proc_create("Init Process");
	KASSERT(NULL != proc_init);
	KASSERT(PID_INIT == proc_init->p_pid);
	kthread_t *init_thread=kthread_create(proc_init, initproc_run, 0, NULL);
	KASSERT(NULL != init_thread);	
	return init_thread;
}

/**
 * The init thread's function changes depending on how far along your Weenix is
 * developed. Before VM/FI, you'll probably just want to have this run whatever
 * tests you've written (possibly in a new process). After VM/FI, you'll just
 * exec "/bin/init".
 *
 * Both arguments are unused.
 *
 * @param arg1 the first argument (unused)
 * @param arg2 the second argument (unused)
 */

void * thread1(int arg1, void * arg2)
{
  int i=0;
 	
	dbg(DBG_TEST,"Thread 1 of Process 1\n");
	kmutex_init(&mutex);
  	
	dbg(DBG_TEST,"Thread 1 of Process 1 trying to lock mutex\n");
	kmutex_lock(&mutex);
  	dbg(DBG_TEST,"Thread 1 of Process 1 locked mutex\n");
    	sched_switch();
	kmutex_unlock(&mutex);
  	dbg(DBG_TEST,"Thread 1 of Process 1 trying to unlock mutex\n");
	dbg(DBG_TEST,"Thread 1 of Process 1 unlocked mutex\n");
       
   return (void *)1;
}

void * thread2(int arg1, void * arg2)
{

	dbg(DBG_TEST,"Thread 2 of Process 1\n");
  	dbg(DBG_TEST,"Thread 2 of Process 1 trying to lock mutex, should be put on sleep \n");
	kmutex_lock(&mutex);
  	dbg(DBG_TEST,"Thread 2 of Process 1 locked mutex\n");
    	
	dbg(DBG_TEST,"Thread 2 of Process 1 trying to unlock mutex\n");
	kmutex_unlock(&mutex);
  	dbg(DBG_TEST,"Thread 2 of Process 1 unlocked mutex\n");
       
return (void *)1;
}

void * produce(int arg1, void * arg2)
{
	int i=0;	
	dbg(DBG_TEST,"Producer Thread Started\n");     
	while(i<10)
	{
		kmutex_lock(&pcmutex);		
		if(filled!=10)
		{
			if(pro==10)
			   pro=0;
			p[pro]=(int)'A'+i;	
			dbg(DBG_TEST,"\nProducer produced %c Queue Length %d\n",p[pro],filled+1);
			i++;		
			pro++;
			filled++;
		}
		kmutex_unlock(&pcmutex);
		if(i%2==0)		
		sched_switch();		
	}	
        dbg(DBG_TEST,"Producer Left\n");
	return (void *)1;
}

void * consume(int arg1, void * arg2)
{
	int i=0;	
	dbg(DBG_TEST,"Consumer Thread Started\n");     
	while(i<10)
	{
		kmutex_lock(&pcmutex);		
		if(filled!=0)		
		{
			if(con==10)
			   con=0;
			dbg(DBG_TEST,"\nConsumer consumed %c Queue Length %d\n",p[con],filled-1);
			i++;
			con++;
			filled--;
		}		
		kmutex_unlock(&pcmutex);
		sched_switch();		
	}	
        dbg(DBG_TEST,"Consumer Left\n");
	return (void *)1;}

/*Deadlock Code */

void *threadFunc1(int somearg1,void *somearg2)
{
    dbg(DBG_TEST,"Thread A: Locking mutex 1\n");
    kmutex_lock(&mutex1);
    dbg(DBG_TEST,"Thread A: Lock secured, mutex 1\n");
    dbg(DBG_TEST,"Switching to another Thread ... \n");
    sched_switch();
    dbg(DBG_TEST,"Switched back to Thread A \n");
    dbg(DBG_TEST,"Thread A: Locking mutex 2\n");
    kmutex_lock(&mutex2);   
    dbg(DBG_TEST,"Thread A: Lock secured, mutex 2\n");
    kmutex_unlock(&mutex2);
    dbg(DBG_TEST,"Thread A: Lock released, mutex 2\n");
    kmutex_unlock(&mutex1);
    dbg(DBG_TEST,"Thread A: Lock released, mutex 1\n");
    return (void *)1;
}

void *threadFunc2(int somearg1,void *somearg2)
{
    dbg(DBG_TEST,"Thread B: Locking mutex 2\n");
    kmutex_lock(&mutex2);
    dbg(DBG_TEST,"Thread B: Lock secured, mutex 2\n");
    dbg(DBG_TEST,"Switching to another Thread ... \n");
    sched_switch();
    dbg(DBG_TEST,"Switched back to Thread B \n");
    dbg(DBG_TEST,"Thread B: Locking mutex 1\n");
    kmutex_lock(&mutex1);   
    dbg(DBG_TEST,"Thread B: Lock secured, mutex 1\n");
    kmutex_unlock(&mutex2);
    dbg(DBG_TEST,"Thread B: Lock released, mutex 2\n");
    kmutex_unlock(&mutex1);
    dbg(DBG_TEST,"Thread B: Lock released, mutex 1\n");
    return (void *)1;
}

void* info_foo(int arg1, void *arg2)
{
	dbg(DBG_TEST,"In Thread of Process having PID : %d\n",curproc->p_pid);
	return (void *)1;
}

void* sp_foo(int arg1, void *arg2)
{
	int i=0;
	dbg(DBG_TEST,"Thread %d Started of Special Test Process\n",arg1);     
	while(i<(5*arg1))
	{
		dbg(DBG_TEST,"Switching to another Thread ... \n");
    		sched_switch();
		dbg(DBG_TEST,"Switched back to Thread %d\n",arg1);
		i++;
	}
	dbg(DBG_TEST,"Thread %d Completed of Special Test Process\n",arg1);     
	return (void *)1;
}


static void *
initproc_run(int arg1, void *arg2)
{
 
 if(CREATE_RES)
 {
	proc_t *p1, *p2;
	kthread_t *k1, *k2;
	p1 = proc_create("Custom Process 1");
	k1 = kthread_create(p1,info_foo, 0, NULL);
	p2 = proc_create("Custom Process 2");
	k2 = kthread_create(p2,info_foo, 0, NULL);
	sched_make_runnable(k1);
	sched_make_runnable(k2);
	if(DESTROY_RES)
	{
		do_waitpid( -1 , 0 , NULL );
		do_waitpid( -1 , 0 , NULL );	  		
	}
 }
 if(DEADLOCK)
 {
	proc_t *p3;
	kmutex_init(&mutex1);
	kmutex_init(&mutex2);
	p3 = proc_create("Deadlock_Process");
	kthread_t *k3,*k4;
	k3 = kthread_create(p3, threadFunc1, 0, NULL);
	k4 = kthread_create(p3, threadFunc2, 0, NULL);
	sched_make_runnable(k3);
	sched_make_runnable(k4);
	do_waitpid( -1 , 0 , NULL );	
 }
 if(PRO_CON)
 {
	int i = 0;
	proc_t *p4, *p5;
	kthread_t *k5,*k6;
	kmutex_init(&pcmutex);
	p4 = proc_create("Producer Process");	
	p5 = proc_create("Consumer Process");
	k5 = kthread_create(p4, produce, 0, NULL);
	k6 = kthread_create(p5, consume, 1, NULL);
	sched_make_runnable(k5);
	sched_make_runnable(k6); 
	do_waitpid( -1 , 0 , NULL );
	do_waitpid( -1 , 0 , NULL );
 }
 if(OTHER)
 {
	proc_t *p6;
	kthread_t *k7,*k8;
	kmutex_init(&pcmutex);
	p6 = proc_create("Special Test Process");	
	k7 = kthread_create(p6, sp_foo, 1, NULL);
	k8 = kthread_create(p6, sp_foo, 2, NULL);
	sched_make_runnable(k7);
	sched_make_runnable(k8); 
	do_waitpid( -1 , 0 , NULL );
 }
	/* Add some commands to the shell */
	kshell_add_command("test1", test1, "tests something...");
	kshell_add_command("test2", test2, "tests something else...");
	/* Create a kshell on a tty */
	int err = 0;
	kshell_t *ksh = kshell_create(0);
	KASSERT(ksh && "did not create a kernel shell as expected");
	/* Run kshell commands until user exits */
	while ((err = kshell_execute_next(ksh)) > 0);
	KASSERT(err == 0 && "kernel shell exited with an error\n");
	kshell_destroy(ksh);
	return NULL;
}



/**
 * Clears all interrupts and halts, meaning that we will never run
 * again.
 */

static void
hard_shutdown()
{
#ifdef __DRIVERS__
        vt_print_shutdown();
#endif
        __asm__ volatile("cli; hlt");
}

