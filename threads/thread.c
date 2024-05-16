#include "threads/thread.h"
#include <debug.h>
#include <random.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "intrinsic.h"
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;   /* # of timer ticks spent idle. */
static long long kernel_ticks; /* # of timer ticks in kernel threads. */
static long long user_ticks;   /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4          /* # of timer ticks to give each thread. */
static unsigned thread_ticks; /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread(thread_func *, void *aux);

static void idle(void *aux UNUSED);
static struct thread *next_thread_to_run(void);
static void init_thread(struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule(void);
static tid_t allocate_tid(void);

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *)(pg_round_down(rrsp())))

// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = {0, 0x00af9a000000ffff, 0x00cf92000000ffff};

/* ------------ added for Project.1 ------------ */

/* sleep상태(BLOCKED)의 thread list */
static struct list sleep_list;

/**
 * @brief t가 idle thread인지 확인한다.
 *
 * @param t thread for compare 
 */
bool is_idle_thread(struct thread *t) { return t == idle_thread; }

bool priority_ascending_sort(const struct list_elem *a,
                             const struct list_elem *b, void *aux) {
  struct thread *a_t = list_entry(a, struct thread, elem);
  struct thread *b_t = list_entry(b, struct thread, elem);

  return (a_t->priority) > (b_t->priority);
}

void print_priority(struct list *list) {
  struct list_elem *e;
  for (e = list_begin(list); e != list_end(list); e = list_next(e)) {
    struct thread *t = list_entry(e, struct thread, elem);
    printf("%d ", t->priority);
  }
  printf("\n");
}

/**
 * @brief 현재 running thread의 ready_list의 가장 높은 우선순위보다 낮다면
 *        선점을 양보한다.
*/
void preempt_schedule(void) {
  if (list_empty(&ready_list)) return;

  int curr_t_p = thread_get_priority();
  int next_t_p =
      list_entry(list_front(&ready_list), struct thread, elem)->priority;

  if (curr_t_p < next_t_p) thread_yield();
}

/* -------------------- end -------------------- */

/**
 * @brief current thread를 ticks만큼 sleep 상태(BLOCKED)로 변경한다.
 *
 * @param ticks 잠재울 ticks
 */
void thread_sleep(int64_t ticks) {
  struct thread *curr_t = thread_current();
  enum intr_level old_level;

  old_level = intr_disable();      /* interrupt off */
  ASSERT(!is_idle_thread(curr_t)); /* check curr_t is idle */

  thread_set_wakeup_ticks(curr_t, ticks); /* ticks 설정 */

  list_push_back(&sleep_list, &curr_t->elem); /* sleep_list에 넣어준다*/

  /* Project.1 alarm clock solution 1 */
  // thread_block();
  /*
    Project.1 alarm clock solution 2
    thread_block() 안에서도 schedule()이 호출되기에 차이는 크지않다.
    허나 do_schedule()을 호출시에 내부적으로 destruction_req를 처리하기에
    do_schedule()을 호출하는 것이 메모리관점에서 효율적이라 생각한다.
  */
  do_schedule(THREAD_BLOCKED);

  intr_set_level(old_level); /* restore interrupt */
}

/**
 * @brief sleep_list를 순회하면서 특정 시간이 지난 thread를 깨운다
 *
 * @param ticks timer_ticks() : start
 */
void thread_check_awake(int64_t ticks) {
  struct list_elem *curr_ll_e;
  struct thread *curr_t;

  curr_ll_e = list_begin(&sleep_list);

  while (1) {
    if (curr_ll_e == list_end(&sleep_list)) break;

    curr_t = list_entry(curr_ll_e, struct thread, elem);

    if (curr_t->wakeup_ticks <= ticks) {
      curr_ll_e = list_remove(curr_ll_e);
      thread_unblock(curr_t);
      continue;
    }

    curr_ll_e = list_next(curr_ll_e);
  }
}

/**
 * @brief thread의 wakeup_ticks를 설정한다
 *
 * @param t thread의 pointer
 * @param ticks 설정할 wakeup_ticks
 */
