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
#include "threads/synch.h"
#include "list.h"
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
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);

	char *parsing;
	/*file_name을 받아와서 null 기준으로 문자열 파싱*/
	strtok_r(file_name," ", &parsing);

	/* Create a new thread to execute FILE_NAME. */
	tid = thread_create (file_name, PRI_DEFAULT, initd, fn_copy);
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
tid_t
process_fork (const char *name, struct intr_frame *if_ UNUSED) {
	/* Clone current thread to new thread.*/

	/* --- Project 2: system call --- */
	// if에 담긴 현재 CPU 상태, 즉 실행중이던 부모 프로세스 context들을 복사한다.
	// if를 직접 넘기면 race condition이나 동기화 측면의 문제가 발생할 수 있기 때문에,
	// 부모 스레드의 parent_if 필드에 if를 복사하여 부모 스레드를 __do_fork의 인자로 넘겨준다.
	struct thread *parent = thread_current();
	memcpy(&parent->parent_if, if_, sizeof(struct intr_frame));

	// 전달받은 thread_name으로 스레드 생성
	// 복제된 if를 가지고 있는 parent를 인자로 넣어 __do_fork() 진행
	// 자식 프로세스가 running되면 즉시 __do_fork를 실행하게 된다.
	tid_t pid = thread_create(name, PRI_DEFAULT, __do_fork, parent); 

	// 스레드 생성에 실패했을 경우 TID_ERROR 리턴
	if (pid == TID_ERROR) {
		return TID_ERROR;
	}

	// 부모 스레드의 child_list에서, pid를 이용해 방금 생성한 자식 스레드 가져오기
	struct thread *child = get_child(pid);

	// 자식 프로세스가 실행하는 __do_fork가 끝나기를 기다린다.
	// if 복제가 완료되면, __do_fork 함수에서 자식 프로세스가 sema_up을 해준다.
	sema_down(&child->fork_sema);

	// 여기부터는 자식 프로세스의 __do_fork가 완료된 후 진행된다
	if (child->exit_status == -1)
    {
        return TID_ERROR;
    }
	// fork할 때 부모에게는 자식의 PID를, 자식에게는 0을 리턴하는 것이 POSIX 표준이다.
	return pid;
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte(uint64_t *pte, void *va, void *aux)
{
    struct thread *current = thread_current();
    struct thread *parent = (struct thread *)aux;
    void *parent_page;
    void *newpage;
    bool writable;

	// 주어진 가상 주소(va)가 커널 영역에 있다면 즉시 true를 리턴한다.
	// 커널 페이지는 복사할 필요가 없기 때문 -> 커널 영역은 모든 프로세스에 의해 공유되는 메모리 영역이다
    if (is_kernel_vaddr(va))
    {
        return true;
    }

    // 부모의 페이지 테이블에서 va와 매핑되는 실제 페이지를 찾는다. 실제 물리 메모리 주소.
	// 만약 없다면 false를 리턴한다
    parent_page = pml4_get_page(parent->pml4, va);
    if (parent_page == NULL)
    {
        printf("[fork-duplicate] failed to fetch page for user vaddr 'va'\n");
        return false;
    }

#ifdef DEBUG
    // pte: address pointing to one page table entry
    // *pte: page table entry = address of the physical frame
    void *test = ptov(PTE_ADDR(*pte)) + pg_ofs(va); // should be same as parent_page -> Yes!
    uint64_t va_offset = pg_ofs(va);                // should be 0; va comes from PTE, so there must be no 12bit physical offset
#endif
    
	// 자식 프로세스(지금 running 중이다)를 위한 페이지를 할당한다
	// PAL_USER는 유저 영역에 페이지를 할당하도록 하는 플래그이다
    newpage = palloc_get_page(PAL_USER);
    if (newpage == NULL)
    {
        printf("[fork-duplicate] failed to palloc new page\n");
        return false;
    }

    /* 4. TODO: Duplicate parent's page to the new page and
     *    TODO: check whether parent's page is writable or not (set WRITABLE
     *    TODO: according to the result). */

	// 부모 페이지를(PTE, 4096byte) newpage에 복사한다
    memcpy(newpage, parent_page, PGSIZE);

	// 부모 페이지 쓰기 가능 여부를 검사하여 writable에 결과를 저장한다 (boolean)
    writable = is_writable(pte);


    /* 5. Add new page to child's page table at address VA with WRITABLE
     *    permission. */
	// 자식 프로세스 페이지 테이블에 새로운 페이지를(PTE) 추가한다
	// va, writable을 설정하여 새로운 페이지 추가
    if (!pml4_set_page(current->pml4, va, newpage, writable))
    {
        /* 6. TODO: if fail to insert page, do error handling. */
		// 실패하면 false 반환
        printf("Failed to map user virtual page to given physical frame\n");
        return false;
    }

#ifdef DEBUG
    // TEST) is 'va' correctly mapped to newpage?
    if (pml4_get_page(current->pml4, va) != newpage)
        printf("Not mapped!"); // never called

    printf("--Completed copy--\n");
#endif

    return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */

static void
__do_fork(void *aux)
{
	/* 자식 프로세스는 running 되자마자 바로 __do_fork를 실행한다!!*/

    struct intr_frame if_;
	// 인터럽트 프레임이 aux의 parent_if 필드에 복사되어 있다.
    struct thread *parent = (struct thread *)aux;
    struct thread *current = thread_current();
    
    struct intr_frame *parent_if;
    bool succ = true;
    
	// 복사해서 받아온 if를 parent_if 포인터 변수에 담는다.
    parent_if = &parent->parent_if;

#ifdef DEBUG
    printf("[Fork] Forking from %s to %s\n", parent->name, current->name);
#endif

	// 부모 인터럽트 프레임을 자식 프로세스로 복사한다
    memcpy(&if_, parent_if, sizeof(struct intr_frame));

    // 자식 프로세스 리턴값을 0으로 설정한다
	// fork할 때 부모에게는 자식의 PID를, 자식에게는 0을 리턴하는 것이 POSIX 표준이다.
    if_.R.rax = 0;

    /* 2. Duplicate PT */
	// 자식 프로세스를 위한 페이지 테이블 생성 및 할당
    current->pml4 = pml4_create();
    if (current->pml4 == NULL)
        goto error;

	// 페이지 테이블을 CPU의 테이블 레지스터에 로드하고 TSS를 업데이트한다
	// TSS : 태스크 상태 세그먼트
	// 태스크 : 프로세스, 스레드 같은 실행 단위를 포괄적으로 지칭
	// 이 부분에서는 스택 포인터 관련 정보를 업데이트한다
    process_activate(current);

#ifdef VM
    supplemental_page_table_init(&current->spt);
    if (!supplemental_page_table_copy(&current->spt, &parent->spt))
        goto error;
#else
	// 부모 페이지 테이블의 PTE를 순회하며, 현재 자식 스레드 페이지 테이블로 복제한다
    if (!pml4_for_each(parent->pml4, duplicate_pte, parent))
        goto error;
#endif

    /* TODO: Your code goes here.
     * TODO: Hint) To duplicate the file object, use `file_duplicate`
     * TODO:       in include/filesys/file.h. Note that parent should not return
     * TODO:       from the fork() until this function successfully duplicates
     * TODO:       the resources of parent.*/

    
    if (parent->fd_idx == FDT_COUNT_LIMIT)
        goto error;

	// fd table 복제
    for (int i = 0; i < FDT_COUNT_LIMIT; i++)
    {
		// 부모의 fd table에서 파일을 가져온다
        struct file *file = parent->fd_table[i];
        if (file == NULL)
            continue;
        // if 'file' is already duplicated in child don't duplicate again but share it
        bool found = false;
        if (!found)
        {
			// 부모 fd table의 file을 new_file에 복제한다
            struct file *new_file;
            if (file > 2)
                new_file = file_duplicate(file);
            else
                new_file = file;
			
			// 복제한 file을 자식 프로세스 fd table에 그대로 할당한다
            current->fd_table[i] = new_file;
        }
    }

	// 부모의 fd_idx도 복제한다
    current->fd_idx = parent->fd_idx;

#ifdef DEBUG
    printf("[do_fork] %s Ready to switch!\n", current->name);
#endif

	// 부모는 자식 스레드를 생성해 놓고 sema_down으로 if 복제, 즉 __do_fork가 끝나기를 기다리고 있었다.
	// 복제가 완료되었으니 sema_up을 해서 부모의 process_fork 함수가 이어서 진행되도록 한다.
    sema_up(&current->fork_sema);

    /* Finally, switch to the newly created process. */
    if (succ)
        do_iret(&if_);
error:
    // thread_exit();
    // project 2 : system call

	// 에러가 났을 경우 exit_status를 ERROR로 설정하고 sema_up 해준 후 프로세스를 종료한다.
    current->exit_status = TID_ERROR;
    sema_up(&current->fork_sema);
    exit(TID_ERROR);
}

/* --- Project 2: system call --- */
// pid에 해당하는 현재 스레드의 자식 스레드 반환
struct thread
*get_child(int pid) {

	// 현재 스레드의 child list 가져오기
	struct thread *cur = thread_current ();
	struct list *child_list = &cur->child_list;
	
	// child list를 순회하며 인자로 받은 pid에 해당하는 스레드를 반환한다
	for (struct list_elem *e = list_begin(child_list); e != list_end(child_list); e = list_next(e)){
		struct thread *t = list_entry(e, struct thread, child_elem);
		if (t->tid == pid) {
			return t;
		}
	}
	
	// child list에 pid에 해당하는 스레드가 없다면 NULL을 반환한다
	return NULL;
}



/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
int
process_exec (void *f_name) {
	char *file_name = f_name;
	bool success;
	
	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;
 
	/* We first kill the current context */
	process_cleanup ();
 
	/*parsing한 인자를 담을 argu배열의 길이는 pintos제한 128바이트*/
	char *argu[128];
	char *token, *parsing;
	int cnt = 0;
	/* strtok_r 함수를 통해서 첫 번째로 얻은 값을 token에 저장, 나머지를 parsing에 저장 */
	/* token이 NULL일때까지 반복 */
	/* strtok_r 함수에 NULL값을 받아온다면 이전 호출 이후의 남은 문자열에서 토큰을 찾음, 따라서 token에는 다음 문자 저장*/
	for(token=strtok_r(file_name," ",&parsing);token!=NULL; token=strtok_r(NULL," ",&parsing))
	{
		argu[cnt++]=token;
	}
 
	/* And then load the binary */
	
	success = load (file_name, &_if);
	
	argument_stack(argu,cnt,&_if.rsp);

	/*if 구조체의 필드값 갱신*/
	_if.R.rdi=cnt;
	_if.R.rsi=(char*)_if.rsp+8;

	/*_if.rsp를 시작 주소로하여 메모리 덤프를 생성. 메모리 덤프의 크기는 16진수로*/
	// hex_dump(_if.rsp, _if.rsp, USER_STACK - (uint64_t)_if.rsp, true);
 
	/* If load failed, quit. */
	palloc_free_page (file_name);
	if (!success)
		return -1;
 
	/* Start switched process. */
	do_iret (&_if);
	NOT_REACHED ();
}


void argument_stack(char **argu, int count, void **rsp)
{
    // 프로그램 이름, 인자 문자열 push
    for (int i = count - 1; i > -1; i--)
    {
        for (int j = strlen(argu[i]); j > -1; j--)
        {
            (*rsp)--;                      // 스택 주소 감소
            **(char **)rsp = argu[i][j]; // 주소에 문자 저장
        }
        argu[i] = *(char **)rsp; 
    }
 
    /* 정렬 패딩 push
	rsp의 값이 8의 배수가 될 때까지 스택에 0 넣어서 패딩맞춰주기*/
    int padding = (int)*rsp % 8;
    for (int i = 0; i < padding; i++)
    {
        (*rsp)--;
        **(uint8_t **)rsp = 0; // rsp 직전까지 값 채움
    }
 
    // 인자 문자열 종료를 나타내는 0 push
    (*rsp) -= 8;
    **(char ***)rsp = 0; // char* 타입의 0 추가
 
    // 각 인자 문자열의 주소 push
    for (int i = count - 1; i > -1; i--)
    {
        (*rsp) -= 8; // 다음 주소로 이동
        **(char ***)rsp = argu[i]; // char* 타입의 주소 추가
    }
 
    // return address push
    (*rsp) -= 8;
    **(void ***)rsp = 0; // void* 타입의 0 추가
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
int process_wait(tid_t child_tid UNUSED)
{	
	// tid에 해당하는 자식 스레드를 가져온다.
    struct thread *child = get_child(child_tid);

    // 자식 스레드가 아닌 경우 return -1
    if (child == NULL)
        return -1;

	// 자식 프로세스가 끝날 때 까지 잠든다.(BLOCKED 되어 있는다)
    sema_down(&child->wait_sema);

	/// 자는 중 ///
	/// 자는 중 ///

	// 여기서부터는 깨어났다. 
	// 자식 프로세스 측에서 끝낼 준비를 다 했다는 의미로, process_exit 함수에서 sema_up을 해 주었다.
    // 자식은 부모가 자신을 child_list에서 지우기고 exit status를 return 하는 것을 기다리기 위해 sema_down 하여 자고 있다.
    // 자식 프로세스를 child_list에서 지우기
    list_remove(&child->child_elem);

    // child_list에서 지웠으므로, 이제 자식이 종료될 수 있도록 sema_up을 해 준다.
    sema_up(&child->free_sema);

	// 자식 프로세스 exit status를 return 한다.
    return child->exit_status;
}

/* Exit the process. This function is called by thread_exit (). */
void process_exit(void)
{
	// 프로세스 종료를 위한 정리 작업을 하는 함수
	// exit 시스템 콜에서 이미 exit status는 설정되었다.
	// 설정된 이후 thread_exit()를 통해 process_exit()가 호출된 것이다.

	// fd table에 할당되어 있는 열린 파일들을 모두 닫는다.
    struct thread *cur = thread_current();
    for (int i = 2; i < FDT_COUNT_LIMIT; i++)
    {
		// close 시스템 콜
        close(i);
    }
	
    // 메모리 누수 방지를 위해 fd table을 할당 해제한다.
    palloc_free_multiple(cur->fd_table, FDT_PAGES);

	// 이제 끝낼 준비가 되었다.
	// 따라서 process_wait()에서 자식이 끝날 때 까지 자고 있는 부모를 깨워준다.
    sema_up(&cur->wait_sema);

	// process_wait()에서 부모가 child_list에서 자식을 제거한 후 exit status를 return할 수 있도록 sema_down으로 자고 있는다.
    sema_down(&cur->free_sema);

	/// 자는 중 ///
	/// 자는 중 ///

	// 부모는 작업을 마쳤다. 스레드의 페이지 테이블을 할당 해제한다.
	// 나머지 세부적인 종료 절차들은 운영체제가 수행한 후 종료된다.
    process_cleanup();
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
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/* Allocate and activate page directory. */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());

	/* Open executable file. */
	file = filesys_open (file_name);
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}

	/* Read and verify executable header. */
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
		struct Phdr phdr;

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

	/* Set up stack. */
	if (!setup_stack (if_))
		goto done;

	/* Start address. */
	if_->rip = ehdr.e_entry;

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */

	t->running = file;

	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	file_close (file);
	return success;
}


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
