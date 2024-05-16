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

/* One semaphore in a list. */
struct semaphore_elem {
  struct list_elem elem;      /* List element. */
  struct semaphore semaphore; /* This semaphore. */
};

/* ---------- added for Project.1-2 ---------- */
static void remove_donor_lock(struct lock *lock) {
  struct thread *curr_t = thread_current();
  struct list_elem *donor_e;

  for (donor_e = list_begin(&curr_t->donations);
       donor_e != list_end(&curr_t->donations); donor_e = list_next(donor_e)) {
    struct thread *t = list_entry(donor_e, struct thread, donation_elem);

    if (t->wait_on_lock == NULL) break;

    if (t->wait_on_lock == lock) list_remove(&(t->donation_elem));
  }
}

/**
 * @brief struct list wait_on_lock에 대기중인 thread의
 *        priority를 lock->holder에게 상속한다
 * 
 * @details [case.1] : nested
 *          
 *          [case.2] : multiple
*/
static void donate_priority(void) {
  struct thread *curr_t = thread_current();
  int prev_priority = curr_t->priority;
  int depth = 0;

  while (depth < 8) {

    if (curr_t->wait_on_lock == NULL) break;

    curr_t = curr_t->wait_on_lock->holder;

    if (curr_t->priority < prev_priority) {
      curr_t->priority = prev_priority;
      prev_priority = curr_t->priority;
    }

    depth++;

    //   if (lock->holder->priority < curr_t->priority) {
    //     lock->holder->priority = curr_t->priority;
    //     list_push_back(&lock->holder->donations, &curr_t->donation_elem);
    //   }

    //   curr_t = lock->holder;
    //   lock = curr_t->wait_on_lock;

    //   depth++;
    // }
  }
}
/**
 * @brief lock->holder가 특정 lock에 대해 lock_release() 했을때
 *        상속받은 priority를 원래 priority로 되돌리거나
 *        다른 wait_on_lock의 thread->priority를 상속받는다
*/
void update_priority_donation(void) {
  struct thread *curr_t = thread_current();
  struct list_elem *higher_e;
  struct thread *higher_t;

  curr_t->priority = curr_t->initial_priority;

  if (list_empty(&curr_t->donations)) return;

  higher_e = list_max(&curr_t->donations, &priority_ascending_sort, NULL);
  higher_t = list_entry(higher_e, struct thread, donation_elem);
  // printf("\n-------------------------\n");
  // printf("higher_t->priority : %d\n", higher_t->priority);
  // printf("curr_t->priority : %d\n", curr_t->priority);
  // printf("\n-------------------------\n");
  // if (higher_t == list_begin(&curr_t->donations))

  if (curr_t->priority < higher_t->priority) {
    curr_t->priority = higher_t->priority;
  }
}

static bool asending_priority_cond(const struct list_elem *a,
                                   const struct list_elem *b,
                                   void *aux UNUSED) {
  struct semaphore_elem *sema_a, *sema_b;
  struct list_elem *e_a, *e_b;
  struct thread *t_a, *t_b;

  sema_a = list_entry(a, struct semaphore_elem, elem);
  sema_b = list_entry(b, struct semaphore_elem, elem);

  e_a = list_begin(&sema_a->semaphore.waiters);
  e_b = list_begin(&sema_b->semaphore.waiters);

  t_a = list_entry(e_a, struct thread, elem);
  t_b = list_entry(e_b, struct thread, elem);

  return (t_a->priority) > (t_b->priority);
}

/* ------------------------------------------- */

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void sema_init(struct semaphore *sema, unsigned value) {
  ASSERT(sema != NULL);

  sema->value = value;
  list_init(&sema->waiters);
}

/**
 * @brief 리소스에 접근이 가능하면 value를 감소시키고 리소스에 접근을 불가능하게 만든다.
  				만약,value가 0이라면, 현재 쓰레드를 waiters 리스트에 추가하고 block한다
 *
 *
 * @param sema 세마포어 사용하고자 하는 🚽
 * 
 * @note : Down or "P" operation on a semaphore.  Waits for SEMA's value
           to become positive and then atomically decrements it.

					 [세마포어에서 아래 또는 "P" 연산. SEMA의 값이 양수가 되기를 기다렸다가
					 원자적으로 감소시킨다.]

           This function may sleep, so it must not be called within an
           interrupt handler.  This function may be called with
           interrupts disabled, but if it sleeps then the next scheduled
           thread will probably turn interrupts back on. This is
           sema_down function.
					 
					 [이 함수는 잠이 들 수 있으므로 인터럽트 핸들러 내에서
					 호출하면 안 됩니다. 인터럽트가 비활성화된 상태에서 이 기능을 호출할 수 있지만,
					 이 기능이 비활성화되면 다음에 예약된 스레드가 인터럽트를 다시 켤 것입니다.
					 이것이 sema_down 함수입니다.]
 */
