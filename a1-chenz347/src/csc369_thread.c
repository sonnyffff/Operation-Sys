#include "csc369_thread.h"

#define _GNU_SOURCE 1  /* To pick up REG_RIP */

#include <stdarg.h>
#include <stdint.h>
#include <ucontext.h>

#include <stdlib.h>
//****************************************************************************
// Private Definitions
//****************************************************************************
typedef enum
{
  CSC369_THREAD_BLOCKED=0,
  CSC369_THREAD_RUNNING=1,
  CSC369_THREAD_READY=2,
} CSC369_ThreadState;

/**
 * The Thread Control Block.
 */
typedef struct
{
  Tid   	 				id;					/* tcb id*/
  CSC369_ThreadState 		thread_state;		/* states */
  ucontext_t 				user_level_context; /* context */
  void 						*next;				/* */
} TCB;

//**************************************************************************************************
// Private Global Variables (Library State)
//**************************************************************************************************
static TCB gThreadTotal[CSC369_MAX_THREADS];  /* tcbs for all threads */
static TCB *gThreadRunningHead = NULL;		  /* pointer to current running thread */
static TCB *gThreadReadyHead = NULL;		  /* pointer to current head of ready queue*/
//**************************************************************************************************
// Helper Functions
//**************************************************************************************************
static TCB *findNewTcb()
{
	for(int i = 0;i < CSC369_MAX_THREADS;i++) {
		if(gThreadTotal[i].thread_state == CSC369_THREAD_BLOCKED)
			return &gThreadTotal[i];
	}
	return NULL;
}

void 
MyThreadStub(void (*f)(void *), void *arg)
{
	f(arg);
	CSC369_ThreadExit();
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
	
	ucp->uc_mcontext.gregs[REG_RDI] = (uintptr_t)arg_1;
	ucp->uc_mcontext.gregs[REG_RSI] = (uintptr_t)arg_2;
	return 0;
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
		gThreadTotal[i].next = NULL;
		gThreadTotal[i].thread_state = CSC369_THREAD_BLOCKED;
		gThreadTotal[i].user_level_context.uc_stack.ss_sp = NULL;
	}
	/*
		Create the 0th thread and set current running thread to #0
	*/
	gThreadTotal[0].thread_state = CSC369_THREAD_RUNNING; 
	gThreadRunningHead = &gThreadTotal[0];
	gThreadReadyHead = NULL;
	return 0;
}

Tid
CSC369_ThreadId(void)
{
	if (gThreadRunningHead == NULL)
		return CSC369_ERROR_THREAD_BAD;
	return gThreadRunningHead->id;
}

/*
	Create a thread and put at the end of the ready queue.
*/
Tid
CSC369_ThreadCreate(void (*f)(void*), void* arg)
{
 	/* Find a new TCB */
	TCB *tcb_ptr = findNewTcb();
	if (tcb_ptr == NULL)
		return CSC369_ERROR_SYS_THREAD;
	if (tcb_ptr->user_level_context.uc_stack.ss_sp != NULL)
		free(tcb_ptr->user_level_context.uc_stack.ss_sp);
	/* Dynamically allocate a stack */
	char *stack = malloc(CSC369_THREAD_STACK_SIZE);
	if (stack == NULL)
		return CSC369_ERROR_SYS_MEM;
	tcb_ptr->user_level_context.uc_stack.ss_sp = stack;
	tcb_ptr->user_level_context.uc_stack.ss_size = CSC369_THREAD_STACK_SIZE;
	tcb_ptr->user_level_context.uc_link = NULL;
	getcontext(&tcb_ptr->user_level_context);
	my_makecontext(&tcb_ptr->user_level_context,(void*)MyThreadStub,2,f,arg);
   /* Put at the end of the ready queue */
	if (gThreadReadyHead == NULL) {
		gThreadReadyHead = tcb_ptr;
		tcb_ptr->next = NULL;
	} else {
		TCB *ready =  gThreadReadyHead;
		for(int i = 0;i < CSC369_MAX_THREADS;i++) {
			if (ready->next != NULL) {
				ready = ready->next;
			} else {
				ready->next = tcb_ptr;
				tcb_ptr->next = NULL;
				break;
			}
		}
	}
	tcb_ptr->thread_state = CSC369_THREAD_READY;
	return tcb_ptr->id;
}

