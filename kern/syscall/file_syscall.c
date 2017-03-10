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
#include <kern/stat.h>
#include <kern/seek.h>


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
	if(buf == NULL) {
		return EFAULT;
	}
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

int
sys___getcwd(char * buffer, size_t len, int * retval){

	if(buffer == NULL || strlen(buffer) != len) {
		return EFAULT;
	}
	int result = 0;
	char * buf = NULL;
	buf = kmalloc(sizeof(*buffer) * len);
	if(buf == NULL) {
		return EFAULT;
	}
	struct iovec iov;
	struct uio u;
	// struct iovec *iov, struct uio *u,
	// 	  void *kbuf, size_t len, off_t pos, enum uio_rw rw
	uio_kinit(&iov, &u, buf, len-1, 0, UIO_READ);
	result = vfs_getcwd(&u);
	if(result){
		kfree(buf);
		return result;
	};
	buf[len-1 - u.uio_resid] = '\0';
	result = copyout((const void *)buf, (userptr_t)buffer,len);
	if(result){
		kfree(buf);
		return EINVAL;
	}
	*retval = strlen(buf);
	kfree(buf);
	return 0;
}

off_t
sys_lseek(int fd, off_t pos, int whence, int64_t * retval){
	//EBADF, EINVAL, ESPIPE
	if(fd < 0 || fd >= OPEN_MAX || curthread->fileTable[fd] == NULL){
		return EBADF;
	}
	// if(fd >=0 || fd <=2){
	// 	return ESPIPE;
	// }
	int err;
	struct stat * statbuf;
	statbuf = (struct stat *)kmalloc(sizeof(struct stat));
	off_t size, tmppos = 0;

	lock_acquire(curthread->fileTable[fd]->lk);
	if(!VOP_ISSEEKABLE(curthread->fileTable[fd]->vn)){
		lock_release(curthread->fileTable[fd]->lk);
		return ESPIPE;
	}
	err = VOP_STAT(curthread->fileTable[fd]->vn, statbuf);
	if(err){
		lock_release(curthread->fileTable[fd]->lk);
		return err;
	}
	size = statbuf->st_size;
	if(whence == SEEK_SET){
		tmppos = pos;
	}else if(whence == SEEK_CUR){
		tmppos = curthread->fileTable[fd]->offset + pos;
	}else if(whence == SEEK_END){
		tmppos = size + pos;
	}else{
		lock_release(curthread->fileTable[fd]->lk);
		return EINVAL;
	}
	if(tmppos < 0){
		lock_release(curthread->fileTable[fd]->lk);
		return EINVAL;
	}
	// if(whence != SEEK_SET || whence != SEEK_CUR || whence != SEEK_END){
	// 	lock
	// 	return EINVAL;
	// }
	*retval = (int64_t)tmppos;
	curthread->fileTable[fd]->offset = tmppos;
	lock_release(curthread->fileTable[fd]->lk);
	return 0;
}

int
sys_dup2(int oldfd, int newfd, int * retval){
	if(oldfd < 0 || oldfd >= OPEN_MAX || curthread->fileTable[oldfd] == NULL){
		return EBADF;
	}

	if(newfd < 0 || newfd >= OPEN_MAX){
		return EBADF;
	}
	lock_acquire(curthread->fileTable[oldfd]->lk);
	if(curthread->fileTable[newfd] == NULL){
		curthread->fileTable[newfd] = (struct fileHandle *)kmalloc(sizeof(struct fileHandle));
		// KASSERT(fh != NULL);
		if(curthread->fileTable[newfd] == NULL){
			return ENFILE;
		}
		curthread->fileTable[newfd] = curthread->fileTable[oldfd];
	}else{
		sys_close(newfd);
		if(curthread->fileTable[newfd] == NULL){
			curthread->fileTable[newfd] = (struct fileHandle *)kmalloc(sizeof(struct fileHandle));
			// KASSERT(fh != NULL);
			if(curthread->fileTable[newfd] == NULL){
				return ENFILE;
			}
			curthread->fileTable[newfd] = curthread->fileTable[oldfd];
		}else{
			curthread->fileTable[newfd] = curthread->fileTable[oldfd];
		}

	}
	*retval = newfd;
	lock_release(curthread->fileTable[oldfd]->lk);
	return 0;
}
