#include "csc369_thread.h"

#define _GNU_SOURCE 1  /* To pick up REG_RIP */

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <ucontext.h>


#include <assert.h>
#include <sys/time.h>

//#define DEBUG_USE_VALGRIND // uncomment to debug with valgrind
#ifdef DEBUG_USE_VALGRIND
#include <valgrind/valgrind.h>
#endif

#include "csc369_interrupts.h"
//****************************************************************************
// Private Definitions
//****************************************************************************
//#define DEBUG_PRINTF
#ifdef DEBUG_PRINTF
#define MYPRINTF(a) 	printf a
static int gContinue = 0;	
#else
#define MYPRINTF(a)
#endif

typedef enum
{
  CSC369_THREAD_FREE = 0,			/* Initial state*/
  CSC369_THREAD_READY = 1,			/* Ready state */
  CSC369_THREAD_RUNNING = 2,		/* Running state */
  CSC369_THREAD_ZOMBIE = 3,			/* Thread killed */
  CSC369_THREAD_BLOCKED = 4,		/* Blocking threads */
} CSC369_ThreadState;

/**
 * The Thread Control Block.
 */
typedef struct
{
  Tid   	 				id;					/* tcb id*/
  CSC369_ThreadState 		thread_state;		/* states */
  /**
   * The thread context.
   */
  ucontext_t 				user_level_context; /* context */
  /**
   * What code the thread exited with.
   */
  int exit_code;

  /**
   * The queue of threads that are waiting on this thread to finish.
   */
  CSC369_WaitQueue  *join_threads;
  void 			    *next;		  
  void 			    *prev;
} TCB;

/**
 * A wait queue.
 */
typedef struct csc369_wait_queue_t
{
  TCB* head;
} CSC369_WaitQueue;

//**************************************************************************************************
// Private Global Variables (Library State)
//**************************************************************************************************
/**
 * All possible threads have their control blocks stored contiguously in memory.
 */
static TCB gThreadTotal[CSC369_MAX_THREADS];  /* tcbs for all threads */
static TCB *gThreadRunningHead = NULL;		  /* pointer to current running thread */
/**
 * Threads that are ready to run in FIFO order.
 */
static CSC369_WaitQueue ready_threads; 

/**
 * Threads that need to be cleaned up.
 */
static CSC369_WaitQueue zombie_threads; 

//**************************************************************************************************
// Helper Functions
//**************************************************************************************************
void
Queue_Init(CSC369_WaitQueue* queue)
{
	assert(queue != NULL);
	queue->head = NULL;
}

int
Queue_IsEmpty(CSC369_WaitQueue* queue)
{
	assert(queue != NULL);
	if (queue->head == NULL) 
		return 1;
	return 0; 
}
/*
	Put at the end of the queue
*/
void 
Queue_Enqueue(CSC369_WaitQueue* queue, TCB* tcb)
{
	assert(queue != NULL);
	CSC369_InterruptsState const prev_state = CSC369_InterruptsDisable();
	if (queue->head == NULL) {   /* head */
		queue->head = tcb;
		tcb->next = NULL;
		tcb->prev = NULL;
		CSC369_InterruptsSet(prev_state);
		return;
	}
	TCB *temp = queue->head;
	int queue_max = 0; /* Max length, i.e. number of the tcbs */
	do {
		if (temp->next != NULL) {
			temp = temp->next;
		} else {
			temp->next = tcb;
			tcb->next = NULL;
			tcb->prev = temp;
			break;
		}
		queue_max++;
	} while(queue_max < CSC369_MAX_THREADS);
	CSC369_InterruptsSet(prev_state);
	return;
}

/* Get the head of the queue */
TCB* 
Queue_Dequeue(CSC369_WaitQueue* queue)
{
	assert(queue != NULL);
	CSC369_InterruptsState const prev_state = CSC369_InterruptsDisable();
	TCB *temp = queue->head;
	if (temp == NULL) 
		return NULL;
	if (temp->next == NULL) {  /* head */
		queue->head = NULL;
		temp->prev = NULL;
		CSC369_InterruptsSet(prev_state);
		return temp;
	} else {
		TCB *next = (TCB *)temp->next;
		if (next != NULL) {
			next->prev = NULL;
		}
		queue->head = next;	
		temp->next = NULL;
		temp->prev = NULL;
		CSC369_InterruptsSet(prev_state);
		return temp;
	}
}

