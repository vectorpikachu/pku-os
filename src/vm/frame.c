#include "lib/kernel/hash.h"
#include "vm/frame.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"


/** A lock that tries to handle the syncronization problem in frame table.
    
    Actually the hash map is the frame table.
    It is used to protect it.
 */
static struct lock frame_lock;

/** A hash map that: `frame_table: Physical Address -> Frame Table Entry`.
 
    As the FTE records the physcial address, we can easily
    find the physcial address of a FTE.

    However, the opposite is difficult, and thus we use this
    hash map to do this.

    We need a `hash_hash_func` and a `hash_less_func`.
 */
static struct hash frame_table;


/** The helper function of `frame_table`.
    Since physical address is a byte. Using `hash_bytes` here.
    See the Sample hash functions of [hash.h]. 
 */
static unsigned
frame_hash_func (const struct hash_elem *e, void *aux)
{
  struct frame_table_entry *fte = hash_entry (e, 
                                              struct frame_table_entry, 
                                              frame_elem);
  return hash_bytes (&fte->phy_addr, sizeof (fte->phy_addr));
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
  return fte_a->phy_addr < fte_b->phy_addr;
}

/** Initialize the lock and the hash map here. */
void
frame_init (void)
{
  lock_init (&frame_lock);
  hash_init (&frame_table, frame_hash_func, frame_less_func, NULL);
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
frame_alloc (enum palloc_flags flags)
{
  void *frame = palloc_get_page (PAL_USER | flags);
  if (frame == NULL)
    return NULL;
  
  size_t fte_size = sizeof (struct frame_table_entry);
  struct frame_table_entry *fte;
  fte = (struct frame_table_entry *)malloc (fte_size);
  if (fte == NULL)
    return NULL;
  
  fte->user_page = frame;

  /** Pintos maps kernel virtual memory directly to physical memory: 
      the first page of kernel virtual memory is mapped to 
      the first frame of physical memory, 
      the second page to the second frame, and so on. 
      Thus, frames can be accessed through kernel virtual memory.
      
      `uintptr_t vtop (void *va)` returns
      the physical address corresponding to `va`.*/
  fte->phy_addr = (void *)vtop (frame);

  /** Now insert this frame into the frame table. */
  lock_acquire (&frame_lock);
  hash_insert (&frame_table, &fte->frame_elem);
  lock_release (&frame_lock);

  return frame;
}

/** Free a frame. */
void
frame_free (void *frame)
{
  /** Check the validity of a given frame.
      page-aligned: start on a virtual address evenly divisible by the page size.
      Which means: the offset must be 0.
   */
  if (pg_ofs (frame) == 0)
    return;

  /** By storing the address of the frame in an entry
      we can use `hash_find` to find the actual entry.
   */
  size_t fte_size = sizeof (struct frame_table_entry);
  struct frame_table_entry *fte;
  fte = (struct frame_table_entry *)malloc (fte_size);
  if (fte == NULL)
    return;
  
  fte->phy_addr = (void *)vtop (frame);

  struct hash_elem *find_elem = hash_find (&frame_table, &fte->frame_elem);
  free (fte);

  /** Do not find the entry. */
  if (find_elem == NULL)
    return;
  
  fte = hash_entry (find_elem, struct frame_table_entry, frame_elem);

  /** Now delete this frame from the frame table. */
  lock_acquire (&frame_lock);
  hash_delete (&frame_table, &fte->frame_elem);
  lock_release (&frame_lock);
  
  palloc_free_page (fte->user_page);
  free(fte);
}