void sema_down(struct semaphore *sema) {
  enum intr_level old_level;

  ASSERT(sema != NULL);
  ASSERT(!intr_context());

  old_level = intr_disable();
  while (sema->value == 0) {
    /* --------- befroe Project.1-2 ---------
    list_push_back(&sema->waiters, &thread_current()->elem); */

    /* --------- after Project.1-2 --------- */
    list_insert_ordered(&sema->waiters, &thread_current()->elem,
                        (list_less_func *)&priority_ascending_sort, NULL);
    thread_block();
  }

  preempt_schedule();

  sema->value--;
  intr_set_level(old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool sema_try_down(struct semaphore *sema) {
  enum intr_level old_level;
  bool success;

  ASSERT(sema != NULL);

  old_level = intr_disable();
  if (sema->value > 0) {
    sema->value--;
    success = true;
  } else
    success = false;
  intr_set_level(old_level);

  return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void sema_up(struct semaphore *sema) {
  enum intr_level old_level;

  ASSERT(sema != NULL);
  old_level = intr_disable();

  if (!list_empty(&sema->waiters)) {
    /* --------- after Project.1-2 --------- */
    list_sort(&sema->waiters, (list_less_func *)&priority_ascending_sort, NULL);
    thread_unblock(
        list_entry(list_pop_front(&sema->waiters), struct thread, elem));
  }

  sema->value++;
  intr_set_level(old_level);
}

static void sema_test_helper(void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void sema_self_test(void) {
  struct semaphore sema[2];
  int i;

  printf("Testing semaphores...");
  sema_init(&sema[0], 0);
  sema_init(&sema[1], 0);
  thread_create("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
  for (i = 0; i < 10; i++) {
    sema_up(&sema[0]);
    sema_down(&sema[1]);
  }
  printf("done.\n");
}

/* Thread function used by sema_self_test(). */
static void sema_test_helper(void *sema_) {
  struct semaphore *sema = sema_;
  int i;

  for (i = 0; i < 10; i++) {
    sema_down(&sema[0]);
    sema_up(&sema[1]);
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
void lock_init(struct lock *lock) {
  ASSERT(lock != NULL);

  lock->holder = NULL;
  sema_init(&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void lock_acquire(struct lock *lock) {
  ASSERT(lock != NULL);
  ASSERT(!intr_context());
  ASSERT(!lock_held_by_current_thread(lock));

  struct thread *cur_t = thread_current();

  /* ----------- after Project.1-2 ----------- */

  /* 현재 쓰레드가 대기중인 lock이 존재하고, lock을 가진 쓰레드가 존재한다면 */
  if (lock->holder != NULL) {
    cur_t->wait_on_lock = lock;
    list_push_back(&(lock->holder->donations), &(cur_t->donation_elem));
    donate_priority();
  }

  sema_down(&lock->semaphore);

  lock->holder = cur_t; /* lock을 가진다 */
  cur_t->wait_on_lock = NULL; /* lock을 얻었으므로 대기중인 lock은 없다 */
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool lock_try_acquire(struct lock *lock) {
  bool success;

  ASSERT(lock != NULL);
  ASSERT(!lock_held_by_current_thread(lock));

  success = sema_try_down(&lock->semaphore);
  if (success) lock->holder = thread_current();
  return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void lock_release(struct lock *lock) {
  ASSERT(lock != NULL);
  ASSERT(lock_held_by_current_thread(lock));

  remove_donor_lock(lock);
  update_priority_donation();

  lock->holder = NULL;
  sema_up(&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool lock_held_by_current_thread(const struct lock *lock) {
  ASSERT(lock != NULL);

  return lock->holder == thread_current();
}

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void cond_init(struct condition *cond) {
  ASSERT(cond != NULL);

  list_init(&cond->waiters);
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
void cond_wait(struct condition *cond, struct lock *lock) {
  struct semaphore_elem waiter;

  ASSERT(cond != NULL);
  ASSERT(lock != NULL);
  ASSERT(!intr_context());
  ASSERT(lock_held_by_current_thread(lock));

  sema_init(&waiter.semaphore, 0);
  /* ----------- before Project.1-2 -----------
  list_push_back(&cond->waiters, &waiter.elem); */

  /* ----------- after Project.1-2 ----------- */
  list_insert_ordered(&cond->waiters, &waiter.elem, &asending_priority_cond,
                      NULL);
  /* -----------------------------------------*/

  lock_release(lock);
  sema_down(&waiter.semaphore);
  lock_acquire(lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void cond_signal(struct condition *cond, struct lock *lock UNUSED) {
  ASSERT(cond != NULL);
  ASSERT(lock != NULL);
  ASSERT(!intr_context());
  ASSERT(lock_held_by_current_thread(lock));

  if (!list_empty(&cond->waiters)) {
    /* ----------- after Project.1-2 ----------- */
    list_sort(&cond->waiters, &asending_priority_cond, NULL);
    /* -----------------------------------------*/

    sema_up(
        &list_entry(list_pop_front(&cond->waiters), struct semaphore_elem, elem)
             ->semaphore);
  }
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void cond_broadcast(struct condition *cond, struct lock *lock) {
  ASSERT(cond != NULL);
  ASSERT(lock != NULL);

  while (!list_empty(&cond->waiters)) cond_signal(cond, lock);
}
