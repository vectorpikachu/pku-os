#include "lib/kernel/hash.h"
#include "lib/kernel/list.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "vm/page.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"


/** A lock that tries to handle the synchronization problem in frame.c */
static struct lock frame_lock;

/** A hash map that: `frame_table: Physical Address -> Frame Table Entry`.
 
    As the FTE records the physcial address, we can easily
    find the physcial address of a FTE.

    However, the opposite is difficult, and thus we use this
    hash map to do this.

    We need a `hash_hash_func` and a `hash_less_func`.
 */
static struct hash frame_table;

/** The frame list and the clock handle is to
    implement the Clock Algorithm that approximates LRU.
 */
static struct list frame_list;
/** The frame list and the clock handle is to
    implement the Clock Algorithm that approximates LRU.
 */
static struct list_elem *clock_hand;

/** The helper function of `frame_table`.
    Since physical address are bytes. Using `hash_bytes` here.
    See the Sample hash functions of [hash.h]. 
 */
static unsigned
frame_hash_func (const struct hash_elem *e, void *aux)
{
  struct frame_table_entry *fte = hash_entry (e, 
                                              struct frame_table_entry, 
                                              frame_elem);
  return hash_bytes (&fte->frame, sizeof (fte->frame));
}

/** The helper function of `frame_table`.
    Simply compare the physicall address.
 */
static bool
frame_less_func (const struct hash_elem *a,
                 const struct hash_elem *b,
                 void *aux)
{
  struct frame_table_entry *fte_a = hash_entry (a, 
                                                struct frame_table_entry, 
                                                frame_elem);
  struct frame_table_entry *fte_b = hash_entry (b, 
                                                struct frame_table_entry, 
                                                frame_elem);
  return fte_a->frame < fte_b->frame;
}

struct frame_table_entry *
frame_to_evict (uint32_t *pagedir);

/** Initialize the lock and the hash map here. */
void
frame_init (void)
{
  lock_init (&frame_lock);
  hash_init (&frame_table, frame_hash_func, frame_less_func, NULL);
  list_init (&frame_list);
  clock_hand = NULL;
}

/** Allocating a frame.

    The frames used for user pages should be obtained from the "user pool," 
    by calling `palloc_get_page(PAL_USER)`.

    Hint: Why here are flags? Because when we want to replace
    the `palloc_get_page` in process.c, we find the parameters
    are more convenient.

    Currently not regarding of eviction.
 */
void *
frame_alloc (enum palloc_flags flags, void *user_page)
{
  lock_acquire (&frame_lock);
  void *frame = palloc_get_page (PAL_USER | flags);
  if (frame == NULL)
  {
    /* Try to evict a page to get in. */
    struct frame_table_entry *frame_evict = 
      frame_to_evict (thread_current ()->pagedir);

    ASSERT (frame_evict != NULL && frame_evict->rel_thread != NULL);
    
    /* Mark this page as not present. */
    pagedir_clear_page (frame_evict->rel_thread->pagedir,
                        frame_evict->user_page);
    /* Whether this page is dirty. */
    bool dirty = pagedir_is_dirty (frame_evict->rel_thread->pagedir,
                                   frame_evict->user_page)
              || pagedir_is_dirty (frame_evict->rel_thread->pagedir,
                                   frame_evict->frame);
    

    /* Swap this evicted page out. */
    uint32_t st_index = swap_out (frame_evict->frame);
    if (st_index == (uint32_t)-1)
    {
      PANIC ("swap full - cannot evict frame %p for user page %p\n",
            frame_evict->frame, frame_evict->user_page);
    }

    sup_page_table_set_page_swap (frame_evict->rel_thread->sup_pt,
                                  frame_evict->user_page,
                                  st_index);
    sup_page_table_set_dirty (frame_evict->rel_thread->sup_pt,
                              frame_evict->user_page, dirty);
    frame_free_without_lock (frame_evict->frame);
    frame = palloc_get_page (PAL_USER | flags);

    ASSERT (frame != NULL);
  }
  
  size_t fte_size = sizeof (struct frame_table_entry);
  struct frame_table_entry *fte;
  fte = (struct frame_table_entry *)malloc (fte_size);
  if (fte == NULL)
  {
    lock_release (&frame_lock);
    return NULL;
  }
  fte->user_page = user_page;

  /** Pintos maps kernel virtual memory directly to physical memory: 
      the first page of kernel virtual memory is mapped to 
      the first frame of physical memory, 
      the second page to the second frame, and so on. 
      Thus, frames can be accessed through kernel virtual memory.
      
      `uintptr_t vtop (void *va)` returns
      the physical address corresponding to `va`.*/
  fte->frame = frame;

  fte->rel_thread = thread_current ();
  fte->pinning = true; /* Don't evict this frame. */
  fte->cnt = 0;

  /** Now insert this frame into the frame table. */
  hash_insert (&frame_table, &fte->frame_elem);
  list_push_back (&frame_list, &fte->fl_elem);
  lock_release (&frame_lock);

  return frame;
}

