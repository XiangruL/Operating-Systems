#ifndef FILE_SYSCALL_H_
#define FILE_SYSCALL_H_
#include <types.h>
#include <limits.h>
struct fileHandle{
	struct vnode *vn;
	off_t offset;
	struct lock *lk;
	int flags;
	int refcount;
    //mode_t //ignore
};
int fileHandle_init(char * filename, struct vnode *vn, struct fileHandle ** fh, off_t offset, int flags, int refcount);
int fileTable_init(void);
int sys_open(const char * filename, int flags, int * retval);
int sys_write(int fd, const void *, size_t len, int * retval);//int -> size_t, types.h
int sys_read(int fd, void * buf, size_t len, int * retval);
int sys_close(int fd);
int sys_chdir(const char * pathname);
int sys___getcwd(char * buffer, size_t len, int * retval);
off_t sys_lseek(int fd, off_t pos, int whence, int64_t * retval);
int sys_dup2(int oldfd, int newfd, int * retval);
#endif
