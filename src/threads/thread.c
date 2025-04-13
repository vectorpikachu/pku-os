#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "filesys/file.h"
#include "fixed-point.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/** Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/** List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/** List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/** Idle thread. */
static struct thread *idle_thread;

/** Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/** Lock used by allocate_tid(). */
static struct lock tid_lock;

/** File lock */
static struct lock file_lock;

bool schedule_started;

/** Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /**< Return address. */
    thread_func *function;      /**< Function to call. */
    void *aux;                  /**< Auxiliary data for function. */
  };

/** Statistics. */
static long long idle_ticks;    /**< # of timer ticks spent idle. */
static long long kernel_ticks;  /**< # of timer ticks in kernel threads. */
static long long user_ticks;    /**< # of timer ticks in user programs. */

/** Scheduling. */
#define TIME_SLICE 4            /**< # of timer ticks to give each thread. */
static unsigned thread_ticks;   /**< # of timer ticks since last yield. */

/** If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

#define MAX_PROCESS 32

struct list sleep_list;        /**< List of threads sleeping. */
static int load_avg;            /**< System-wide load average. */

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);
void acquire_file_lock (void);
void release_file_lock (void);

/** Acquire file lock. */
void
acquire_file_lock (void) 
{
  lock_acquire (&file_lock);
}


/** Release file lock. */
void
release_file_lock (void) 
{
  lock_release (&file_lock);
}

/** Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  lock_init (&file_lock); /* Init file lock. */
  list_init (&ready_list);
  list_init (&all_list);
  list_init (&sleep_list);

  schedule_started = false;

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

/** Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  load_avg = 0; // Lyu: Initialize the load_avg to 0.

  schedule_started = true;

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/** Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();
  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/** Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/** Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT (function != NULL);

  size_t live_process = list_size (&all_list);
  if (live_process >= MAX_PROCESS) {
    return TID_ERROR; /* Max process limit reached. */
  }

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Lab 2: Intialize this (child) process's structure. */
  t->child = malloc (sizeof (struct child_process));
  t->child->tid = tid;
  sema_init (&t->child->sema, 0);
  list_push_back (&thread_current ()->children, &t->child->child_elem);
  t->child->is_exited = false;
  t->child->exit_status = -1;

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  /* Add to run queue. */
  thread_unblock (t);

  /* Lyu: When a high-priority thread is created, yield it to CPU. */
  if (thread_current ()->priority < priority)
    thread_yield ();

  return tid;
}

/** Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);
  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/** Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  list_insert_ordered (&ready_list, 
    &t->elem, 
    thread_priority_compare_greater, 
    NULL);
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

/** Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/** Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/** Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/** Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();

  /* Lab 2: Thread_exit is now actually exiting process. */
  thread_current ()->child->exit_status = thread_current ()->exit_status;
  sema_up (&thread_current ()->child->sema);

  file_close (thread_current ()->file_exec);

  /* Close all opened files. */
  struct list *file_list = &thread_current ()->file_list;
  struct list_elem *e;
  acquire_file_lock ();
  for (e = list_begin (file_list); e != list_end (file_list); 
       e = list_remove (e)) {
    struct process_file *pf = list_entry (e, struct process_file, file_elem);
    file_close (pf->file);
    free (pf);
  }
  release_file_lock ();

  struct list *children = &thread_current ()->children;
  /* Free all child_process entries we allocated (if we're the parent). */
  while (!list_empty (children)) {
    e = list_pop_front (children);
    struct child_process *cp = list_entry (e, struct child_process, child_elem);
    free (cp);
  }

  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/** Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) 
    list_insert_ordered (&ready_list, 
      &cur->elem, 
      thread_priority_compare_greater, 
      NULL);
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/** Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/** Sets the current thread's priority to NEW_PRIORITY.
 ADDITION: After the priority is set, the thread should be immediately
 sheduled again. Using thread_yield() here.
 ATTENTION: The original priority should be updated as well.
 And if there is not any locks held by the thread, 
 the priority should be updated to the new priority. 
 And if the new priority is higher than the original priority, 
 the priority should be updated to the new priority. 
 But in other case(hold locks and new priority is lower than the original priority), 
 we should not update the priority, 
 because we don't know whether the priority is being donated to or not, 
 if we lower the priority, it may cause deadlock.
 ATTENTION: The priority argument to thread_set_priority()
 should be ignored if multi-level feedback queue scheduler is enabled.
 */