void 
Queue_Remove(CSC369_WaitQueue* queue, TCB* tcb)
{
	assert(queue != NULL);
	CSC369_InterruptsState const prev_state = CSC369_InterruptsDisable();
	TCB *temp = queue->head;
	if (temp == NULL) {
		CSC369_InterruptsSet(prev_state);
		return;
	}
	int queue_max = 0; /* Max length, i.e. number of the tcbs */
	do {
		if (temp == tcb) {  
			if(temp->prev == NULL) {  /* head */
				TCB *next = (TCB *)temp->next;
				if (next != NULL) 
					next->prev = NULL;
				queue->head = next;
			} else {	/* middle */
				TCB *prev = (TCB *)temp->prev;
				TCB *next = (TCB *)temp->next;
				prev->next = next;
				if (next != NULL)
					next->prev = prev;
			}
			temp->prev = NULL;
			temp->next = NULL;
			CSC369_InterruptsSet(prev_state);
			return;
		} 
		/* */
		if(temp->next != NULL) {
			temp = temp->next;
		} else {
			break;
		}
		queue_max++;
	} while(queue_max < CSC369_MAX_THREADS);
	CSC369_InterruptsSet(prev_state);
	return; 
}

static TCB *findNewTcb()
{
	CSC369_InterruptsState const prev_state = CSC369_InterruptsDisable();
	for(int i = 0;i < CSC369_MAX_THREADS;i++) {
		if (gThreadTotal[i].thread_state == CSC369_THREAD_FREE){
			gThreadTotal[i].thread_state = CSC369_THREAD_READY;
			gThreadTotal[i].exit_code = CSC369_EXIT_CODE_NORMAL;
			CSC369_InterruptsSet(prev_state);
			return &gThreadTotal[i];
		}
	}
	CSC369_InterruptsSet(prev_state);
	return NULL;
}

void 
MyThreadStub(void (*f)(void *), void *arg)
{
	CSC369_InterruptsSet(CSC369_INTERRUPTS_ENABLED);
	f(arg);
	CSC369_ThreadExit(CSC369_EXIT_CODE_NORMAL);
}

/*
	replace makecontext
*/
int my_makecontext(ucontext_t *ucp,void(*func)(void),int arg_num,void *arg_1,void *arg_2)
{
	if(ucp == NULL)
		return -1;
	greg_t *sp;
	sp = (greg_t *) ((uintptr_t) ucp->uc_stack.ss_sp + ucp->uc_stack.ss_size);
	sp -= 1;
	sp = (greg_t *) ((((uintptr_t) sp) & -16L) - 8);
	int link = 1;
	ucp->uc_mcontext.gregs[REG_RIP] = (uintptr_t)func;
	ucp->uc_mcontext.gregs[REG_RBX] = (uintptr_t)&sp[link];
	ucp->uc_mcontext.gregs[REG_RSP] = (uintptr_t)sp;
	if (arg_num == 2) {
		ucp->uc_mcontext.gregs[REG_RDI] = (uintptr_t)arg_1;
		ucp->uc_mcontext.gregs[REG_RSI] = (uintptr_t)arg_2;
	}
	return 0;
}

void my_on_exit()
{
	CSC369_InterruptsDisable();
	for (int i = 0;i < CSC369_MAX_THREADS;i++) {
		if (gThreadTotal[i].user_level_context.uc_stack.ss_sp != NULL) {
			#ifdef DEBUG_USE_VALGRIND
				VALGRIND_STACK_DEREGISTER(gThreadTotal[i].user_level_context.uc_stack.ss_sp);
			#endif
			free(gThreadTotal[i].user_level_context.uc_stack.ss_sp);
			gThreadTotal[i].user_level_context.uc_stack.ss_sp = NULL;
		}
		if (gThreadTotal[i].join_threads != NULL) {
			while (Queue_IsEmpty(gThreadTotal[i].join_threads) == 0) {
				Queue_Dequeue(gThreadTotal[i].join_threads);
			}
			CSC369_WaitQueueDestroy(gThreadTotal[i].join_threads);
			gThreadTotal[i].join_threads = NULL;
		}
	}
}


