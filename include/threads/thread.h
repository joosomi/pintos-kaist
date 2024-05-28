#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#ifdef VM
#include "vm/vm.h"
#endif

/* --------------- added for PROJECT.2-1 --------------- */

#include "threads/synch.h"

/* --------------- added for PROJECT.2-2 --------------- */

#include "filesys/file.h"

/* ----------------------------------------------------- */

/* States in a thread's life cycle. */
enum thread_status {
  THREAD_RUNNING, /* Running thread. */
  THREAD_READY,   /* Not running but ready to run. */
  THREAD_BLOCKED, /* Waiting for an event to trigger. */
  THREAD_DYING    /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) - 1) /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0      /* Lowest priority. */
#define PRI_DEFAULT 31 /* Default priority. */
#define PRI_MAX 63     /* Highest priority. */

/* ------------ added for Project.1-1 ------------ */

#define WAKEUP_TICKS_DEFAULT 0

/* ------------ added for Project.1-3 ------------ */

extern int load_avg;

#define LOAD_AVG_DEFAULT 0

#define NICE_MIN -20
#define NICE_DEFAULT 0
#define NICE_MAX 20

#define RECENT_CPU_DEFAULT 0

/* ------- fixed_point(고정 소수점) 산술 macro -------
  format : 17.14

  n : integer
  F : 1 << 14
  x : Fixed-Point
  y : Fixed-Point  */

#define F 16384

/**
 * @brief 정수를 fixed-point로 변환
*/
#define INT_TO_FP(n) (n * F)

/**
 * @brief fixed-point의 소수점을 버리고 정수로 변환
 * 
 * @note ZERO 라고 부르는 이유는 0의 방향으로 반올림하기 때문이다.
 *       ex) 2.9 -> 2, -2.9 -> -2
*/
#define FP_TO_INT_ZERO(x) (x / F)

/**
 * @brief fixed-point의 소수점을 반올림하여 정수로 변환
 * 
 * @note NEAREST 라고 부르는 이유는 가장 가까운 정수로 올리기 때문이다.
 *       ex) 2.7 -> 3, -2.7 -> -3
*/
#define FP_TO_INT_NEAREST(x) (x >= 0 ? ((x + F / 2) / F) : ((x - F / 2) / F))

#define ADD_FP(x, y) (x + y)
#define SUB_FP(x, y) (x - y)

#define ADD_FP_AND_INT(x, n) (x + (n * F))
#define SUB_FP_AND_INT(x, n) (x - (n * F))

#define MUL_FP(x, y) (((int64_t)x) * y / F)
#define MUL_FP_AND_INT(x, n) (x * n)

#define DIV_FP(x, y) (((int64_t)x) * F / y)
#define DIV_FP_AND_INT(x, n) (x / n)

/* ----------- added for PROJECT.2-2 (bad-access) ----------- */

#define DUMMY "DUMMY"

/* ---------------------------------------------------------- */

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread {
  /* Owned by thread.c. */
  tid_t tid;                 /* Thread identifier. */
  enum thread_status status; /* Thread state. */
  char name[16];             /* Name (for debugging purposes). */
  int priority;              /* Priority. */
  /* Shared between thread.c and synch.c. */
  struct list_elem elem; /* List element. */

  /* ----------- added for PROJECT.1-1 ----------- */

  int64_t wakeup_ticks; /* sleep 상태에서 깨어날 시간 */

  /* ----------- added for PROJECT.1-2 ----------- */

  unsigned initial_priority; /* 상속 받기전 origin_priority */
  struct lock *wait_on_lock; /* 현재 쓰레드가 대기중인 lock */
  struct list donations;     /* priority를 상속해준 기부자(thread) */
  struct list_elem donation_elem; /* struct donations list를 위한 elem */

  /* ----------- added for PROJECT.1-3 ----------- */

  /**
   * @brief CPU사용을 얼마나 양보할지 표현하는 정수
   *  
   * @details nice lower : decrease priority
   *          nice higher : increase priority
   * 
   *          즉, nice가 높을수록 양보할 쓰레드라는것을 의미하므로
   *          priority가 낮아진다.
  */
  int nice;
  int recent_cpu; /* **최근** CPU사용량을 표현하는 Fixed_Point */

  /* 모든 쓰레드를 관리하는 all_thread_list를 위한 elem */;
  struct list_elem all_thread_elem;

  /* --------------- added for PRJECT.2-2  --------------- */

  int exit_status;         /* 쓰레드의 종료 상태 */
  int success_load_status; /* load 성공 여부 */

  /* for file manipulation */
  struct file **fdt; /* file descriptor table */
  int next_fd;       /* 테이블에 등록된 fd + 1
                           즉, 다음에 저장될 fd의 값을 의미한다 */

  struct semaphore exit_sema;
  struct semaphore load_sema;

  /* For Process hierarchy */
  struct list child_list;      /* 자식 쓰레드들을 관리하는 list */
  struct list_elem child_elem; /* 자식 쓰레드들을 관리하는 list_elem */
  struct thread *parent_t;     /* 부모 쓰레드 */

  /* ----------------------------------------------------- */

  // #ifdef USERPROG
  /* Owned by userprog/process.c. */
  uint64_t *pml4; /* Page map level 4 */
// #endif
#ifdef VM
  /* Table for whole virtual memory owned by thread. */
  struct supplemental_page_table spt;
#endif

  /* Owned by thread.c. */
  struct intr_frame tf; /* Information for switching */
  unsigned magic;       /* Detects stack overflow. */
};

/* ----------- added for Project.1 ----------- */

/* Alarm Clock */
bool is_idle_thread(struct thread *t);

void thread_sleep(int64_t ticks);
void thread_check_awake(int64_t ticks);
void thread_set_wakeup_ticks(struct thread *t, int64_t ticks);

/* ----------- added for Project.1-2 ----------- */

bool cmp_ascending_priority(const struct list_elem *a,
                            const struct list_elem *b, void *aux);
void check_preempt(void);

/* ----------- added for Project.1-3 ----------- */

typedef void (*all_thread_list_func)(struct thread *t, void *aux);

void thread_set_priority_mlfqs(struct thread *t, void *aux UNUSED);

int thread_calc_load_avg(int load_avg, int ready_thread_cnt);
void thread_update_load_avg(void);

int thread_calc_decay(void);

void thread_increase_recent_cpu_of_running(void);
void thread_update_recent_cpu(struct thread *t, void *aux UNUSED);

void thread_foreach(all_thread_list_func exec, void *aux UNUSED);

/* -------------------------------------------- */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init(void);
void thread_start(void);

void thread_tick(void);
void thread_print_stats(void);

typedef void thread_func(void *aux);
tid_t thread_create(const char *name, int priority, thread_func *, void *);

void thread_block(void);
void thread_unblock(struct thread *);

struct thread *thread_current(void);
tid_t thread_tid(void);
const char *thread_name(void);

void thread_exit(void) NO_RETURN;
void thread_yield(void);

int thread_get_priority(void);
void thread_set_priority(int);

int thread_get_nice(void);
void thread_set_nice(int);
int thread_get_recent_cpu(void);
int thread_get_load_avg(void);

void do_iret(struct intr_frame *tf);

#endif /* threads/thread.h */
