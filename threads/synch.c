/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */

void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	// value가 0이면, 즉 들어갈 수 없다면 waiters 에 들어가기
	// if여도 기능상 문제는 없지만 while로 하는게 race condition이나 Spurious Wakeup (가짜 깨어남) 상황에서
	// 올바르게 동작하게 해 준다
	while(sema->value == 0) {
		
		// waiters 리스트에 우선순위 정렬하여 스레드 삽입
		list_insert_ordered(&sema->waiters, &thread_current()->elem, compare_thread_priority, NULL);

		// waiters에 있는 동안 block된다
		thread_block ();
	}
	sema->value--;
	intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
// waiters의 스레드를 1개 unblock하고, semaphore 값을 1 올리는 함수
void
sema_up (struct semaphore *sema) {

	enum intr_level old_level;
	ASSERT (sema != NULL);
	old_level = intr_disable ();

	// waiter가 존재할 때 진행
	if (!list_empty (&sema->waiters)) {
		
		// waiters 우선순위 순으로 정렬해주기
		list_sort(&sema->waiters, compare_thread_priority, NULL);

		// unblock으로 인해 ready_list로 들어갈 때 우선순위 정렬되어 들어가게 된다.
		thread_unblock (list_entry (list_pop_front (&sema->waiters),
					struct thread, elem));
	}

	// sema 값 올리기, 이후 unblock된 스레드가 알아서 바로 down 한다
	sema->value++;

	// waiters의 스레드가 unblock되어 ready_list에 들어갔으니, 우선순위에 따라 선점하도록 함수 실행
	thread_preemption();


	intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) {
	// 세마포어 배열 선언 ( 세마포어 2개 만듦 )
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	// 세마포어 2개의 값을 각각 0으로 초기화
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);

	// 스레드 생성 + tid_t 형태의 tid를 리턴받아옴
	// 이름, 우선순위, 실행할 함수, sema 배열을 인자로 전달
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
// sema_self_test()에서 사용되는 함수, sema[0] sema[1]을 내리고 올리는 걸 테스트한다.
// 자식 스레드가 이 함수를 실행한다
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
// 인자로 lock을 요청한다
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

	// 현재 스레드 가지고 오기
	struct thread *cur = thread_current();
	
	// 다른 스레드가 lock을 이미 가지고 있다면
	if(lock->holder) {

		// 어떤 lock을 기다리는지 필드에 추가
		cur->waitOnLock = lock;

		// holder의 donation 리스트에 현재 스레드를 우선순위 내림차순 정렬하여 추가해준다
		list_insert_ordered(&lock->holder->donation, &cur->donationElem, compare_thread_donate_priority, NULL);

		// 현재 스레드부터 waitOnLock을 확인하여 파고들어가 priority를 donate 한다
		donate_priority();
	}

	// 해당 lock의 세마포어 값을 내린다
	// 자리가 없다면 스레드는 waiters 에 추가되고 block된다
	sema_down (&lock->semaphore);

	// down됐으면 lock을 획득했다는 것이니 waitOnLock을 NULL로 바꿔준다
	cur->waitOnLock = NULL;
	lock->holder = cur;
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
// 인자로 받은 lock을 해제하고, lock의 semaphore를 1 올린다.
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

	// 해제할 lock을 기다리는 스레드들을 donation 리스트에서 제거한다
	remove_with_lock(lock);
	// donation 리스트가 수정되었으니, 우선순위를 재설정해 준다
	refresh_priority();

	// lock의 holder를 제거한다
	lock->holder = NULL;
	// semaphore 값을 올려 1자리를 만들어준다
	sema_up (&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
// 세마포어를 사용해서 현재 스레드의 lock을 해제하고 잠들게 하는 함수
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	// 0으로 초기화 해 두면, sema_down을 실행할 때 스레드가 대기 상태가 된다(waiters list에 들어간다)
	sema_init (&waiter.semaphore, 0);

	// cond waiters의 각 세마포어의 우선순위가 가장 높은 스레드끼리만 비교하여 sema를 정렬해서 넣는다.
	list_insert_ordered(&cond->waiters, &waiter.elem, sema_compare_priority, NULL);

	// 현재 스레드의 lock 풀기
	lock_release (lock);

	// down을 실행하고 기다리기. 위에서 value 0 으로 초기화 했으므로, 외부에서 cond_signal을 줄 때까지 자게 된다.(blocked)
	sema_down (&waiter.semaphore);

	// cond_signal을 받아서 unblock 되면, 그 때 lock을 요청한다.
	lock_acquire (lock);
}
// lock을 기다리는 애들은 lock->semaphore->waiters 에 있다.
// 


bool sema_compare_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {

	struct semaphore_elem *a_sema = list_entry(a, struct semaphore_elem, elem);
	struct semaphore_elem *b_sema = list_entry(b, struct semaphore_elem, elem);

	struct list *waiter_a_sema = &(a_sema->semaphore.waiters);
	struct list *waiter_b_sema = &(b_sema->semaphore.waiters);

	struct thread *aHead = list_entry(list_begin(waiter_a_sema), struct thread, elem);
	struct thread *bHead = list_entry(list_begin(waiter_b_sema), struct thread, elem);

	if(aHead->priority > bHead->priority) {
		return true;
	}
	else {
		return false;
	}
}





/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
// cond_wait로 자고 있는 스레드를 깨우는 함수
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters)) {

		// cond waiters 우선순위 순으로 정렬
		list_sort(&cond->waiters, sema_compare_priority, NULL);

		// condition waiters에 있는 HEAD semaphore_elem의 semaphore를 가져와서 sema_up 해준다.
		sema_up(&list_entry (list_pop_front (&cond->waiters), struct semaphore_elem, elem)->semaphore);
	}
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}
