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
// 
//세마포어의 값이 양수가 될 때까지 대기 -> 양수되면 그 값을 감소시킴. 
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable (); //인터럽트 비활성화. 세마포어 연산 중에 다른 인터럽트가 발생하지 않도록.

	/*세마포어의 값이 0(세마포어가 사용중이면) - 기다리는 쓰레드 Waiters목록에 정렬하여 삽입 ,
	현재 쓰레드를 block 상태로 전환. => 세마포어의 값이 양수가 될 때까지 대기 */
	
	// while -> if문으로 바꿔도 테스트는 통과하지만 While문은 sema->value가 0인지 반복적으로 검사하므로
	//다른 쓰레드의 영향을 받게 되어도 안정성 보장.
	while (sema->value == 0) {
		// list_push_back (&sema->waiters, &thread_current ()->elem);
		list_insert_ordered(&sema->waiters, &thread_current()->elem, compare_thread_priority, NULL);  // 현재 쓰레드를 우선순위 순으로 waiters 리스트에 삽입 => 높은 우선순위의 스레드가 가장 먼저 공유 자원에 접근할 수 있도록
		thread_block ();
	}
	//세마포어의 값이 양수가 되면 -1. => 자원에 대한 접근 권한 획득
	sema->value--; 
	intr_set_level (old_level); //원래의 인터럽트 레벨로 복원
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
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();

	//세마포어의 대기자 목록이 비어있지 않다면, 대기 중인 쓰레드가 있다면
	if (!list_empty (&sema->waiters)) {
		//sema->waiters - 기다리고 있는 쓰레드 리스트 : 우선순위 순서로 정렬 => 
		//thread_unblock할 때 가장 높은 우선순위를 가진 쓰레드가 먼저 깨어나게끔
		list_sort(&sema->waiters, compare_thread_priority, NULL);

		thread_unblock (list_entry (list_pop_front (&sema->waiters),
					struct thread, elem));
	}
	
	sema->value++; //세마포어의 값 1 증가 / 깨울 수 있는 쓰레드의 수 증가 -> 
	preempt_thread(); //현재 쓰레드와 ready_list에 있는 쓰레드의 우선순위 비교 - 우선순위가 더 높은 쓰레드로 전환
	intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
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
/* lock 획득 함수*/
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL); 
	ASSERT (!intr_context ()); 
	//Lock을 획득하는 동작은 인터럽트 컨텍스트에서 수행되면 안됨. 일반적으로 사용자 레벨의 코드에서만 수행되어야 한다.
	ASSERT (!lock_held_by_current_thread (lock)); //현재 쓰레드가 이미 해당 락을 보유하고 있지 않는지 확인
	//락을 이미 보유한 쓰레드가 다시 시도할 경우 데드락 발생시킬 수 있음. 

	if (thread_mlfqs) {
		sema_down(&lock->semaphore);
		lock->holder = thread_current();
		return ;
	}

	//해당 Lock의 holder가 이미 존재한다면(다른 쓰레드가 Lock을 보유하고 있다면)
	if (lock->holder != NULL) {
		thread_current()->wait_on_lock = lock; 

		// list_push_back(&lock->holder->donations, &thread_current()->d_elem);
		//현재 쓰레드를 락을 보유한 쓰레드의 donations 리스트에 우선순위에 따라 삽입 
		list_insert_ordered(&lock->holder->donations, &(thread_current()->d_elem),compare_donation_priority, NULL);

		donate_priority(); // priority donation: lock을 보유한 쓰레드의 우선순위를 현재 쓰레드의 우선순위로 설정
	}
	sema_down (&lock->semaphore); 
	//lock에 연결된 세마포어의 값이 0보다 크면 1감소, 그렇지 않으면 대기 상태로 전환
	//lock을 획득하기 위해 대기하거나 즉시 획득하는 역할 -> Lock 획득시 1--(공유 자원에 대한 접근 권한 획득했음을 의미) 

	//현재 쓰레드가 lock을 획득하게 되면, 
	thread_current()->wait_on_lock = NULL; //현재 쓰레드가 더 이상 이 lock을 기다리고 있지 않음 표시
	lock->holder = thread_current(); //lock의 소유자는 현재 쓰레드로 설정

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
/*현재 쓰레드가 소유한 Lock 해제, 해당 잠금에 대기 중인 다른 쓰레드가 있다면 실행되도록*/
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock)); //lock을 해제하기 전에 현재 쓰레드가 Lock의 소유자인지 확인
	lock->holder = NULL; //lock의 소유자를 NULL로
	
	

	if(!thread_mlfqs){
		remove_with_lock(lock);
		revoke_priority();
	}

	sema_up (&lock->semaphore); /*세마포어의 값 1증가. -> 
	만약 세마포어의 값이 0이하였다면, 하나 이상의 쓰레드가 lock을 얻기 위해 대기 중이었음.
	-> 하나의 쓰레드가 깨어나서 lock을 얻을 수 있게 됨 */
	
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
/* 조건 변수 초기화. 조건 변수는 한 부분의 코드가 특정 조건을 signal하고, 
이를 기다리고 있는 다른 코드가 그 신호를 받아 해당 조건에 대해 행동을 취할 수 있도록 하는 동기화 매커니즘*/
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters); //조건 변수에 waiters 리스트(조건 변수를 기다리고 있는 쓰레드들) 초기화.
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

