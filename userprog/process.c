#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);

/* General process initializer for initd and other process. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */

/*file_name 복사해서 새로운 메모리 영역에 저장,
file_name을 실행하는 새로운 쓰레드 생성
-> 쓰레드 ID를 반환하거나 실패시 TID_ERROR return

-> 커맨드 라인이 'echo x y z'가 통째로 file name으로 들어간다. 
-> 커맨드 라인 인자 분리하는 과정
*/
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page (0); //file_name의 복사본 저장 -> 페이지 크기의 메모리 할당 - 할당된 페이지 0으로 초기화
	
	if (fn_copy == NULL) 	//메모리 할당에 실패하면 
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE); //fn_copy에 file_name 복사

	/*Argument Passing
	file_name을 받아와서 null 기준으로 문자열 파싱

	strtok_r(): 지정된 문자를 기준으로 문자열을 자름*/
	char *token, parsing;
	token = strtok_r(file_name, " ", &parsing);

	/* Create a new thread to execute FILE_NAME. */
	/*
	tid : 쓰레드의 id -> 시스템에서 각 쓰레드를 고유하게 식별하는 값
		initd: 새로 생성된 쓰레드가 실행할 함수, 전달될 인자: fn_copy
	*/
	tid = thread_create (token, PRI_DEFAULT, initd, fn_copy);
	if (tid == TID_ERROR)
		palloc_free_page (fn_copy);
	return tid;
}

/* A thread function that launches first user process. */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	process_init ();

	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
/* process_fork: 현재 쓰레드를 복제하여 새로운 쓰레드 생성
- name:새로운 쓰레드(자식 프로세스) 이름
- if_: 인터럽트 프레임 주소,  현재 쓰레드의 상태를 자식 쓰레드에 복사하기 위해 사용
*/
tid_t
process_fork (const char *name, struct intr_frame *if_ UNUSED) {
	/* Clone current thread to new thread.*/
	struct thread *parent = thread_current(); //부모 쓰레드 - 현재 쓰레드 

	//부모 쓰레드의 인터럽트 프레임 복사 
	memcpy(&parent->parent_if, if_, sizeof(struct intr_frame));

	//새로운 쓰레드(자식 프로세스) 생성 
	tid_t tid = thread_create(name, PRI_DEFAULT, __do_fork, parent);

	//새로운 쓰레드 생성 실패 시 return TID_ERROR 
	if (tid == TID_ERROR) {
		return TID_ERROR;
	}

	//생성된 자식 쓰레드를 tid를 통해서 찾음
	struct thread *child = get_child_process(tid);
	 // 자식 쓰레드의 fork semaphore를 대기 상태로 만들어서 실행 일시 중지
	 //__do_fork 함수가 실행되어 로드가 완료될 때까지 부모는 대기한다. 
	sema_down(&child->fork_sema);
	
	if (child->exit_status == TID_ERROR) {
		return TID_ERROR;
	} 
	return tid;
}


//현재 쓰레드의 자식 쓰레드 중에서(child_list) 주어진 pid와 일치하는 자식 쓰레드를 찾아서 return 
//int pid => 찾고자 하는 자식 프로세스 식별자 
// 주어진 자식 프로세스 식별자(pid)에 해당하는 쓰레드 구조체 검색 - 존재하지 않으면 NULL return
struct thread *get_child_process (int pid){
	struct thread *cur = thread_current();

	struct list *child_list = &cur->child_list; //현재 쓰레드의 자식 쓰레드 목록

	//자식 쓰레드 목록을 순회하면서 주어진 pid와 일치하는 자식 쓰레드가 있는지 찾는다.
	for (struct list_elem *e = list_begin(child_list); e != list_end(child_list); e = list_next(e)){
		struct thread *t = list_entry(e, struct thread, child_elem);
		
		//일치하는 자식 프로세스 반환
		if (t->tid == pid){ 
			return t;
		}
	}
	return NULL; //일치하는 자식 쓰레드가 없을 경우 return NULL
}


