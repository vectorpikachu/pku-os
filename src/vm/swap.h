/** Swap Table.
    Using `bitmap` to implement.
    1. We only need to know whether slot $n$ is free.
    
    Swap space is a space on a hard disk that is a backup 
    for physical memory.
    In Pintos, swap space is a block device.

    Note that a disk sector is 512B while a page is 4096B.

    The swap table tracks in-use and free swap slots.

    It should allow picking an unused swap slot for evicting a page 
    from its frame to the swap partition.

    It should allow freeing a swap slot when its page is read back 
    or the process whose page was swapped is terminated.

    Swap slots should be allocated lazily, that is, 
    only when they are actually required by eviction.

    If the system needs more memory resources and the RAM is full,
    inactive pages in memory are moved to the swap space.

    Resonsiblity:
    1. Finding free slots for new swap-out requests.
    2. Identifying which slot to swap in page faults.
 */

#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "lib/stdint.h"

void swap_init (void);
uint32_t swap_out (void *page);
void swap_in (uint32_t st_index, void *page);

#endif