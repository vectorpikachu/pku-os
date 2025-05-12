#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "devices/shutdown.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/thread.h"

#ifdef VM
#include "vm/frame.h"
#include "vm/page.h"
#endif

#define SYSCALL_NUM 30

/* syscalls array */
static void (*syscalls[SYSCALL_NUM])(struct intr_frame *);


int32_t get_argument (int arg_index, struct intr_frame *f);
static int get_user (const uint8_t *uaddr);
static bool put_user (uint8_t *udst, uint8_t byte);
void check_valid_pointer (const void *vaddr);
void exit_err (void);
struct process_file *get_process_file (int fd);

void preset_pages (void *buffer, size_t size);
void unpin_preset_pages (void *buffer, size_t size);

static void syscall_handler (struct intr_frame *);
static void sys_halt (struct intr_frame *);
static void sys_exit (struct intr_frame *);
static void sys_exec (struct intr_frame *);
static void sys_wait (struct intr_frame *);
static void sys_create (struct intr_frame *);
static void sys_remove (struct intr_frame *);
static void sys_open (struct intr_frame *);
static void sys_filesize (struct intr_frame *);
static void sys_read (struct intr_frame *);
static void sys_write (struct intr_frame *);
static void sys_seek (struct intr_frame *);
static void sys_tell (struct intr_frame *);
static void sys_close (struct intr_frame *);


/** Get the argument at the index arg_index. */
int32_t
get_argument (int arg_index, struct intr_frame *f)
{
  int32_t *arg_ptr;
  arg_ptr = (int32_t *) (f->esp + arg_index * sizeof (int32_t));
  check_valid_pointer (arg_ptr);
  return *arg_ptr;
}


/** Find the process file with the given file descriptor. */
struct process_file *
get_process_file (int fd)
{
  struct list_elem *e;
  struct process_file *pf = NULL;
  struct list *file_list = &thread_current ()->file_list;
  
  for (e = list_begin (file_list); e != list_end (file_list); 
       e = list_next (e)) {
    pf = list_entry (e, struct process_file, file_elem);
    if (pf->fd == fd) {
      return pf;
    }
  }
  return NULL;
}

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
        : "=&a" (result) : "m" (*uaddr));
  return result;
}
   
/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
        : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}

/** Check valid pointer.
    1. A null pointer.
    2. A pointer to unmapped virtual memory.
    3. A pointer to kernel virtual address space (above PHYS_BASE).
 */
void
check_valid_pointer (const void *vaddr)
{
  if (vaddr == NULL)
    exit_err ();
  if (!is_user_vaddr (vaddr))
    exit_err ();
  
  if (pagedir_get_page (thread_current ()->pagedir, vaddr) == NULL)
    exit_err ();
  
  /* Check sc-boundary-3:
     Invokes a system call with the system call number positioned
     such that its first byte is valid but the remaining bytes of
     the number are in invalid memory. Must kill process.
   */
  for (int i = 0; i < 4; i++) {
    if (get_user ((const uint8_t *) vaddr + i) == -1)
      exit_err ();
  }
}


/** Exit with error. */
void
exit_err (void)
{
  thread_current ()->exit_status = -1;
  thread_exit ();
}

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  syscalls[SYS_HALT] = &sys_halt;
  syscalls[SYS_EXIT] = &sys_exit;
  syscalls[SYS_EXEC] = &sys_exec;
  syscalls[SYS_WAIT] = &sys_wait;
  syscalls[SYS_CREATE] = &sys_create;
  syscalls[SYS_REMOVE] = &sys_remove;
  syscalls[SYS_OPEN] = &sys_open;
  syscalls[SYS_FILESIZE] = &sys_filesize;
  syscalls[SYS_READ] = &sys_read;
  syscalls[SYS_WRITE] = &sys_write;
  syscalls[SYS_SEEK] = &sys_seek;
  syscalls[SYS_TELL] = &sys_tell;
  syscalls[SYS_CLOSE] = &sys_close;
}


