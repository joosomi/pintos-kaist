#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "include/filesys/filesys.h"
#include "include/filesys/file.h"
#include "threads/synch.h"
#include "lib/kernel/console.h"
#include "threads/palloc.h"
#include "devices/input.h"
#include "threads/init.h"
#include "userprog/process.h"


void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
		
	lock_init(&filesys_lock);
}

/* The main system call interface */
void syscall_handler (struct intr_frame *f UNUSED) {
 
	/* rax = 시스템 콜 넘버 */
	int syscall_n = f->R.rax; /* 시스템 콜 넘버 */
	switch (syscall_n)
	{
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		exit(f->R.rdi);
		break;
	case SYS_FORK:
		f->R.rax = fork(f->R.rdi, f);
		break;
	case SYS_EXEC:
		if (exec(f->R.rdi) == -1)
        {
            exit(-1);
        }
		break;
	case SYS_WAIT:
		f->R.rax = wait(f->R.rdi);
		break;
	case SYS_CREATE:
		f->R.rax = create(f->R.rdi, f->R.rsi);
		break;
	case SYS_REMOVE:
		f->R.rax = remove(f->R.rdi);
		break;
	case SYS_OPEN:
		f->R.rax = open(f->R.rdi);
		break;
	case SYS_FILESIZE:
		f->R.rax = filesize(f->R.rdi);
		break;
	case SYS_READ:
		f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_WRITE:
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_SEEK:
		seek(f->R.rdi, f->R.rsi);
		break;
	case SYS_TELL:
		f->R.rax = tell(f->R.rdi);
		break;
	case SYS_CLOSE:
		close(f->R.rdi);
		break;
	default:
		thread_exit();
		break;
	}
}

void check_address(void *addr) {
	struct thread *t = thread_current();

	/* --- Project 2: User memory access --- */
	// if (!is_user_vaddr(addr)||addr == NULL) 
	//-> 이 경우는 유저 주소 영역 내에서도 할당되지 않는 공간 가리키는 것을 체크하지 않음. 그래서 
	// pml4_get_page를 추가해줘야!
	if (!is_user_vaddr(addr)||addr == NULL||
	pml4_get_page(t->pml4, addr)== NULL)
	{
		exit(-1);
	}
}

/* pintos 종료시키는 함수 */
void halt(void){
	power_off();
}

/* 파일 생성하는 시스템 콜 */
bool create (const char *file, unsigned initial_size) {
	/* 성공이면 true, 실패면 false */
	check_address(file);
	if (filesys_create(file, initial_size)) {
		return true;
	}
	else {
		return false;
	}
}

bool remove (const char *file) {
	check_address(file);
	if (filesys_remove(file)) {
		return true;
	} else {
		return false;
	}
}

/* 현재 프로세스를 종료시키는 시스템 콜 */
void exit(int status)
{
	struct thread *t = thread_current();
	t->exit_status = status;
	printf("%s: exit(%d)\n", t->name, status); // Process Termination Message
	/* 정상적으로 종료됐다면 status는 0 */
	/* status: 프로그램이 정상적으로 종료됐는지 확인 */
	thread_exit();
}


int write (int fd, const void *buffer, unsigned size) {
	check_address(buffer);
	struct file *fileobj = find_file_by_fd(fd);
	int read_count;
	if (fd == STDOUT_FILENO) {
		putbuf(buffer, size);
		read_count = size;
	}	
	
	else if (fd == STDIN_FILENO) {
		return -1;
	}

	else {
		
		lock_acquire(&filesys_lock);
		read_count = file_write(fileobj, buffer, size);
		lock_release(&filesys_lock);

	}
}

int open(const char *file)
{
    check_address(file);
    struct file *open_file = filesys_open(file);

    if (open_file == NULL)
    {
        return -1;
    }
    // fd table에 file추가
    int fd = add_file_to_fdt(open_file);

    // fd table 가득 찼을경우
    if (fd == -1)
    {
        file_close(open_file);
    }
    return fd;
}

 /* 파일을 현재 프로세스의 fdt에 추가 */
int add_file_to_fdt(struct file *file)
{
    struct thread *cur = thread_current();
    struct file **fdt = cur->fd_table;

    // Find open spot from the front
    //  fd 위치가 제한 범위 넘지않고, fd table의 인덱스 위치와 일치한다면
    while (cur->fd_idx < FDT_COUNT_LIMIT && fdt[cur->fd_idx])
    {
        cur->fd_idx++;
    }

    // error - fd table full
    if (cur->fd_idx >= FDT_COUNT_LIMIT)
        return -1;

    fdt[cur->fd_idx] = file;
    return cur->fd_idx;
}

/*  fd 값을 넣으면 해당 file을 반환하는 함수 */
static struct file *find_file_by_fd(int fd)
{
    struct thread *cur = thread_current();
    if (fd < 0 || fd >= FDT_COUNT_LIMIT)
    {
        return NULL;
    }
    return cur->fd_table[fd];
}

void remove_file_from_fdt(int fd) {
    struct thread *cur = thread_current();

    // error : invalid fd
    if (fd < 0 || fd >= FDT_COUNT_LIMIT)
        return;

    cur->fd_table[fd] = NULL;
}

/* file size를 반환하는 함수 */
int filesize(int fd)
{
    struct file *open_file = find_file_by_fd(fd);
    if (open_file == NULL)
    {
        return -1;
    }
    return file_length(open_file);
}

int read(int fd, void *buffer, unsigned size)
{
    check_address(buffer);
    off_t read_byte;
    uint8_t *read_buffer = buffer;
    if (fd == 0)
    {
        char key;
        for (read_byte = 0; read_byte < size; read_byte++)
        {
            key = input_getc();
            *read_buffer++ = key;
            if (key == '\0')
            {
                break;
            }
        }
    }
    else if (fd == 1)
    {
        return -1;
    }
    else
    {
        struct file *read_file = find_file_by_fd(fd);
        if (read_file == NULL)
        {
            return -1;
        }
        lock_acquire(&filesys_lock);
        read_byte = file_read(read_file, buffer, size);
        lock_release(&filesys_lock);
    }
    return read_byte;
}

void seek(int fd, unsigned position) {
	if (fd < 2) {
		return;
	}
	struct file *file = find_file_by_fd(fd);
	check_address(file);
	if (file == NULL) {
		return;
	}
	file_seek(file, position);
}

unsigned tell (int fd) {
	if (fd < 2) {
		return;
	}
	struct file *file = find_file_by_fd(fd);
	check_address(file);
	if (file == NULL) {
		return;
	}
	return file_tell(file);
}

void close(int fd){

	if(fd < 2) {
		return;
	}

    struct file *fileobj = find_file_by_fd(fd);
    if (fileobj == NULL)
    {
        return;
    }

    remove_file_from_fdt(fd);

	file_close(fileobj);
}


tid_t fork(const char *thread_name, struct intr_frame *f) {
	
	return process_fork(thread_name, f);
}


int exec(char *file_name)
{
    check_address(file_name);
    int file_size = strlen(file_name) + 1;
    char *fn_copy = palloc_get_page(PAL_ZERO);
    if (fn_copy == NULL)
    {
        exit(-1);
    }
    strlcpy(fn_copy, file_name, file_size); // file 이름만 복사
    if (process_exec(fn_copy) == -1)
    {
        return -1;
    }
    NOT_REACHED();
    return 0;
}


int wait (tid_t pid)
{
	process_wait(pid);
}

