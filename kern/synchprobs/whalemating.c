/*
 * Copyright (c) 2001, 2002, 2009
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
 * Driver code is in kern/tests/synchprobs.c We will
 * replace that file. This file is yours to modify as you see fit.
 *
 * You should implement your solution to the whalemating problem below.
 */

#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

static struct semaphore *sem_mating;
static struct lock *lock_male;
static struct lock *lock_female;
static struct lock *lock_mm;
// static struct cv *cv_male;
// static struct cv *cv_female;
static struct cv *cv_mating;
static struct lock *lock_sem;
// static volatile unsigned mating_count;
/*
 * Called by the driver during initialization.
 */

void whalemating_init() {
	sem_mating = sem_create("sem_mating",0);
	lock_male = lock_create("lock_male");
	lock_female = lock_create("lock_female");
	lock_mm = lock_create("lock_mm");
	// cv_male = cv_create("cv_male");
	// cv_female = cv_create("cv_female");
	cv_mating = cv_create("cv_mating");
	lock_sem = lock_create("lock_sem");
	// mating_count = 0;
	return;
}

/*
 * Called by the driver during teardown.
 */

void
whalemating_cleanup() {
	sem_destroy(sem_mating);
	lock_destroy(lock_male);
	lock_destroy(lock_female);
	lock_destroy(lock_mm);
	// cv_destroy(cv_male);
	// cv_destroy(cv_female);
	cv_destroy(cv_mating);
	lock_destroy(lock_sem);
	return;
}

void
male(uint32_t index)
{
	(void)index;
	male_start(index);

	lock_acquire(lock_male);
	lock_acquire(lock_sem);
	V(sem_mating);
	// mating_count++;
	KASSERT(sem_mating->sem_count <= 3);
	if(sem_mating->sem_count < 3){
	cv_wait(cv_mating, lock_sem);
	}

	if(sem_mating->sem_count > 0){
		P(sem_mating);
		cv_signal(cv_mating, lock_sem);
	}

	lock_release(lock_sem);
	lock_release(lock_male);

	male_end(index);
	/*
	 * Implement this function by calling male_start and male_end when
	 * appropriate.
	 */

	return;
}

void
female(uint32_t index)
{
	(void)index;
	female_start(index);

	lock_acquire(lock_female);
	lock_acquire(lock_sem);
	V(sem_mating);
	// mating_count++;
	KASSERT(sem_mating->sem_count <= 3);
	if(sem_mating->sem_count < 3){
	cv_wait(cv_mating, lock_sem);
	}

	if(sem_mating->sem_count > 0){
		P(sem_mating);
		cv_signal(cv_mating, lock_sem);
	}

	lock_release(lock_sem);
	lock_release(lock_female);

	female_end(index);
	/*
	 * Implement this function by calling female_start and female_end when
	 * appropriate.
	 */
	return;
}

void
matchmaker(uint32_t index)
{
	(void)index;
	matchmaker_start(index);

	lock_acquire(lock_mm);
	lock_acquire(lock_sem);
	V(sem_mating);
	// mating_count++;
	KASSERT(sem_mating->sem_count <= 3);
	if(sem_mating->sem_count < 3){
	cv_wait(cv_mating, lock_sem);
	}

	if(sem_mating->sem_count > 0){
		P(sem_mating);
		cv_signal(cv_mating, lock_sem);
	}

	lock_release(lock_sem);
	lock_release(lock_mm);

	matchmaker_end(index);
	/*
	 * Implement this function by calling matchmaker_start and matchmaker_end
	 * when appropriate.
	 */
	return;
}
