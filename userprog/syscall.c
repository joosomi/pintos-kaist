#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "filesys/file.h"
#include "devices/input.h"


void syscall_entry (void);
void syscall_handler (struct intr_frame *);

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

/*-----------------------------------------------------------*/
	lock_init(&filesys_lock);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	/*User Stack에 저장되어 있는 시스템 콜 넘버를 이용해서 시스템 콜 핸들러 구현
	스택 포인터가 유저 영역인지 확인
	저장된 인자 값이 포인터일 경우 유저 영역의 주소인지 확인
	0: halt, 1: exit . . . */

	int syscall_num = f->R.rax; //rax: system call number

	/* 
	인자 들어오는 순서:
	1번째 인자: %rdi
	2번째 인자: %rsi
	3번째 인자: %rdx
	4번째 인자: %r10
	5번째 인자: %r8
	6번째 인자: %r9 
	*/

	switch (syscall_num) {
		case SYS_HALT:
			halt();
			break;
		case SYS_EXIT:
			exit(f->R.rdi);
			break;
		// case SYS_FORK:
		// 	fork(f->R.rdi);
			// break;
		case SYS_EXEC:	
			exec(f->R.rdi);
			break;
		// case SYS_WAIT:
		// 	wait(f->R.rdi);
		//	break;
		case SYS_CREATE:
			f->R.rax = create(f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE:
			f-> R.rax = remove(f->R.rdi);
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
	}
}

/*-----------------------------------------------------------------*/
/*----Project 2 : User Memory Access----*/
// *addr: 검사하고자 하는 메모리 주소를 가리키는 포인터 
void check_address(void *addr) {
	/*포인터가 가리키는 주소가 유저 영역의 주소인지 확인
	잘못된 접근일 경우 프로세스 종료*/

	struct thread *cur = thread_current();
	
	if (!(is_user_vaddr(addr)) || pml4_get_page(cur-> pml4, addr) == NULL || addr == NULL) {
		exit(-1);
	}

	/*is_user_vaddr(addr): 사용자 영역의 가상 메모리 주소인지 확인하는 함수 
	pml4_get_page(t->pml4, addr): 가상 메모리 주소의 유효성 판단 -> 해당 주소에 매핑된 페이지가 있는지 확인
		=> NULL 반환하면 가상 주소가 유효하지 않거나 접근할 수 없는 영역*/
}

/*-----------------------------------------------------------*/
/*halt(): pintos 종료시키는 시스템 콜 */
void halt(void) {
	power_off();
}

/*exit: 현재 프로세스만 종료시키는 시스템 콜
- 정상적으로 종료시 status 0
- status: 프로그램이 정상적으로 종료됐는지 확인*/
// 쓰레드를 종료시키는 함수는 thread_exit()

void exit (int status){
	struct thread *cur = thread_current();
	cur->exit_status = status;
	printf("%s: exit(%d)\n", cur->name, status);
	thread_exit(); //쓰레드 종료
}



/*create: 파일을 생성하는 시스템 콜 
- 성공 true, 실패일 경우 false 리턴
- file: 생성할 파일의 이름 및 경로 정보
- initial_size: 생성할 파일의 크기*/
bool create (const char *file, unsigned initial_size) {
	/*주소 값이 사용자 영역에서 사용하는 주소 값인지 확인*/
	check_address(file);

	bool success =filesys_create(file, initial_size);
		
	return success;
}

/*remove: 파일을 삭제하는 시스템 콜
- file: 제거할 파일의 이름 및 경로 정보
- 성공 true, 실패일 경우 false 리턴*/
bool remove (const char *file){
	/*주소 값이 사용자 영역에서 사용하는 주소 값인지 확인*/
	check_address(file);

	if(filesys_remove(file)) return true;
	else return false;
}


/*-----------------------------------------------------------*/
/*exec: 현재 프로세스를 명령어로 입력 받은 실행 파일로 변경하는 함수*/
int exec (const char *file_name){
	/*주소 값이 사용자 영역에서 사용하는 주소값인지 확인, 유효한지 확인*/
	check_address(file_name); 
	
	int file_size = strlen(file_name) + 1 ; // null 문자 포함하여 + 1 하여 파일 이름의 길이 계산

	/*palloc_get_page(): 페이지 단위로 메모리를 할당하는 함수 
	PAL_ZERO flag를 활용해서 할당된 메모리를 0으로 초기화 -> 쓰레기 값이 남지 않도록*/
	char *fn_copy = palloc_get_page(PAL_ZERO);
	
	/*메모리 할당에 실패했을 경우*/
	if (fn_copy == NULL){
		exit(-1);
	}

	/*메모리 할당에 성공했을 경우
	- 할당된 메모리 영역에 파일 이름 복사*/
	strlcpy(fn_copy, file_name, file_size);

	/*process_exec(fn_copy)를 호출해서 복사된 파일의 이름으로 프로세스 실행
	-> 실패시 exit(-1) 프로그램 종료*/
	if (process_exec(fn_copy) == -1) {
		exit(-1);
	}
}