//**************************************************************************************************
// thread.h Functions
//**************************************************************************************************
int
CSC369_ThreadInit(void)
{
	/* Initialize */
	for (int i = 0;i < CSC369_MAX_THREADS;i++) {
		gThreadTotal[i].id = i;
		gThreadTotal[i].thread_state = CSC369_THREAD_FREE;
		memset(&gThreadTotal[i].user_level_context,0,sizeof(ucontext_t));
		gThreadTotal[i].user_level_context.uc_stack.ss_sp = NULL;
		gThreadTotal[i].user_level_context.uc_link = NULL;
		gThreadTotal[i].user_level_context.uc_stack.ss_size = 0;
		gThreadTotal[i].next = NULL;
		gThreadTotal[i].prev = NULL;
		gThreadTotal[i].join_threads = NULL;
		gThreadTotal[i].exit_code = 0;
	}
	/*
		Create the 0th thread and set current running thread to #0
	*/
	gThreadTotal[0].thread_state = CSC369_THREAD_RUNNING; 
	gThreadTotal[0].join_threads = CSC369_WaitQueueCreate();
	Queue_Init(gThreadTotal[0].join_threads);
	void *stack = (void *)malloc(CSC369_THREAD_STACK_SIZE);
	memset(stack,0,CSC369_THREAD_STACK_SIZE);
	gThreadTotal[0].user_level_context.uc_stack.ss_sp = stack;
	gThreadTotal[0].user_level_context.uc_stack.ss_size = CSC369_THREAD_STACK_SIZE;
	gThreadTotal[0].user_level_context.uc_stack.ss_flags = 0;
	#ifdef DEBUG_USE_VALGRIND
		VALGRIND_STACK_REGISTER(stack,stack+CSC369_THREAD_STACK_SIZE);
	#endif
	gThreadRunningHead = &gThreadTotal[0];
	
	Queue_Init(&ready_threads);
	Queue_Init(&zombie_threads);
	atexit(my_on_exit);
	return 0;
}


Tid
CSC369_ThreadId(void)
{
	CSC369_InterruptsState const prev_state = CSC369_InterruptsDisable();
	if (gThreadRunningHead == NULL) {
		CSC369_InterruptsSet(prev_state);
		return CSC369_ERROR_THREAD_BAD;
	}
	Tid tid = gThreadRunningHead->id;
	CSC369_InterruptsSet(prev_state);
	return tid;
}
Tid
CSC369_ThreadCreate(void (*f)(void*), void* arg)
{
	CSC369_InterruptsState const prev_state = CSC369_InterruptsDisable();
	/* Find a new TCB */
	TCB *tcb_ptr = findNewTcb();
	if (tcb_ptr == NULL) {
		CSC369_InterruptsSet(prev_state);
		return CSC369_ERROR_SYS_THREAD;
	}
	if (tcb_ptr->user_level_context.uc_stack.ss_sp != NULL) {
		#ifdef DEBUG_USE_VALGRIND
				VALGRIND_STACK_DEREGISTER(tcb_ptr->user_level_context.uc_stack.ss_sp);
		#endif
		free(tcb_ptr->user_level_context.uc_stack.ss_sp);
		tcb_ptr->user_level_context.uc_stack.ss_sp = NULL;
	}
	/* Dynamically allocate a stack */
	char *stack = calloc(1,CSC369_THREAD_STACK_SIZE);
	if (stack == NULL) {
		CSC369_InterruptsSet(prev_state);
		return CSC369_ERROR_SYS_MEM;
	}
	#ifdef DEBUG_USE_VALGRIND
		VALGRIND_STACK_REGISTER(stack,stack+CSC369_THREAD_STACK_SIZE);
	#endif
	if (tcb_ptr->join_threads != NULL) {
		while(Queue_IsEmpty(tcb_ptr->join_threads) == 0) {
			Queue_Dequeue(tcb_ptr->join_threads);
		}
		CSC369_WaitQueueDestroy(tcb_ptr->join_threads);
		tcb_ptr->join_threads = NULL;
	}
	tcb_ptr->join_threads = CSC369_WaitQueueCreate();
	Queue_Init(tcb_ptr->join_threads);
	
	tcb_ptr->user_level_context.uc_stack.ss_sp = stack;
	tcb_ptr->user_level_context.uc_stack.ss_size = CSC369_THREAD_STACK_SIZE;
	tcb_ptr->user_level_context.uc_stack.ss_flags = 0;
	tcb_ptr->user_level_context.uc_link = NULL;
	getcontext(&tcb_ptr->user_level_context);
	my_makecontext(&tcb_ptr->user_level_context,(void*)MyThreadStub,2,f,arg);
    /* Put at the end of the ready queue */
	Queue_Enqueue(&ready_threads,tcb_ptr);
	Tid ret_tid = tcb_ptr->id;
	MYPRINTF(("CSC369_ThreadCreate:%d Queue_IsEmpty:%d gContinue:%d\n",ret_tid,Queue_IsEmpty(&ready_threads),gContinue++));
    CSC369_InterruptsSet(prev_state);
	return ret_tid;
}

