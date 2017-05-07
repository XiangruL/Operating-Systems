This *README.md* includes my brief design idea and working sequence that I still remember. I try my best to record all these in case I forget this memorable experience. 
# ASST1: Synchronization  


### Locks

  - Explore and understand relvant code: wchan.h, thread.c, ...
  - Read existing semaphore implementation
  - Use KASSERT()
  - Lock implementation: use blocking lock, *spinlock*, to implement non-blocking lock
  - Read test file ``` synchtest.c```
  - Debug: try to use os161-gdb, it's a little different from original gdb. Remember go to *kernel* folder before ``` os161-gdb kernel```, then type ``` target remote unix:.sockets/gdb``` to establish a connection between kernel and debuger
 
### Condition Variables

- Producer & Consumer User Case:

```c
void producer() {
    item = produce();
    lock_acquire(buff_lock);
    while (buffer.size() == MAX) {
        cv_wait(cv_prod, buff_lock);
    }
    buffer.add(item);
    cv_signal(cv_cons, buff_lock);
    lock_release(buff_lock);

}
```
```c
void consumer() {
    lock_acquire(buff_lock);
    while (buffer.size() == 0) {
        cv_wait(cv_cons, buff_lock);
    }
    item = buffer.get();
    consume(item);
    cv_signal(cv_proc, buff_lock);
    lock_release(buff_lock);
}
```

### Reader-Writer Locks

- Liveness Property: there shouldn't be any starvation for ***write***
- My implementation (I use CV and lock, but it also can be implemented by spinlock and wchan): 
  - All ***read*** can be executed, in another word can hold rwlock, simultaneously if there is no ***write*** waiting in queue or holding rwlock
  - After a ***write*** request entering lock waiting queue, new ***read*** request should wait(this ensure Liveness Property)
  - An example: some requests come in as this sequence, R1-R2-R3-W1-R4-R5-W2-...
    1. At first R1 finds outno one hold rwlock, so R1 acquires and holds it
    2. Then R2 and R3 come in, when they try to acquire rwlock they know rwlock is in READ mode, so they don't need rwlock to be released. 
    3. W1 comes in and is notified rwlock is in WRITE mode, so it should wait for someone releasing this lock.
    4. Now R4 comes in, and it spots that there is a ***write*** in waiting queue, so it should wait until this write be executed successfully.  
    5. Now R2 and R1 quit, they don't release rwlock because R3 is still running, then R3 wakes all ***read and write*** waiting in queue and quit.
    6. ...

