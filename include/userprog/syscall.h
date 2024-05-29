#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

/* --------------- added for PROJECT.2-1 --------------- */

#include <stdbool.h>
#include <threads/synch.h>
#include "lib/user/syscall.h" /* added for PROJECT.2-2 pid_t */

/* --------------- added for PROJECT.2-2 --------------- */

struct fd_elem {
  int fd;
  struct list_elem elem;
  struct file *file_ptr;
};

/* ----------------------------------------------------- */

void syscall_init(void);

/* --------------- added for PROJECT.2-1 --------------- */

void validate_adress(const void *addr);

/* --------------- System Calls  --------------- */

struct lock filesys_lock; /* read(), write()시에 동기화 문제를 위한 lock  */

void do_halt(void);
void do_exit(int status);
bool do_create(const char *file, unsigned initial_size);
bool do_remove(const char *file);
int do_open(const char *file);
int do_filesize(int fd);
int do_read(int fd, void *buffer, unsigned size);
int do_write(int fd, const void *buffer, unsigned size);
void do_close(int fd);
void do_seek(int fd, unsigned position);
unsigned do_tell(int fd);
pid_t do_fork(const char *thread_name);
int do_wait(pid_t pid);
int do_exec(const char *cmd_line);

/* ----------------------------------------------------- */

#endif /* userprog/syscall.h */
