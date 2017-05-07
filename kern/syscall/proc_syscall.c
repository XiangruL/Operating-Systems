#include <types.h>
#include <limits.h>
#include <current.h>
#include <mips/trapframe.h>
#include <proc.h>
#include <addrspace.h>
#include <kern/errno.h>
#include <proc_syscall.h>
#include <vnode.h>
#include <copyinout.h>
#include <syscall.h>
#include <kern/wait.h>
#include <kern/fcntl.h>
#include <vfs.h>
#include <vm.h>
#include <bitmap.h>

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

    //copy parent's as to child's new addrspace
    struct addrspace * newas = NULL;
    int result = 0;
    result = as_copy(curproc->p_addrspace, &newas);//no need for as_create
    if(result){
        kfree(newtf);
        return ENOMEM;
    }

    //create new proc, set child's PPID to parent's PID
    struct proc * newproc = NULL;
    newproc = proc_create_runprogram("child");
    if(newproc == NULL){
        kfree(newtf);
        as_destroy(newas);//now same as kfree(newas)
        return ENOMEM;
    }

    newproc->p_PPID = curproc->p_PID;

    /* MOVE into thread_fork: Copy filetable from proc to newproc
	file handle is not null, increase reference num by 1 */
    for(int fd=0;fd<OPEN_MAX;fd++)
	{
        newproc->fileTable[fd] = curproc->fileTable[fd];
        if(newproc->fileTable[fd] != NULL){
			newproc->fileTable[fd]->refcount++;
		}
	}
    // thread_fork do the remaining work
    result = thread_fork("test_thread_fork", newproc, enter_forked_process, newtf, (unsigned long) newas);
    if(result) {
        return result;
    }


    *retval = newproc->p_PID;

    curproc->p_numthreads++;
    return 0;
}

int sys_waitpid(pid_t pid, int * status, int options, pid_t *retval) {

    if(status != NULL){
        if(status == (int*) 0x40000000 || status == (int*) 0x80000000){
            return EFAULT;
        }

        if(((int)status & 3) != 0) {
            return EFAULT;
        }
    }
	if(options != 0){
		return EINVAL;
	}
    if(pid < PID_MIN || pid >= PID_MAX){
        return ESRCH;
    }
	// int exitstatus;
	// int result;
	struct proc * p = procTable[pid];

	if(p == NULL){
		return ESRCH;
	}
	if(p->p_PPID != curproc->p_PID){
		// Current process can't wait on itself
		return ECHILD;
	}

	lock_acquire(p->p_lk);
	while(!p->p_exit) {
		cv_wait(p->p_cv, p->p_lk);
	}
	lock_release(p->p_lk);
    if(status != NULL){

        *status = p->p_exitcode;
    }
    as_destroy(p->p_addrspace);
    lock_destroy(p->p_lk);
    spinlock_cleanup(&p->p_lock);
    cv_destroy(p->p_cv);
    p->p_addrspace = NULL;
    p->p_thread = NULL;
    kfree(p->p_name);
    procTable[p->p_PID] = NULL;
    kfree(p);
    p = NULL;
	*retval = pid;
	return 0;
}

void sys__exit(int exitcode, bool trap_sig) {
    struct proc * p = curproc;
    KASSERT(procTable[p->p_PID] != NULL);

    lock_acquire(p->p_lk);
    p->p_exit = true;
    if(trap_sig){
        p->p_exitcode = _MKWAIT_SIG(exitcode);
    }else{
        p->p_exitcode = _MKWAIT_EXIT(exitcode);
    }
    for (int fd = 0; fd < OPEN_MAX; fd++) {
        sys_close(fd);
    }
    if(procTable[p->p_PPID] != NULL && procTable[p->p_PPID]->p_exit == false){
        cv_broadcast(p->p_cv, p->p_lk);
        lock_release(p->p_lk);
    }else{
        lock_release(p->p_lk);
        as_destroy(p->p_addrspace);
        lock_destroy(p->p_lk);
        spinlock_cleanup(&p->p_lock);
        cv_destroy(p->p_cv);
        p->p_addrspace = NULL;
        p->p_thread = NULL;
        kfree(p->p_name);
        procTable[curproc->p_PID] = NULL;
        kfree(p);
        p = NULL;
    }
    thread_exit();
    panic("sys__exit failed in proc_syscall");
}