- Should write your own rwtest.c to test your rwlock( need `bmake && bmake install` in `/os161` folder to MAKE new test`)

### Synchronization Problems

- Whale Mating Problem: it should be straightforward and intuitive if you choose the right primitives
- Four-way stops Problem: think out your own rule, and implement by some primitives, pay attention to deadlock situation

# ASST2: System Calls

### Before Writing Code

- Read os161 syscall's man page
- Read `menu.c` & `runprogram.c`: get to know how kernel load and run a user-level program
- Read `trap.c` & `syscall.c`: understand workflow from user program calling a function to kernel choosing corresponding syscall (Now you should know how to let user be able to call a syscall if you've already implemented it)
- Design file table and process table structure as best as you can, even it's not so hard to change or add/remove attributes later
- Figure out where to init your file table and process table, and whether your file table and process table need synchronization primitives (need `fileTable_init` to init console)
- Think out what methods your syscalls rely on (most of them were implemented and offerd to you,  `proc.h, current.h, vfs.h, vnode.h, uio.h, copyinout.h, types.h, limits.h, stat.h, seek.h, errno.h, fcntl.h and etc` for file syscalls, and all those mentioned before plus `trapframe.h, addrspace.h, vm.h and etc` for proc syscalls.
- Read os161 syscall's man page again, now you should know clearly what you should write
- P.S. Kindly remind: mark everything you're not sure in this stage, it will save you lots of time when you debug ASST 3

### File Syscalls

```c
int sys_open(const char * filename, int flags, int * retval);
int sys_write(int fd, const void *, size_t len, int * retval);
int sys_read(int fd, void * buf, size_t len, int * retval);
int sys_close(int fd);
int sys_chdir(const char * pathname);
int sys___getcwd(char * buffer, size_t len, int * retval);
int sys_lseek(int fd, off_t pos, int whence, int64_t * retval);
int sys_dup2(int oldfd, int newfd, int * retval);
```
- clumsy sys_write: silly version, but should be enough to test your syscall logic, if everything is okay you should pass consoletest; when you have your own file table, you can write your file syscalls
- sys_open: notice all pointers passed from syscall.c to your syscall are user pointer, so you need user copyin/out pass value between user and kernel pointer
- sys_write/read: since `vnode` doesn't care about offset, you should maintain `offset` variable in each of your fileTable_entry, and you may need a lock to protect your `offset` and other variables
- sys_lseek: everything you need is in `stat.h` and `seek.h`
- sys___getcwd: may need `bzero()` or add a `\0` manually, nothing else special
- sys_chdir: `vfs.h`
- sys_dup2/close: need `refcount` to count number of pointers point to this `fileHandle` and if `newfd` in dup2 is not empty, you should close it first, then allocate a piece of new memory to dup.

### Process Syscalls

```c
int sys_getpid(pid_t * retval);
int sys_fork(struct trapframe * tf, int * retval);
int sys_waitpid(pid_t pid, int * status, int options, pid_t *retval);
void sys__exit(int exitcode, bool trap_sig);
int sys_execv(const char * program, char ** args);
```
ASST3 needs another syscall:
```c
int sys_sbrk(int amount, vaddr_t * retval);
```

- sys_getpid: intuitive
- sys_waitpid/__exit: who should destroy all child's structures, whose lock and cv should parent and child use (in my implementation, child structure can be destroyed by either waitpid or exit func based on whether child exits before parent wait)
- sys_fork: copy trapframe, copy addrspace, allocate a new proc structrue for child then call `threadfork()`. Since all tests related to fork are hard to debug  and error messages are not so useful, so the first step is convincing yourself there is no error, like NULL pointer usage, in all of your syscall functions.
- enter_forked_process: refer to `syscall.c`, must do this `tf->tf_epc += 4`, otherwise, child will start at where parent stopped, which is sys_fork. So child will restart to do exactly same thing over and over again. And you should refer to `trap.c` for using `mips_usermode()`
- sys_execv: the most complicated part from ASST 1, tips from [Jinhao's bolg (it's an archive)](http://web.archive.org/web/20130924003646/http://jhshi.me/2012/03/11/os161-execv-system-call/)
  - Copy arguments from user space into kernel buffer
  - Open the executable, create a new address space and load the elf into it
  - Copy the arguments from kernel buffer into user stack
  - Return user mode using enter_new_process
- sys_execv: first step mentioned above is tricky:
  - Since this userptr array `**args` stores in userspace, we cannot check whether `args[i] == NULL` straightly, so we must use `copyin` first then check whether our kernel ptr points to `NULL`. This means we cannot know length of `args` before copy, so we need a pre-defined `ARG_LENGTH_MAX`, it should be big enough to pass bigexec test and small enough to let kernel not encounter `ENOMEM` error
  - Another not so beautiful solution to avoid `ENOMEM` error: use `strlen(copy[i])` to get length, but this `copy[i]` pointer, comes from first `copyin` step, still point to userspace, so we shouldn't touch it when we write syscall, but it can work without generating an error......
  - After we get all seperate args, we should pad all of them to one single area
- sys_execv: last three steps in `sys_execv` are similar to `runprogram.c`

# ASST3: Virtual Memory

### Before Writing Code


### Coremap


### Pagetables


### Swapping




