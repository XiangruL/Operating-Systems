#include <types.h>
#include <limits.h>
#include <current.h>
#include <mips/trapframe.h>
#include <proc.h>
#include <addrspace.h>
#include <kern/errno.h>
#include <proc_syscall.h>

//#include <syscall.h>

pid_t
sys_getpid(void){
    return curproc->p_PID;
}

int sys_fork(struct trapframe * tf, int * err){
    //copy parent's tf to child's new trapframe
    struct trapframe * newtf;
    newtf = (struct trapframe *)kmalloc(sizeof(struct trapframe));
    if(newtf == NULL){
        *err = ENOMEM;
        return -1;//
    }
    memcpy(newtf, tf, sizeof(struct trapframe));

    //create new proc, set child's PPID to parent's PID
    struct proc * newproc;
    newproc = proc_create_runprogram("child");
    if(newproc == NULL){
        kfree(newtf);
       // as_destroy(newas);//now same as kfree(newas)
        *err = ENOMEM;
        return -1;
    }
    newproc->p_PPID = curproc->p_PID;

    //copy parent's as to child's new addrspace
    struct addrspace *newas = newproc->p_addrspace;
    int result = 0;
    result = as_copy(curproc->p_addrspace, &newas);
    if(newas == NULL){
        kfree(newtf);
        *err = ENOMEM;
        return -1;
    }

    /* copy filetable from proc to newproc
	file handle is not null, increase reference num by 1 */ 
    for(int fd=0;fd<OPEN_MAX;fd++)
	{
        newproc->fileTable[fd] = curproc->fileTable[fd];
        if(newproc->fileTable[fd] != NULL){
			newproc->fileTable[fd]->refcount++;
		}
	}

    // thread_fork do the remaining work
    result = thread_fork("test_thread_fork", newproc, entrypoint, newtf, (unsigned long) newas);//data1, data2
    if(result) {
        return result;
    }
    result = newproc->p_PID;
    newproc->p_cwd = curproc->p_cwd;
    curproc->p_numthreads++;
    return result;
}

void
entrypoint(void *data1, unsigned long data2){

    struct trapframe tf;
    // tf = (struct trapframe *)kmalloc(sizeof(struct trapframe));
    struct trapframe* newtf = (struct trapframe*) data1;
    struct addrspace* newas = (struct addrspace*) data2;
    tf.tf_v0 = 0;//retval
    tf.tf_a3 = 0;//return 0 = success
    tf.tf_epc += 4;
    /*
    *Upon syscall return, the PC stored in the trapframe (tf_epc) must
    *be incremented by 4 bytes, which is the length of one instruction.
    *Otherwise the return code restart at the same point.
    */
    memcpy(&tf, newtf, sizeof(struct trapframe));
    kfree(newtf);
    newtf = NULL;
    // proc_setas(newas);
    curproc->p_addrspace = newas;
    as_activate();

    mips_usermode(&tf);
    // struct trapframe tf;
    //
	// bzero(&tf, sizeof(tf));
    //
	// tf.tf_status = CST_IRQMASK | CST_IEp | CST_KUp;
	// tf.tf_epc = entry;
	// tf.tf_a0 = argc;
	// tf.tf_a1 = (vaddr_t)argv;
	// tf.tf_a2 = (vaddr_t)env;
	// tf.tf_sp = stack;
    //
	// mips_usermode(&tf);
};
