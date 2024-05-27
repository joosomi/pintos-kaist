#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_create_initd(const char *file_name);
tid_t process_fork(const char *name, struct intr_frame *if_);
int process_exec(void *f_name);
int process_wait(tid_t);
void process_exit(void);
void process_activate(struct thread *next);

/* --------------- added for PROJECT.2-1 --------------- */

void parse_argument(char *command_line, char *argv[], int *argc);
void push_argment_stack(char *argv[], int argc, struct intr_frame *_if);

/* --------------- added for PROJECT.2-1 --------------- */

int process_add_file(struct file *file_p);
struct file *process_get_file(int fd);
void process_close_file(int fd);
int read(int fd, void *buffer, unsigned size);

/* ----------------------------------------------------- */

#endif /* userprog/process.h */
