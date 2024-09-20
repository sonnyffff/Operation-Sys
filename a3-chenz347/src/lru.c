#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "sim.h"
#include "pagetable.h"
#include "pagetable_generic.h"
#define MYPRINTF(a)  //printf a
static list_head g_list_head;
/* Page to evict is chosen using the accurate LRU algorithm.
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */
int
lru_evict(void)
{
  /* Remove the first one from list */
  struct list_entry *del_pos = g_list_head.head.next;
  list_del(del_pos);
  struct frame * frame_ptr = container_of(del_pos,struct frame,double_list);
  #if 1
  return frame_ptr->frame_id;
  #else
  for (size_t i = 0; i < memsize; i++) {
    if(&coremap[i] == frame_ptr)
      return i;
  }
  return 0;
  #endif
}

/* This function is called on each access to a page to update any information
 * needed by the LRU algorithm.
 * Input: The page table entry for the page that is being accessed.
 */
void
lru_ref(int frame)
{
  MYPRINTF(("lru_ref in:%d\n",frame));
  if((coremap[frame].double_list.next != NULL) &&
     (coremap[frame].double_list.prev != NULL))
    list_del(&coremap[frame].double_list);
  list_add_tail(&g_list_head,&coremap[frame].double_list);
}

/* Initialize any data structures needed for this replacement algorithm. */
void
lru_init(void)
{
  MYPRINTF(("lru_ref lru_init\n"));
  /* linked list initialization */
  list_init(&g_list_head);
  for (size_t i = 0; i < memsize; i++) {
    list_entry_init(&coremap[i].double_list);
    coremap[i].frame_id = i;
  }
}

/* Cleanup any data structures created in lru_init(). */
void
lru_cleanup(void)
{
}
