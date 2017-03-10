#include <types.h>
#include <syscall.h>
#include <kern/unistd.h>
#include <lib.h>
#include <uio.h>
#include <current.h>
#include <proc.h>
#include <vnode.h>
#include <vfs.h>
#include <kern/fcntl.h>
#include <file_syscall.h>
#include <synch.h>
#include <kern/errno.h>
#include <copyinout.h>


int
fileHandle_init(char * filename, struct vnode *vn, struct fileHandle ** fh, off_t offset, int flags, int refcount)
{
	fh[0] = (struct fileHandle *)kmalloc(sizeof(struct fileHandle));
	// KASSERT(fh != NULL);
	if(fh[0] == NULL){
		return ENFILE;
	}
	fh[0]->vn = vn;
	fh[0]->flags = flags;
	fh[0]->offset = offset;
	fh[0]->refcount = refcount;
	fh[0]->lk = lock_create(filename);
	return 0;
}

int
fileTable_init(void)
{
	struct vnode *v0;//stdin
	struct vnode *v1;//stdout
	struct vnode *v2;//stderr
	// char console[5] = "con:";
	char * console = NULL;
	char * console1 = NULL;
	char * console2 = NULL;
	console = kstrdup("con:");
	console1 = kstrdup("con:");
	console2 = kstrdup("con:");
	// int err = 0;

	if(vfs_open(console, O_RDONLY, 0, &v0)) {
		kfree(console);
		kfree(console1);
		kfree(console2);
		vfs_close(v0);
		return EINVAL;
	}
	if(fileHandle_init(console, v0, &curthread->fileTable[0], 0, O_RDONLY, 1)){
		kfree(console);
		kfree(console1);
		kfree(console2);
		vfs_close(v0);
		return ENFILE;
	}

	if(vfs_open(console1, O_WRONLY, 0, &v1)) {
		// kfree(v0);
		// kfree(v1);
		// kfree(v2);
		kfree(console);
		kfree(console1);
		kfree(console2);
		lock_destroy(curthread->fileTable[0]->lk);
		kfree(curthread->fileTable[0]);
		vfs_close(v0);
		vfs_close(v1);
		return EINVAL;
	}
	if(fileHandle_init(console1, v1, &curthread->fileTable[1], 0, O_WRONLY, 1)){
		// kfree(v0);
		// kfree(v1);
		// kfree(v2);
		kfree(console);
		kfree(console1);
		kfree(console2);
		lock_destroy(curthread->fileTable[0]->lk);
		kfree(curthread->fileTable[0]);
		vfs_close(v0);
		vfs_close(v1);
		return ENFILE;
	}

	if(vfs_open(console2, O_WRONLY, 0, &v2)) {
		// kfree(v0);
		// kfree(v1);
		// kfree(v2);
		kfree(console);
		kfree(console1);
		kfree(console2);
		lock_destroy(curthread->fileTable[0]->lk);
		kfree(curthread->fileTable[0]);
		lock_destroy(curthread->fileTable[1]->lk);
		kfree(curthread->fileTable[1]);
		vfs_close(v0);
		vfs_close(v1);
		vfs_close(v2);
		return EINVAL;
	}

	if(fileHandle_init(console2, v2, &curthread->fileTable[2], 0, O_WRONLY, 1)){
		// kfree(v0);
		// kfree(v1);
		// kfree(v2);
		kfree(console);
		kfree(console1);
		kfree(console2);
		lock_destroy(curthread->fileTable[0]->lk);
		kfree(curthread->fileTable[0]);
		lock_destroy(curthread->fileTable[1]->lk);
		kfree(curthread->fileTable[1]);
		vfs_close(v0);
		vfs_close(v1);
		vfs_close(v2);
		return ENFILE;
	}
	return 0;
}
int
sys_open(const char * filename, int flags, int * retval)
{
	int index = 3, err = 0;
	size_t len;
	// off_t off = 0;
	struct vnode * v;
	char * name = (char *)kmalloc(sizeof(char) * PATH_MAX);
	err = copyinstr((const_userptr_t)filename, name, PATH_MAX, &len);
	if(err){
		kfree(name);
		return err;
	}
	while(index < OPEN_MAX){
		if(curthread->fileTable[index] == NULL){
			break;
		}else{
			index++;
		}
	}
	if(index == OPEN_MAX){
		kfree(name);
		return EMFILE;
	}
	if(vfs_open(name, flags, 0, &v)){
		kfree(name);
		return EINVAL;
	}
	if(fileHandle_init(name, v, &curthread->fileTable[index], 0, flags, 1)){
		vfs_close(v);
		return EINVAL;
	}
	fileHandle_init(name, v,&curthread->fileTable[index], 0, flags, 1);		
	*retval = index;
	kfree(name);
	return 0;
}

