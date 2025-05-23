#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include "userprog/gdt.h"
#include "threads/interrupt.h"
#include "threads/thread.h"

/* For the operations of pages. */
#include "userprog/pagedir.h"
#include "threads/vaddr.h"

/* Memeset */
#include "string.h"

#include "filesys/file.h"
#include "userprog/syscall.h"

/* Add headers of VM. */
#ifdef VM
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"
static long long page_fault_depth;
#define MAX_FAULT_DEPTH 4
#define MAX_STACK_SIZE 0x800000   /**< the default stack limit is 8 MB. */
#endif

/** Number of page faults processed. */
static long long page_fault_cnt;

static void kill (struct intr_frame *);
static void page_fault (struct intr_frame *);

void
terminate_process (void *fault_addr,
                   bool not_present, 
                   bool write, 
                   bool user,
                   struct intr_frame *f);

/** Registers handlers for interrupts that can be caused by user
   programs.

   In a real Unix-like OS, most of these interrupts would be
   passed along to the user process in the form of signals, as
   described in [SV-386] 3-24 and 3-25, but we don't implement
   signals.  Instead, we'll make them simply kill the user
   process.

   Page faults are an exception.  Here they are treated the same
   way as other exceptions, but this will need to change to
   implement virtual memory.

   Refer to [IA32-v3a] section 5.15 "Exception and Interrupt
   Reference" for a description of each of these exceptions. */
