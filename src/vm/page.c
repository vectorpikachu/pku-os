#include "lib/kernel/hash.h"
#include "vm/page.h"


/** Helper functions of creating a supplemental page table.

    Use hash_bytes here.
 */
static unsigned
sup_hash_func (const struct hash_elem *elem, void *aux)
{
  struct sup_page_table_entry *sup_pte = 
    hash_entry (elem, struct sup_page_table_entry, sup_elem);
  size_t uaddr_size = sizeof (sup_pte->user_page);
  return hash_bytes (sup_pte->user_page, uaddr_size);
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


/** Set the page in supplemental page table. */
bool
sup_page_table_set_page (struct sup_page_table *sup_pt, void *user_page)
{
  size_t sup_pte_size = sizeof (struct sup_page_table_entry);
  struct sup_page_table_entry *sup_pte;
  sup_pte = (struct sup_page_table_entry *)malloc (sup_pte_size);

  sup_pte->user_page = user_page;
  sup_pte->status = ON_FRAME;

  struct hash_elem *inserted = hash_insert (&sup_pt->page_map, &sup_pte->sup_elem);
  if (inserted == NULL)
    return true;
  else
  {
    free (sup_pte);
    return false;
  }
}