int
sys_write(int fd, const void *buffer, size_t len, int * retval)
{
	//EBADF
	if(fd < 0 || fd >= OPEN_MAX){
		return EBADF;
	}
	if(curthread->fileTable[fd] == NULL || curthread->fileTable[fd]->flags == O_RDONLY){
		return EBADF;
	}
	int result = 0;
	void * buf = NULL;
	buf = kmalloc(sizeof(*buffer) * len);
	result = copyin((const_userptr_t)buffer,buf,len);
	if(result){
		kfree(buf);
		return EINVAL;
	}
	struct iovec iov;
	struct uio u;
	lock_acquire(curthread->fileTable[fd]->lk);
	// struct iovec *iov, struct uio *u,
	// 	  void *kbuf, size_t len, off_t pos, enum uio_rw rw
	uio_kinit(&iov, &u, buf, len, curthread->fileTable[fd]->offset, UIO_WRITE);
	// u.uio_space = curproc->p_addrspace;
	result = VOP_WRITE(curthread->fileTable[fd]->vn, &u);
	if(result){
		kfree(buf);
		lock_release(curthread->fileTable[fd]->lk);
		return result;
	}
	kfree(buf);
	curthread->fileTable[fd]->offset = u.uio_offset;
	*retval = len - u.uio_resid;
	lock_release(curthread->fileTable[fd]->lk);
	return 0;
}

int
sys_read(int fd, void * buffer, size_t len, int * retval){
	//EBADF
	if(fd < 0 || fd >= OPEN_MAX){
		return EBADF;
	}
	if(curthread->fileTable[fd] == NULL || curthread->fileTable[fd]->flags == O_WRONLY){
		return EBADF;
	}
	int result = 0;
	void * buf = NULL;
	buf = kmalloc(sizeof(*buffer) * len);
	// result = copyin((const_userptr_t)buffer,buf,len);
	// if(result){
	// 	kfree(buf);
	// 	return EINVAL;
	// }
	struct iovec iov;
	struct uio u;
	// struct iovec *iov, struct uio *u,
	// 	  void *kbuf, size_t len, off_t pos, enum uio_rw rw
	lock_acquire(curthread->fileTable[fd]->lk);
	uio_kinit(&iov, &u, buf, len, curthread->fileTable[fd]->offset, UIO_READ);
	// u.uio_space = curproc->p_addrspace;
	result = VOP_READ(curthread->fileTable[fd]->vn, &u);
	if(result){
		kfree(buf);
		lock_release(curthread->fileTable[fd]->lk);
		return result;
	}
	result = copyout((const void *)buf, (userptr_t)buffer,len);
	if(result){
		kfree(buf);
		return EINVAL;
	}
	kfree(buf);
	curthread->fileTable[fd]->offset = u.uio_offset;
	*retval = len - u.uio_resid;
	lock_release(curthread->fileTable[fd]->lk);
	return 0;
}

int
sys_close(int fd){
	//EBADF
	if(fd < 0 || fd >= OPEN_MAX || curthread->fileTable[fd] == NULL){
		return EBADF;
	}
	lock_acquire(curthread->fileTable[fd]->lk);
	if(curthread->fileTable[fd]->refcount > 1){
		curthread->fileTable[fd]->refcount--;
		lock_release(curthread->fileTable[fd]->lk);
	}else{
		vfs_close(curthread->fileTable[fd]->vn);
		lock_release(curthread->fileTable[fd]->lk);
		kfree(curthread->fileTable[fd]);
		curthread->fileTable[fd] = NULL;
	}
	return 0;
}
