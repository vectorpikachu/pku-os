#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "lib/kernel/hash.h"

/** Supplemental Page Table.

    The supplemental page table supplements the page table 
    with additional data about each page.
    - It is needed because of the limitations 
      imposed by the page table's format.
    - Such a data structure is often called a "page table" also;
      we add the word "supplemental" to reduce confusion.
    
    
    1. Most importantly, on a page fault, the kernel looks up the
       virtual page that faulted in the supplemental page table 
       to find out what data should be there.

    2. Second, the kernel consults the supplemental page table 
       when a process terminates, to decide what resources to free.
    
    Comparing with the page directory.
 */


 /** Page status
  
    Locate the page that faulted in the supplemental page table. 
    If the memory reference is valid, use the supplemental page table entry 
    to locate the data that goes in the page, which might be in the file system, 
    or in a swap slot, or it might simply be an all-zero page.
  */
enum page_status
  {
    ALL_ZERO,
    ON_FRAME,
  };

/** Supplemental Page Table. Add to the process's members to record.  */
struct sup_page_table
  {
    struct hash page_map;
  };

/** The Supplemental Page Table Entry.

    This comment may not be useful.
    The page_dir will maintain page directory that has mappings for kernel
    virtual addresses, but none for user virtual addresses.
 */
struct sup_page_table_entry
  {
    void *user_page;            /** The user page's virtual address. */
    struct hash_elem sup_elem;  /** The hash element. */
    enum page_status status;    /** The status of the page. */
  };

struct sup_page_table *sup_page_table_create (void);
void sup_page_table_destroy (struct sup_page_table *sup_pt);
bool sup_page_table_set_page (struct sup_page_table *sup_pt, void *user_page);

struct sup_page_table_entry *sup_page_table_find (
  struct sup_page_table *sup_pt, void *page
);

#endif
