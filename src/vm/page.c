#include "lib/kernel/hash.h"
#include "vm/page.h"
#include "userprog/pagedir.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"


/** Helper functions of creating a supplemental page table.

    Use hash_bytes here.
 */
static unsigned
sup_hash_func (const struct hash_elem *elem, void *aux)
{
  struct sup_page_table_entry *sup_pte = 
    hash_entry (elem, struct sup_page_table_entry, sup_elem);
  size_t uaddr_size = sizeof (sup_pte->user_page);
  return hash_bytes (&sup_pte->user_page, uaddr_size);
}


/** Simply compare the address.  */
static bool
sup_less_func (const struct hash_elem *a,
               const struct hash_elem *b,
               void *aux)
{
  struct sup_page_table_entry *sup_pte_a = 
    hash_entry (a, struct sup_page_table_entry, sup_elem);
  struct sup_page_table_entry *sup_pte_b = 
    hash_entry (b, struct sup_page_table_entry, sup_elem);

  return sup_pte_a->user_page < sup_pte_b->user_page;
}

/** Free all the entry. */
static void
sup_action_func (struct hash_elem *elem, void *aux)
{
  struct sup_page_table_entry *sup_pte = 
    hash_entry (elem, struct sup_page_table_entry, sup_elem);
  free (sup_pte);
}

/** Create a supplemental page table. */
struct sup_page_table *
sup_page_table_create (void)
{
  size_t sup_pt_size = sizeof (struct sup_page_table);
  struct sup_page_table *sup_pt;
  sup_pt = (struct sup_page_table *)malloc (sup_pt_size);

  hash_init (&sup_pt->page_map, sup_hash_func, sup_less_func, NULL);
  return sup_pt;
}


/** Destroy the supplemental page table. */
void
sup_page_table_destroy (struct sup_page_table *sup_pt)
{
  if (sup_pt == NULL)
    return;
  
  hash_destroy (&sup_pt->page_map, sup_action_func);
  free (sup_pt);
}


/** Find the corresponding entry of PAGE in supplemental page table SUP_PT. */
struct sup_page_table_entry *
sup_page_table_find (struct sup_page_table *sup_pt, void *page)
{
  struct sup_page_table_entry *sup_pte;
  size_t sup_pte_size = sizeof (struct sup_page_table_entry);
  sup_pte = (struct sup_page_table_entry *)malloc (sup_pte_size);

  sup_pte->user_page = page;

  struct hash_elem *find_elem = hash_find (&sup_pt->page_map, &sup_pte->sup_elem);
  free (sup_pte);
  if (find_elem == NULL)
    return NULL;
  return hash_entry (find_elem, struct sup_page_table_entry, sup_elem);
}

/** Set the page in supplemental page table whose status == ON_FRAME. */
bool
sup_page_table_set_page_frame (struct sup_page_table *sup_pt, 
                               void *user_page, 
                               void *frame)
{
  struct sup_page_table_entry *sup_pte;
  size_t sup_pte_size = sizeof (struct sup_page_table_entry);
  sup_pte = (struct sup_page_table_entry *)malloc (sup_pte_size);

  sup_pte->user_page = user_page;
  sup_pte->status = ON_FRAME;
  sup_pte->frame = frame;
  sup_pte->dirty = false;
  sup_pte->st_index = -1; /* Didn't swap out. */

  struct hash_elem *insert_elem = hash_insert (&sup_pt->page_map, &sup_pte->sup_elem);
  if (insert_elem == NULL)
    return true;
  else
  {
    free (sup_pte);
    return false;
  }
}


/** Set the page in supplemental page table whose status == ALL_ZERO. */
bool
sup_page_table_set_page_zero (struct sup_page_table *sup_pt, 
                              void *user_page)
{
  struct sup_page_table_entry *sup_pte;
  size_t sup_pte_size = sizeof (struct sup_page_table_entry);
  sup_pte = (struct sup_page_table_entry *)malloc (sup_pte_size);

  sup_pte->user_page = user_page;
  sup_pte->status = ALL_ZERO;
  sup_pte->frame = NULL;
  sup_pte->dirty = false;

  struct hash_elem *insert_elem = hash_insert (&sup_pt->page_map, &sup_pte->sup_elem);
  if (insert_elem == NULL)
    return true;
  else
  {
    free (sup_pte);
    return false;
  }
}