void
thread_set_priority (int new_priority) 
{
  if (thread_mlfqs) {
    return;
  }
  enum intr_level old_level = intr_disable ();
  thread_current ()->original_priority = new_priority;
  if (list_empty (&thread_current ()->locks) || new_priority > thread_current ()->priority) {
    thread_current ()->priority = new_priority;
    thread_yield ();
  }
  intr_set_level (old_level);
}

/** Each thread also has a priority, between 0 (PRI_MIN) through 63 (PRI_MAX), 
 * which is recalculated using the following formula every fourth tick:
 * priority = PRI_MAX - (recent_cpu / 4) - (nice * 2)
 * The calculated priority is always adjusted to lie in the valid range PRI_MIN to PRI_MAX.
 * ATTENTION: Still remember the thread_forearch() function? This is meant to be used here.
 * ATTENTION: Exclude the idle thread.
 */
void
update_priority (struct thread *t, void *aux UNUSED)
{
  // Exclude the idle thread
  if (t == idle_thread) {
    return;
  }
  int new_priority = FP_TO_INT_NEAREST (
    FP_SUB_INT (
      FP_SUB (INT_TO_FP (PRI_MAX), FP_DIV_INT (t->recent_cpu, 4)), 
      t->nice * 2)
    );

  if (new_priority > PRI_MAX) {
    new_priority = PRI_MAX;
  } else if (new_priority < PRI_MIN) {
    new_priority = PRI_MIN;
  }
  t->priority = new_priority;
}

int
get_ready(void)
{
  return list_size(&ready_list);
}

/** **On each timer tick**, the running thread's recent_cpu is incremented by 1. */
void
increment_recent_cpu (void)
{
  ASSERT (thread_mlfqs);
  ASSERT (intr_context ());
  struct thread *t = thread_current ();
  if (t != idle_thread) {
    t->recent_cpu = FP_ADD_INT (t->recent_cpu, 1);
  }
}

/** **Once per second**, every thread's recent_cpu is updated this way:
 * recent_cpu = (2 * load_avg) / (2 * load_avg + 1) * recent_cpu + nice
 * ATTENTION: Still remember the thread_forearch() function? This is meant to be used here.
 */
void
update_recent_cpu (struct thread *t, void *aux UNUSED)
{
  if (t != idle_thread) {
    int load_avg_twice = FP_MUL_INT (load_avg, 2);
    int coefficient = FP_DIV (load_avg_twice, FP_ADD_INT (load_avg_twice, 1));
    t->recent_cpu = FP_ADD_INT (FP_MUL (coefficient, t->recent_cpu), t->nice);
  }
}

/** Update the recent_cpu and priority of given threads. */
void
update_recent_cpu_and_priority (struct thread *t, void *aux UNUSED)
{
  update_recent_cpu (t, NULL);
  update_priority (t, NULL);
}

/** load_avg estimates the average number of threads ready to run over the past minute. 
 * It is initialized to 0 at boot and recalculated **once per second** as follows, 
 * where ready_threads is the number of threads that are either **running** 
 * or ready to run at time of update (not including the idle thread).
 * load_avg = (59/60) * load_avg + (1/60) * ready_threads
 */
void
update_load_avg (void)
{
  int ready_threads = list_size (&ready_list);
  if (thread_current () != idle_thread) {
    // except the idle thread, the running thread should be counted as well
    ready_threads++;
  }
  int coefficient1 = FP_DIV_INT (INT_TO_FP (59), 60);
  int coefficient2 = FP_DIV_INT (INT_TO_FP (1), 60);
  load_avg = FP_ADD (FP_MUL (coefficient1, load_avg), FP_MUL_INT (coefficient2, ready_threads));
}

/** Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

/** Sets the current thread's nice value to NICE.
 * And recalculates the thread's priority based on the new value.
 * If the running thread no longer has the highest priority, yields.
 */