void
exception_init (void) 
{
  /* These exceptions can be raised explicitly by a user program,
     e.g. via the INT, INT3, INTO, and BOUND instructions.  Thus,
     we set DPL==3, meaning that user programs are allowed to
     invoke them via these instructions. */
  intr_register_int (3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
  intr_register_int (4, 3, INTR_ON, kill, "#OF Overflow Exception");
  intr_register_int (5, 3, INTR_ON, kill,
                     "#BR BOUND Range Exceeded Exception");

  /* These exceptions have DPL==0, preventing user processes from
     invoking them via the INT instruction.  They can still be
     caused indirectly, e.g. #DE can be caused by dividing by
     0.  */
  intr_register_int (0, 0, INTR_ON, kill, "#DE Divide Error");
  intr_register_int (1, 0, INTR_ON, kill, "#DB Debug Exception");
  intr_register_int (6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
  intr_register_int (7, 0, INTR_ON, kill,
                     "#NM Device Not Available Exception");
  intr_register_int (11, 0, INTR_ON, kill, "#NP Segment Not Present");
  intr_register_int (12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
  intr_register_int (13, 0, INTR_ON, kill, "#GP General Protection Exception");
  intr_register_int (16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
  intr_register_int (19, 0, INTR_ON, kill,
                     "#XF SIMD Floating-Point Exception");

  /* Most exceptions can be handled with interrupts turned on.
     We need to disable interrupts for page faults because the
     fault address is stored in CR2 and needs to be preserved. */
  intr_register_int (14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/** Prints exception statistics. */
void
exception_print_stats (void) 
{
  printf ("Exception: %lld page faults\n", page_fault_cnt);
}

/** Handler for an exception (probably) caused by a user process. */
static void
kill (struct intr_frame *f) 
{
  /* This interrupt is one (probably) caused by a user process.
     For example, the process might have tried to access unmapped
     virtual memory (a page fault).  For now, we simply kill the
     user process.  Later, we'll want to handle page faults in
     the kernel.  Real Unix-like operating systems pass most
     exceptions back to the process via signals, but we don't
     implement them. */
     
  /* The interrupt frame's code segment value tells us where the
     exception originated. */
  switch (f->cs)
    {
    case SEL_UCSEG:
      /* User's code segment, so it's a user exception, as we
         expected.  Kill the user process.  */
      printf ("%s: dying due to interrupt %#04x (%s).\n",
              thread_name (), f->vec_no, intr_name (f->vec_no));
      intr_dump_frame (f);
      exit (-1); /* Kill with status -1. */ 

    case SEL_KCSEG:
      /* Kernel's code segment, which indicates a kernel bug.
         Kernel code shouldn't throw exceptions.  (Page faults
         may cause kernel exceptions--but they shouldn't arrive
         here.)  Panic the kernel to make the point.  */
      intr_dump_frame (f);
      PANIC ("Kernel bug - unexpected interrupt in kernel"); 

    default:
      /* Some other code segment?  Shouldn't happen.  Panic the
         kernel. */
      printf ("Interrupt %#04x (%s) in unknown segment %04x\n",
             f->vec_no, intr_name (f->vec_no), f->cs);
      exit (-1); /* Kill with status -1. */
    }
}

/** Page fault handler.  This is a skeleton that must be filled in
   to implement virtual memory.  Some solutions to project 2 may
   also require modifying this code.

   At entry, the address that faulted is in CR2 (Control Register
   2) and information about the fault, formatted as described in
   the PF_* macros in exception.h, is in F's error_code member.  The
   example code here shows how to parse that information.  You
   can find more information about both of these in the
   description of "Interrupt 14--Page Fault Exception (#PF)" in
   [IA32-v3a] section 5.15 "Exception and Interrupt Reference". */
static void
page_fault (struct intr_frame *f) 
{
  bool not_present;  /**< True: not-present page, false: writing r/o page. */
  bool write;        /**< True: access was write, false: access was read. */
  bool user;         /**< True: access by user, false: access by kernel. */
  void *fault_addr;  /**< Fault address. */

  /* Obtain faulting address, the virtual address that was
     accessed to cause the fault.  It may point to code or to
     data.  It is not necessarily the address of the instruction
     that caused the fault (that's f->eip).
     See [IA32-v2a] "MOV--Move to/from Control Registers" and
     [IA32-v3a] 5.15 "Interrupt 14--Page Fault Exception
     (#PF)". */
  asm ("movl %%cr2, %0" : "=r" (fault_addr));

  /* Turn interrupts back on (they were only off so that we could
     be assured of reading CR2 before it changed). */
  intr_enable ();

  /* Count page faults. */
  page_fault_cnt++;

#ifdef VM
  page_fault_depth++;
  if (page_fault_depth > MAX_FAULT_DEPTH)
  {
    page_fault_depth--;
    f->eip = (void (*) (void)) f->eax;
    f->eax = -1;
    return;
  }
#endif


  /* Determine cause. */
  not_present = (f->error_code & PF_P) == 0;
  write = (f->error_code & PF_W) != 0;
  user = (f->error_code & PF_U) != 0;

  if (!not_present)
  {
    /* Try to violate. */
#ifdef VM
    page_fault_depth--;
#endif
    /* Using sys_exit, we will not walk into the page fault again. */
    exit (-1);
  }

  if (user && !is_user_vaddr (fault_addr))
  {
   /* User accessing, and the address is not user addr. */
#ifdef VM
    page_fault_depth--;
#endif
    exit (-1);
  }

#ifdef VM
  
  /* Locate the page that faulted in the supplemental page table. */
  struct thread *cur = thread_current ();
  void *fault_page = (void *) pg_round_down (fault_addr);

  /* You will need to be able to obtain the current value 
     of the user program's stack pointer.  */
  void *esp = user ? f->esp : cur->esp;

  /* Is stack in user memory? 
     PHYS_BASE - MAX_STACK_SIZE <= fault_addr && fault_addr < PHYS_BASE
     PUSH may cause a page fault 4 bytes below the stack pointer.
     PUSHA instruction pushes 32 bytes at once, 
     so it can fault 32 bytes below the stack pointer. */
  bool is_stack_access = (PHYS_BASE - MAX_STACK_SIZE <= fault_addr 
                          && fault_addr < PHYS_BASE)
                      && (esp <= fault_addr 
                          || fault_addr == f->esp - 4
                          || fault_addr == f->esp - 32);
  if (is_stack_access) {
    /* if the stack grows past its current size,
       allocate additional pages as necessary.
       The simplest way is to allocate a zero-filled page. */
    if (!sup_page_table_find (cur->sup_pt, fault_page)) {
      sup_page_table_set_page_zero (cur->sup_pt, fault_page);
    }
  }

  if (!sup_page_table_set_page (cur->sup_pt, cur->pagedir, fault_page))
  {
    page_fault_depth--;
    terminate_process (fault_addr, not_present, write, user, f);
    return;
  }
  
  page_fault_depth--;
#else
  terminate_process (fault_addr, not_present, write, user, f);
  return;
#endif
}


/** Hey, I don't want to delete this codes. Let it be here. */
void
terminate_process (void *fault_addr,
                   bool not_present, 
                   bool write, 
                   bool user,
                   struct intr_frame *f)
{
  /* page fault triggered by a bad reference from a system call */  
  if (!user) {
    f->eip = (void (*) (void)) f->eax;
    f->eax = -1;
    return;
  }
  kill (f);
}