/** The most direct exit system call. */
void exit (int status)
{
  thread_current ()->exit_status = status;
  thread_exit ();
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  int syscall_num = get_argument (0, f);
  syscalls[syscall_num] (f);
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

/** Runs the executable whose name is given in cmd_line, passing any
 * given arguments, and returns the new process's program id (pid).
 * System Call: pid_t exec (const char *cmd_line). Get the argument - cmd_line.
 */
static void
sys_exec (struct intr_frame *f) 
{
  const char *cmd_line = (const char *) get_argument (1, f);
  check_valid_pointer (cmd_line);
  f->eax = process_execute (cmd_line);
}

/** Waits for a child process pid and retrieves the child's exit status.
 * System Call: int wait (pid_t pid). Get the argument - pid.
 */
static void
sys_wait (struct intr_frame *f) 
{
  tid_t pid = (tid_t) get_argument (1, f);
  f->eax = process_wait (pid);
}


/** Creates a file with the given name and initial size.  */
static void
sys_create (struct intr_frame *f) 
{
  const char *file = (const char *) get_argument (1, f);
  unsigned initial_size = get_argument (2, f);
  check_valid_pointer (file);
  acquire_file_lock ();
  f->eax = filesys_create (file, initial_size);
  release_file_lock ();
}

/** Remove the file named FILE.  Returns true on success, false on failure. */
static void
sys_remove (struct intr_frame *f) 
{
  const char *file = (const char *) get_argument (1, f);
  check_valid_pointer (file);
  acquire_file_lock ();
  f->eax = filesys_remove (file);
  release_file_lock ();
}


/** Open the file named FILE and return a file descriptor for it, or -1
    We need to record the file descriptor in the process's file list.
  */
static void
sys_open (struct intr_frame *f) 
{
  const char *file = (const char *) get_argument (1, f);
  check_valid_pointer (file);
  acquire_file_lock ();
  struct file *opened_file = filesys_open (file);
  release_file_lock ();
  if (opened_file == NULL) {
    f->eax = -1; /* Failed to open the file. */
  } else {
    struct process_file *pf = malloc (sizeof (struct process_file));
    pf->file = opened_file;
    pf->fd = thread_current ()->fd++;
    list_push_back (&thread_current ()->file_list, &pf->file_elem);
    f->eax = pf->fd; /* Return the file descriptor. */
  }
}


/** Returns the file size of FILE, or -1 on error. */
static void
sys_filesize (struct intr_frame *f) 
{
  int fd = get_argument (1, f);
  struct process_file *pf;
  pf = get_process_file (fd);
  if (pf == NULL) {
    f->eax = -1; /* File not found. */
  } else {
    acquire_file_lock ();
    f->eax = file_length (pf->file);
    release_file_lock ();
  }
}


/** Read from file opened as FD. */
static void
sys_read (struct intr_frame *f) 
{
  int fd = get_argument (1, f);
  void *buffer = (void *) get_argument (2, f);
  unsigned size = get_argument (3, f);
  check_valid_pointer (buffer);
  if (fd == 0) {
    for (unsigned i = 0; i < size; i++) {
      ((uint8_t *) buffer)[i] = input_getc ();
    }
    f->eax = size;
  } else {
    struct process_file *pf = get_process_file (fd);
    if (pf == NULL) {
      f->eax = -1; /* File not found. */
    } else {
#ifdef VM
      preset_pages (buffer, size);
#endif
      acquire_file_lock ();
      f->eax = file_read (pf->file, buffer, size);
      release_file_lock ();
#ifdef VM
      unpin_preset_pages (buffer, size);
#endif
    }
  }
}

/** System Call: int write (int fd, const void *buffer, unsigned size) */
static void
sys_write (struct intr_frame *f) 
{
  int fd = get_argument (1, f);
  const char *buffer = (const char *) get_argument (2, f);
  check_valid_pointer (buffer);
  off_t size = get_argument (3, f);
  if (fd == 1) {
    putbuf (buffer, size);
    f->eax = size;
  } else {
    struct process_file *pf = get_process_file (fd);
    if (pf == NULL) {
      f->eax = -1; /* File not found. */
    } else {
#ifdef VM
      preset_pages (buffer, size);
#endif
      acquire_file_lock ();
      f->eax = file_write (pf->file, buffer, size);
      release_file_lock ();
#ifdef VM
      unpin_preset_pages (buffer, size);
#endif
    }
  }
}


/** Seek. 
    Changes the next byte to be read or written 
    in open file fd to position */
static void
sys_seek (struct intr_frame *f) 
{
  int fd = get_argument (1, f);
  off_t position = get_argument (2, f);
  struct process_file *pf = get_process_file (fd);
  if (pf == NULL) {
    f->eax = -1; /* File not found. */
  } else {
    acquire_file_lock ();
    file_seek (pf->file, position);
    release_file_lock ();
  }
}

/** Tell System Call.  */
static void
sys_tell (struct intr_frame *f) 
{
  int fd = get_argument (1, f);
  struct process_file *pf = get_process_file (fd);
  if (pf == NULL) {
    f->eax = -1; /* File not found. */
  } else {
    acquire_file_lock ();
    f->eax = file_tell (pf->file);
    release_file_lock ();
  }
}


/** Close System Call. */
static void
sys_close (struct intr_frame *f) 
{
  int fd = get_argument (1, f);
  struct process_file *pf = get_process_file (fd);
  if (pf != NULL) {
    acquire_file_lock ();
    file_close (pf->file);
    release_file_lock ();
    list_remove (&pf->file_elem);
    free (pf);
  }
}

/** We must pin the page holding the resources. */
void
preset_pages (void *buffer, size_t size)
{
  struct sup_page_tabe *sup_pt = thread_current ()->sup_pt;
  uint32_t *pagedir = thread_current ()->pagedir;
  void *user_page = pg_round_down (buffer);
  while (user_page <= buffer + size)
  {
    sup_page_table_set_page (sup_pt, pagedir, user_page);
    page_pin (sup_pt, user_page);
    user_page += PGSIZE;
  }
}


/** Unpin this pre-pinned pages. */
void
unpin_preset_pages (void *buffer, size_t size)
{
  struct sup_page_tabe *sup_pt = thread_current ()->sup_pt;
  uint32_t *pagedir = thread_current ()->pagedir;
  void *user_page = pg_round_down (buffer);
  while (user_page <= buffer + size)
  {
    page_unpin (sup_pt, user_page);
    user_page += PGSIZE;
  }
}