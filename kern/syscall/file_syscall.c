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

int
sys_write(int fd, const void *buffer, int len)
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
