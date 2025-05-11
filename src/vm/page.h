#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "lib/kernel/hash.h"
#include "filesys/file.h"

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
    SWAP_SLOT,
    FILE_SYS,
  };

/** Supplemental Page Table. Add to the process's members to record.  */
struct sup_page_table
  {
    struct hash page_map;
  };

/** The Supplemental Page Table Entry.
 
    we can set the SPTE in the SPT according to the different statue now.

    This comment may not be useful.
    The page_dir will maintain page directory that has mappings for kernel
    virtual addresses, but none for user virtual addresses.
 */
struct sup_page_table_entry
  {
    void *user_page;            /** The user page's virtual address. */
    struct hash_elem sup_elem;  /** The hash element. */
    enum page_status status;    /** The status of the page. */

    bool dirty;                 /** The dirty bit. */

    /* For the user page on the frame, we can record the frame.
       So this member can only be used when status == ON_FRAME. */
    void *frame;                /** The frame that the page is on. */

    /* For the user page in the swap slot. status == SWAP_SLOT. */
    uint32_t st_index;          /** The swap table index. */

    /* For the user page in the file system. status == FILE_SYS.
       We must record all the necessary infos about the file. */
    struct file *file;          /** The file this user page is in. */
    off_t ofs;                  /** The offset of this file. */
    uint32_t read_bytes;        /** The number of bytes to read from the file. */
    uint32_t zero_bytes;        /** The number of bytes to initialize to zero
                                    following the bytes read. */
    bool writable;              /** Whether the file is writable. */
  };

struct sup_page_table *sup_page_table_create (void);
void sup_page_table_destroy (struct sup_page_table *sup_pt);

struct sup_page_table_entry *sup_page_table_find (
  struct sup_page_table *sup_pt, void *page
);


bool
sup_page_table_set_page_frame (struct sup_page_table *sup_pt, 
                               void *user_page, 
                               void *frame);

bool
sup_page_table_set_page_zero (struct sup_page_table *sup_pt, 
                              void *user_page);

bool
sup_page_table_set_page_swap (struct sup_page_table *sup_pt, 
                              void *user_page,
                              uint32_t st_index);

bool
sup_page_table_set_page_file (struct sup_page_table *sup_pt,
                              void *user_page,
                              struct file * file,
                              off_t ofs,
                              uint32_t read_bytes,
                              uint32_t zero_bytes,
                              bool writable);


bool
sup_page_table_set_page (struct sup_page_table *sup_pt,
                         uint32_t *pagedir, void *user_page);

bool
sup_page_table_set_dirty (struct sup_page_table *sup_pt, void *page, bool value);
#endif