#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
// 부모 프로세스의 주소 공간을 자식 프로세스로 복사하는 기능 
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current (); //현재 실행 중인 쓰레드
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable; //페이지가 쓰기 가능한지 여부

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	//만약 va가 커널 주소라면 return true
	if (is_kernel_vaddr(va)) {
		return true;
	}

	/* 2. Resolve VA from the parent's page map level 4. */
	parent_page = pml4_get_page (parent->pml4, va); //부모의 pml4에서 va에 해당하는 페이지 가져옴
	//만약 부모 페이지가 null이면 return false
	if (parent_page == NULL) {
		return false;
	}

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */
	newpage = palloc_get_page(PAL_USER);
	if (newpage == NULL) {
		return false ;
	}

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	memcpy(newpage, parent_page, PGSIZE); //부모의 페이지를 새 페이지로 복사
	writable = is_writable(pte); //페이지 테이블 엔트리(pte)를 통해서 부모 페이지가 쓰기 가능한지 확인


	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: if fail to insert page, do error handling. */
		return false;
	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */

/*__do_fork(): 부모 프로세스로부터 자식 프로세스를 생성하는 작업 수행 - 인자 aux는 부모 쓰레드 */
static void
__do_fork (void *aux) {
	struct intr_frame if_; //자식 프로세스의 인터럽트 프레임을 저장할 구조체
	struct thread *parent = (struct thread *) aux; //부모 프로세스의 쓰레드 구조체
	struct thread *current = thread_current (); //현재 프로세스의 쓰레드 구조체 

	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	struct intr_frame *parent_if; //부모의 인터럽트 프레임을 가리킬 포인터 
	bool succ = true;

	parent_if = &parent->parent_if; //부모 프로세스의 intr_frame 포인터 

	/* 1. Read the cpu context to local stack. */
	/* 부모 프로세스의 CPU 컨텍스트를 자식 프로세스의 로컬 스택에 복사*/
	memcpy (&if_, parent_if, sizeof (struct intr_frame));

	if_.R.rax = 0; //자식 프로세스의 리턴값 0으로 설정

	/* 2. Duplicate PT 
		페이지 테이블 복제*/
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;

	process_activate (current); //자식 프로세스를 활성화
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/

	if (parent->next_fd_idx == FDT_COUNT_LIMIT) //파일 디스크립터 테이블이 가득 찬 경우 에러 처리
		goto error;

	
	//표준 파일 디스크립터(0,1)을 제외하고 복제
	for (int fd = 2; fd < FDT_COUNT_LIMIT; fd++){
		struct file *file = parent->fdt[fd]; //부모의 파일 디스크립터를 가져옴

		if (file == NULL)
			continue;

		current->fdt[fd] = file_duplicate(file); //파일 복제 
	}

	current ->next_fd_idx = parent->next_fd_idx; // 다음 파일 디스크립터의 인덱스 설정
	sema_up(&current->fork_sema); //자식 프로세스가 준비되었음을 부모에게 알림

	// process_init ();

	/* Finally, switch to the newly created process.
	 	새로 생성된 프로세스로 전환*/
	if (succ)
		do_iret (&if_);
error:
	sema_up(&parent->fork_sema); //에러 발생시 세마포어를 올려, 부모가 기다리지 않도록 한다. 
	exit(TID_ERROR); //에러 발생 시 자식 프로세스 종료시킴
	// thread_exit ();
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. 

 f_name: 인자는 커맨드 라인 -> 다시 parsing 해주어야 함 
 새 프로세스를 실행하는 역할
 */
int
process_exec (void *f_name) {
	char *file_name = f_name;
	bool success;

	/*Project2. Argument Parsing*/
	char file_name_copy[128]; //원본을 복사할 배열의 크기는 최대 128 byte
	memcpy(file_name_copy, file_name, strlen(file_name) + 1); //원본 문자열을 memcpy로 복사하고 1을 더해줘야 함(\0)

	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current context */
	process_cleanup ();

	/* And then load the binary */
	success = load (file_name_copy, &_if);

	/* If load failed, quit.
	로드에 실패한 경우, 파일에 할당된 메모리 페이지 해제 */
	palloc_free_page (file_name);
	if (!success)
		return -1;

	// hex_dump(_if.rsp, _if.rsp, USER_STACK - _if.rsp, true);

	/* Start switched process. */
	do_iret (&_if); //인터럽트 프레임에 저장된 상태 복원
	NOT_REACHED ();
}



/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */

/*주어진 자식 프로세스가 종료될 때까지 기다리고, 종료 상태 반환*/
int
process_wait (tid_t child_tid UNUSED) {
	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */
	// for (int i = 0; i < 100000000; i++)
    // {
	// }
	// return -1;

	//자식 프로세스의 쓰레드 구조체 child 
	struct thread *child = get_child_process(child_tid);

	//자식 프로세스가 존재하지 않는다면 return -1
	if (child == NULL) {
		return -1;
	}
	//자식 프로세스가 종료될 때까지 대기 
	sema_down(&child-> wait_sema);

	//자식 프로세스의 exit_status를 가져옴. 
	int exit_status = child->exit_status;
	
	//자식 프로세스를 child_list에서 제거
	list_remove(&child->child_elem);

	//자식 프로세스를 해제할 수 있도록 신호를 보냄
	sema_up(&child->free_sema);

	//자식 프로세스의 exit_status를 return
	return exit_status;
}


/* Exit the process. This function is called by thread_exit (). 
- 프로세스가 종룔될 때 메모리 누수를 방지하기 위해 프로세스에 열린 모든 파일을 닫음
- File descriptor Table 메모리 해제*/
/* 프로세스에 열린 모든 파일을 닫음
파일 디스크립터 테이블의 최댓값을 이용해 파일 디스크립터의 최소값인 2가 될 때까지 파일을 닫음
파일 디스크립터 테이블 메모리 해제 */
void
process_exit (void) {

	struct thread *cur = thread_current ();

	//프로세스에 열린 모든 파일을 닫음
	//의문) i = 2 부터 0 부터? => 
	for (int i = 0; i < FDT_COUNT_LIMIT; i++){
		close(i);
	}
	//파일 디스크립터 테이블 메모리 해제 	
	if (cur->fdt != NULL) {
		palloc_free_multiple(cur->fdt, FDT_PAGES);
	}


	//현재 실행 중인 파일을 닫음 
	if (cur->running != NULL) {
		file_close(cur->running);
	}
	

	process_cleanup ();

	//부모 프로세스가 프로세스가 종료되었음을 알 수 있게 wait_sema up
	sema_up(&cur->wait_sema);

	//부모 프로세스가 free_sema 자원을 해제할 때까지 대기 
	sema_down(&cur->free_sema);
	
}

/* Free the current process's resources. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */

/* if_ : interrupt frame를 가리키는 포인터 */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/* Allocate and activate page directory. */
	t->pml4 = pml4_create (); /*페이지 디렉토리 생성*/
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());

	/*----------------------Argument Parsing-----------------------------*/
	char *token; //현재 토큰을 저장할 포인터
	char *save_ptr;  //strtok_r 함수가 내부적으로 사용하는 상태 정보를 저장할 포인터
	char *argv[128]; // 
	uint64_t argc = 0; //토큰 개수 

	for (token = strtok_r(file_name, " ", &save_ptr); token != NULL; token = strtok_r(NULL, " ", &save_ptr)) {
		argv[argc++] = token; // argv에 parsing된 현재 토큰 저장
		
	}
	/*-------------------------------------------------------------------*/

	/* Open executable file. */
	file = filesys_open (file_name); /*프로그램 파일 open*/
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}

	/* Read and verify executable header. */
	/*ELF 파일의 헤더 정보를 읽어와 저장*/
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr; /*배치 정보를 읽어와 저장*/

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}
	
	t->running = file; //현재 실행 중인 쓰레드가 실행하고 있는 파일 (어떤 파일을 실행하고 있는지 추적하기 위함)
	file_deny_write(file); /*해당 파일에 대한쓰기 접근을 금지 
	-> 파일을 읽기 전용으로 만듦. 실행 중인 파일이 변경되지 않도록 보호. */

	/* Set up stack. */
	/*스택 초기화*/
	if (!setup_stack (if_))
		goto done;

	/* Start address. */
	if_->rip = ehdr.e_entry;

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */
	/*-----------------------------------------------------------*/	
	//rsp -> stack pointer
	//rdi: first argument, rsi: second argument 
	argument_stack(argv, argc, if_);

	
	// if (file != NULL) {
    // 	printf("Setting running file and denying write\n");
	// 	t->running = file; 
	// 	file_deny_write(file); 
	// }
	
	
	// printf("argc: %d\n", argc);
	// // argv 배열의 원소들 출력
    // printf("argv elements:\n");
    // for (uint64_t i = 0; i <= argc; i++) {
    //     printf("argv[%d]: %s\n", i, argv[i]);
    // }
	/*-----------------------------------------------------------*/	
	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	// file_close (file);
	return success;
}