void
CSC369_ThreadExit(int exit_code)
{
	CSC369_InterruptsState const prev_state = CSC369_InterruptsDisable();
    Tid tid = CSC369_ThreadId();
	if (tid >=0 && tid < CSC369_MAX_THREADS) {
		gThreadTotal[tid].next = NULL;
		gThreadTotal[tid].prev = NULL;
		gThreadTotal[tid].thread_state = CSC369_THREAD_FREE;
		gThreadTotal[tid].exit_code = exit_code;
		gThreadRunningHead = NULL;
	}
	/* Check if any thread is waiting on this one */
	if(Queue_IsEmpty(gThreadTotal[tid].join_threads)) {
		TCB *first_ready = Queue_Dequeue(&ready_threads);
		if (first_ready == NULL) { 
			CSC369_InterruptsSet(prev_state);
			exit(exit_code);
		}
		gThreadRunningHead = first_ready;
		gThreadRunningHead->thread_state = CSC369_THREAD_RUNNING;
		gThreadRunningHead->user_level_context.uc_link = NULL;
		TCB *tempRun = gThreadRunningHead;
		MYPRINTF(("CSC369_ThreadExit:finished %d to %d exit_code:%d gContinue:%d\n",tid,tempRun->id,exit_code,gContinue++));
		setcontext(&tempRun->user_level_context);
	} else {
		/* Wake up waiting threads */
		TCB *first_ready = Queue_Dequeue(gThreadTotal[tid].join_threads);
		gThreadRunningHead = first_ready;
		gThreadRunningHead->thread_state = CSC369_THREAD_RUNNING;
		gThreadRunningHead->user_level_context.uc_link = NULL;
		while (Queue_IsEmpty(gThreadTotal[tid].join_threads) == 0) {
			TCB *ready_from_join = Queue_Dequeue(gThreadTotal[tid].join_threads);
			ready_from_join->thread_state = CSC369_THREAD_READY;
			Queue_Enqueue(&ready_threads,ready_from_join);
		}
		setcontext(&first_ready->user_level_context);
	}
	/* Can't get here */
}

