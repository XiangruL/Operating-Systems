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
fileHandle_init(char * filename, struct vnode *vn, struct fileHandle * fh, off_t offset, int flags, int refcount)
{
	fh = (struct fileHandle *)kmalloc(sizeof(struct fileHandle));
	if(fh == NULL){
		return ENFILE;
	}
	fh->vn = vn;
	fh->flags = flags;
	fh->offset = offset;
	fh->refcount = refcount;
	fh->lk = lock_create(filename);
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
	console = kstrdup("con:");
	// int err = 0;

	if(vfs_open(console, O_RDONLY, 0, &v0)) {
		// kfree(v0);
		// kfree(v1);
		// kfree(v2);
		vfs_close(v0);
		return EINVAL;
	}
	if(fileHandle_init(console, v0, curthread->fileTable[0], 0, O_RDONLY, 1)){
		// kfree(v0);
		// kfree(v1);
		// kfree(v2);
		vfs_close(v0);
		return ENFILE;
	}

	if(vfs_open(console, O_WRONLY, 0, &v1)) {
		// kfree(v0);
		// kfree(v1);
		// kfree(v2);
		lock_destroy(curthread->fileTable[0]->lk);
		kfree(curthread->fileTable[0]);
		vfs_close(v0);
		vfs_close(v1);
		return EINVAL;
	}
	if(fileHandle_init(console, v1, curthread->fileTable[1], 0, O_WRONLY, 1)){
		// kfree(v0);
		// kfree(v1);
		// kfree(v2);
		lock_destroy(curthread->fileTable[0]->lk);
		kfree(curthread->fileTable[0]);
		vfs_close(v0);
		vfs_close(v1);
		return ENFILE;
	}

	if(vfs_open(console, O_WRONLY, 0, &v2)) {
		// kfree(v0);
		// kfree(v1);
		// kfree(v2);
		lock_destroy(curthread->fileTable[0]->lk);
		kfree(curthread->fileTable[0]);
		lock_destroy(curthread->fileTable[1]->lk);
		kfree(curthread->fileTable[1]);
		vfs_close(v0);
		vfs_close(v1);
		vfs_close(v2);
		return EINVAL;
	}
	if(fileHandle_init(console, v2, curthread->fileTable[2], 0, O_WRONLY, 1)){
		// kfree(v0);
		// kfree(v1);
		// kfree(v2);
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
	err = copyinstr((const_userptr_t)filename,name, PATH_MAX, &len);
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
		return EMFILE;
	}
	if(vfs_open(name, flags, 0, &v)){
		vfs_close(v);
		kfree(name);
		return EINVAL;
	}
	if(fileHandle_init(name, v,curthread->fileTable[index], 0, flags, 1)){
		vfs_close(v);
		kfree(name);
		curthread->fileTable[index] = NULL;
		return ENFILE;
	}
	*retval = index;
	kfree(name);
	return 0;
}
int
sys_write(int fd, const void *buffer, size_t len)
{
	struct iovec iov;
	struct uio u;
	int result;

	struct vnode *v;
	char console[5] = "con:";
	(void)fd;

	// kprintf("BEGIN TO WRITE!!!");

	/* Initialize uio */
	// iov.iov_ubase = (userptr_t)buffer;
	// iov.iov_len = len;
	// u.uio_iov = &iov;
	// u.uio_iovcnt = 1;
	// u.uio_resid = len;
	// u.uio_segflg = UIO_USERSPACE;
	// u.uio_rw = UIO_WRITE;
	// u.uio_space = curproc->p_addrspace;

	// uio_kinit(&iov, &u, (userptr_t)buffer, len, 0, UIO_WRITE);
	uio_kinit(&iov, &u, (userptr_t)buffer, len, 0, UIO_WRITE);
	u.uio_segflg = UIO_USERSPACE;
    u.uio_space = curproc->p_addrspace;

	result = vfs_open(console, O_WRONLY, 0, &v);
	if (result){
		vfs_close(v);
		return result;
	}

	result = VOP_WRITE(v, &u);
	if (result){
		vfs_close(v);
		return result;
	}

	vfs_close(v);
	return 0;

}
