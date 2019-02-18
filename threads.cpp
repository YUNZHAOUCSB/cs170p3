/*
 * CS170 - Operating Systems
 * Project 3 Solution Attempt
 * Author of synchronization parts: Evan Murray, Perm# 8954976
 * Author of original code: TA's of CS170
 */


#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>
#include <sys/time.h>
#include <signal.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <string.h>
#include <queue>
#include <semaphore.h>
#include <atomic>
#include <iostream>


/* 
 * these could go in a .h file but i'm lazy 
 * see comments before functions for detail
 */
void signal_handler(int signo);
void the_nowhere_zone(void);
static int ptr_mangle(int p);

//sync functions
void lock();
void unlock();
void pthread_exit_wrapper();

/* 
 *Timer globals 
 */
static struct timeval tv1,tv2;
static struct itimerval interval_timer = {0}, current_timer = {0}, zero_timer = {0};
static struct sigaction act;


/*
 * Timer macros for more precise time control 
 */

#define PAUSE_TIMER setitimer(ITIMER_REAL,&zero_timer,&current_timer)
#define RESUME_TIMER setitimer(ITIMER_REAL,&current_timer,NULL)
#define START_TIMER current_timer = interval_timer; setitimer(ITIMER_REAL,&current_timer,NULL)
#define STOP_TIMER setitimer(ITIMER_REAL,&zero_timer,NULL)
/* number of ms for timer */
#define INTERVAL 50


//thread statuses
#define READY 1
#define BLOCKED 2
#define EXITED 3

//max value for semaphores
#define SEM_VALUE_MAX 65536
//max value for threads
#define MAX_THREADS 128

/*
 * Thread Control Block definition 
 */
typedef struct {
	/* pthread_t usually typedef as unsigned long int */
	pthread_t id;
	/* jmp_buf usually defined as struct with __jmpbuf internal buffer
	   which holds the 6 registers for saving and restoring state */
	jmp_buf jb;
	/* stack pointer for thread; for main thread, this will be NULL */	
	char *stack;

	//changed from original code
    //for thread synchronization
    int status;
    void *return_value;
    sem_t sem_synch;
	//end change

} tcb_t;

/*
 * Semaphore struct definition
 */
typedef struct {
    int value;
    std::queue<tcb_t*> *wait_q;
    bool init = false;
    std::atomic_flag lock_stream = ATOMIC_FLAG_INIT;
} semaphore;


/* 
 * Globals for thread scheduling and control 
 */

/* queue for pool thread, easy for round robin */
static std::queue<tcb_t*> thread_pool;  //CHANGED TO QUEUE OF POINTERS, changed all operations to be pointer ops
/* keep separate handle for main thread */
static tcb_t* main_tcb; //CHANGED TO POINTER
static tcb_t* garbage_collector; //CHANGED TO POINTER

/* for assigning id to threads; main implicitly has 0 */
static unsigned long id_counter = 1; 
/* we initialize in pthread_create only once */
static int has_initialized = 0;

//added this to see if all threads have exited
int num_threads_exited = 0;


/*
 * init()
 *
 * Initialize thread subsystem and scheduler
 * only called once, when first initializing timer/thread subsystem, etc... 
 */
