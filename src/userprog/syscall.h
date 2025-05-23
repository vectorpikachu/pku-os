#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#ifdef VM
#include "userprog/process.h"
#endif

void syscall_init (void);

void exit (int status);
#ifdef VM
bool munmap (mapid_t map_id);
#endif

#endif /**< userprog/syscall.h */
