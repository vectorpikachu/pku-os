#ifndef VM_FRAME_H
#define VM_FRAME_H

/** Frame table.
    
    The frame table contains one entry for each frame 
    that contains a user page.

    Each entry in the frame table contains a pointer to the page, 
    if any, that currently occupies it, and other data of your choice.

    The frame is actually the data structures that records
    the allocation of the physical pages.
 */

#include "lib/kernel/hash.h"
#include "lib/kernel/list.h"

#include "threads/palloc.h"
#include "threads/thread.h"

/** The Frame Table Entry.
    It contains a user page (a pointer to the page).
    It records the physcial address.

    However, this requires us to maintain a hash map
    that maps the physical address to this entry!
 */
struct frame_table_entry
  {
    void *frame;                    /** Physical frame. */
    struct hash_elem frame_elem;    /** The hash element. See [frame.c] */
    void *user_page;                /** Pointer to the user page. */

    struct thread *rel_thread;      /** The related thread. */
    struct list_elem fl_elem;       /** The list element to form a frame list. */

    bool pinning;                   /** The pinning value: whether should be evicted. */
  };


void frame_init (void);
void *frame_alloc (enum palloc_flags flags, void *user_page);
void frame_free (void *frame);

void frame_free_without_lock (void *frame);

void frame_pin (void *frame);
void frame_unpin (void *frame);

#endif