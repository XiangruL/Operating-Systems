/*
 * All the contents of this file are overwritten during automated
 * testing. Please consider this before changing anything in this file.
 */

#include <types.h>
#include <lib.h>
#include <clock.h>
#include <thread.h>
#include <synch.h>
#include <test.h>
#include <kern/test161.h>
#include <spinlock.h>

static struct semaphore *exitsem;
static struct rwlock *rwlk;

/*
 * Use these stubs to test your reader-writer locks.
 */

int rwtest(int nargs, char **args) {
	(void)nargs;
	(void)args;

	// kprintf_n("rwt1 unimplemented\n");
	success(TEST161_SUCCESS, SECRET, "rwt1");

	return 0;
}


static
void
readthread(void *junk1, unsigned long junk2)
{
	(void)junk1;
	(void)junk2;

	random_yielder(4);
	rwlock_acquire_read(rwlk);
	random_yielder(4);
	random_yielder(4);
	rwlock_release_read(rwlk);
	kprintf_n("read thread success\n");
	V(exitsem);
}

static
void
writethread(void *junk1, unsigned long junk2)
{
	(void)junk1;
	(void)junk2;

	random_yielder(4);
	rwlock_acquire_write(rwlk);
	random_yielder(4);
	random_yielder(4);
	rwlock_release_write(rwlk);
	kprintf_n("write thread success\n");
	V(exitsem);
}



int rwtest2(int nargs, char **args) {

	(void)nargs;
	(void)args;

	unsigned i = 0;
	int result;

	kprintf_n("Starting rwt2...\n");

	exitsem = sem_create("exitsem", 0);
	if (exitsem == NULL) {
		panic("rwt2: sem_create failed\n");
	}
	rwlk = rwlock_create("rwt2_rwlock");

	for(; i < 5; i++){
		result = thread_fork("rwt2", NULL, readthread, NULL, 0);
		if (result) {
			panic("rwt2: thread_fork failed\n");
		}
		result = thread_fork("rwt2", NULL, writethread, NULL, 0);
		if (result) {
			panic("rwt2: thread_fork failed\n");
		}
		result = thread_fork("rwt2", NULL, readthread, NULL, 0);
		if (result) {
			panic("rwt2: thread_fork failed\n");
		}
	}
	for(;i > 0; i--){
		P(exitsem);
		P(exitsem);
		P(exitsem);
	}

	sem_destroy(exitsem);
	exitsem = NULL;
	rwlock_destroy(rwlk);
	rwlk = NULL;

	kprintf_t("\n");
	success(TEST161_SUCCESS, SECRET, "rwt2");

	return 0;
}

int rwtest3(int nargs, char **args) {
	(void)nargs;
	(void)args;

	kprintf_n("Starting rwt3...\n");
	kprintf_n("(This test panics on success!)\n");

	struct rwlock *testrwlock = rwlock_create("testrwlock");
	if (testrwlock == NULL) {
		panic("rwt3: rwlock_create failed\n");
	}

	secprintf(SECRET, "Should panic...", "rwt3");
	rwlock_release_write(testrwlock);

	/* Should not get here on success. */

	success(TEST161_FAIL, SECRET, "rwt3");

	rwlock_destroy(testrwlock);
	testrwlock = NULL;

	return 0;
}

int rwtest4(int nargs, char **args) {
	(void)nargs;
	(void)args;

	kprintf_n("Starting rwt4...\n");
	kprintf_n("(This test panics on success!)\n");

	struct rwlock *testrwlock = rwlock_create("testrwlock");
	if (testrwlock == NULL) {
		panic("rwt4: rwlock_create failed\n");
	}

	secprintf(SECRET, "Should panic...", "rwt4");
	rwlock_release_read(testrwlock);

	/* Should not get here on success. */

	success(TEST161_FAIL, SECRET, "rwt4");

	rwlock_destroy(testrwlock);
	testrwlock = NULL;

	return 0;
}

int rwtest5(int nargs, char **args) {
	(void)nargs;
	(void)args;

	kprintf_n("Starting rwt5...\n");
	kprintf_n("(This test panics on success!)\n");

	struct rwlock *testrwlock = rwlock_create("testrwlock");
	if (testrwlock == NULL) {
		panic("rwt5: rwlock_create failed\n");
	}

	secprintf(SECRET, "Should panic...", "rwt5");
	rwlock_acquire_write(testrwlock);
	rwlock_destroy(testrwlock);

	/* Should not get here on success. */

	success(TEST161_FAIL, SECRET, "rwt5");

	rwlock_destroy(testrwlock);
	testrwlock = NULL;



	return 0;
}