/*
*execv:
*Copy arguments from user space into kernel buffer
*Open the executable, create a new address space and load the elf into it
*Copy the arguments from kernel buffer into user stack
*Return user mode using enter_new_process
*/
int
sys_execv(const char * program, char ** args){
    // one of the arguments is an invalid pointer
    if (program == NULL || args == NULL) {
	return EFAULT;
    }
    if ((int *)args == (int *)0x80000000 || (int *)args == (int *)0x40000000) {
	return EFAULT;
    }
    /***step1: copy argrs from user space into kernel buffer ***/

    // allocate memory for args named as copy

    char ** copy = (char **)kmalloc(sizeof(char *) * 4096);//4096 for exec badcall
    // copy args
    int result = 0;
    // count args num
    int args_count = 0;
    for (;; args_count++) {
        result = copyin((userptr_t)&(args[args_count]), &(copy[args_count]), sizeof(char *));
    	if (result) {
    		return result;
    	}
        if(copy[args_count] == NULL){
            break;
        }
    }

    // calculate padding size
    int total_len = (args_count + 1) * 4; // offset length
    // char * kargs[args_count];
    char ** kargs = (char **)kmalloc(sizeof(char *) * args_count);
    size_t actual_size;

    // int total_size = 0;
	int r_size = 0;
    for (int i = 0; i < args_count; i++) {

        if ((int *)(copy[i]) == (int *)0x80000000 || (int *)(copy[i]) == (int *)0x40000000) {
    	       return EFAULT;
        }
    	r_size = strlen(copy[i]) + 1;
    	kargs[i] = (char *)kmalloc (sizeof(char) * r_size);
    	result = copyinstr((userptr_t)copy[i], kargs[i], r_size, &actual_size);
    	if (result) {
            kprintf("Bad here \n");
    		return result;
    	}
    	// total_len = actual kargs[i] len + remainder
        // if(i < args_count - 1){
        //        total_len += strlen(kargs[i]) + 1 + (4 - (strlen(kargs[i]) + 1) % 4) % 4;
        int t = (strlen(kargs[i]) + 1)%4;
        if (t != 0) {
                t = 4 - t;
        }
        total_len = total_len + strlen(kargs[i]) + 1 + t;

    }
    // total args num is greater than ARG_MAX
    if (total_len > ARG_MAX) {
         return E2BIG;
    }
    // padding
    char *kargs_pad = (char *)kmalloc(sizeof(char) * total_len);
    int offset = (args_count + 1) * 4;

    for (int i = 0; i < args_count; i++) {
    	((char **)kargs_pad)[i] = (char *)offset;
    	strcpy(&(kargs_pad[offset]), kargs[i]);
    	// move to new offset
    	int temp = strlen(kargs[i]) + 1;
    	if (temp % 4 != 0) {
    		temp += 4 - temp % 4;
    	}
    	offset += temp;
    	//offset += strlen(kargs[i]) + 1 + (4 - (strlen(kargs[i]) + 1) % 4) % 4;
    }
    ((char **) kargs_pad)[args_count] = NULL;

    kfree(copy);
    // copy = NULL;
    for(int i = args_count - 1; i >= 0; i--){
        if(kargs[i] != NULL){
            kfree(kargs[i]);
        }
    }
    kfree(kargs);
    // copy program
    char progname[PATH_MAX];
    result = copyinstr((userptr_t)program, progname, PATH_MAX, &actual_size);
    if (result) {
        return result;
    }
    if(actual_size == 1){
        return EINVAL;
    }
    /** step 2 run_program**/
    struct addrspace *as;
    struct vnode *v;
    vaddr_t entry_point, stackptr;
    result = vfs_open(progname, O_RDONLY, 0, &v);
    if (result) {
        return result;
    }
    as_destroy(curproc->p_addrspace);
    as = as_create();
    if (as == NULL) {
        vfs_close(v);
        return ENOMEM;
    }
    // switch to it and activate it
    proc_setas(as);
    as_activate();
    // load the executable

    result = load_elf(v, &entry_point);
    if (result) {
        vfs_close(v);
        return result;
    }

    vfs_close(v);

    /** step 3 copy from kernel to userland **/
    result = as_define_stack(as, &stackptr);
    if (result) {
        return result;
    }
    stackptr -= total_len; // point to the bottom
    for (int i = 0; i < args_count; i++) {
        ((char **)kargs_pad)[i] += stackptr;
    }
    result = copyout(kargs_pad, (userptr_t)stackptr, total_len);
    if (result) {
        return result;
    }

    kfree(kargs_pad);
    kargs_pad = NULL;
    /** step 4 enter new process **/
    enter_new_process(args_count, (userptr_t)stackptr, NULL, stackptr, entry_point);
    panic("execv should not return\n");

    return 0;
}

