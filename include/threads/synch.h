#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>

void update_priority_donation(void);
/**
 * @brief 🚽 변기.
 * 				다른 실행 쓰레드들과의 동기화를 위한 그저 **타입 변수**
 * 				semaphore를 단순히 하나의 모듈 (또는 라이브러리 라고 생각하자)
 * 
 * @param value 변기 개수 (사용가능한 리소스의 개수)
 * @param waiters 대기중인 사람들(대기중인 쓰레드들의 list)
*/
struct semaphore {
  unsigned value;      /* Current value. */
  struct list waiters; /* List of waiting threads. */
};

void sema_init(struct semaphore *, unsigned value);
void sema_down(struct semaphore *);
bool sema_try_down(struct semaphore *);
void sema_up(struct semaphore *);
void sema_self_test(void);

/**
 * @brief 🚻 화장실.
 * 				lock을 통해 다른 쓰레드들과의 동기화를 위한 타입 변수
 * 
 * @param holder 화장실을 잠근 사람 (현재 lock을 갖고있는 thread)
 * @param semaphore 🚽 변기. (lock에 접근이 가능한 리소스의 개수와 lock을 가지려고 
 * 									대기중인 쓰레드들의 list)
*/
struct lock {
  struct thread *holder;      /* Thread holding lock (for debugging). */
  struct semaphore semaphore; /* Binary semaphore controlling access. */
};

void lock_init(struct lock *);
void lock_acquire(struct lock *);
bool lock_try_acquire(struct lock *);
void lock_release(struct lock *);
bool lock_held_by_current_thread(const struct lock *);

/* Condition variable. */
struct condition {
  struct list waiters; /* List of waiting threads. */
};

void cond_init(struct condition *);
void cond_wait(struct condition *, struct lock *);
void cond_signal(struct condition *, struct lock *);
void cond_broadcast(struct condition *, struct lock *);

/* Optimization barrier.
 *
 * The compiler will not reorder operations across an
 * optimization barrier.  See "Optimization Barriers" in the
 * reference guide for more information.*/
#define barrier() asm volatile("" : : : "memory")

#endif /* threads/synch.h */
