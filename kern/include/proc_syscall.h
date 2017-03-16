#ifndef PROC_SYSCALL_H_
#define PROC_SYSCALL_H_
#include <types.h>
#include <limits.h>

int sys_getpid(pid_t * retval);
int sys_fork(struct trapframe * tf, int * retval);
int sys_waitpid(pid_t pid, int * status, int options, pid_t *retval);
void sys__exit(int exitcode, bool trap_sig);
int sys_execv(const char * program, char ** args);
#endif