/*cond_wait: 특정 조건이 만족될 때까지 쓰레드가 대기*/
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0); 

	// list_push_back (&cond->waiters, &waiter.elem);

	/*condition -> waiters: 특정 조건이 충족되기를 기다리고 있는 쓰레드의 리스트 
	waiters 리스트에 추가되는 각 semaphore_elem이 대표하는 가장 우선순위가 높은 쓰레드의 우선순위를 기준으로 정렬
	*/
	list_insert_ordered(&cond->waiters, &waiter.elem, compare_sema_priority, NULL);
	
	lock_release (lock); //현재 쓰레드가 보유하고 있는 락 해제 -> 다른 쓰레드들이 공유 자원에 접근 가능 
	
	//현재 semaphore의 Value = 0 이니까 sema_down() => Blocked 상태가 됨.
	sema_down (&waiter.semaphore); 

	// waiter 세마포어의 값 0 이상이 될 때까지 현재 쓰레드 대기(cond_signal, cond_broadcast)
	lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */

/*condvar를 사용하는 곳에서/조건이 충족되었을 때,  대기 중인 쓰레드 하나를 꺠우는 역할*/
/*sema_up(): 세마 포어 값 증가 
- 세마포어를 사용하고 있던 쓰레드가 자원 사용을 마치고 다른 쓰레드가 자원을 사용할 수 있도록 허용하는데 사용
sema_up => 조건 변수에 의해 대기 중이던 세마포어의 값 증가 -> 대기중인 쓰레드 하나가 깨어나서 실행 상태로 전환 
*/
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters)){
		list_sort(&cond->waiters, compare_sema_priority, NULL);

		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
	}
	
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.
 \\
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

/*Compare_sema_priority*/
// 각 세마포어의 waiters에서 우선순위가 가장 큰 쓰레드끼리 비교
bool compare_sema_priority(const struct list_elem *a, const struct list_elem *b, void *aux){
	struct semaphore_elem *sema_elem_a = list_entry(a, struct semaphore_elem, elem);
	struct semaphore_elem *sema_elem_b = list_entry(b, struct semaphore_elem, elem);


	struct list *waiters_list_a = &(sema_elem_a -> semaphore.waiters);
	struct list *waiters_list_b = &(sema_elem_b -> semaphore.waiters);

	struct thread *first_thread_a = list_entry(list_begin(waiters_list_a), struct thread, elem);
	struct thread *first_thread_b = list_entry(list_begin(waiters_list_b), struct thread, elem);

	if (first_thread_a ->priority > first_thread_b->priority) {
		return true;
	} else return false;
}


