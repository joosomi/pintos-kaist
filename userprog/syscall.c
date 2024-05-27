#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "filesys/filesys.h" /* added for PROJECT.2-2 */
#include "intrinsic.h"
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/loader.h"
#include "threads/synch.h" /* added for PROJECT.2-2 */
#include "threads/thread.h"
#include "userprog/gdt.h"
#include "userprog/process.h" /* added for PROJECT.2-2 */

void syscall_entry(void);
void syscall_handler(struct intr_frame *);

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

void syscall_init(void) {
  write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 | ((uint64_t)SEL_KCSEG)
                                                               << 32);
  write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

  /* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
  write_msr(MSR_SYSCALL_MASK,
            FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

  /* --------------- added for PROJECT.2-2 --------------- */

  lock_init(&filesys_lock); /* read(), write()시에 동기화 문제를 위한 lock  */

  /* ----------------------------------------------------- */
}

/**
 * @brief System call num에 맞는 System call 호출 handler
 * 
 * @param f interrupt frame ->R.rax에 syscall_num이 저장되어있음
 * 
 * @note  The main system call interface  
 * 
 * @details passing arguments register
 *          %rdi(1st), %rsi(2nd), %rdx(3rd), %r10(4th), %r8(5th), %r9(6th)
*/
void syscall_handler(struct intr_frame *f UNUSED) {
  /* --------------- added for PROJECT.2-2 --------------- */

  int syscall_num = f->R.rax;

  switch (syscall_num) {
    case SYS_HALT:
      halt();
      break;

    case SYS_EXIT: /* int status */
      exit((int)(f->R.rdi));
      break;

      // case SYS_FORK: /* const char *thread_name */
      //   fork(f->R.rdi);
      //   break;

      // case SYS_EXEC: /* const char *file */
      //   exec(f->R.rdi);
      //   break;

      // case SYS_WAIT: /* pid_t */
      //   wait(f->R.rdi);
      //   break;

    case SYS_CREATE: /* const char *file, unsigned initial_size */
      create((char *)(f->R.rdi), f->R.rsi);
      break;

    case SYS_REMOVE: /* const char *file */
      remove((char *)(f->R.rdi));
      break;

    case SYS_OPEN: /* const char *file */
      open((char *)(f->R.rdi));
      break;

    case SYS_FILESIZE: /* int fd */
      filesize(f->R.rdi);
      break;

    case SYS_READ: /* int fd, void *buffer, unsigned length */
      read(f->R.rdi, (void *)(f->R.rsi), f->R.rdx);
      break;

    case SYS_WRITE: /* int fd, const void *buffer, unsigned length */
      write(f->R.rdi, (void *)(f->R.rsi), f->R.rdx);
      break;

    case SYS_SEEK: /* int fd, unsigned position */
      seek(f->R.rdi, f->R.rsi);
      break;

    case SYS_TELL: /* int fd */
      tell(f->R.rdi);
      break;

    case SYS_CLOSE: /* int fd */
      close(f->R.rdi);
      break;

      // case SYS_DUP2: /* int oldfd, int newfd */
      //   dup2(f->R.rdi, f->R.rsi);
      //   break;

    default: /* undefined syscall */
      exit(-1);
      break;
  }

  /* --------------- before PROJECT.2-2 --------------- 
  printf("system call!\n"); 
  thread_exit(); */
}

/* --------------- added for PROJECT.2-2 --------------- */

/**
 * @brief pointer address가 kernel address에 접근하는지 확인하고
 *        접근한다면 throw 한다.
 * 
 * @param addr pointer address
*/
void check_user_address(const void *addr) {
  struct thread *curr_t = thread_current();

  if (is_kernel_vaddr(addr) || addr == NULL ||
      pml4_get_page(curr_t->pml4, addr) == NULL)
    exit(-1);
}

/**
 * @brief pintOS를 종료한다.
*/
void halt(void) { power_off(); }

/**
 * @brief current Process를 종료한다.
 * 
 * @param status Process의 종료 상태
 * 
 * @details process 정상종료 ? status = 0 : status = -1
*/
void exit(int status) {
  struct thread *cur = thread_current();

  cur->exit_status = status;

  printf("%s: exit(%d)\n", cur->name, status);

  thread_exit();
}

/**
 * @brief 파일을 생성한다.
 * 
 * @param file 생성할 파일의 이름 및 경로 정보
 * @param initial_size 생성할 파일 크기
 * 
 * @return bool 파일 생성 성공 여부
*/
bool create(const char *file, unsigned initial_size) {
  check_user_address(file);

  return filesys_create(file, initial_size);
}

/**
 * @brief 파일을 삭제한다.
 * 
 * @param file 삭제할 파일의 이름 및 경로 정보
 * 
 * @return bool 파일 삭제 성공 여부
*/
bool remove(const char *file) {
  check_user_address(file);

  return filesys_remove(file);
}

/**
 * @brief file을 open하고 file descriptor를 반환한다.
 * 
 * @param file open할 file의 이름 및 경로 정보
*/
int open(const char *file) {
  struct thread *curr = thread_current();
  struct file *file_p;
  int fd;

  check_user_address(file);

  file_p = filesys_open(file);

  if (!file_p) return -1;

  fd = process_add_file(file_p);

  if (fd == -1) {
    exit(-1);
    return;
  }

  return fd;
}

/**
 * @brief fd에 해당하는 file의 크기를 반환한다.
 * 
 * @param fd file descriptor
*/
int filesize(int fd) {
  struct thread *curr = thread_current();
  struct file *file_p;

  // ASSERT(fd < curr->next_fd);
  ASSERT(fd >= 0);

  curr = file_p = process_get_file(fd);

  if (!file_p) return -1;

  return file_length(file_p);
}