Tid
CSC369_ThreadKill(Tid tid)
{
	CSC369_InterruptsState const prev_state = CSC369_InterruptsDisable();
	if (tid == CSC369_ThreadId()) {
		CSC369_InterruptsSet(prev_state);
		return CSC369_ERROR_THREAD_BAD;
	}
	/* check if thread with tid is ready and valid*/
	if (tid >= 0 && tid < CSC369_MAX_THREADS) { 
		MYPRINTF(("CSC369_ThreadKill gThreadTotal[%d].thread_state:%d gContinue:%d\n",tid,gThreadTotal[tid].thread_state,gContinue++));
		if (gThreadTotal[tid].thread_state == CSC369_THREAD_FREE) {
			CSC369_InterruptsSet(prev_state);
			return CSC369_ERROR_SYS_THREAD;
		}
		/* Can't kill self */
		if (gThreadTotal[tid].thread_state == CSC369_THREAD_RUNNING) {
			CSC369_InterruptsSet(prev_state);
			return CSC369_ERROR_THREAD_BAD;
		}
		if (gThreadTotal[tid].thread_state == CSC369_THREAD_READY) {

		    MYPRINTF(("CSC369_ThreadKill_2 gThreadTotal[%d].thread_state:%d gContinue:%d\n",tid,gThreadTotal[tid].thread_state,gContinue++));
			/* Remove from ready queue */
			Queue_Remove(&ready_threads, &gThreadTotal[tid]);
		    MYPRINTF(("CSC369_ThreadKill_3 gThreadTotal[%d].thread_state:%d gContinue:%d\n",tid,gThreadTotal[tid].thread_state,gContinue++));
			/*
				Wake up all waiting threads.
			*/
			while(Queue_IsEmpty(gThreadTotal[tid].join_threads) == 0) {
				TCB *ready_from_join = Queue_Dequeue(gThreadTotal[tid].join_threads);
				ready_from_join->thread_state = CSC369_THREAD_READY;
				Queue_Enqueue(&ready_threads,ready_from_join);
			}
			MYPRINTF(("CSC369_ThreadKill_4 gThreadTotal[%d].thread_state:%d gContinue:%d\n",tid,gThreadTotal[tid].thread_state,gContinue++));
			/* Put in zombie queue */
			Queue_Enqueue(&zombie_threads, &gThreadTotal[tid]);
			MYPRINTF(("CSC369_ThreadKill_5 gThreadTotal[%d].thread_state:%d gContinue:%d\n",tid,gThreadTotal[tid].thread_state,gContinue++));
			if (gThreadTotal[tid].user_level_context.uc_stack.ss_sp != NULL){
				#ifdef DEBUG_USE_VALGRIND
					VALGRIND_STACK_DEREGISTER(gThreadTotal[tid].user_level_context.uc_stack.ss_sp);
				#endif
				free(gThreadTotal[tid].user_level_context.uc_stack.ss_sp);
				gThreadTotal[tid].user_level_context.uc_stack.ss_sp = NULL;
			}
			gThreadTotal[tid].thread_state = CSC369_THREAD_ZOMBIE;
			gThreadTotal[tid].exit_code = CSC369_EXIT_CODE_KILL;
			CSC369_InterruptsSet(prev_state);
			return tid;
		}
		/* Blocked thread get killed  */
		if (gThreadTotal[tid].thread_state == CSC369_THREAD_BLOCKED) {
			 MYPRINTF(("CSC369_ThreadKill_6 gThreadTotal[%d].thread_state:%d gContinue:%d\n",tid,gThreadTotal[tid].thread_state,gContinue++));
			/* Remove from ready queue */
			for (int k = 0; k < CSC369_MAX_THREADS;k++) {
				if ( gThreadTotal[k].join_threads != NULL) {
					if (Queue_IsEmpty(gThreadTotal[k].join_threads) == 0)
						Queue_Remove(gThreadTotal[k].join_threads, &gThreadTotal[tid]);
				}
			}
			/*
				Wake up all waiting threads.
			*/
			while (Queue_IsEmpty(gThreadTotal[tid].join_threads) == 0) {
				TCB *ready_from_join = Queue_Dequeue(gThreadTotal[tid].join_threads);
				ready_from_join->thread_state = CSC369_THREAD_READY;
				Queue_Enqueue(&ready_threads,ready_from_join);
			}
			
			/* Put in zombie queue */
			Queue_Enqueue(&zombie_threads, &gThreadTotal[tid]);
			if (gThreadTotal[tid].user_level_context.uc_stack.ss_sp != NULL){
				#ifdef DEBUG_USE_VALGRIND
					VALGRIND_STACK_DEREGISTER(gThreadTotal[tid].user_level_context.uc_stack.ss_sp);
				#endif
				free(gThreadTotal[tid].user_level_context.uc_stack.ss_sp);
				gThreadTotal[tid].user_level_context.uc_stack.ss_sp = NULL;
			}
			gThreadTotal[tid].thread_state = CSC369_THREAD_ZOMBIE;
			gThreadTotal[tid].exit_code = CSC369_EXIT_CODE_KILL;
			CSC369_InterruptsSet(prev_state);
			return tid;
		}
	}
	CSC369_InterruptsSet(prev_state);
	return CSC369_ERROR_TID_INVALID;
}


