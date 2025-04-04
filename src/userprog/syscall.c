#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

#define SYSCALL_NUM 30

/* syscalls array */
static void (*syscalls[SYSCALL_NUM])(struct intr_frame *);

static void syscall_handler (struct intr_frame *);
static void sys_halt (struct intr_frame *);
static void sys_exit (struct intr_frame *);

static void sys_write (struct intr_frame *);


/** Get the argument at the index arg_index. */
int32_t
get_argument (int arg_index, struct intr_frame *f)
{
  int32_t *arg_ptr;
  arg_ptr = (int32_t *) (f->esp + arg_index * sizeof (int32_t));
  return *arg_ptr;
}

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  syscalls[SYS_HALT] = sys_halt;
  syscalls[SYS_EXIT] = sys_exit;

  syscalls[SYS_WRITE] = sys_write;
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  printf ("system call!\n");
  thread_exit ();
}

/** Terminates Pintos by calling shutdown_power_off() */
static void
sys_halt (struct intr_frame *f) 
{
  shutdown_power_off ();
}

/** Terminates the current user program, returning status to the kernel.
 * System Call: void exit (int status). Get the argument - status.
 */
static void
sys_exit (struct intr_frame *f) 
{
  thread_current ()->exit_status = get_argument (1, f);
  thread_exit ();
}


/** System Call: int write (int fd, const void *buffer, unsigned size) */
static void
sys_write (struct intr_frame *f) 
{
  int fd = get_argument (1, f);
  const char *buffer = (const char *) get_argument (2, f);
  unsigned size = get_argument (3, f);
  if (fd == 1) {
    putbuf (buffer, size);
    f->eax = size;
  }
}
