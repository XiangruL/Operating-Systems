#ifndef PROC_SYSCALL_H_
#define PROC_SYSCALL_H_
#include <types.h>
#include <limits.h>

pid_t sys_getpid(void);
int sys_fork(struct trapframe * tf, int * err);
void entrypoint(void * data1, unsigned long data2);
#endif