int
sys_sbrk(int amount, vaddr_t * retval){
    *retval = -1;
    struct addrspace * as = curproc->p_addrspace;
    size_t heap_vbound = as->heap_vbound;
    if(amount % PAGE_SIZE != 0){
        kprintf("sbrk amount is not page aligned\n");
        return EINVAL;
    }

    int npages = amount / PAGE_SIZE;

    if((int)heap_vbound + npages < 0){
        kprintf("sbrk bound < 0\n");
        return EINVAL;
    }

    if(amount + heap_vbound * PAGE_SIZE >= USERSTACK - VM_STACKPAGES * PAGE_SIZE){
        kprintf("sbrk bound exceeds stackbase\n");
        return ENOMEM;
    }


    if(npages < 0 && as->pageTable != NULL){
        bool cm_lk_hold_before = false;
    	if(!spinlock_do_i_hold(&cm_lock)){
    		spinlock_acquire(&cm_lock);
    	}else{
    		cm_lk_hold_before = true;
    	}

        //destroy pte
        struct pageTableNode * pre = as->pageTable;
        struct pageTableNode * cur = pre->next;
        while(cur != NULL){
            wait_page_if_busy(cur->pt_pas / PAGE_SIZE);

            if(cur->pt_vas >= as->heap_vbase + (as->heap_vbound + npages) * PAGE_SIZE && cur->pt_vas < as->heap_vbase + as->heap_vbound * PAGE_SIZE){
                pre->next = cur->next;
                if(cur->pt_inDisk){
                    bitmap_unmark(vm_bitmap, cur->pt_bm_index);
                }else{
                    user_free_onepage(PADDR_TO_KVADDR(cur->pt_pas));
                }
        		kfree(cur);
                cur = pre->next;
            }else{
                pre = cur;
                cur = cur->next;
            }
        }
        // head
        pre = as->pageTable;
        cur = pre->next;
        wait_page_if_busy(pre->pt_pas / PAGE_SIZE);
        if(pre->pt_vas >= as->heap_vbase + (as->heap_vbound + npages) * PAGE_SIZE && pre->pt_vas < as->heap_vbase + as->heap_vbound * PAGE_SIZE){
            if(pre->pt_inDisk){
                bitmap_unmark(vm_bitmap, pre->pt_bm_index);
            }else{
                user_free_onepage(PADDR_TO_KVADDR(pre->pt_pas));
            }
            kfree(pre);
            as->pageTable = cur;
        }
        as_activate();

        if(!cm_lk_hold_before){
    		spinlock_release(&cm_lock);
    	}
    }
    *retval = as->heap_vbase + as->heap_vbound * PAGE_SIZE;
    as->heap_vbound += npages;

    return 0;
}
