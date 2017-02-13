/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Synchronization primitives.
 * The specifications of the functions are in synch.h.
 */

#include <types.h>
#include <lib.h>
#include <spinlock.h>
#include <wchan.h>
#include <thread.h>
#include <current.h>
#include <synch.h>

////////////////////////////////////////////////////////////
//
// Semaphore.

struct semaphore *
sem_create(const char *name, unsigned initial_count)
{
	struct semaphore *sem;

	sem = kmalloc(sizeof(*sem));
	if (sem == NULL) {
		return NULL;
	}

	sem->sem_name = kstrdup(name);
	if (sem->sem_name == NULL) {
		kfree(sem);
		return NULL;
	}

	sem->sem_wchan = wchan_create(sem->sem_name);
	if (sem->sem_wchan == NULL) {
		kfree(sem->sem_name);
		kfree(sem);
		return NULL;
	}

	spinlock_init(&sem->sem_lock);
	sem->sem_count = initial_count;

	return sem;
}

void
sem_destroy(struct semaphore *sem)
{
	KASSERT(sem != NULL);

	/* wchan_cleanup will assert if anyone's waiting on it */
	spinlock_cleanup(&sem->sem_lock);
	wchan_destroy(sem->sem_wchan);
	kfree(sem->sem_name);
	kfree(sem);
}

void
P(struct semaphore *sem)
{
	KASSERT(sem != NULL);

	/*
	 * May not block in an interrupt handler.
	 *
	 * For robustness, always check, even if we can actually
	 * complete the P without blocking.
	 */
	KASSERT(curthread->t_in_interrupt == false);

	/* Use the semaphore spinlock to protect the wchan as well. */
	spinlock_acquire(&sem->sem_lock);
	while (sem->sem_count == 0) {
		/*
		 *
		 * Note that we don't maintain strict FIFO ordering of
		 * threads going through the semaphore; that is, we
		 * might "get" it on the first try even if other
		 * threads are waiting. Apparently according to some
		 * textbooks semaphores must for some reason have
		 * strict ordering. Too bad. :-)
		 *
		 * Exercise: how would you implement strict FIFO
		 * ordering?
		 */
		wchan_sleep(sem->sem_wchan, &sem->sem_lock);
	}
	KASSERT(sem->sem_count > 0);
	sem->sem_count--;
	spinlock_release(&sem->sem_lock);
}

void
V(struct semaphore *sem)
{
	KASSERT(sem != NULL);

	spinlock_acquire(&sem->sem_lock);

	sem->sem_count++;
	KASSERT(sem->sem_count > 0);
	wchan_wakeone(sem->sem_wchan, &sem->sem_lock);

	spinlock_release(&sem->sem_lock);
}

////////////////////////////////////////////////////////////
//
// Lock.

struct lock *
lock_create(const char *name)
{
	struct lock *lock;

	lock = kmalloc(sizeof(*lock));
	if (lock == NULL) {
		return NULL;
	}

	lock->lk_name = kstrdup(name);
	if (lock->lk_name == NULL) {
		kfree(lock);
		return NULL;
	}

	HANGMAN_LOCKABLEINIT(&lock->lk_hangman, lock->lk_name);

	// add stuff here as needed
	lock->lk_wchan = wchan_create(lock->lk_name);
	if (lock->lk_wchan == NULL) {
		kfree(lock->lk_name);
		kfree(lock);
		return NULL;
	}

	spinlock_init(&lock->lk_slk);
	lock->lk_hold = false;
	lock->lk_thread = NULL;
	return lock;
}

void
lock_destroy(struct lock *lock)
{
	KASSERT(lock != NULL && lock->lk_thread == NULL);

	// add stuff here as needed
	// if(!wchan_isempty(lock->lk_wchan, &lock->lk_slk)){
	// 	spinlock_acquire(&lock->lk_slk);
	// 	wchan_wakeall(lock->lk_wchan,&lock->lk_slk);
	// 	spinlock_release(&lock->lk_slk);
	// }''
	spinlock_cleanup(&lock->lk_slk);
	wchan_destroy(lock->lk_wchan);
	kfree(lock->lk_name);
	lock->lk_thread = NULL;
	kfree(lock);
}