void
thread_set_nice (int nice UNUSED) 
{
  enum intr_level old_level = intr_disable ();
  thread_current ()->nice = nice;
  update_priority (thread_current (), NULL);
  thread_yield ();
  intr_set_level (old_level);
}

/** Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  return thread_current ()->nice;
}

/** Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  int hundred_load_avg = FP_MUL_INT (load_avg, 100);
  return FP_TO_INT_NEAREST (hundred_load_avg);
}

/** Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  int hundred_recent_cpu = FP_MUL_INT (thread_current ()->recent_cpu, 100);
  return FP_TO_INT_NEAREST (hundred_recent_cpu);
}

/** Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/** Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /**< The scheduler runs with interrupts off. */
  function (aux);       /**< Execute the thread function. */
  thread_exit ();       /**< If function() returns, kill the thread. */
}

/** Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/** Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/** Does basic initialization of T as a blocked thread named
   NAME.
   ATTENTION: if multi-level feedback queue scheduler is enabled,
   The priority argument to thread_create() should be ignored, I will set it to zero.
    */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  enum intr_level old_level;

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  if (thread_mlfqs) {
    t->priority = 0;
  } else {
    t->priority = priority;
  }
  t->magic = THREAD_MAGIC;

  /* Lab 2: Initialize a (parent) process. */
  list_init (&t->children);

  if (t == initial_thread) {
    t->parent = NULL; /* It is parent process. */
  }
  else {
    t->parent = thread_current (); /* It is child process. */
  }

  sema_init (&t->sema, 0);
  t->exit_status = -1;
  t->child_exit = false;

  list_init (&t->file_list);
  t->fd = 2; /* File descriptors start from 2. */

  t->file_exec = NULL; /* Executable file. */
  /* Initialize newly-added fields. */
  list_init (&t->locks);
  t->waiting_lock = NULL;
  t->sleep_ticks = 0;
  t->original_priority = priority;
  t->nice = 0; // initial thread, nice value is 0
  t->recent_cpu = 0;

  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);
}

/** Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/**
 Compare the priority of two threads. It's a less function for list_max.
 */
bool
thread_priority_compare (const struct list_elem *a_, const struct list_elem *b_, void *aux UNUSED)
{
  const struct thread *a = list_entry (a_, struct thread, elem);
  const struct thread *b = list_entry (b_, struct thread, elem);
  return a->priority < b->priority;
}

/** Greater version of thread_priority_compare. */
bool
thread_priority_compare_greater (const struct list_elem *a_, const struct list_elem *b_, void *aux UNUSED)
{
  const struct thread *a = list_entry (a_, struct thread, elem);
  const struct thread *b = list_entry (b_, struct thread, elem);
  return a->priority > b->priority;
}

/** Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else {
    // Lyu: Should return the highest priority thread in the ready list
    struct list_elem *highest_priority_thread = list_pop_front (&ready_list);
    struct thread *t = list_entry (highest_priority_thread, struct thread, elem);
    if (!list_empty (&ready_list) && strcmp (t->name, "idle") == 0) {
      struct thread *next = list_entry (list_pop_front (&ready_list), struct thread, elem);
      return next;
    }
    return list_entry (highest_priority_thread, struct thread, elem);
  }
}

/** Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/** Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/** Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/** Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);

/** Sleep the thread t, insert it in the sleep_list. */
void
thread_sleep (struct thread *t) {
  list_push_back (&sleep_list, &t->sleepelem);
} 

/** Check the sleep_list and wake up the sleeping threads.
  And I find there is a function thread_foreach. But I don't want to change.
  */
void
thread_wake_up (void) 
{
  struct list_elem *e;
  ASSERT (intr_get_level () == INTR_OFF);
  for (e = list_begin (&sleep_list); e != list_end (&sleep_list); e = list_next (e)) {
    struct thread *t = list_entry (e, struct thread, sleepelem);
    if (t->status == THREAD_BLOCKED && t->sleep_ticks > 0) {
      t->sleep_ticks--;
      if (t->sleep_ticks == 0) {
        list_remove (&t->sleepelem);
        thread_unblock (t);
        if (t->priority > thread_current ()->priority) {
          intr_yield_on_return ();
        }
      }
    }
  }
}