/*-----------------------------------------------------------------*/

/*argv배열에 저장된 인자들을 스택에 역순으로 삽입,
이후 각 인자의 주소를 스택에 삽입, 마지막으로 리턴 주소 설정
argv: 인자 문자열 배열, argc: 인자의 개수, if_: interrupt frame 구조체*/

void argument_stack(char **argv, int argc, struct intr_frame *if_){

	char *arg_address[128]; /*각 인자의 주소를 저장할 배열*/

	//스택은 높은 주소에서 낮은 주소로 쌓이기 때문에 역순으로 삽입
	//argc -1 부터 0까지 역순으로 반복
	for (int i = argc - 1 ; i >= 0 ; i--) {
		int argv_len = strlen(argv[i]); //현재 인자의 길이(스택에 복사할 떄 필요한 공간을 확보하기 위함) 

		if_->rsp = if_->rsp - (argv_len + 1); 
		//현재 스택 포인터 = 스택 포인터를 [ 인자의 길이 + 1(NULL 문자)] 만큼 감소시킴

		memcpy(if_->rsp, argv[i], argv_len + 1 ); //인자를 스택에 복사 
		
		arg_address[i] = if_->rsp; //복사된 인자의 시작 주소를 arg_address[i]에 저장
	}

	//8 byte Padding
	//스택 포인터가 8바이트로 정렬되지 않은 경우 while문으로 계속 패딩 
	while(if_->rsp % 8 != 0) {

		if_->rsp--; //스택 포인터 1 byte 감소 -> 스택의 높은 주소에서 낮은 주소로 이동하는 것
		
		*(uint8_t *)if_->rsp = 0; //현재 스택 포인터가 가리키는 위치에 0 저장
	}

	
	//주소값 Insert
	//if_->rsp: stack pointer, 현재 스택의 최상단 

	// argc부터 1씩 감소시키며 0까지 반복
	for (int i = argc; i >=0; i--){

		if_->rsp = if_->rsp - 8; //각 반복마다 스택 포인터를 8바이트 감소시킴
		 
		if (i==argc) {
			memset(if_->rsp, 0, sizeof(char **)); 
			//sizeof(char **): 포인터의 크기 
			//i==argc일 때 => NULL 포인터로 채움(끝 표시)
		}

		//실제 인자값의 주소 복사
		else {
			//memcpy 사용하여 arg_address[i]의 주소를 if_->rsp위치에 복사 
			memcpy(if_->rsp, &arg_address[i], sizeof(char **)); //실제 인자의 주소를 스택에 복사 - 8바이트를 복사
			/* sizeof(char *)와 같은 결과 but  이중 포인터를 달*/
		}
	}

	//모든 인자가 스택에 설정된 후에 스택 포인터를 다시 8바이트 감소시키고
	//fake return address 를 스택에 저장
	if_->rsp = if_->rsp - 8; 
	memset(if_->rsp, 0, sizeof(void *));


	if_->R.rdi = argc;
	if_->R.rsi = if_->rsp + 8; 
}
/*-----------------------------------------------------------------*/

/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		void *aux = NULL;
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, aux))
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */

	return success;
}
#endif /* VM */
