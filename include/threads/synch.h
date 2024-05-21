#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>
#include "threads/thread.h"
// 세마포어 코드 //////////////
/* A counting semaphore. */
struct semaphore {
	unsigned value;             // 현재 값 /* Current value. */
	struct list waiters;        // 대기중인 스레드 목록 /* List of waiting threads. */
};

// 세마포어의 초기값 설정
void sema_init (struct semaphore *, unsigned value);
// 값이 양수가 될 때까지 기다렸다가 1 감소시키기, (들어가기)
void sema_down (struct semaphore *);
// 현재 값이 양수라면 값 내리고 true 반환, 아니라면 false 반환
// 일반적인 상황에선 down쓰는게 좋은 같다
bool sema_try_down (struct semaphore *);
// 값을 1 올리기 (나오기)
void sema_up (struct semaphore *);
// 세마포어 구조체가 정상적으로 동작하는지 테스트하는 함수
void sema_self_test (void);
// semaphore_elem 구조체를 2개 받아서, 각 세마포어 waiters의 HEAD 스레드끼리의 우선순위를 비교하는 함수
bool sema_compare_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);

/* Lock. */
// lock 구조체
// lock은 value 1로 초기화된다. ( 하나의 스레드만 lock을 가진다, binary semaphore )
struct lock {
	struct thread *holder;      /* Thread holding lock (for debugging). */
	struct semaphore semaphore; /* Binary semaphore controlling access. */
};

void lock_init (struct lock *);
void lock_acquire (struct lock *);
bool lock_try_acquire (struct lock *);
void lock_release (struct lock *);
bool lock_held_by_current_thread (const struct lock *);

/* Condition variable. */
struct condition {
	struct list waiters;        /* List of waiting threads. */
};

void cond_init (struct condition *);
void cond_wait (struct condition *, struct lock *);
void cond_signal (struct condition *, struct lock *);
void cond_broadcast (struct condition *, struct lock *);

/* Optimization barrier.
 *
 * The compiler will not reorder operations across an
 * optimization barrier.  See "Optimization Barriers" in the
 * reference guide for more information.*/
#define barrier() asm volatile ("" : : : "memory")

#endif /* threads/synch.h */