void
lock_acquire(struct lock *lock)
{
	/* Call this (atomically) before waiting for a lock */


	// (void)lock;  // suppress warning until code gets written
	KASSERT(lock != NULL && !lock_do_i_hold(lock));
	KASSERT(curthread->t_in_interrupt == false);

	HANGMAN_WAIT(&curthread->t_hangman, &lock->lk_hangman);

	spinlock_acquire(&lock->lk_slk);


	while(lock->lk_hold){
		wchan_sleep(lock->lk_wchan, &lock->lk_slk);
	}
		KASSERT(!lock->lk_hold);
		lock->lk_hold = true;
		lock->lk_thread = curthread;

	spinlock_release(&lock->lk_slk);
	/* Call this (atomically) once the lock is acquired */
	HANGMAN_ACQUIRE(&curthread->t_hangman, &lock->lk_hangman);
}

void
lock_release(struct lock *lock)
{
	/* Call this (atomically) when the lock is released */

	// Write this
	KASSERT(lock != NULL && lock_do_i_hold(lock));
	KASSERT(curthread->t_in_interrupt == false);

	spinlock_acquire(&lock->lk_slk);

	if(lock_do_i_hold(lock)){

		lock->lk_hold = false;
		lock->lk_thread = NULL;
		wchan_wakeone(lock->lk_wchan, &lock->lk_slk);
		HANGMAN_RELEASE(&curthread->t_hangman, &lock->lk_hangman);
	}
	spinlock_release(&lock->lk_slk);
	//(void)lock;  // suppress warning until code gets written

}

bool
lock_do_i_hold(struct lock *lock)
{
	// Write this
	KASSERT(lock!=NULL);
	if(lock->lk_thread == curthread){
		return true;
	}else{
		return false;
	}
	//(void)lock;  // suppress warning until code gets written

	//return true; // dummy until code gets written
}

////////////////////////////////////////////////////////////
//
// CV


struct cv *
cv_create(const char *name)
{
	struct cv *cv;

	cv = kmalloc(sizeof(*cv));
	if (cv == NULL) {
		return NULL;
	}

	cv->cv_name = kstrdup(name);
	if (cv->cv_name==NULL) {
		kfree(cv);
		return NULL;
	}

	// add stuff here as needed
	cv->cv_wchan = wchan_create(cv->cv_name);
	if (cv->cv_wchan == NULL) {
		kfree(cv->cv_name);
		kfree(cv);
		return NULL;
	}
	spinlock_init(&cv->cv_slk);
	return cv;
}

void
cv_destroy(struct cv *cv)
{
	KASSERT(cv != NULL);

	// add stuff here as needed
	spinlock_cleanup(&cv->cv_slk);
	wchan_destroy(cv->cv_wchan);
	kfree(cv->cv_name);
	kfree(cv);

}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	// Write this
	// KASSERT()
	KASSERT(lock != NULL && lock_do_i_hold(lock) && cv != NULL);
	KASSERT(curthread->t_in_interrupt == false);

	spinlock_acquire(&cv->cv_slk);
	lock_release(lock);
	wchan_sleep(cv->cv_wchan, &cv->cv_slk);
	spinlock_release(&cv->cv_slk);
	lock_acquire(lock);
	// (void)cv;    // suppress warning until code gets written
	// (void)lock;  // suppress warning until code gets written
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	// Write this
	KASSERT(lock != NULL && lock_do_i_hold(lock) && cv != NULL);
	KASSERT(curthread->t_in_interrupt == false);

	spinlock_acquire(&cv->cv_slk);
	if(!wchan_isempty(cv->cv_wchan, &cv->cv_slk)){
		wchan_wakeone(cv->cv_wchan, &cv->cv_slk);
	}
	spinlock_release(&cv->cv_slk);
	// (void)cv;    // suppress warning until code gets written
	// (void)lock;  // suppress warning until code gets written
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	// Write this
	KASSERT(lock != NULL && lock_do_i_hold(lock) && cv != NULL);
	spinlock_acquire(&cv->cv_slk);
	if(!wchan_isempty(cv->cv_wchan, &cv->cv_slk)){
		wchan_wakeall(cv->cv_wchan, &cv->cv_slk);
	}
	spinlock_release(&cv->cv_slk);
	// (void)cv;    // suppress warning until code gets written
	// (void)lock;  // suppress warning until code gets written
}