void init() {
	/* on signal, call signal_handler function */
	act.sa_handler = signal_handler;
	/* set necessary signal flags; in our case, we want to make sure that we intercept
	   signals even when we're inside the signal_handler function (again, see man page(s)) */
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_NODEFER;

	/* register sigaction when SIGALRM signal comes in; shouldn't fail, but just in case
	   we'll catch the error  */
	if(sigaction(SIGALRM, &act, NULL) == -1) {
		perror("Unable to catch SIGALRM");
		exit(1);
	}

	/* set timer in seconds */
	interval_timer.it_value.tv_sec = INTERVAL/1000;
	/* set timer in microseconds */
	interval_timer.it_value.tv_usec = (INTERVAL*1000) % 1000000;
	/* next timer should use the same time interval */
	interval_timer.it_interval = interval_timer.it_value;
	/* create thread control buffer for main thread, set as current active tcb */
	main_tcb = (tcb_t*)malloc(sizeof(tcb_t));
	main_tcb->id = 0;
	main_tcb->stack = NULL;
    //changed from original code
    // set status to ready
    main_tcb->status = READY;
    sem_init(&(main_tcb->sem_synch), 0, 1);
	//end change

	/* front of thread_pool is the active thread */
	thread_pool.push(main_tcb);

	/* set up garbage collector */
	garbage_collector = (tcb_t*)malloc(sizeof(tcb_t));
	garbage_collector->id = 128;
	garbage_collector->stack = (char *) malloc (32767);
	/* initialize jump buf structure to be 0, just in case there's garbage */
	memset(&garbage_collector->jb,0,sizeof(garbage_collector->jb));
	/* the jmp buffer has a stored signal mask; zero it out just in case */
	sigemptyset(&garbage_collector->jb->__saved_mask);

	/* garbage collector 'lives' in the_nowhere_zone */
	garbage_collector->jb->__jmpbuf[4] = ptr_mangle((uintptr_t)(garbage_collector->stack+32759));
	garbage_collector->jb->__jmpbuf[5] = ptr_mangle((uintptr_t)the_nowhere_zone);
	/* Initialize timer and wait for first sigalarm to go off */
	START_TIMER;
	pause();	
}



/* 
 * pthread_create()
 * 
 * create a new thread and return 0 if successful.
 * also initializes thread subsystem & scheduler on
 * first invocation 
 */
int pthread_create(pthread_t *restrict_thread, const pthread_attr_t *restrict_attr,
                   void *(*start_routine)(void*), void *restrict_arg) {
	/* set up thread subsystem and timer */
	if(!has_initialized) {
		has_initialized = 1;
		init();
	}

	/* pause timer while creating thread */
    PAUSE_TIMER;

	/* create thread control block for new thread
	   restrict_thread is basically the thread id 
	   which main will have access to */
	tcb_t *tmp_tcb;
	tmp_tcb = (tcb_t*)malloc(sizeof(tcb_t));
	tmp_tcb->id = id_counter++;
	*restrict_thread = tmp_tcb->id;
	/* simulate function call by pushing arguments and return address to the stack
	   remember the stack grows down, and that threads should implicitly return to
	   pthread_exit after done with start_routine */

	tmp_tcb->stack = (char *) malloc (32767);

	*(int*)(tmp_tcb->stack+32763) = (int)restrict_arg;
	*(int*)(tmp_tcb->stack+32759) = (int)pthread_exit_wrapper;
	
	/* initialize jump buf structure to be 0, just in case there's garbage */
	memset(&tmp_tcb->jb,0,sizeof(tmp_tcb->jb));
	/* the jmp buffer has a stored signal mask; zero it out just in case */
	sigemptyset(&tmp_tcb->jb->__saved_mask);
	
	/* modify the stack pointer and instruction pointer for this thread's
	   jmp buffer. don't forget to mangle! */
	tmp_tcb->jb->__jmpbuf[4] = ptr_mangle((uintptr_t)(tmp_tcb->stack+32759));
	tmp_tcb->jb->__jmpbuf[5] = ptr_mangle((uintptr_t)start_routine);

    //changed from original code
    // set status to ready
    tmp_tcb->status = READY;
    sem_init(&(tmp_tcb->sem_synch), 0, 1);
	//end change

	/* new thread is ready to be scheduled! */
	thread_pool.push(tmp_tcb);

    printf("THREAD CREATED\n");
    /* resume timer */
    RESUME_TIMER;

    return 0;	
}



/* 
 * pthread_self()
 *
 * just return the current thread's id
 * undefined if thread has not yet been created
 * (e.g., main thread before setting up thread subsystem) 
 */
pthread_t pthread_self(void) {
	if(thread_pool.size() == 0) {
		return 0;
	} else {
		return (pthread_t)thread_pool.front()->id;
	}
}



/* 
 * pthread_exit()
 *
 * pthread_exit gets returned to from start_routine
 * here, we should clean up thread (and exit if no more threads) 
 */
