#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <stdbool.h>
#include "threads/synch.h"
#include "filesys/file.h"
#include "threads/thread.h"

void syscall_init (void);

////
struct lock filesys_lock;
#define STDOUT_FILENO 1
#define STDIN_FILENO 0

void check_address(void *addr);
void halt(void);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
void exit(int status);
int write (int fd, const void *buffer, unsigned size);
int open (const char *file);
int add_file_to_fdt(struct file *file);
static struct file *find_file_by_fd(int fd);
void remove_file_from_fdt(int fd);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell (int fd);
void close(int fd);
tid_t fork (const char *thread_name, struct intr_frame *f);
int exec(char *file_name);
int wait (tid_t pid);



////
#endif /* userprog/syscall.h */