/**
 * @brief fd에 해당하는 file에서 length만큼 읽어 buffer에 저장한다.
 * 
 * @param fd file descriptor
 * @param buffer file에서 읽어온 데이터를 저장할 buffer
 * @param length 읽어올 데이터의 크기
 * 
 * @return int 읽어온 데이터의 크기
 * 
 * @details [use case]
 *          if (fd == 0)
 *          키보드의 모든 입력이 read()로 처리된다. 현실세계(?)에서 터미널, 게임, 
 *          타자연습 등등 사용했던 모든 키보드 입력은 read() 시스템콜이었다.
 *          
 *          else 
 *          file에서 읽어오는 경우다. file에서 length만큼 읽어 buffer에 저장한다.
 * 
 *          input_getc() : 키보드로부터 한 문자를 읽어온다. 키보드를 입력하지
 *          않았을땐 계속해서 기다린다.
 * 
 *          input_getc() 예제를 통해 이해해보자.
 *          user가 "abcdef"를 입력할것이라 가정하자
 *          그렇다면 a를 입력했을때 즉, keyboard를 눌렀을떄 input_getc()가 
 *          호출되어 'a'를 반환하게 된다.
 *          그리고 for문을 통해 length만큼 반복되지 않았따면 계속해서
 *          input_getc()는 기다린다.
 * 
 *          이후 enter(= '\n')을 입력하게 되면 for문을 탈출한다.
 * 
 *          file_read() : Reads SIZE bytes from FILE into BUFFER,
 *          즉, file에서 length만큼 읽어서 buffer(user가 볼 수 있는)에 저장한다.
*/
int read(int fd, void *buffer, unsigned length) {
  struct thread *curr = thread_current();
  struct file *file_p;
  int read_bytes = 0;

  /* exception handling */
  check_user_address(buffer);

  // ASSERT(fd < curr->next_fd);
  ASSERT(fd >= 0);

  lock_acquire(&filesys_lock); /* read()시에 동기화 문제를 위한 lock */

  /* (fd == 0) 즉, 키보드의 입력을 받는경우 */
  if (fd == 0) {
    for (unsigned i = 0; i < length; i++) {
      ((char *)buffer)[i] = input_getc();

      if (((char *)buffer)[i] == '\n') { /* "enter" 입력시 탈출 */
        i++;
        length = i;
        break;
      }
    }

    return length;
  }

  else {
    file_p = process_get_file(fd); /* fd에 해당하는 file을 가져온다 */

    if (!file_p) return -1;

    read_bytes = file_read(file_p, buffer, length);
  }

  lock_release(&filesys_lock); /* read()시에 동기화 문제를 위한 lock 해제 */

  return read_bytes;
}

/**
 * @brief buffer에 있는 데이터를 fd에 해당하는 file에 length 만큼 저장한다.
 * 
 * @param fd file descriptor
 * @param buffer file에 저장할 데이터
 * @param length file에 저장할 데이터의 크기
 * 
 * @return int file에 저장된 데이터의 크기
 * 
 * @details [use case]
 *          if (fd == 1)
 *          -> 터미널에 출력하는 경우, 한발 더 나아가 모니터에 출력된다고 봐도
 *          무방하다
 * 
 *          else
 *          -> file에 저장하는 경우다. file에 buffer에 있는 데이터를 length만큼
 *          저장한다.
 * 
 *          putbuf() : buffer에 있는 데이터를 length만큼 console에 출력한다.
*/
int write(int fd, const void *buffer, unsigned length) {
  struct thread *curr = thread_current();
  struct file *file_p;
  int write_bytes = 0;

  /* exception handling */
  check_user_address(buffer);

  /* ✅ TODO 
     ASSERT(fd < curr->next_fd); */
  ASSERT(fd >= 0);

  lock_acquire(&filesys_lock); /* write()시에 동기화 문제를 위한 lock */

  /* (fd == 1) 즉, 표준 출력으로 출력하는 경우 */
  if (fd == 1) {
    putbuf(buffer, length);
    write_bytes = length;
  }

  else {
    file_p = process_get_file(fd); /* fd에 해당하는 file을 가져온다 */

    if (!file_p) return -1;

    write_bytes = file_write(file_p, buffer, length);
  }

  lock_release(&filesys_lock); /* write()시에 동기화 문제를 위한 lock 해제 */

  curr->tf.R.rax = write_bytes;

  return write_bytes;
}

/**
 * 
*/
void close(int fd) {
  struct thread *curr = thread_current();
  struct file *file;

  /* fdt를 64까지 한바퀴 돌고나서 중간에 삭제해 비어있는 곳에
     next_fd가 가르킨다면..?
     즉, 30인 fd를 삭제해서 next_fd가 30인데 60번째 등록된걸 삭제하고싶은경우 */
  // ASSERT(fd < curr->next_fd);
  ASSERT(fd >= 2);

  file = process_get_file(fd);

  if (!file) return;

  process_close_file(fd);
}

/**
 * @brief open된 file(fd)의 읽기, 쓰기 위치를 position으로 이동한다.
 * 
 * @param fd file descriptor
 * @param position 이동할 위치
*/
void seek(int fd, unsigned position) {
  struct thread *curr = thread_current();
  struct file *file;

  // ASSERT(fd < curr->next_fd);
  ASSERT(fd >= 2);

  file = process_get_file(fd);

  if (!file) return;

  file_seek(file, position);
}

unsigned tell(int fd) {
  struct thread *curr = thread_current();
  struct file *file;
  off_t position;

  // ASSERT(fd < curr->next_fd);
  ASSERT(fd >= 2);

  file = process_get_file(fd);

  if (!file) return -1;

  position = file_tell(file);

  if (position < 0) return -1;

  return position;
}

/* ----------------------------------------------------- */