int
CSC369_ThreadYield()
{
	CSC369_InterruptsState const prev_state = CSC369_InterruptsDisable();
	if (Queue_IsEmpty(&ready_threads)) {
		if(gThreadRunningHead == NULL) {
			CSC369_InterruptsSet(prev_state);
			return CSC369_ERROR_OTHER;
		}
		CSC369_InterruptsSet(prev_state);
		return gThreadRunningHead->id;
	}
	/* choose the first thread in the ready queue */
	TCB *first_ready = Queue_Dequeue(&ready_threads);
	first_ready->user_level_context.uc_link = NULL;

	TCB *my_ = gThreadRunningHead;
	/* put current thread in the tail of ready queue */
	Queue_Enqueue(&ready_threads,my_);
	/* saving current running fields to first_ready */
	my_->thread_state = CSC369_THREAD_READY;
	
	gThreadRunningHead = first_ready;
	gThreadRunningHead->thread_state = CSC369_THREAD_RUNNING;
	Tid ret_id = first_ready->id;
	/* context switch */
	assert((my_->user_level_context.uc_stack.ss_sp != NULL));
	assert((first_ready->user_level_context.uc_stack.ss_sp != NULL));
	MYPRINTF(("from %d to %d gContinue:%d\n",my_->id,first_ready->id,gContinue++));
	getcontext(&my_->user_level_context);
	if (my_->thread_state == CSC369_THREAD_READY) {		
		MYPRINTF(("to %d   gContinue:%d\n",first_ready->id,gContinue++));
		setcontext(&first_ready->user_level_context);
	} else { 
		MYPRINTF(("back %d gContinue:%d\n",my_->id,gContinue++));
		CSC369_InterruptsSet(prev_state);
	}
	return ret_id;
}

int
CSC369_ThreadYieldTo(Tid tid)
{
	CSC369_InterruptsState const prev_state = CSC369_InterruptsDisable();
	if (tid >=0 && tid < CSC369_MAX_THREADS){
		if (gThreadTotal[tid].thread_state == CSC369_THREAD_FREE) {
			CSC369_InterruptsSet(prev_state);
			return CSC369_ERROR_THREAD_BAD;
		}
		if (gThreadTotal[tid].thread_state == CSC369_THREAD_RUNNING) {
			CSC369_InterruptsSet(prev_state);
			return tid;
		}
		if (gThreadTotal[tid].thread_state == CSC369_THREAD_READY) {
			/* Loop to find the thread with tid and remove it from ready queue*/
			Queue_Remove(&ready_threads, &gThreadTotal[tid]);
			/* put current thread in the tail of ready queue */
			Queue_Enqueue(&ready_threads,gThreadRunningHead);
		
			/* context switch */
			TCB *hit_one = &gThreadTotal[tid];
			TCB *my_ = gThreadRunningHead;
			my_->thread_state = CSC369_THREAD_READY;
			hit_one->user_level_context.uc_link = NULL;
			gThreadRunningHead = hit_one;
			gThreadRunningHead->thread_state = CSC369_THREAD_RUNNING;
			getcontext(&my_->user_level_context);
			if(my_->thread_state == CSC369_THREAD_READY) {
				MYPRINTF(("CSC369_ThreadYieldTo[%d] to %d   gContinue:%d\n",tid,hit_one->id,gContinue++));
				setcontext(&hit_one->user_level_context);
			} else {
				MYPRINTF(("CSC369_ThreadYieldTo[%d] back %d gContinue:%d\n",tid,my_->id,gContinue++));
				CSC369_InterruptsSet(prev_state);
			}
			return tid;
		} else if(gThreadTotal[tid].thread_state == CSC369_THREAD_ZOMBIE) {
			CSC369_InterruptsSet(prev_state);
			return CSC369_ERROR_THREAD_BAD;
		} else if(gThreadTotal[tid].thread_state == CSC369_THREAD_BLOCKED) {
			CSC369_InterruptsSet(prev_state);
			return CSC369_ERROR_THREAD_BAD;
		}
	}
	CSC369_InterruptsSet(prev_state);
	return CSC369_ERROR_TID_INVALID;
}


