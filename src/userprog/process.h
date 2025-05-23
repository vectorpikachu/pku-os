#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#ifdef VM
#include "kernel/list.h"
#include "filesys/file.h"
#endif

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

#ifdef VM
typedef int mapid_t; /* Set mapid_t. */

/** This is to track which files are mapped into which pages. */
struct process_map {
    mapid_t map_id;          /**< The mapping id. */
    struct list_elem elem;   /**< Form a list in process. */
    struct file *file;       /**< The mapped file. */ 
    void *addr;              /**< The address that is mapping to. */
    size_t size;             /**< The file size. */
};
#endif

#endif /**< userprog/process.h */