void
frame_free_without_lock (void *frame)
{
  /** Check the validity of a given frame.
      page-aligned: start on a virtual address evenly divisible by the page size.
      Which means: the offset must be 0.
   */
  ASSERT (pg_ofs (frame) == 0);
  ASSERT (lock_held_by_current_thread (&frame_lock));
  ASSERT (is_kernel_vaddr (frame));

  /** By storing the address of the frame in an entry
      we can use `hash_find` to find the actual entry.
   */
  size_t fte_size = sizeof (struct frame_table_entry);
  struct frame_table_entry *fte;
  fte = (struct frame_table_entry *)malloc (fte_size);
  if (fte == NULL)
    return;
  
  fte->frame = frame;

  struct hash_elem *find_elem = hash_find (&frame_table, &fte->frame_elem);
  free (fte);

  /** Do not find the entry. */
  if (find_elem == NULL)
  {
    /* Should directly panic OS. But not return. */
    PANIC ("Cannot find the frame.");
  }
  
  fte = hash_entry (find_elem, struct frame_table_entry, frame_elem);

  /** Now delete this frame from the frame table. */
  
  hash_delete (&frame_table, &fte->frame_elem);
  list_remove (&fte->fl_elem);
  
  palloc_free_page (fte->frame);
  free(fte);
}


/** Just remove this fte from frame table. See process_exit. */
void
fte_remove (void *frame)
{
  lock_acquire (&frame_lock);
  /** Check the validity of a given frame.
      page-aligned: start on a virtual address evenly divisible by the page size.
      Which means: the offset must be 0.
   */
  ASSERT (pg_ofs (frame) == 0);
  ASSERT (lock_held_by_current_thread (&frame_lock));
  ASSERT (is_kernel_vaddr (frame));

  /** By storing the address of the frame in an entry
      we can use `hash_find` to find the actual entry.
   */
  size_t fte_size = sizeof (struct frame_table_entry);
  struct frame_table_entry *fte;
  fte = (struct frame_table_entry *)malloc (fte_size);
  if (fte == NULL)
    return;
  
  fte->frame = frame;

  struct hash_elem *find_elem = hash_find (&frame_table, &fte->frame_elem);
  free (fte);

  /** Do not find the entry. */
  if (find_elem == NULL)
  {
    /* Should directly panic OS. But not return. */
    PANIC ("Cannot find the frame.");
  }
  
  fte = hash_entry (find_elem, struct frame_table_entry, frame_elem);

  /** Now delete this frame from the frame table. */
  
  hash_delete (&frame_table, &fte->frame_elem);
  list_remove (&fte->fl_elem);
  free(fte);
  lock_release (&frame_lock);
}

/** Free a frame. */
void
frame_free (void *frame)
{
  lock_acquire (&frame_lock);
  frame_free_without_lock (frame);
  lock_release (&frame_lock);
}

/** Choose the frame to be evicted.
    Clock Algo.
  */
struct frame_table_entry *
frame_to_evict (uint32_t *pagedir)
{
  size_t size = list_size (&frame_list);

  /* Even if all use bits set, will eventually loop
     all the way around */
  while (true)
  {
    struct frame_table_entry *fte;
    if (clock_hand == NULL || clock_hand == list_end (&frame_list))
      clock_hand = list_begin (&frame_list);
    else
      clock_hand = list_next (clock_hand);
    fte = list_entry (clock_hand, struct frame_table_entry, fl_elem);
    if (fte->pinning)
    {
      /* This frame is currently pinning. */
      continue;
    }
    if (pagedir_is_accessed (pagedir, fte->user_page))
    {
      pagedir_set_accessed (pagedir, fte->user_page, false);
      continue;
    }

    return fte;
  }
  return NULL;
}

/** Pinning a frame. */
void
frame_pin (void *frame)
{
  lock_acquire (&frame_lock);
  struct frame_table_entry *fte;
  size_t fte_size = sizeof (struct frame_table_entry);
  fte = (struct frame_table_entry *)malloc (fte_size);

  fte->frame = frame;
  struct hash_elem *find_elem = hash_find (&frame_table, &fte->frame_elem);
  if (find_elem == NULL)
  {
    free (fte);
    PANIC ("Cannot find the frame.");
  }
  
  free (fte);
  fte = hash_entry (find_elem, struct frame_table_entry, frame_elem);
  fte->pinning = true;

  lock_release (&frame_lock);
}

/** Umpinning a frame. */
void
frame_unpin (void *frame)
{
  lock_acquire (&frame_lock);
  struct frame_table_entry *fte;
  size_t fte_size = sizeof (struct frame_table_entry);
  fte = (struct frame_table_entry *)malloc (fte_size);

  fte->frame = frame;
  struct hash_elem *find_elem = hash_find (&frame_table, &fte->frame_elem);
  if (find_elem == NULL)
  {
    free (fte);
    PANIC ("Cannot find the frame.");
  }
  
  free (fte);
  fte = hash_entry (find_elem, struct frame_table_entry, frame_elem);
  fte->pinning = false;

  lock_release (&frame_lock);
}