/** Set the page in supplemental page table whose status == SWAP_SLOT.
    The problem is: if we want to set this page in SWAP,
    we must find it and swap out it.
 */
bool
sup_page_table_set_page_swap (struct sup_page_table *sup_pt, 
                              void *user_page,
                              uint32_t st_index)
{
  struct sup_page_table_entry *sup_pte;
  sup_pte = sup_page_table_find (sup_pt, user_page);
  if (sup_pte == NULL)
    return false;

  sup_pte->status = SWAP_SLOT;
  sup_pte->frame = NULL;
  sup_pte->st_index = st_index;
  return true;
}

/** Set the page in supplemental page table whose status == SWAP_SLOT. */
bool
sup_page_table_set_page_file (struct sup_page_table *sup_pt,
                              void *user_page,
                              struct file * file,
                              off_t ofs,
                              uint32_t read_bytes,
                              uint32_t zero_bytes,
                              bool writable)
{
  struct sup_page_table_entry *sup_pte;
  size_t sup_pte_size = sizeof (struct sup_page_table_entry);
  sup_pte = (struct sup_page_table_entry *)malloc (sup_pte_size);

  sup_pte->user_page = user_page;
  sup_pte->status = FILE_SYS;
  sup_pte->frame = NULL;
  sup_pte->dirty = false;
  sup_pte->file = file;
  sup_pte->ofs = ofs;
  sup_pte->read_bytes = read_bytes;
  sup_pte->zero_bytes = zero_bytes;
  sup_pte->writable = writable;

  struct hash_elem *insert_elem = hash_insert (&sup_pt->page_map, &sup_pte->sup_elem);
  if (insert_elem == NULL)
    return true;
  else
  {
    free (sup_pte);
    return false;
  }
}

/* Set the page. Extract this method from the fault_handler in exception.c */
bool
sup_page_table_set_page (struct sup_page_table *sup_pt,
                         uint32_t *pagedir, void *user_page)
{
  struct sup_page_table_entry *sup_pte = 
   sup_page_table_find (sup_pt, user_page);

  /* If the memory reference is valid, 
     use the supplemental page table entry 
     to locate the data that goes in the page. */

  /* Any invalid access terminates the process 
     and thereby frees all of its resources.*/
  if (sup_pte == NULL)
    return false;

  if (sup_pte->status == ON_FRAME)
  {
    /* Already loaded. */
    return true;
  }

  /* Obtain a frame to store the page. */
  void *frame = frame_alloc (PAL_USER);
  if (frame == NULL)
    return false;

  /* Fetch the data into the frame, 
     by reading it from the file system 
     or swap, zeroing it, etc. */
   bool writable = true;
   switch (sup_pte->status)
   {
   case ALL_ZERO:
      memset (frame, 0, PGSIZE);
      break;
   case ON_FRAME:
      /* Already on the frame. */
      break;
   case SWAP_SLOT:
      /* Swap in. */
      swap_in (sup_pte->st_index, frame);
      break;
   case FILE_SYS:
      file_seek (sup_pte->file, sup_pte->ofs);
      off_t read_bytes = file_read (sup_pte->file, frame, sup_pte->read_bytes);
      if (read_bytes != sup_pte->read_bytes)
      {
        frame_free (frame); /* Release the resources before terminating. */
        return false;
      }
     
      memset (frame + read_bytes, 0, sup_pte->zero_bytes);
      writable = sup_pte->writable;
      break;
   default:
      return false;
   }

  /* Point the page table entry for the faulting virtual address 
     to the physical page. */
  if (!pagedir_set_page (pagedir, user_page, frame, writable))
  {
    /* The setting fails. */
    frame_free (frame);
    return false;
  }
  sup_pte->frame = frame;
  sup_pte->status = ON_FRAME;
   
  pagedir_set_dirty (pagedir, frame, false);

  return true;
}