void thread_set_wakeup_ticks(struct thread *t, int64_t ticks) {
  ASSERT(t != NULL);
  t->wakeup_ticks = ticks;
}

/* -------------------- end ------------------ */

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void thread_init(void) {
  ASSERT(intr_get_level() == INTR_OFF);

  /* Reload the temporal gdt for the kernel
   * This gdt does not include the user context.
   * The kernel will rebuild the gdt with user context, in gdt_init (). */
  struct desc_ptr gdt_ds = {.size = sizeof(gdt) - 1, .address = (uint64_t)gdt};
  lgdt(&gdt_ds);

  /* Init the globla thread context */
  lock_init(&tid_lock);
  list_init(&ready_list);
  list_init(&sleep_list);
  list_init(&destruction_req);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread();
  init_thread(initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid();
}

/**
 * @brief idle_thread를 생성하거나 선점쓰레드에 대한 스케쥴링을 실행한다.
 * 
 * @ref thread_create()
 * 
 * @details idle_started 세마포어를 0으로 초기화하는것은 idle 쓰레드를
            생성할때 비동기적인 thread_start 함수의 메커니즘 때문에 
            다른 쓰레드에 대한 방해가 없도록 하기 위함이다.
            🔗 (이에 대한 내용은 thread_create()에 대한 주석을 참고하자)

            2가지 case가 있다고 가정하자
   
            [case.1] : thread_create()가 먼저 끝난경우
   
            idle 함수에서 내부적으로 sema_up()을 실행하기에 sema->value는 1이된다 
            이후 thread_create()에 대한 호출이 끝나고 sema_down이 호출되더라 
            sema->value가 1이기에 while문에 빠지지 않는다 
   
            [case.2] : sema_down()이 먼저 호출된 경 
   
            sema_down()이 호출되면 sema->value가 0이되어 while문에 들어가게된다 
            이후 idle 함수에서 sema_up()을 호출하면 sema->value가 1이되어 
            while문을 빠져나오게된다 
   
            즉, sema->value를 0으로 시작했을때 어떤 경우든 현재 lock을 유지하면 
            다른 쓰레드에게 방해받지 않고 action(idle 쓰레드를 생성)을 수행가능하다 
 *             
 * @note Starts preemptive thread scheduling by enabling interrupts.
 *       Also creates the idle thread
 */
void thread_start(void) {
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init(&idle_started, 0);
  /* idle 함수를 thread_create에 넘기고 idle(&idle_started)로
     호출하게 해준다 */
  thread_create("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling.
     brief 후자로 기술한 함수 목적 */
  intr_enable();

  /* Wait for the idle thread to initialize idle_thread.
     즉, idle_thread가 생성될때까지 항상 보장해준다 */
  sema_down(&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void thread_tick(void) {
  struct thread *t = thread_current();

  /* Update statistics. */
  if (t == idle_thread) idle_ticks++;
#ifdef USERPROG
  else if (t->pml4 != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE) intr_yield_on_return();
}

/* Prints thread statistics. */
void thread_print_stats(void) {
  printf("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
         idle_ticks, kernel_ticks, user_ticks);
}

/*  */
/**
 * @brief new_thread를 생성하고 ⛔️항상 ready_list에 넣은 뒤⛔️ schduling 한다.
 *
 * @param name 새로 생성할 thread의 이름
 * @param priority 새로 생성할 thread의 우선순위
 * @param function 새로 생성할 thread가 실행할 함수
 * @param aux 새로 생성할 thread가 실행할 함수의 인자
 *
 * @note Creates a new kernel thread named NAME with the given initial
         PRIORITY, which executes FUNCTION passing AUX as the argument,
         and adds it to the ready queue.  Returns the thread identifier
         for the new thread, or TID_ERROR if creation fails.

         If thread_start() has been called, then the new thread may be
         scheduled before thread_create() returns.  It could even exit
         before thread_create() returns.  Contrariwise, the original
         thread may run for any amount of time before the new thread is
         scheduled.  Use a semaphore or some other form of
         synchronization if you need to ensure ordering.

         [thread_start()가 호출된 경우, 새 스레드는 thread_create()가 반환되기 전에
         예약될 수 있다. thread_create()가 돌아오기 전에 종료될 수도 있습니다.
         반대로, 원본 스레드는 새로운 스레드가 스케줄링되기 전에 임의의 시간 동안
         실행될 수 있다. 순서가 필요한 경우 세마포어 또는 다른 형태의 동기화를 사용하십시오.]

         -> 즉, 비동기적일 수 있다. 

         The code provided sets the new thread's `priority' member to
         PRIORITY, but no actual priority scheduling is implemented.
         Priority scheduling is the goal of Problem 1-3.
 */
tid_t thread_create(const char *name, int priority, thread_func *function,
                    void *aux) {
  struct thread *new_t;
  tid_t tid;

  ASSERT(function != NULL);

  /* Allocate thread.
     thread 구조체를 위한 메모리 할당 */
  new_t = palloc_get_page(PAL_ZERO);
  if (new_t == NULL) return TID_ERROR;

  /* Initialize thread.
     아래에서 unblock 하는 이유는 init_thread에서 BLOCKED으로 초기화 */
  init_thread(new_t, name, priority);
  tid = new_t->tid = allocate_tid();

  /* Call the kernel_thread if it scheduled.
   * Note) rdi is 1st argument, and rsi is 2nd argument. */
  new_t->tf.rip = (uintptr_t)kernel_thread;
  new_t->tf.R.rdi = (uint64_t)function;
  new_t->tf.R.rsi = (uint64_t)aux;
  new_t->tf.ds = SEL_KDSEG;
  new_t->tf.es = SEL_KDSEG;
  new_t->tf.ss = SEL_KDSEG;
  new_t->tf.cs = SEL_KCSEG;
  new_t->tf.eflags = FLAG_IF;

  /* 새로 생성한 쓰레드를 ready_queue에 넣는다.
     thread_unblock() 이라는 함수 명에 혼동되면 안된다.
     단순히 thread의 state를 ready로 바꿔주는것 뿐만 아니라
     ready_list에 넣어주는 역할도 한다. */
  thread_unblock(new_t);

  /* ----------- added for Project.1-2 Priority Scheduling -----------
    new_thread가 ready_list에 들어가게되는데 arg로 받은 new_thread보다
    running_thread가 우선순위가 높다면 ruuning_thread를 양보한다. */

  if (thread_get_priority() < priority) thread_yield();

  /* --------------------------------------------------------------- */

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void thread_block(void) {
  ASSERT(!intr_context());
  ASSERT(intr_get_level() == INTR_OFF);
  thread_current()->status = THREAD_BLOCKED;
  schedule();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data.
   


    */
/**
 * @brief "BLOCKED" 상태의 current thread를 "READY" 상태로 전환하고 
 *        ready_list에 넣는다.
 * 
 * @param t "BLOCKED" 상태의 thread
 * 
 * @note Transitions a blocked thread T to the ready-to-run state.
         This is an error if T is not blocked. (Use thread_yield() to
         make the running thread ready.)

         차단된 스레드 T를 실행 준비 상태로 전환합니다. T가 차단되지 않으면 오류입니다.
         (thread_yield()를 사용하여 실행 중인 스레드를 준비하십시오.)

         This function does not preempt the running thread.  This can
         be important: if the caller had disabled interrupts itself,
         it may expect that it can atomically unblock a thread and
         update other data.

         이 기능은 실행 중인 스레드를 선점하는 것이 아니다. 이것은 중요할 수 있다: 발신자가
         인터럽트 자체를 비활성화했다면, 스레드의 차단을 원자적으로 해제하고 다른 데이터를
         업데이트할 수 있을 것으로 예상할 수 있다.
*/
void thread_unblock(struct thread *t) {
  enum intr_level old_level;

  ASSERT(is_thread(t));

  old_level = intr_disable();
  ASSERT(t->status == THREAD_BLOCKED);

  /* ---------- before Project.1-2 ----------
  list_push_back(&ready_list, &t->elem); */
  /* --------------------------------------- */

  /* ---------- after Project.1-2 ---------- */
  list_insert_ordered(&ready_list, &(t->elem), priority_ascending_sort, NULL);
  /* --------------------------------------- */

  t->status = THREAD_READY;

  intr_set_level(old_level);
}

/* Returns the name of the running thread. */
const char *thread_name(void) { return thread_current()->name; }

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *thread_current(void) {
  struct thread *t = running_thread();

  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT(is_thread(t));
  ASSERT(t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t thread_tid(void) { return thread_current()->tid; }

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void thread_exit(void) {
  ASSERT(!intr_context());

#ifdef USERPROG
  process_exit();
#endif

  /* Just set our status to dying and schedule another process.
     We will be destroyed during the call to schedule_tail(). */
  intr_disable();
  do_schedule(THREAD_DYING);
  NOT_REACHED();
}

/**
 * @brief 현재 Running중인 thread를 ready_list에 넣고 schedule한다
 *
 * @details Yields the CPU.  The current thread is not put to sleep and
 *          may be scheduled again immediately at the scheduler's whim.
 */
void thread_yield(void) {
  struct thread *curr = thread_current();
  enum intr_level old_level;

  ASSERT(!intr_context());

  old_level = intr_disable();
  /* before Project.1-2 */
  // if (curr != idle_thread) list_push_back(&ready_list, &curr->elem);

  /* after Project.1-2 */
  if (curr != idle_thread)
    list_insert_ordered(&ready_list, &curr->elem, priority_ascending_sort,
                        NULL);

  do_schedule(THREAD_READY);
  intr_set_level(old_level);
}

/**
 * @brief 현재 Running thread의 우선순위를 변경한다
 *
 * @details Sets the current thread's priority to NEW_PRIORITY.
 *
 * @note Project.1-2
 *       Running thread의 우선순위가 31이라 가정하자
 *       ready_list에서 우선순위가 가장 높은 아이가 31일때
 *       Running thread의 우선순위를 15로 낮춘다면
 *       priority inversion이 생기기에 rescheduling 해야한다.
 */
void thread_set_priority(int new_priority) {
  struct thread *curr_t = thread_current();
  curr_t->initial_priority = new_priority;

  /* 대기중인 thread가 없다면 넘어간다. */
  update_priority_donation();

  preempt_schedule();
}

/* Returns the current thread's priority. */
int thread_get_priority(void) { return thread_current()->priority; }

/* Sets the current thread's nice value to NICE. */
void thread_set_nice(int nice UNUSED) {
  /* TODO: Your implementation goes here */
}

/* Returns the current thread's nice value. */
int thread_get_nice(void) {
  /* TODO: Your implementation goes here */
  return 0;
}

/* Returns 100 times the system load average. */
int thread_get_load_avg(void) {
  /* TODO: Your implementation goes here */
  return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int thread_get_recent_cpu(void) {
  /* TODO: Your implementation goes here */
  return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void idle(void *idle_started_ UNUSED) {
  struct semaphore *idle_started = idle_started_;

  idle_thread = thread_current();
  sema_up(idle_started);

  for (;;) {
    /* Let someone else run. */
    intr_disable();
    thread_block();

    /* Re-enable interrupts and wait for the next one.

       The `sti' instruction disables interrupts until the
       completion of the next instruction, so these two
       instructions are executed atomically.  This atomicity is
       important; otherwise, an interrupt could be handled
       between re-enabling interrupts and waiting for the next
       one to occur, wasting as much as one clock tick worth of
       time.

       See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
       7.11.1 "HLT Instruction". */
    asm volatile("sti; hlt" : : : "memory");
  }
}

/* Function used as the basis for a kernel thread. */
static void kernel_thread(thread_func *function, void *aux) {
  ASSERT(function != NULL);

  intr_enable(); /* The scheduler runs with interrupts off. */
  function(aux); /* Execute the thread function. */
  thread_exit(); /* If function() returns, kill the thread. */
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void init_thread(struct thread *t, const char *name, int priority) {
  ASSERT(t != NULL);
  ASSERT(PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT(name != NULL);

  memset(t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy(t->name, name, sizeof t->name);
  t->tf.rsp = (uint64_t)t + PGSIZE - sizeof(void *);
  t->priority = priority;
  t->magic = THREAD_MAGIC;

  /* ----------- added for Project.1 ----------- */
  t->wakeup_ticks = 0;

  /* ----------- added for Project.2 ----------- */
  t->initial_priority = priority;
  t->wait_on_lock = NULL;
  list_init(&t->donations);
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *next_thread_to_run(void) {
  if (list_empty(&ready_list))
    return idle_thread;
  else
    return list_entry(list_pop_front(&ready_list), struct thread, elem);
}

/**
 * @brief Use iretq to launch the thread
 *
 * @link https://stay-present.tistory.com/98
 */
void do_iret(struct intr_frame *tf) {
  __asm __volatile(
      "movq %0, %%rsp\n"
      "movq 0(%%rsp),%%r15\n"
      "movq 8(%%rsp),%%r14\n"
      "movq 16(%%rsp),%%r13\n"
      "movq 24(%%rsp),%%r12\n"
      "movq 32(%%rsp),%%r11\n"
      "movq 40(%%rsp),%%r10\n"
      "movq 48(%%rsp),%%r9\n"
      "movq 56(%%rsp),%%r8\n"
      "movq 64(%%rsp),%%rsi\n"
      "movq 72(%%rsp),%%rdi\n"
      "movq 80(%%rsp),%%rbp\n"
      "movq 88(%%rsp),%%rdx\n"
      "movq 96(%%rsp),%%rcx\n"
      "movq 104(%%rsp),%%rbx\n"
      "movq 112(%%rsp),%%rax\n"
      "addq $120,%%rsp\n"
      "movw 8(%%rsp),%%ds\n"
      "movw (%%rsp),%%es\n"
      "addq $32, %%rsp\n"
      "iretq"
      :
      : "g"((uint64_t)tf)
      : "memory");
}

/**
 * @brief : running_thread의 컨텍스트를 저장하고, 다음 스레드의 컨텍스트를
 *          복구한다. ⛔️ 쓰레드는 이미 next_thread로 전환됐다..? ⛔️
 *
 * @var tf_cur : 현재 실행중인 thread의 interrupt frame 주소
 * @var tf : 새로 실행할 thread의 interrupt frame 주소
 *
 * @note : Switching the thread by activating the new thread's page
           tables, and, if the previous thread is dying, destroying it.
           At this function's invocation, we just switched from thread
           PREV, the new thread is already running, and interrupts are
           still disabled.
           It's not safe to call printf() until the thread switch is
           complete.  In practice that means that printf()s should be
           added at the end of the function.

 * @note : 새 스레드의 페이지 테이블을 활성화하여 스레드를 전환하고,
           이전 스레드가 죽는다면 스레드를 파괴합니다. 이 함수를 호출할 때,
           우리는 방금 스레드 PREV에서 전환했고, 새로운 스레드는 이미 실행
           중이며, 인터럽트는 여전히 비활성화되어 있다. 스레드 스위치가 완료될
           때까지 printf()를 호출하는 것은 안전하지 않다. 실제로는 함수의 끝에
           printf()를 추가해야 한다는 것을 의미한다.
 */
static void thread_launch(struct thread *th) {
  /**
   *
   */

  uint64_t tf_cur = (uint64_t)&running_thread()->tf;
  uint64_t tf = (uint64_t)&th->tf;
  ASSERT(intr_get_level() == INTR_OFF);

  /* The main switching logic.
   * We first restore the whole execution context into the intr_frame
   * and then switching to the next thread by calling do_iret.
   * Note that, we SHOULD NOT use any stack from here
   * until switching is done. */
  __asm __volatile(
      /* Store registers that will be used. */
      "push %%rax\n"
      "push %%rbx\n"
      "push %%rcx\n"
      /* Fetch input once */
      "movq %0, %%rax\n"
      "movq %1, %%rcx\n"
      "movq %%r15, 0(%%rax)\n"
      "movq %%r14, 8(%%rax)\n"
      "movq %%r13, 16(%%rax)\n"
      "movq %%r12, 24(%%rax)\n"
      "movq %%r11, 32(%%rax)\n"
      "movq %%r10, 40(%%rax)\n"
      "movq %%r9, 48(%%rax)\n"
      "movq %%r8, 56(%%rax)\n"
      "movq %%rsi, 64(%%rax)\n"
      "movq %%rdi, 72(%%rax)\n"
      "movq %%rbp, 80(%%rax)\n"
      "movq %%rdx, 88(%%rax)\n"
      "pop %%rbx\n"  // Saved rcx
      "movq %%rbx, 96(%%rax)\n"
      "pop %%rbx\n"  // Saved rbx
      "movq %%rbx, 104(%%rax)\n"
      "pop %%rbx\n"  // Saved rax
      "movq %%rbx, 112(%%rax)\n"
      "addq $120, %%rax\n"
      "movw %%es, (%%rax)\n"
      "movw %%ds, 8(%%rax)\n"
      "addq $32, %%rax\n"
      "call __next\n"  // read the current rip.
      "__next:\n"
      "pop %%rbx\n"
      "addq $(out_iret -  __next), %%rbx\n"
      "movq %%rbx, 0(%%rax)\n"  // rip
      "movw %%cs, 8(%%rax)\n"   // cs
      "pushfq\n"
      "popq %%rbx\n"
      "mov %%rbx, 16(%%rax)\n"  // eflags
      "mov %%rsp, 24(%%rax)\n"  // rsp
      "movw %%ss, 32(%%rax)\n"
      "mov %%rcx, %%rdi\n"
      "call do_iret\n"
      "out_iret:\n"
      :
      : "g"(tf_cur), "g"(tf)
      : "memory");
}

/**
 * @brief 현재 돌고있는 Running thread를 status로 변경하고 schedule을 호출한다.
 *
 * @ref schedule
 *
 * @param status 현재 Running thread의 status를 변경하고자하는 state
 *
 * @details Schedules a new process. At entry, interrupts must be off.
 *          This function modify current thread's status to status and then
 *          finds another thread to run and switches to it.
 *          It's not safe to call printf() in the schedule().
 */
static void do_schedule(int status) {
  /* 예외처리 */
  ASSERT(intr_get_level() == INTR_OFF);
  ASSERT(thread_current()->status == THREAD_RUNNING);

  /*
    Destroy the current thread if it is dying
    destruction_req에 담길때는 schedule()이 호출될때 schedule 내부에서
    죽은 상태의 쓰레드들을 destruction_req에 넣어둔다.
  */
  while (!list_empty(&destruction_req)) {
    struct thread *victim =
        list_entry(list_pop_front(&destruction_req), struct thread, elem);
    palloc_free_page(victim);
  }
  thread_current()->status = status;
  schedule();
}

/**
 * @brief Running_tread와 next_thread를 스위칭한다.
 */
static void schedule(void) {
  struct thread *curr = running_thread();
  struct thread *next = next_thread_to_run();

  ASSERT(intr_get_level() == INTR_OFF);
  ASSERT(curr->status != THREAD_RUNNING);
  ASSERT(is_thread(next));
  /* Mark us as running. */
  next->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate(next);
#endif

  if (curr != next) {
    /* If the thread we switched from is dying, destroy its struct
       thread. This must happen late so that thread_exit() doesn't
       pull out the rug under itself.
       We just queuing the page free reqeust here because the page is
       currently used by the stack.
       The real destruction logic will be called at the beginning of the
       schedule(). */
    if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
      ASSERT(curr != next);
      list_push_back(&destruction_req, &curr->elem);
    }

    /* Before switching the thread, we first save the information
       of current running. */
    thread_launch(next);
  }
}

/* Returns a tid to use for a new thread. */
static tid_t allocate_tid(void) {
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire(&tid_lock);
  tid = next_tid++;
  lock_release(&tid_lock);

  return tid;
}
