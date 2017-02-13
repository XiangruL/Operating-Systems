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

int rwtest2(int nargs, char **args) {
	(void)nargs;
	(void)args;

	// kprintf_n("rwt2 unimplemented\n");
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
