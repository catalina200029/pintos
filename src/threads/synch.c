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

static bool sema_elem_less_func(const struct list_elem *a,
                                const struct list_elem *b,
                                void *aux UNUSED);

///* Priority donation on lock acquire */
//static void
//donate_priority(struct thread *donator, struct lock *lock);

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
     decrement it.

   - up or "V": increment the value (and wake up one waiting
     thread, if any). */
void
sema_init(struct semaphore *sema, unsigned value) {
    ASSERT (sema != NULL);

    sema->value = value;
    ordered_list_init(&sema->waiters, thread_less_func, NULL);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. */
void
sema_down(struct semaphore *sema) {
    enum intr_level old_level;

    ASSERT (sema != NULL);
    ASSERT (!intr_context());

    old_level = intr_disable();
    while (sema->value == 0) {
        ordered_list_insert(&sema->waiters, &thread_current()->elem);
        thread_block();
    }
    sema->value--;
    intr_set_level(old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down(struct semaphore *sema) {
    enum intr_level old_level;
    bool success;

    ASSERT (sema != NULL);

    old_level = intr_disable();
    if (sema->value > 0) {
        sema->value--;
        success = true;
    } else {
        success = false;
    }
    intr_set_level(old_level);

    return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up(struct semaphore *sema) {
    enum intr_level old_level;

    ASSERT (sema != NULL);

    struct thread *awoken_thread = NULL;

    old_level = intr_disable();
    if (!ordered_list_empty(&sema->waiters)) {
        // Resort list -- side effects may have occurred on priorities due to
        // donation through locks.
        ordered_list_resort(&sema->waiters);
        awoken_thread = list_entry (ordered_list_pop_front(&sema->waiters),
                                    struct thread, elem);
        ASSERT(awoken_thread != NULL);
        thread_unblock(awoken_thread);
    }
    sema->value++;

    // Technically, we should be checking that the next thread to run now
    // has a higher priority than the current thread. However, if this
    // thread is not awoken_thread, and it didn't before, it still won't.
    // So we assume the case where it is awoken_thread.
    // Could make a function in thread.h that tells the running thread to
    // yield if it is no longer the highest priority thread?
    if (!intr_context() && awoken_thread != NULL) {
        thread_yield();
    }

    intr_set_level(old_level);
}

static void sema_test_helper(void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test(void) {
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
static void
sema_test_helper(void *sema_) {
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
void
lock_init(struct lock *lock) {
    ASSERT (lock != NULL);

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
void
lock_acquire(struct lock *lock) {
    ASSERT (lock != NULL);
    ASSERT (!intr_context());
    ASSERT (!lock_held_by_current_thread(lock));

    struct thread *curr = thread_current();
    struct thread *lock_holder = lock->holder;

    ASSERT(curr->priority.lock_blocked_by == NULL);

    if (lock_holder != NULL) {
        curr->priority.lock_blocked_by = lock;
        list_push_front(&lock_holder->priority.donors, &curr->donor_elem);
        thread_recalculate_effective_priority(lock_holder);
    }

    sema_down(&lock->semaphore);
    lock->holder = curr;
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire(struct lock *lock) {
    bool success;

    ASSERT (lock != NULL);
    ASSERT (!lock_held_by_current_thread(lock));

    success = sema_try_down(&lock->semaphore);
    if (success) {
        lock->holder = thread_current();
    }
    return success;
}

/* Releases LOCK, which must be owned by the current thread.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
// TODO: Possibly remove intr disable
void
lock_release(struct lock *lock) {
    ASSERT (lock != NULL);
    ASSERT (lock_held_by_current_thread(lock));

    enum intr_level old_level = intr_disable();

    // Remove lock from holder's acquired locks list.
    lock->holder = NULL;

    /* Have everything in the tail of the waiters list move their donation to
       the head of the waiters list */
    struct thread *to_be_woken = NULL;
    struct ordered_list *waiters = lock_get_waiters(lock);
    if (!ordered_list_empty(waiters)) {
        to_be_woken = list_entry(
                ordered_list_front(waiters),
                struct thread,
                elem
        );


        /* Revoke donations of all waiters.
         * Explicitly revoke the head's. Move the tails' donations to the
         * head */
        struct list_elem *e = list_next(&to_be_woken->elem);
        struct thread *still_waiting;
        for (; e != list_end(&waiters->list); e = list_next(e)) {
            still_waiting = list_entry(e, struct thread, elem);
            list_push_front(
                    &to_be_woken->priority.donors,
                    &still_waiting->donor_elem
            );
        }

        list_remove(&to_be_woken->donor_elem);

        // to_be_woken now has the donations of the other waiters. We need to
        // null out it's lock_blocked_by and recalculate its priority.
        to_be_woken->priority.lock_blocked_by = NULL;
        thread_recalculate_effective_priority(to_be_woken);
    }

    // Recalculate effective priority of releasing thread.
    thread_recalculate_effective_priority(thread_current());

    intr_set_level(old_level);
    sema_up(&lock->semaphore);
}

/*
 * Returns a reference to the list of waiters of the lock.
 */
struct ordered_list *
lock_get_waiters(struct lock *lock) {
    return &lock->semaphore.waiters;
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread(const struct lock *lock) {
    ASSERT (lock != NULL);

    return lock->holder == thread_current();
}

/* One semaphore in a list. */
struct semaphore_elem {
    struct list_elem elem;              /* List element. */
    struct semaphore semaphore;         /* This semaphore. */
    int waiting_thread_priority;        /* Priority of thread using the condvar */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init(struct condition *cond) {
    ASSERT (cond != NULL);

    ordered_list_init(&cond->waiters, sema_elem_less_func, NULL);
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
void
cond_wait(struct condition *cond, struct lock *lock) {
    struct semaphore_elem waiter;

    ASSERT (cond != NULL);
    ASSERT (lock != NULL);
    ASSERT (!intr_context());
    ASSERT (lock_held_by_current_thread(lock));

    sema_init(&waiter.semaphore, 0);
    waiter.waiting_thread_priority = thread_get_priority();
    ordered_list_insert(&cond->waiters, &waiter.elem);
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
void
cond_signal(struct condition *cond, struct lock *lock UNUSED) {
    ASSERT (cond != NULL);
    ASSERT (lock != NULL);
    ASSERT (!intr_context());
    ASSERT (lock_held_by_current_thread(lock));

    if (!ordered_list_empty(&cond->waiters)) {
        ordered_list_resort(&cond->waiters);
        sema_up(
                &list_entry (ordered_list_pop_front(&cond->waiters),
                             struct semaphore_elem, elem)->semaphore
        );
    }
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast(struct condition *cond, struct lock *lock) {
    ASSERT (cond != NULL);
    ASSERT (lock != NULL);

    while (!ordered_list_empty(&cond->waiters)) {
        cond_signal(cond, lock);
    }
}

static bool
sema_elem_less_func(const struct list_elem *a, const struct list_elem *b,
                    void *aux UNUSED) {
    // Get sema elems
    struct semaphore_elem *sema_elem_a = list_entry(
            a,
            struct semaphore_elem,
            elem
    );
    struct semaphore_elem *sema_elem_b = list_entry(
            b,
            struct semaphore_elem,
            elem
    );

    return sema_elem_a->waiting_thread_priority >
           sema_elem_b->waiting_thread_priority;
}