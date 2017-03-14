#include <types.h>
#include <limits.h>
#include <current.h>
#include <mips/trapframe.h>
#include <proc.h>
#include <addrspace.h>
#include <kern/errno.h>
#include <proc_syscall.h>
#include <vnode.h>
#include <syscall.h>

int
sys_getpid(pid_t * retval){
    *retval = curproc->p_PID;
    return 0;
}

int sys_fork(struct trapframe * tf, int * retval){
    //copy parent's tf to child's new trapframe
    struct trapframe * newtf = NULL;
    newtf = (struct trapframe *)kmalloc(sizeof(struct trapframe));
    if(newtf == NULL){
        return ENOMEM;
    }
    memcpy(newtf, tf, sizeof(struct trapframe));


    //create new proc, set child's PPID to parent's PID
    struct proc * cu = curproc;
    struct proc * newproc = NULL;
    newproc = proc_create_runprogram(cu->p_name);
    if(newproc == NULL){
        kfree(newtf);
        // as_destroy(newas);//now same as kfree(newas)
        return ENOMEM;
    }


    //copy parent's as to child's new addrspace
    int result = 0;
    as_copy(proc_getas(), &(newproc->p_addrspace));
    if(newproc->p_addrspace == NULL){
        kfree(newtf);
        newtf = NULL;
        proc_destroy(newproc);
        return ENOMEM;
    }

    newproc->p_PPID = cu->p_PID;
    /* copy filetable from proc to newproc
	file handle is not null, increase reference num by 1 */
    for(int fd=0;fd<OPEN_MAX;fd++)
	{
        newproc->fileTable[fd] = cu->fileTable[fd];
        if(newproc->fileTable[fd] != NULL){
			newproc->fileTable[fd]->refcount++;
		}
	}

    // thread_fork do the remaining work
    result = thread_fork("test_thread_fork", newproc, enter_forked_process, newtf,  (unsigned long)(newproc->p_addrspace));//data1, data2
    if(result) {
        kfree(newtf);
        newtf = NULL;
        proc_destroy(newproc);
        return result;
    }
    *retval = newproc->p_PID;//could panic if just return p_PID
    spinlock_acquire(&cu->p_lock);
	if (cu->p_cwd != NULL) {
		VOP_INCREF(cu->p_cwd);
		newproc->p_cwd = cu->p_cwd;
	}
	spinlock_release(&cu->p_lock);
    cu->p_numthreads++;
    return 0;
}
//
// void
// entrypoint(void *data1, unsigned long data2){
//
//     struct trapframe tf;
//     // tf = (struct trapframe *)kmalloc(sizeof(struct trapframe));
//     struct trapframe* newtf = (struct trapframe*) data1;
//     struct addrspace* newas = (struct addrspace*) data2;
//     tf.tf_v0 = 0;//retval
//     tf.tf_a3 = 0;//return 0 = success
//     tf.tf_epc += 4;
//     /*
//     *Upon syscall return, the PC stored in the trapframe (tf_epc) must
//     *be incremented by 4 bytes, which is the length of one instruction.
//     *Otherwise the return code restart at the same point.
//     */
//     memcpy(&tf, newtf, sizeof(struct trapframe));
//     kfree(newtf);
//     newtf = NULL;
//     // proc_setas(newas);
//     curproc->p_addrspace = newas;
//     as_activate();
//     mips_usermode(&tf);
// }
