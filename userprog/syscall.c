#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
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


	if (!is_user_vaddr(addr) || addr == NULL || pml4_get_page(t->pml4, addr) == NULL)
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
	
	lock_acquire(&filesys_lock);

	if (filesys_create(file, initial_size)) {
		lock_release(&filesys_lock);
		return true;
	} else {
		lock_release(&filesys_lock);
		return false;
	}
}

bool remove (const char *file) {
	check_address(file);

	lock_acquire(&filesys_lock);

	if (filesys_remove(file)) {
		lock_release(&filesys_lock);
		return true;
	} else {
		lock_release(&filesys_lock);
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
	lock_acquire(&filesys_lock);
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
	lock_release(&filesys_lock);
    return fd;
}

 /* 파일을 현재 프로세스의 fdt에 추가 */
int add_file_to_fdt(struct file *file)
{
    struct thread *cur = thread_current();
    struct file **fdt = cur->fd_table;

    //  fd 범위를 넘지 않는 선에서, 할당 가능한 fd 번호를 찾는다.
    while (cur->fd_idx < FDT_COUNT_LIMIT && fdt[cur->fd_idx])
    {
        cur->fd_idx++;
    }

    // fd table이 꽉 찼을 경우 -1 리턴
    if (cur->fd_idx >= FDT_COUNT_LIMIT)
        return -1;

	// fd table에 파일을 할당하고 fd 번호를 리턴한다
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

	lock_acquire(&filesys_lock);
	int fileLength = file_length(open_file);
	lock_release(&filesys_lock);

    return fileLength;
}

int read(int fd, void *buffer, unsigned size)
{
    check_address(buffer);
	
	// 읽은 바이트 수 저장할 변수
    off_t read_byte;
	// 버퍼를 바이트 단위로 접근하기 위한 포인터
    uint8_t *read_buffer = buffer;

	// 표준입력일 경우 데이터를 읽는다
    if (fd == 0)
    {
        char key;
        for (read_byte = 0; read_byte < size; read_byte++)
        {
			// input_getc 함수로 입력을 가져오고, buffer에 저장한다
            key = input_getc();
            *read_buffer++ = key;

			// 널 문자를 만나면 종료한다.
            if (key == '\0')
            {
                break;
            }
        }
    }
	// 표준출력일 경우 -1을 리턴한다.
    else if (fd == 1)
    {
        return -1;
    }
	// 2이상, 즉 파일일 경우 파일을 읽어온다.
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

	// 읽어온 바이트 수 리턴
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

	// fd에 해당하는 파일 찾기
    struct file *fileobj = find_file_by_fd(fd);
    if (fileobj == NULL)
    {
        return;
    }

	// fd table에서 파일 삭제하기
    remove_file_from_fdt(fd);

	lock_acquire(&filesys_lock);
	// 파일 닫기
	file_close(fileobj);
	lock_release(&filesys_lock);
}


tid_t fork(const char *thread_name, struct intr_frame *f) {
	
	return process_fork(thread_name, f);
}


int exec(char *file_name)
{
    check_address(file_name);

	// file_name의 길이를 구한다. 
	// strlen은 널 문자를 포함하지 않기 때문에 널 문자 포함을 위해 1을 더해준다.
    int file_name_size = strlen(file_name) + 1;

	// 새로운 페이지를 할당받고 0으로 초기화한다.(PAL_ZERO)
	// 여기에 file_name을 복사할 것이다
    char *fn_copy = palloc_get_page(PAL_ZERO);
    if (fn_copy == NULL)
    {
        exit(-1);
    }

	// file_name 문자열을 file_name_size만큼 fn_copy에 복사한다
    strlcpy(fn_copy, file_name, file_name_size);

	// process_exec 호출, 여기서 인자 파싱 및 file load 등등이 일어난다.
	// file 실행이 실패했다면 -1을 리턴한다.
    if (process_exec(fn_copy) == -1)
    {
        return -1;
    }

    NOT_REACHED();
    return 0;
}


int wait (tid_t pid)
{
	// pid에 해당하는 자식 프로세스가 종료되기를 기다린다.
	process_wait(pid);
}