void pthread_exit(void *value_ptr) {
	printf("Thread Exiting.\n");
	/* just exit if not yet initialized */
	if(has_initialized == 0) {
		exit(0);
	}

	//changed from original code
    //Does this collect pointer right?
    lock();
    thread_pool.front()->status = EXITED;
    thread_pool.front()->return_value = value_ptr; //TODO: is this right?
	printf("Signaling semaphore...\n");
    sem_post(&(thread_pool.front()->sem_synch));
	num_threads_exited++; //increment because thread has exited
    unlock();
	//wait until exit code has been collected to clean this thread up
    pause();
	//end change

    STOP_TIMER;
	if(thread_pool.front()->id == 0) {
        /* if its the main thread, still keep a reference to it
           we'll longjmp here when all other threads are done */
        main_tcb = thread_pool.front();
        if(setjmp(main_tcb->jb)) {
            /* garbage collector's stack should be freed by OS upon exit;
               We'll free anyways, for completeness */
            free((void*) garbage_collector->stack);
            exit(0);
        }
    }

    /* Jump to garbage collector stack frame to free memory and scheduler another thread.
       Since we're currently "living" on this thread's stack frame, deleting it while we're
       on it would be undefined behavior */
    longjmp(garbage_collector->jb,1);
}


/* 
 * signal_handler()
 * 
 * called when SIGALRM goes off from timer 
 */
void signal_handler(int signo) {
	printf("SIGNAL\n");
	/* if no other thread, just return */
	if(thread_pool.size() <= 1) {
		return;
	}
	
	/* Time to schedule another thread! Use setjmp to save this thread's context
	   on direct invocation, setjmp returns 0. if jumped to from longjmp, returns
	   non-zero value. */
	if(setjmp(thread_pool.front()->jb) == 0) {
		/* switch threads */
		// changed from original code
		if (num_threads_exited == thread_pool.size()) {
			//all threads have exited, none are blocked
			//set all their statuses to ready so they can be cleaned up
			std::queue< tcb_t* > thread_pool_copy = thread_pool;
			while (thread_pool_copy.size() > 0) {
				thread_pool_copy.front()->status = READY;
				thread_pool_copy.pop();
			}
			num_threads_exited = -1;
		}
        //if thread is blocked then find next thread that isn't blocked or exited
        do {
            thread_pool.push(thread_pool.front());
            thread_pool.pop();
        } while (thread_pool.front()->status == BLOCKED || thread_pool.front()->status == EXITED);
		//end change

		/* resume scheduler and GOOOOOOOOOO */
		longjmp(thread_pool.front()->jb,1);
	}

	/* resume execution after being longjmped to */
	return;
}


/* 
 * the_nowhere_zone()
 * 
 * used as a temporary holding space to safely clean up threads.
 * also acts as a pseudo-scheduler by scheduling the next thread manually
 */
void the_nowhere_zone(void) {
	/* free stack memory of exiting thread 
	   Note: if this is main thread, we're OK since
	   free(NULL) works */ 
	free((void*) thread_pool.front()->stack);
	thread_pool.front()->stack = NULL;

	//changed from original code
    //free the semaphore
    sem_destroy(&(thread_pool.front()->sem_synch));
	//decrement because thread will no longer be apart of queue
	num_threads_exited--;
	//end change

	/* Don't schedule the thread anymore */
	thread_pool.pop();

	/* If the last thread just exited, jump to main_tcb and exit.
	   Otherwise, start timer again and jump to next thread*/
	if(thread_pool.size() == 0) {
		longjmp(main_tcb->jb,1);
	} else {

        //changed from original code
        //makes sure next thread were long jumping to isnt blocked or exited
        while (thread_pool.front()->status == BLOCKED || thread_pool.front()->status == EXITED) {
            thread_pool.push(thread_pool.front());
            thread_pool.pop();
        }
		//end change

		START_TIMER;
        longjmp(thread_pool.front()->jb,1);
	}
}

/* 
 * ptr_mangle()
 *
 * ptr mangle magic; for security reasons 
 */
int ptr_mangle(int p)
{
    unsigned int ret;
    __asm__(" movl %1, %%eax;\n"
        " xorl %%gs:0x18, %%eax;"
        " roll $0x9, %%eax;"
        " movl %%eax, %0;"
    : "=r"(ret)
    : "r"(p)
    : "%eax"
    );
    return ret;
}

