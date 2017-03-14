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