//****************************************************************************
// New Assignment 2 Definitions - Task 2
//****************************************************************************
CSC369_WaitQueue*
CSC369_WaitQueueCreate(void)
{
	void *queue = (void *)calloc(1,sizeof(CSC369_WaitQueue));
	if(queue != NULL) {
		memset(queue,0,sizeof(CSC369_WaitQueue));
		#ifdef DEBUG_USE_VALGRIND
			VALGRIND_STACK_REGISTER(queue,queue+sizeof(CSC369_WaitQueue));
		#endif
		return (CSC369_WaitQueue*)queue;
	}
	return NULL;
}

int
CSC369_WaitQueueDestroy(CSC369_WaitQueue* queue)
{
	if (queue == NULL)
		return CSC369_ERROR_OTHER;
	CSC369_InterruptsState const prev_state = CSC369_InterruptsDisable();
	/* Destroy only when empty */
	if (Queue_IsEmpty(queue)==0) {
		CSC369_InterruptsSet(prev_state);
		return CSC369_ERROR_OTHER;
	}
	#ifdef DEBUG_USE_VALGRIND
		VALGRIND_STACK_DEREGISTER(queue);
	#endif
	free(queue);
	CSC369_InterruptsSet(prev_state);
	return 0;
}

void
CSC369_ThreadSpin(int duration)
{
  struct timeval start, end, diff;

  int ret = gettimeofday(&start, NULL);
  assert(!ret);

  while (1) {
    ret = gettimeofday(&end, NULL);
    assert(!ret);
    timersub(&end, &start, &diff);

    if ((diff.tv_sec * 1000000 + diff.tv_usec) >= duration) {
      return;
    }
  }
}

int
CSC369_ThreadSleep(CSC369_WaitQueue* queue)
{
	assert(queue != NULL);
	CSC369_InterruptsState const prev_state = CSC369_InterruptsDisable();
	if (Queue_IsEmpty(&ready_threads)) {
		CSC369_InterruptsSet(prev_state);
		return CSC369_ERROR_SYS_THREAD;
	}
	Tid running = CSC369_ThreadId();
	gThreadTotal[running].thread_state = CSC369_THREAD_BLOCKED;
	Queue_Enqueue(queue, &gThreadTotal[running]);
	/* choose the first thread in the ready queue */
	TCB *first_ready = Queue_Dequeue(&ready_threads);
	/* saving current running fields to first_ready */
	TCB *my_ = &gThreadTotal[running];
	my_->thread_state = CSC369_THREAD_BLOCKED;
	first_ready->user_level_context.uc_link = NULL;
	gThreadRunningHead = first_ready;
	gThreadRunningHead->thread_state = CSC369_THREAD_RUNNING;
	MYPRINTF(("CSC369_ThreadSleep from %d to %d gContinue:%d\n",my_->id,first_ready->id,gContinue++));
	getcontext(&my_->user_level_context);
	if(my_->thread_state == CSC369_THREAD_BLOCKED) {
		MYPRINTF(("CSC369_ThreadSleep to %d   gContinue:%d\n",first_ready->id,gContinue++));
		setcontext(&first_ready->user_level_context);
	}
	MYPRINTF(("CSC369_ThreadSleep back %d   gContinue:%d\n",my_->id,gContinue++));
	CSC369_InterruptsSet(prev_state);
    return first_ready->id;
}

