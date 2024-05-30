#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "filesys/filesys.h"   /* added for PROJECT.2-2 */
#include "include/lib/stdio.h" /* added for PROJECT.2-2 STD_FILENO */
#include "intrinsic.h"
#include "lib/user/syscall.h" /* added for PROJECT.2-2 pid_t */
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

static struct file *convert_fd_to_file(int fd) {
  struct list_elem *e;
  struct thread *cur_thread = thread_current();
  for (e = list_begin(cur_thread->fdt); e != list_end(cur_thread->fdt);
       e = list_next(e)) {
    struct fd_elem *tmp = list_entry(e, struct fd_elem, elem);
    if (fd == tmp->fd) {
      return tmp->file_ptr;
    }
  }
  return NULL;
}

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
  memcpy(&thread_current()->parent_if, f, sizeof(struct intr_frame));

  switch (syscall_num) {
    case SYS_HALT:
      do_halt();
      break;

    case SYS_EXIT: /* int status */
      do_exit(f->R.rdi);
      break;

    case SYS_FORK: /* const char *thread_name */
      f->R.rax = do_fork(f->R.rdi);
      break;

    case SYS_EXEC: /* const char *file */
      f->R.rax = do_exec(f->R.rdi);
      break;

    case SYS_WAIT: /* pid_t */
      f->R.rax = do_wait(f->R.rdi);
      break;

    case SYS_CREATE: /* const char *file, unsigned initial_size */
      f->R.rax = do_create(f->R.rdi, f->R.rsi);
      break;

    case SYS_REMOVE: /* const char *file */
      f->R.rax = do_remove(f->R.rdi);
      break;

    case SYS_OPEN: /* const char *file */
      f->R.rax = do_open(f->R.rdi);
      break;

    case SYS_FILESIZE: /* int fd */
      f->R.rax = do_filesize(f->R.rdi);
      break;

    case SYS_READ: /* int fd, void *buffer, unsigned length */
      f->R.rax = do_read(f->R.rdi, f->R.rsi, f->R.rdx);
      break;

    case SYS_WRITE: /* int fd, const void *buffer, unsigned length */
      f->R.rax = do_write(f->R.rdi, f->R.rsi, f->R.rdx);
      break;

    case SYS_SEEK: /* int fd, unsigned position */
      do_seek(f->R.rdi, f->R.rsi);
      break;

    case SYS_TELL: /* int fd */
      f->R.rax = do_tell(f->R.rdi);
      break;

    case SYS_CLOSE: /* int fd */
      do_close(f->R.rdi);
      break;

      // case SYS_DUP2: /* int oldfd, int newfd */
      //   dup2(f->R.rdi, f->R.rsi);
      //   break;

    default: /* undefined syscall */
      printf("syscall ERROR ! \n");
      do_exit(-1);
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
void validate_adress(const void *addr) {
  struct thread *curr_t = thread_current();

  if (is_kernel_vaddr(addr)) do_exit(-1);

  if (addr == NULL || !is_user_vaddr(addr)) do_exit(-1);

  if (pml4_get_page(curr_t->pml4, addr) == NULL) do_exit(-1);
}

/**
 * @brief 🟢 pintOS를 종료한다.
*/
void do_halt(void) { power_off(); }

/**
 * @brief 🟢 current Process를 종료한다.
 * 
 * @param status Process의 종료 상태
 * 
 * @details process 정상종료 ? status = 0 : status = -1
*/
void do_exit(int status) {
  struct thread *cur = thread_current();

  cur->exit_status = status;

  printf("%s: exit(%d)\n", cur->name, status);

  thread_exit();
}

/**
 * @brief 🟢 파일을 생성한다.
 * 
 * @param file 생성할 파일의 이름 및 경로 정보
 * @param initial_size 생성할 파일 크기
 * 
 * @return bool 파일 생성 성공 여부
*/
bool do_create(const char *file, unsigned initial_size) {
  bool success;

  validate_adress(file);

  lock_acquire(&filesys_lock);
  success = filesys_create(file, initial_size);
  lock_release(&filesys_lock);

  return success ? true : false;
}
/**
 * @brief 🟢 파일을 삭제한다.
 * 
 * @param file 삭제할 파일의 이름 및 경로 정보
 * 
 * @return bool 파일 삭제 성공 여부
*/
bool do_remove(const char *file) {
  bool succ;

  validate_adress(file);

  lock_acquire(&filesys_lock);
  succ = filesys_remove(file);
  lock_release(&filesys_lock);

  return succ;
}

/**
 * @brief file을 open하고 file descriptor를 반환한다.
 * 
 * @param file open할 file의 이름 및 경로 정보
*/
int do_open(const char *file) {
  validate_adress(file);

  struct thread *cur_thread = thread_current();
  lock_acquire(&filesys_lock);
  struct file *file_ptr = filesys_open(file);
  lock_release(&filesys_lock);
  if (!file_ptr) {
    return -1;
  }
  // if (!strcmp(file, cur_thread->name)) {
  //   file_deny_write(file_ptr);
  // }
  struct fd_elem *file_elem = (struct fd_elem *)malloc(sizeof(struct fd_elem));
  file_elem->fd = cur_thread->next_fd++;
  file_elem->file_ptr = file_ptr;
  list_push_back(cur_thread->fdt, &file_elem->elem);
  return file_elem->fd;
}

/**
 * @brief 🟢 fd에 해당하는 file의 크기를 반환한다.
 * 
 * @param fd file descriptor
*/
int do_filesize(int fd) {
  // struct file *file_p;
  // off_t result;

  // if (fd < 0) return -1;

  // file_p = process_get_file(fd);

  // if (!file_p) return -1;

  // lock_acquire(&filesys_lock);
  // result = file_length(file_p);
  // lock_release(&filesys_lock);

  // return result;

  struct thread *cur_thread = thread_current();
  struct file *file_ptr = convert_fd_to_file(fd);
  int ret = file_ptr ? file_length(file_ptr) : -1;
  return ret;
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
int do_read(int fd, void *buffer, unsigned length) {
  struct thread *curr = thread_current();
  struct file *file_p;
  size_t read_bytes = 0;

  validate_adress(buffer);

  if (fd < 0 || fd >= curr->next_fd || fd == STDOUT_FILENO) {
    return -1;
  }

  if (fd == STDIN_FILENO) {
    for (unsigned i = 0; i < length; i++) {
      ((char *)buffer)[i] = input_getc();

      if (((char *)buffer)[i] == '\n') return i + 1;
    }

    return length;
  }

  file_p = convert_fd_to_file(fd);
  if (!file_p) return -1;

  lock_acquire(&filesys_lock);
  read_bytes = file_read(file_p, buffer, length);
  lock_release(&filesys_lock);

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
int do_write(int fd, const void *buffer, unsigned length) {
  struct thread *curr = thread_current();
  struct file *file_p;
  int write_bytes = 0;

  validate_adress(buffer);

  if (fd < 0 || fd >= curr->next_fd || fd == STDIN_FILENO) {
    return -1;
  }

  lock_acquire(&filesys_lock);

  if (fd == STDOUT_FILENO) {
    putbuf(buffer, length);
    write_bytes = length;
  } else {
    file_p = convert_fd_to_file(fd);

    if (!file_p) {
      lock_release(&filesys_lock);
      return -1;
    }

    write_bytes = file_write(file_p, buffer, length);
  }

  lock_release(&filesys_lock);

  return write_bytes;
}

void do_close(int fd) {
  struct thread *cur_thread = thread_current();
  struct list_elem *e;
  struct fd_elem *file_elem;
  for (e = list_begin(cur_thread->fdt); e != list_end(cur_thread->fdt);
       e = list_next(e)) {
    file_elem = list_entry(e, struct fd_elem, elem);

    if (fd == file_elem->fd) {
      break;
    }
  }
  if (e == list_end(cur_thread->fdt)) {
    do_exit(-1);
  }
  list_remove(e);
  lock_acquire(&filesys_lock);
  file_close(file_elem->file_ptr);
  lock_release(&filesys_lock);
  free(file_elem);
}

/**
 * @brief open된 file(fd)의 읽기, 쓰기 위치를 position으로 이동한다.
 * 
 * @param fd file descriptor
 * @param position 이동할 위치
*/
void do_seek(int fd, unsigned position) {
  struct thread *curr = thread_current();
  struct file *file;

  if (fd < 2) return NULL;

  file = convert_fd_to_file(fd);

  if (!file) return NULL;

  lock_acquire(&filesys_lock);
  file_seek(file, position);
  lock_release(&filesys_lock);
}

unsigned do_tell(int fd) {
  struct file *file;
  off_t position;

  file = convert_fd_to_file(fd);

  if (!file) return -1;

  lock_acquire(&filesys_lock);
  position = file_tell(file);
  lock_release(&filesys_lock);

  if (position < 0) return -1;

  return position;
}

/**
 *@brief 현재 process의 복제본 프로세스(child)를 생성합니다.
 *
 *@param thread_name 생성할 프로세스의 이름
*/
pid_t do_fork(const char *thread_name) {
  validate_adress(thread_name);

  struct thread *t = thread_current();
  pid_t child_pid = process_fork(thread_name, NULL);

  if (child_pid == TID_ERROR) {
    return TID_ERROR;
  }

  struct thread *child_thread;
  struct list_elem *e;
  for (e = list_begin(&t->child_list); e != list_end(&t->child_list);
       e = list_next(e)) {
    child_thread = list_entry(e, struct thread, child_elem);
    if (child_thread->tid == child_pid) {
      break;
    }
  }
  if (e == list_end(&t->child_list)) {
    return TID_ERROR;
  }

  sema_down(&child_thread->fork_sema);
  if (child_thread->exit_status == TID_ERROR) {
    return TID_ERROR;
  }

  return child_pid;

  // return process_fork(thread_name, NULL);
}

int do_exec(const char *cmd_line) {
  char *file_copy = malloc(strlen(cmd_line) + 1);
  int result = -1;

  strlcpy(file_copy, cmd_line, strlen(cmd_line) + 1);
  result = process_exec(file_copy);
  free(file_copy);

  if (result == -1) return -1;
}

int do_wait(pid_t pid) { return process_wait(pid); }

/* ----------------------------------------------------- */