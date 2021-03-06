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
 * Driver code is in kern/tests/synchprobs.c We will replace that file. This
 * file is yours to modify as you see fit.
 *
 * You should implement your solution to the stoplight problem below. The
 * quadrant and direction mappings for reference: (although the problem is, of
 * course, stable under rotation)
 *
 *   |0 |
 * -     --
 *    01  1
 * 3  32
 * --    --
 *   | 2|
 *
 * As way to think about it, assuming cars drive on the right: a car entering
 * the intersection from direction X will enter intersection quadrant X first.
 * The semantics of the problem are that once a car enters any quadrant it has
 * to be somewhere in the intersection until it call leaveIntersection(),
 * which it should call while in the final quadrant.
 *
 * As an example, let's say a car approaches the intersection and needs to
 * pass through quadrants 0, 3 and 2. Once you call inQuadrant(0), the car is
 * considered in quadrant 0 until you call inQuadrant(3). After you call
 * inQuadrant(2), the car is considered in quadrant 2 until you call
 * leaveIntersection().
 *
 * You will probably want to write some helper functions to assist with the
 * mappings. Modular arithmetic can help, e.g. a car passing straight through
 * the intersection entering from direction X will leave to direction (X + 2)
 * % 4 and pass through quadrants X and (X + 3) % 4.  Boo-yah.
 *
 * Your solutions below should call the inQuadrant() and leaveIntersection()
 * functions in synchprobs.c to record their progress.
 */

#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

/*
 * Called by the driver during initialization.
 */
#ifndef NUM_QUADRANTS
#define NUM_QUADRANTS 4
#endif
#ifndef GO_STRAIGHT
#define GO_STRAIGHT 0
#endif
#ifndef TURN_LEFT
#define TURN_LEFT 1
#endif
#ifndef TURN_RIGHT
#define TURN_RIGHT 2
#endif
#ifndef NUM_LOCKS_IN_STOPLIGHT
#define NUM_LOCKS_IN_STOPLIGHT 3
#endif


static struct semaphore * sem[NUM_QUADRANTS];
static struct lock * lock_sem[NUM_LOCKS_IN_STOPLIGHT];
void
stoplight_init() {
	for(int i=0; i<NUM_QUADRANTS; i++){
		sem[i] = sem_create("sem_stoplight",1);
	}

	for(int i=0; i<NUM_LOCKS_IN_STOPLIGHT; i++){
		lock_sem[i] = lock_create("lock_stoplight");
	}

	return;
}

/*
 * Called by the driver during teardown.
 */

void stoplight_cleanup() {
	for(int i=0; i<NUM_QUADRANTS; i++){
		sem_destroy(sem[i]);
	}
	for(int i=0; i<NUM_LOCKS_IN_STOPLIGHT; i++){
		lock_destroy(lock_sem[i]);
	}
	return;
}

void
turnright(uint32_t direction, uint32_t index)
{
	(void)direction;
	(void)index;
	lock_acquire(lock_sem[TURN_RIGHT]);
	P(sem[direction]);
	inQuadrant(direction, index);
	leaveIntersection(index);
	V(sem[direction]);
	lock_release(lock_sem[TURN_RIGHT]);

	/*
	 * Implement this function.
	 */
	return;
}
void
gostraight(uint32_t direction, uint32_t index)
{
	(void)direction;
	(void)index;
	lock_acquire(lock_sem[GO_STRAIGHT]);
	P(sem[direction]);
	inQuadrant(direction, index);
	P(sem[(direction - 1) % NUM_QUADRANTS]);
	inQuadrant((direction - 1) % NUM_QUADRANTS, index);
	// lock_release(lock_sem);
	//
	// lock_acquire(lock_sem);
	V(sem[direction]);
	leaveIntersection(index);
	V(sem[(direction - 1) % NUM_QUADRANTS]);
	lock_release(lock_sem[GO_STRAIGHT]);
	/*
	 * Implement this function.
	 */
	return;
}
void
turnleft(uint32_t direction, uint32_t index)
{
	(void)direction;
	(void)index;
	lock_acquire(lock_sem[TURN_LEFT]);
	P(sem[direction]);
	inQuadrant(direction, index);
	P(sem[(direction - 1) % NUM_QUADRANTS]);
	inQuadrant((direction - 1) % NUM_QUADRANTS, index);
	// lock_release(lock_sem);
	//
	// lock_acquire(lock_sem);
	V(sem[direction]);
	P(sem[(direction - 2) % NUM_QUADRANTS]);
	inQuadrant((direction - 2) % NUM_QUADRANTS, index);
	// lock_release(lock_sem);
	//
	// lock_acquire(lock_sem);
	V(sem[(direction - 1) % NUM_QUADRANTS]);
	leaveIntersection(index);
	V(sem[(direction - 2) % NUM_QUADRANTS]);
	lock_release(lock_sem[TURN_LEFT]);
	/*
	 * Implement this function.
	 */
	return;
}