/*compare donation priority*/
bool compare_donation_priority(const struct list_elem *a, const struct list_elem *b, void *aux){
	struct thread *th_a = list_entry(a, struct thread, d_elem);
	struct thread *th_b = list_entry(b, struct thread, d_elem);

	return th_a->priority > th_b-> priority;
}



/*
priority donation을 수행하는 함수
현재 쓰레드가 기다리고 있는 Lock과 연결된 모든 쓰레드들을 순회하며
현재 쓰레드의 우선순위를 lock을 보유하고 있는 쓰레드에게 donate
*/
void donate_priority(void) {

	struct thread *cur = thread_current();
	int cnt = 0;

	for (cnt; cnt < 9; cnt++){
		
		//현재 쓰레드를 기다리고 있는 lock이 없으면 반복문 종료
		if (cur->wait_on_lock == NULL) {
			break;
		}
		else 
		{	
			//holder = 현재 lock을 보유하고 있는 쓰레드
			struct thread *holder = cur->wait_on_lock->holder; 	
			//lock을 보유하고 있는 쓰레드의 우선순위를 현재 쓰레드의 우선순위로 설정(priority donation)
			holder->priority = cur->priority;
			cur= holder;
		}
	
	}			
		// cur->wait_on_lock->holder->priority = cur_priority; 
	
}	



/*특정 Lock을 기다리는 모든 쓰레드를 현재 쓰레드의 donation list에서 Remove*

/*lock_release() 했을 때 donations 리스트에서 해당 엔트리를 삭제하기 위한 함수*/
/*현재 쓰레드의 donations list를 확인하여 해지할 Lock을 보유하고 있는 엔트리 삭제*/
void remove_with_lock(struct lock *lock){
	struct thread *cur = thread_current();
	struct list_elem *elem = list_begin(&cur->donations);


	//lock이 여러개일 때 -> 순회가 필요
	//현재 쓰레드의 Donations(다른 쓰레드로 부터 기부받은 우선순위 리스트 순회)
	for (elem; elem!= list_end(&cur -> donations);){
		struct thread *t = list_entry(elem, struct thread, d_elem);

		//현재 쓰레드가 lock을 기다리고 있다면 donation list에서 제거
		if (t->wait_on_lock == lock){
			elem = list_remove(&t->d_elem);
		}
		else {
			elem = list_next(elem);
		}
	}
}
/*
 해당 lock 자원을 더 이상 안기다리게 되었을 때,
더 이상 priority donation이 적용되지 않도록 함.
-> 특정 쓰레드가 어떤 락을 기다리면서 더 높은 우선순위의 쓰레드로부터 Priority donation을 
받았다면, 그 기부받은 우선순위를 donations 리스트에서 제거해야 한다.
*/





/*
쓰레드의 우선순위가 변경되었을 떄 donation을 고려하여 우선순위를 다시 결정하는 함수

현재 쓰레드의 우선순위를 Donate 받기 전의 우선순위로 변경
OR 
(multiple-donations)인 경우에는,  
가장 우선순위가 높은 donations 리스트의 쓰레드와 현재 쓰레드의 우선순위를 비교해서
높은 우선순위의 값을 현재 쓰레드의 우선순위로 설정
*/
void revoke_priority(void){
	struct thread *cur = thread_current();

	//현재 쓰레드의 우선순위를 원래 초기 우선순위 값으로 되돌림!
	cur->priority = cur->init_priority;

	//기부 받은 우선순위가 있다면 
	if (!list_empty(&cur->donations)){
		//donations list에서 가장 큰 우선순위 값
		//list_begin에서 list_front로 수정
		struct thread *donation_max_priority = list_entry(list_front(&cur->donations), struct thread, d_elem);

		//기부 받은 우선순위와 현재 쓰레드의 우선순위를 비교해서, 
		//기부 받은 우선순위 값이 더 크다면 우선순위 업데이트
		// if (donation_max_priority->priority > cur->priority){
		cur->priority = donation_max_priority->priority;
		// }
	}
}