/*-----------------------------------------------------------*/
/*wait: */
// int wait (pid_t pid){}


/*fork: */
// pid_t fork (const char *thread_name){}


/*-----------------------------------------------------------*/
/* 현재 쓰레드의  file descriptor table에 file 추가
해당 파일에 할당된 파일 디스크립터 반환 */
int add_file_to_fdt(const char *file){	
	struct thread *cur = thread_current();
	struct file **fdt = cur->fdt; //현재 쓰레드의 file descriptor table

	int fd_idx = cur->next_fd_idx; 

	//FDT에서 사용 가능한 인덱스 찾기 
	while (cur->fdt[fd_idx] != NULL && fd_idx < FDT_COUNT_LIMIT){
		fd_idx++;
	}

	//사용 가능한 인덱스가 없을 경우 return -1
	if (fd_idx >= FDT_COUNT_LIMIT) {
		return -1 ;
	}

	cur->next_fd_idx  = fd_idx; //다음 사용 가능한 인덱스 업데이트
	fdt[fd_idx] = file; //FDT에 파일 추가
	return fd_idx; //할당된 파일 디스크립터 반환

}


/*open: 주어진 파일 이름을 기반으로 파일을 열어주는 함수
파일 열기에 성공하면 0이 아닌 정수의 file descriptor를, 실패하면 return -1

0(STDIN_FILENO), 1(STDOUT_FILENO)은 console을 위해 이미 예약되어 있는 fd 번호
각 프로세스는 고유의 fd 집합을 가지고 자식 프로세스도 물려 받음. */
int open (const char *file){
	check_address(file);

	struct file *open_file = filesys_open(file); //file open

	//파일 열기 실패시 return -1
	if (open_file == NULL) {
		return -1;
	}

	int fd = add_file_to_fdt(open_file);

	//fd == -1: 파일을 열 수 없는 경우
	if (fd == -1) {
		file_close(open_file);
		return -1; 
	}

	return fd;
}

/*-----------------------------------------------------------*/

/*filesize: 파일의 크기를 알려주는 시스템 콜
성공 시 파일의 크기가 몇 바이트인지 크기 return, 실패 시 -1 return*/
int filesize (int fd){
	struct file *open_file = find_file_by_fd(fd);

	if (open_file == NULL) {
		return -1;
	}

	return file_length(open_file);
}


/*파일 디스크립터를 이용하여 파일 객체 검색
해당 파일의 길이 return, 해당 파일이 존재하지 않으면 -1 return*/
static struct file *find_file_by_fd(int fd) {
	struct thread *cur = thread_current();

	if (fd < 0 || fd >= FDT_COUNT_LIMIT){
		return -1;
	}

	return cur->fdt[fd];
}

/*-----------------------------------------------------------*/
/*read(), write() => 
- 시스템 콜에서 파일 접근하기 전에 Lock을 획득하도록 구현, 파일에 접근이 끝난 뒤 Lock 해제
- STDIN(standard input) = 0, STDOUT(standard output) = 1 
*/

/*read: 열린 파일의 데이터를 읽는 시스템 콜,
- 성공 시 읽은 바이트 수 return , 실패 시 -1 반환
- buffer: 읽은 데이터를 저장할 버퍼의 주소 값
- size: 읽을 데이터 크기
- fd = 0이면 키보드의 데이터를 읽어 버퍼에 저장 (input_getc()이용)

buffer 안에 fd로 열려있는 파일로부터 size byte를 읽음
*/
int read (int fd, void *buffer, unsigned size){
	check_address(buffer);	
	check_address(buffer + size -1 ); //버퍼의 끝 주소도 사용자 영역 내에 있는지 유효성 검사

	int read_size;  //읽은 크기
	unsigned char *buf = buffer;

	// 파일 디스크립터를 파일 객체로 변환
	struct file *read_file = find_file_by_fd(fd);


	/*파일 객체가 유효하지 않거나,
	표준 출력을 가리키는 경우 return -1*/
	if (read_file == NULL || read_file == STDOUT_FILENO){
		return -1;
	}

	//표준 입력 STDIN(0)일 경우 읽기 진행
	if (read_file == STDIN_FILENO) {
		for (read_size = 0; read_size < size; read_size ++){
			char key = input_getc(); //한 문자씩 입력 받음
			*buf++ = key; //받은 문자를 버퍼에 저장
			
			//NULL 문자(\0) 만나면 반복 종료
			if (key == '\0'){
				break;
			}
		}
	}
	//그 외의 파일 디스크립터
	else {
		//파일 시스템 접근시 동시성 문제를 피하기 위해 Lock 걸기
		lock_acquire(&filesys_lock);
		read_size = file_read(read_file, buffer, size); //파일에서 데이터 읽기
		lock_release(&filesys_lock); // Lock 해제
	}

	return read_size; //실제로 읽어들인 byte 수 반환
}