/*
 * SYNCHRONIZATION FUNCTIONS
 */

void lock() {
    sigprocmask(SIG_BLOCK, &act.sa_mask, NULL);
}

void unlock() {
    sigprocmask(SIG_UNBLOCK, &act.sa_mask, NULL);
}

void pthread_exit_wrapper (){
    unsigned  int res;
    asm("movl %%eax ,  %0\n":"=r "(res));
    pthread_exit((void *)  res) ;
}

int pthread_join(pthread_t thread, void **value_ptr) {
    lock();

    //get thread from threadID if it is created
	std::queue<tcb_t*> thread_pool_copy = thread_pool;
	tcb_t *temp = thread_pool_copy.front();
	while (temp->id != thread && thread_pool_copy.size() != 0) {
		thread_pool_copy.pop();
		temp = thread_pool_copy.front();
	}

    unlock();

    //wait until the thread parameter has exited and its return value is stored
    sem_wait(&(temp->sem_synch));

	//std::cout << "thread return value: " << temp->return_value << std::endl;

    if (value_ptr != NULL)
        *value_ptr = temp->return_value;

    //change status to ready so pthread_exit can now clean it up
    temp->status = READY;

    //return to the running thread's code
    return 1;
}

int sem_init(sem_t *sem, int pshared, unsigned value) {
    semaphore *temp;
	temp = (semaphore*)malloc(sizeof(semaphore));
    temp->value = value;
    temp->init = true;
    temp->wait_q = new std::queue<tcb_t*>;
    sem->__align = (long int)temp;
}

int sem_destroy(sem_t *sem) {
	semaphore *sem_struct = (semaphore*) sem->__align;
    if (sem_struct->init) {
        //free(sem_struct->init->wait_q);
		//printf("freeing sem struct\n");
        free(sem_struct);
		//printf("freeing semaphore\n");
        //free(sem);
        return 1; // 1 is successful
    }
    else {
        //undefined behavior
        return -1;// -1 is unsuccessful
    }
}


int sem_wait(sem_t *sem) {
	semaphore* sem_struct = (semaphore*) sem->__align;

	//disable interrupts
	lock();

	//decrement value
	sem_struct->value--;

	//if value is >0 just enable interrupts and return
    if (sem_struct->value > 0) {
		unlock();
        return 1;
    }
	//else if value is zero, then need to wait

	//add process to queue
	thread_pool.front()->status = BLOCKED;
	(sem_struct->wait_q)->push(thread_pool.front());

	//atomic function = TRUE
	sem_struct->lock_stream.test_and_set;

	//enable interrupts
	unlock();

	//wait
	printf("semaphore is waiting...\n");
	while (!sem_struct->lock_stream.test_and_set());
	printf("post received by waiting semaphore\n");

	//return
	return 1;
}

int sem_post(sem_t *sem) {
	semaphore* sem_struct = (semaphore*) (sem->__align);
	
	//disable interrupts
	lock();

	//increment value
	sem_struct->value++;

	//if value was zero, then unblock item from queue
    if (sem_struct->value == 1) {

		//pop thread from front of wait q and set its status to ready
		tcb_t *temp = sem_struct->wait_q->front();
		sem_struct->wait_q->pop();
        temp->status = READY;

		//clear the semaphores lock stream
		sem_struct->lock_stream.clear();

		//value must remain at zero
		sem_struct->value--;

		//context switch --> Hoare semantics
		unlock();

		pause(); // should work as next thread will always occur before this one
		// think about the correctness of this some more
    }
	else {
		//value > 0 originally, just enable interrupts
		unlock();
	}
	return 1;
}

/*
 * I don't know why CLion shows an error for the copy queues, even though they work
 *
 * FINISHED: all within the threading library itself
 * - test lock
 * - test unlock
 * - test sem_init
 * - test sem_post
 * - test sem_destroy
 *
 * TODO:
 * - test all functions outside of threading library
 * - test sem_wait
 * - test pthread_join
 *
 */