void
CSC369_ThreadExit()
{
	Tid tid = CSC369_ThreadId();
	if (tid >=0 && tid < CSC369_MAX_THREADS) {
		gThreadTotal[tid].next = NULL;
		gThreadTotal[tid].thread_state = CSC369_THREAD_BLOCKED;
		gThreadRunningHead = NULL;
	}
	
	if (gThreadReadyHead == NULL) {
		if (tid >=0 && tid < CSC369_MAX_THREADS) {
			if (gThreadTotal[tid].user_level_context.uc_stack.ss_sp != NULL)
		    	free(gThreadTotal[tid].user_level_context.uc_stack.ss_sp);
		}
		exit(0);
		return;
	}
	/*
		Choose the first ready thread in the ready queue
	*/
	TCB *first_ready = gThreadReadyHead;
	gThreadReadyHead = first_ready->next;
	gThreadRunningHead = first_ready;
	gThreadRunningHead->thread_state = CSC369_THREAD_RUNNING;
	gThreadRunningHead->user_level_context.uc_link = NULL;
	setcontext(&gThreadRunningHead->user_level_context);
	/* Can't get here */
}
/*
	
*/
Tid
CSC369_ThreadKill(Tid tid)
{
	/* check if thread with tid is ready and valid*/
	if (tid >=0 && tid < CSC369_MAX_THREADS) { 
		if (gThreadTotal[tid].thread_state == CSC369_THREAD_BLOCKED)
			return CSC369_ERROR_SYS_THREAD;
		/* Can't kill self */
		if (gThreadTotal[tid].thread_state == CSC369_THREAD_RUNNING) {
			return CSC369_ERROR_THREAD_BAD;
		}
		if (gThreadTotal[tid].thread_state == CSC369_THREAD_READY) {
			/* change the ready queue and remove the killed thread*/
			TCB *prev = gThreadReadyHead;
			TCB *hit_one = prev;
			/* head */
			if (hit_one->id == tid) {
				gThreadReadyHead = hit_one->next;
			} else {
				/* Loop to find the thread with tid and remove it from ready queue*/
				int find_one  = 0;
				hit_one = prev->next;
				for(int i = 0;i < CSC369_MAX_THREADS;i++) {
					/* found */
					if (hit_one->id == tid) {
						prev->next = hit_one->next;
						find_one = 1;
						break;
					} else {
						if (prev->next != NULL) {
							prev = prev->next;
							hit_one = prev->next;
						}
					}
				}
				if (find_one != 1) { /* not found */
					return CSC369_ERROR_SYS_THREAD;
				}
			}
			if (gThreadTotal[tid].user_level_context.uc_stack.ss_sp != NULL)
				free(gThreadTotal[tid].user_level_context.uc_stack.ss_sp);
			gThreadTotal[tid].next = NULL;
			gThreadTotal[tid].thread_state = CSC369_THREAD_BLOCKED;
			return tid;
		}
	}
	return CSC369_ERROR_TID_INVALID;
}

int
CSC369_ThreadYield()
{
	/* Remain unchanged if there is only one thread */
	if (gThreadReadyHead == NULL) {
		if(gThreadRunningHead == NULL)
			return CSC369_ERROR_OTHER;
		return gThreadRunningHead->id;
	}
	/* choose the first thread in the ready queue */
	TCB *first_ready = gThreadReadyHead;
	gThreadReadyHead = first_ready->next;
	
	/* put current thread in the tail of ready queue */
	TCB *ready =  gThreadReadyHead;
	if (ready == NULL) {
		gThreadReadyHead = gThreadRunningHead;
		gThreadReadyHead->next = NULL;
	} else {
		for(int i = 0;i < CSC369_MAX_THREADS;i++) {
			if (ready->next == NULL) {
				ready->next = gThreadRunningHead;
				gThreadRunningHead->next = NULL;
				break;
			} else {
				ready = ready->next;
			}
		}
	}
	/* saving current running fields to first_ready */
	TCB *my_ = gThreadRunningHead;
	my_->thread_state = CSC369_THREAD_READY;
	first_ready->user_level_context.uc_link = &my_->user_level_context;
	gThreadRunningHead = first_ready;
	gThreadRunningHead->thread_state = CSC369_THREAD_RUNNING;
	/* context switch */
	getcontext(&my_->user_level_context);
	if(my_->thread_state == CSC369_THREAD_READY) {
		setcontext(&first_ready->user_level_context);
	}
	return first_ready->id;
}

int
CSC369_ThreadYieldTo(Tid tid)
{
	if (tid >=0 && tid < CSC369_MAX_THREADS) { 
		if (gThreadTotal[tid].thread_state == CSC369_THREAD_BLOCKED)
			return CSC369_ERROR_THREAD_BAD;
		if (gThreadTotal[tid].thread_state == CSC369_THREAD_RUNNING)
			return tid;
		if (gThreadTotal[tid].thread_state == CSC369_THREAD_READY) {
			TCB *prev = gThreadReadyHead;
			TCB *hit_one = prev;
			/* head */
			if (hit_one->id == tid) {
				gThreadReadyHead = hit_one->next;
			} else {
				/* Loop to find the thread with tid and remove it from ready queue*/
				int find_one  = 0;
				hit_one = prev->next;
				for(int i = 0;i < CSC369_MAX_THREADS;i++) {
					/* found */
					if (hit_one->id == tid) {
						prev->next = hit_one->next;
						find_one = 1;
						break;
					} else {
						if (prev->next != NULL) {
							prev = prev->next;
							hit_one = prev->next;
						}
					}
				}
				if (find_one != 1) { /* not found */
					return CSC369_ERROR_SYS_THREAD;
				}
			}

			/* put current thread to the tail of ready queue */
			TCB *ready_2 =  gThreadReadyHead;
			if (ready_2 == NULL) {
				gThreadReadyHead = gThreadRunningHead;
				gThreadReadyHead->next = NULL;
			} else {
				for(int i = 0;i < CSC369_MAX_THREADS;i++) {
					if (ready_2->next == NULL) {
						ready_2->next = gThreadRunningHead;
						gThreadRunningHead->next = NULL;
						break;
					} else {
						ready_2 = ready_2->next;
					}
				}
			}

			/* context switch */
			TCB *my_ = gThreadRunningHead;
			my_->thread_state = CSC369_THREAD_READY;
			hit_one->user_level_context.uc_link = &my_->user_level_context;
			gThreadRunningHead = hit_one;
			gThreadRunningHead->thread_state = CSC369_THREAD_RUNNING;
			getcontext(&my_->user_level_context);
			if(my_->thread_state == CSC369_THREAD_READY) {
				setcontext(&hit_one->user_level_context);
			}
			return tid;
		}
	}
	return CSC369_ERROR_TID_INVALID;
}
