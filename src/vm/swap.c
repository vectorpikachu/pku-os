#include "vm/swap.h"
#include "devices/block.h"
#include "lib/kernel/bitmap.h"
#include "threads/vaddr.h"


/** Why a pointer but not an object?
    Let's refer to the Pintos doc, it says,
    Swap slots should be allocated *lazily*, that is,
    only when they are actually required by eviction.
 */
static struct block *swap_block;

/** Swap Table.
    Its responsibilty: record the free slots.

    Given a swap index, it will check whether it is free.

    Also, the reason of a pointer is the same as `swap_block`.
 */
static struct bitmap *swap_table;

/** The block device sector's size are 512 B.
    So every page has 8 (PGSIZE / BLOCK_SECTOR_SIZE) block sectors.

    Hint: as we can get the size of SWAP_BLOCK (in SECTORs),
    we must calculate the # of pages by deviding this BLOCKSECS_PER_PAGE.
 */
#define BLOCKSECS_PER_PAGE ((PGSIZE) / (BLOCK_SECTOR_SIZE))

/** It is # of pages in SWAP_BLOCK device. */
static size_t swap_page_num;

/** Initialize the swap space and swap table.
    
    According to TA Session, swap space is a block device of BLOCK_SWAP.

    The swap in/out is measured by the unit of pages. We should
    handle the trans. of SECTOR and PAGE.
 */
void
swap_init ()
{
  swap_block = block_get_role (BLOCK_SWAP);
  if (swap_block == NULL)
    return;
  
  /* Let's calculate the total # of pages in SWAP_BLOCK. */
  swap_page_num = block_size (swap_block) / BLOCKSECS_PER_PAGE;

  /* Initialize the swap table. */
  swap_table = bitmap_create (swap_page_num);

  /* Initially, all available. */
  bitmap_set_all (swap_table, true);
}


/** Swap out the page PAGE. */
uint32_t
swap_out (void *page)
{
  /* Using `bitmap_scan` to find *one* slot. */
  size_t free_slot = bitmap_scan (swap_table, 0, 1, true);
  if (free_slot == BITMAP_ERROR)
    return -1;

  /* Now write the content of this page PAGE to the
     found free slot. We can only write in unit of SECTORs. */
  for (size_t i = 0; i < BLOCKSECS_PER_PAGE; i++)
  {
    size_t sec_index = free_slot * BLOCKSECS_PER_PAGE + i;
    void *buf_start = page + BLOCK_SECTOR_SIZE * i;
    block_write (swap_block, sec_index, buf_start);
  }
  bitmap_set (swap_table, free_slot, false);
  return free_slot;
}

/** Swap in the ST_INDEX's content to the page PAGE. */
void
swap_in (uint32_t st_index, void *page)
{
  for (size_t i = 0; i < BLOCKSECS_PER_PAGE; i++)
  {
    size_t sec_index = st_index * BLOCKSECS_PER_PAGE + i;
    void *buf_start = page + BLOCK_SECTOR_SIZE * i;
    block_read (swap_block, sec_index, buf_start);
  }
  bitmap_set (swap_table, st_index, true);
}