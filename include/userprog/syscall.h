#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdbool.h>
#include "threads/synch.h"


struct lock filesys_lock;

void syscall_init (void);

void check_address(void *addr);
void halt(void);
void exit (int status);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int exec (const char *file_name);
int open(const char *file);
int filesize(int fd);
void close (int fd);
void seek (int fd, unsigned position);
unsigned tell (int fd);
int read (int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);

int add_file_to_fdt(const char *file);
static struct file *find_file_by_fd(int fd);
void remove_file_from_fdt(int fd);



#endif /* userprog/syscall.h */