int
CSC369_ThreadWakeNext(CSC369_WaitQueue* queue)
{
  assert(queue != NULL);
  CSC369_InterruptsState const prev_state = CSC369_InterruptsDisable();
  if(Queue_IsEmpty(queue)){
  	CSC369_InterruptsSet(prev_state);
  	return 0;
  }
  TCB *wakeup = Queue_Dequeue(queue);
  wakeup->thread_state = CSC369_THREAD_READY;
  Queue_Enqueue(&ready_threads,wakeup);
  CSC369_InterruptsSet(prev_state);
  return 1;
}

int
CSC369_ThreadWakeAll(CSC369_WaitQueue* queue)
{
	assert(queue != NULL);
	TCB* wakeup = NULL;
	int wake_num = 0;
	CSC369_InterruptsState const prev_state = CSC369_InterruptsDisable();
	do {
		if(Queue_IsEmpty(queue))
			break;
		wakeup = Queue_Dequeue(queue);
		wakeup->thread_state = CSC369_THREAD_READY;
		Queue_Enqueue(&ready_threads,wakeup);
		wake_num++;
	} while(wake_num < CSC369_MAX_THREADS);
	CSC369_InterruptsSet(prev_state);
	return wake_num;
}

//****************************************************************************
// New Assignment 2 Definitions - Task 3
//****************************************************************************
int
CSC369_ThreadJoin(Tid tid, int* exit_code)
{
	CSC369_InterruptsState const prev_state = CSC369_InterruptsDisable();
	/* Can't join self */
	if (tid == CSC369_ThreadId()) {
		CSC369_InterruptsSet(prev_state);
		return CSC369_ERROR_THREAD_BAD;
	}
	/* Check state of the thread */
	if (tid >=0 && tid < CSC369_MAX_THREADS) {

		MYPRINTF(("CSC369_ThreadJoin gThreadTotal[%d].thread_state:%d gContinue:%d\n",tid,gThreadTotal[tid].thread_state,gContinue++));
		/* Not exited */
		if((gThreadTotal[tid].thread_state == CSC369_THREAD_READY) ||
			(gThreadTotal[tid].thread_state == CSC369_THREAD_BLOCKED)) {
			/* 
				Put current thread into waiting queue
			*/
			Tid runing = CSC369_ThreadId();
			gThreadTotal[runing].thread_state = CSC369_THREAD_BLOCKED;
			Queue_Enqueue(gThreadTotal[tid].join_threads,&gThreadTotal[runing]);
			
			/* Wake up waiting threads */
			TCB *first_ready = Queue_Dequeue(&ready_threads);
			gThreadRunningHead = first_ready;
			gThreadRunningHead->thread_state = CSC369_THREAD_RUNNING;
			gThreadRunningHead->user_level_context.uc_link = NULL;

			
			getcontext(&gThreadTotal[runing].user_level_context);;
			if (gThreadTotal[runing].thread_state == CSC369_THREAD_BLOCKED) {
				MYPRINTF(("CSC369_ThreadJoin from %d to %d gContinue:%d\n",gThreadTotal[runing].id,gThreadRunningHead->id,gContinue++));
				setcontext(&gThreadRunningHead->user_level_context);
			}
			*exit_code = gThreadTotal[tid].exit_code;
			CSC369_InterruptsSet(prev_state);
			return tid;
		} else if (gThreadTotal[tid].thread_state == CSC369_THREAD_FREE){
			*exit_code = gThreadTotal[tid].exit_code;
			CSC369_InterruptsSet(prev_state);
			return CSC369_ERROR_SYS_THREAD;
		} else if(gThreadTotal[tid].thread_state == CSC369_THREAD_ZOMBIE){
			*exit_code = gThreadTotal[tid].exit_code;
			CSC369_InterruptsSet(prev_state);
			return CSC369_ERROR_SYS_THREAD;
		}
	}
	CSC369_InterruptsSet(prev_state);
    return CSC369_ERROR_TID_INVALID;
}
