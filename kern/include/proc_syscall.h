#ifndef PROC_SYSCALL_H_
#define PROC_SYSCALL_H_
#include <types.h>
#include <limits.h>

int sys_getpid(pid_t * retval);
int sys_fork(struct trapframe * tf, int * retval);
void entrypoint(void * data1, unsigned long data2);

#endif