////////////////////////////////////////////////////////////
//
// RW
struct
rwlock * rwlock_create(const char * name){
	struct rwlock *rwlock;
	rwlock = kmalloc(sizeof(*rwlock));
	if (rwlock == NULL) {
		return NULL;
	}

	rwlock->rwlock_name = kstrdup(name);
	if (rwlock->rwlock_name==NULL) {
		kfree(rwlock);
		return NULL;
	}

	rwlock->rwlock_read_hold = false;
	rwlock->rwlock_write_hold = false;
	rwlock->rwlock_write_wait = false;

	rwlock->rwlock_cv = cv_create(rwlock->rwlock_name);
	if (rwlock->rwlock_cv == NULL) {
		kfree(rwlock->rwlock_name);
		kfree(rwlock);
		return NULL;
	}

	rwlock->rwlock_lk = lock_create(rwlock->rwlock_name);
	if (rwlock->rwlock_lk == NULL) {
		kfree(rwlock->rwlock_name);
		kfree(rwlock);
		return NULL;
	}

	return rwlock;
}

void
rwlock_destroy(struct rwlock *rwlock){
	KASSERT(rwlock != NULL);
	lock_destroy(rwlock->rwlock_lk);
	cv_destroy(rwlock->rwlock_cv);
	kfree(rwlock->rwlock_name);
	kfree(rwlock);
}

void
rwlock_acquire_read(struct rwlock *rwlock){
		KASSERT(rwlock != NULL );
		KASSERT(rwlock->rwlock_cv != NULL);
		// spinlock_acquire(&rwlock->rwlock_slk);
		lock_acquire(rwlock->rwlock_lk);

		while(rwlock->rwlock_write_hold || rwlock->rwlock_write_wait){
			// wchan_sleep(rwlock->rwlock_wchan, &rwlock->rwlock_slk);
			cv_wait(rwlock->rwlock_cv, rwlock->rwlock_lk);
		}

		KASSERT(!rwlock->rwlock_write_hold && !rwlock->rwlock_write_wait);
		// rwlock->rwlock_read_count++;

		rwlock->rwlock_read_hold = true;

		// spinlock_release(&rwlock->rwlock_slk);
}

void
rwlock_release_read(struct rwlock *rwlock){
	KASSERT(rwlock != NULL);
	KASSERT(lock_do_i_hold(rwlock->rwlock_lk));
	KASSERT(rwlock->rwlock_cv != NULL);
	rwlock->rwlock_read_hold = false;
	cv_signal(rwlock->rwlock_cv, rwlock->rwlock_lk);
	lock_release(rwlock->rwlock_lk);
}

void
rwlock_acquire_write(struct rwlock *rwlock){
	KASSERT(rwlock != NULL);
	KASSERT(rwlock->rwlock_cv != NULL);
	// spinlock_acquire(&rwlock->rwlock_slk);
	// rwlock->rwlock_write_preenter_count++;
	lock_acquire(rwlock->rwlock_lk);
	while(rwlock->rwlock_read_hold || rwlock->rwlock_write_hold){
		// wchan_sleep(rwlock->rwlock_wchan, &rwlock->rwlock_slk);
		rwlock->rwlock_write_wait = true;
		cv_wait(rwlock->rwlock_cv, rwlock->rwlock_lk);
	}
	KASSERT(!rwlock->rwlock_read_hold && !rwlock->rwlock_write_hold);
	// KASSERT(rwlock->rwlock_write_preenter_count > 0);
	// rwlock->rwlock_write_preenter_count--;
	rwlock->rwlock_write_hold = true;
	rwlock->rwlock_write_wait = false;

	// spinlock_release(&rwlock->rwlock_slk);
}

void
rwlock_release_write(struct rwlock *rwlock){
	KASSERT(rwlock != NULL);
	KASSERT(lock_do_i_hold(rwlock->rwlock_lk));
	KASSERT(rwlock->rwlock_cv != NULL);
	rwlock->rwlock_write_hold = false;
	cv_signal(rwlock->rwlock_cv, rwlock->rwlock_lk);
	lock_release(rwlock->rwlock_lk);

}
