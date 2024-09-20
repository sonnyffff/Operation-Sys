#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "sim.h"
#include "pagetable.h"
#include "pagetable_generic.h"
#include "list.h"
#define MYPRINTF(a)  //printf a
static list_head  g_list_head_clock;
struct list_entry *g_list_hand_entry_clock;
static int mem_full  = 0;
/* Page to evict is chosen using the CLOCK algorithm.
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */
int
clock_evict(void)
{
  MYPRINTF(("clock_evict mem_full:%d\n",mem_full));
  struct frame * frame_ptr = NULL;
  while(1) {
    frame_ptr = container_of(g_list_hand_entry_clock,struct frame,double_list);
    MYPRINTF(("clock_evict :%d %d\n",frame_ptr->frame_id,get_referenced(frame_ptr->pte)));
    g_list_hand_entry_clock = g_list_hand_entry_clock->next;
    if (g_list_hand_entry_clock == &g_list_head_clock.head)
         g_list_hand_entry_clock = g_list_hand_entry_clock->next;

    if (get_referenced(frame_ptr->pte) == true) {
      set_referenced(frame_ptr->pte,false);
    } else {
      MYPRINTF(("clock_evict return:%d\n",frame_ptr->frame_id));
      return frame_ptr->frame_id;
    }
  }
}

/* This function is called on each access to a page to update any information
 * needed by the CLOCK algorithm.
 * Input: The page table entry for the page that is being accessed.
 */
void
clock_ref(int frame)
{
  MYPRINTF(("clock_ref in  frame:%d mem_full:%d\n",frame,mem_full));
  set_referenced(coremap[frame].pte,true); /* set to ref */
  if(mem_full == 0) {
    /* move to next empty memory */
     struct frame * frame_ptr = container_of(g_list_hand_entry_clock,struct frame,double_list);
     if(frame_ptr->frame_id == frame) { /* check */
      g_list_hand_entry_clock = g_list_hand_entry_clock->next;
      if(g_list_hand_entry_clock == &g_list_head_clock.head){
        mem_full = 1;
        g_list_hand_entry_clock = g_list_hand_entry_clock->next;
      }
      }
  } else {
    # if 0
    struct frame * frame_ptr = container_of(g_list_hand_entry_clock,struct frame,double_list);
    if(frame_ptr->frame_id == frame) { /* check */
      while(1) {
        frame_ptr = container_of(g_list_hand_entry_clock,struct frame,double_list);
        MYPRINTF(("memid %d ref:%d\n",frame_ptr->frame_id,get_referenced(frame_ptr->pte)));
        if (get_referenced(frame_ptr->pte) == true) {
          set_referenced(frame_ptr->pte,false);
          g_list_hand_entry_clock = g_list_hand_entry_clock->next;
          if (g_list_hand_entry_clock == &g_list_head_clock.head)
            g_list_hand_entry_clock = g_list_hand_entry_clock->next;
        } else {
          break;
        }
      }
    }
    # endif
  }
  MYPRINTF(("clock_ref out frame:%d mem_full:%d\n",frame,mem_full));
}

/* Initialize any data structures needed for this replacement algorithm. */
void
clock_init(void)
{
  MYPRINTF(("clock_init in,memsize:%d\n",(unsigned int )memsize));
  /* list initialization */  
  list_init(&g_list_head_clock);
  for (size_t i = 0; i < memsize; i++) {
    coremap[i].in_use = false;
    coremap[i].pte = NULL;
    coremap[i].frame_id = i;
    list_entry_init(&coremap[i].double_list);
    list_add_tail(&g_list_head_clock,&coremap[i].double_list);
  }
  g_list_hand_entry_clock = g_list_head_clock.head.next; /* skip head */
  mem_full = 0;
  MYPRINTF(("clock_init out\n"));
}

/* Cleanup any data structures created in clock_init(). */
void
clock_cleanup(void)
{}