/*write: 열린 파일의 데이터를 기록하는 시스템 콜
- 성공 시 기록한 데이터의 바이트 수 return, 실패 시 -1 return
- buffer: 기록할 데이터를 저장한 버퍼의 주소값
- size: 기록할 데이터 크기
- fd 값이 1일 때 버퍼에 저장된 데이터를 화면에 출력(putbuf() 이용)*/
int write (int fd, const void *buffer, unsigned size){
	/*주어진 버퍼의 주소 유효성 검사*/
	check_address(buffer);

	struct file *open_file = find_file_by_fd(fd);

	int written_bytes = 0;

	//파일 시스템 동시성 관리를 위한 lock 획득 
	lock_acquire(&filesys_lock);
	
	//파일 디스크립터가 STDOUT_FILENO(1)이면 버퍼에 저장된 값을 화면에 출력
	//putbuf(): 버퍼 안에 들어있는 값 중 size만큼을 console로 출력
	if (fd == STDOUT_FILENO){
		putbuf(buffer, size);
		
		//실제로 쓰여진 바이트 수 = 요청한 크기
		written_bytes = size;
	}
	//파일 디스크립터가 STDIN_FILENO(0, 표준 입력)이면
	//쓰기 작업은 유효하지 않음
	else if (fd == STDIN_FILENO){
		lock_release(&filesys_lock);
		return -1;
	}
	//파일 디스크립터 >= 2 (일반 파일의 경우)
	//해당 파일 디스크립터에 매칭되는 파일 객체가 없으면 return -1
	else if (fd >= 2) {
		if (open_file == NULL) {
			lock_release(&filesys_lock);
			return -1;
		} else {
			//파일 객체가 유효하면, 주어진 buffer에서 file에 데이터 씀
			written_bytes = file_write(open_file, buffer, size);
		}
	
	}
	lock_release(&filesys_lock);
	return written_bytes;
}


/*seek: 열린 파일의 위치(offset)를 이동하는 시스템 콜
- position: 현재 위치(offset)를 기준으로 이동할 거리
- 파일 디스크립터를 이용하여 파일 객체 검색, 해당 열린 파일의 offset을 position 만큼 이동*/
void seek (int fd, unsigned position){
	struct file *seek_file = find_file_by_fd(fd);


	if (seek_file != NULL){
		file_seek(seek_file, position);
	}	
}


/*tell: 열린 파일의 위치(offset)를 알려주는 시스템 콜 
- 파일 디스크립터를 이용하여 파일 객체 검색, 해당 열린 파일의 위치 반환*/
unsigned tell (int fd){
	struct file *tell_file = find_file_by_fd(fd);


	if (tell_file != NULL) {
		return file_tell(tell_file);
	}

	return 0;
}

/*close: 열린 파일을 닫는 시스템 콜
파일을 닫고 file descriptor를 제거*/
void close (int fd){
	/*해당 파일 디스크립터에 해당하는 파일을 닫음
	파일 디스크립터 엔트리 초기화*/
	struct file *file = find_file_by_fd(fd);
	if(file== NULL){
		return;
	}

	remove_file_from_fdt(fd);

	if (fd<=2 || file <=2){
		return;
	}

	file_close(file);
}


/*
파일 디스크립터에 해당하는 파일을 닫고 해당 엔트리 초기화*/
void remove_file_from_fdt(int fd) {
	struct thread *cur = thread_current();
	struct file **fdt = cur->fdt;

	if (fd < 2 || fd >= FDT_COUNT_LIMIT) {
		return NULL;
	}

	fdt[fd] = NULL;
}
/*----------------------------------------------------------*/
