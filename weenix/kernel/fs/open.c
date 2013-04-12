/*
 *  FILE: open.c
 *  AUTH: mcc | jal
 *  DESC:
 *  DATE: Mon Apr  6 19:27:49 1998
 */

#include "globals.h"
#include "errno.h"
#include "fs/fcntl.h"
#include "util/string.h"
#include "util/printf.h"
#include "fs/vfs.h"
#include "fs/vnode.h"
#include "fs/file.h"
#include "fs/vfs_syscall.h"
#include "fs/open.h"
#include "fs/stat.h"
#include "util/debug.h"

/* find empty index in p->p_files[] */
int
get_empty_fd(proc_t *p)
{
        int fd;

        for (fd = 0; fd < NFILES; fd++) {
                if (!p->p_files[fd])
                        return fd;
        }

        dbg(DBG_ERROR | DBG_VFS, "ERROR: get_empty_fd: out of file descriptors "
            "for pid %d\n", curproc->p_pid);
        return -EMFILE;
}

/*
 * There a number of steps to opening a file:
 *      1. Get the next empty file descriptor.(check)
 *      2. Call fget to get a fresh file_t.(check)
 *      3. Save the file_t in curproc's file descriptor table.(check)
 *      4. Set file_t->f_mode to OR of FMODE_(READ|WRITE|APPEND) based on (check)(not sure if should consider other combinations)
 *         oflags, which can be O_RDONLY, O_WRONLY or O_RDWR, possibly OR'd with
 *         O_APPEND.
 *      5. Use open_namev() to get the vnode for the file_t.(check)
 *      6. Fill in the fields of the file_t.(check)
 *      7. Return new fd.(check)
 *
 * If anything goes wrong at any point (specifically if the call to open_namev (check)
 * fails), be sure to remove the fd from curproc, fput the file_t and return an
 * error.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EINVAL (check)(not sure if should consider other combinations)
 *        oflags is not valid.
 *      o EMFILE (check)
 *        The process already has the maximum number of files open.
 *      o ENOMEM (check)
 *        Insufficient kernel memory was available.
 *      o ENAMETOOLONG
 *        A component of filename was too long.
 *      o ENOENT (check) open_namev should return that which do_open returns
 *        O_CREAT is not set and the named file does not exist.  Or, a
 *        directory component in pathname does not exist.
 *      o EISDIR (check)
 *        pathname refers to a directory and the access requested involved
 *        writing (that is, O_WRONLY or O_RDWR is set).
 *      o ENXIO (check)
 *        pathname refers to a device special file and no corresponding device
 *        exists.
 */

int
do_open(const char *filename, int oflags)
{
	int status;	
	file_t *f;	
	int fd = get_empty_fd(curproc);
	if(fd!=EMFILE)
	{
		/* can fail if memory not allocated */
		f = fget(-1); 
		if(f==NULL)
			return -ENOMEM;
		curproc->p_files[fd] = f;

		if(oflags & O_RDONLY)		
			f->f_mode = FMODE_READ;
		else if(oflags & O_WRONLY)		
			f->f_mode = FMODE_WRITE;
		else if(oflags & O_RDWR)		
			f->f_mode = FMODE_READ | FMODE_WRITE;
		else if(oflags & (O_WRONLY|O_APPEND))		
			f->f_mode = FMODE_WRITE|FMODE_APPEND;
		else if(oflags & (O_RDWR|O_APPEND))		
			f->f_mode = FMODE_READ|FMODE_WRITE|FMODE_APPEND;
		else
			return -EINVAL; /* not sure if other combinations should be handled */
		if(S_ISDIR(f->f_vnode->vn_mode) && (O_WRONLY | O_RDWR))
		{
			curproc->p_files[fd] = NULL;
			fput(f);			
			return -EISDIR;
		}
		if( S_ISCHR(f->f_vnode->vn_mode) || S_ISBLK(f->f_vnode->vn_mode))
		{
			curproc->p_files[fd] = NULL;
			fput(f);			
			return -ENXIO;
		}
		
		

		/* returns int */
		status = open_namev(filename, oflags, (vnode_t **)(&(f->f_vnode)), NULL); 
		if(status<0)
		{
			curproc->p_files[fd] = NULL;
			fput(f);			
			return status;
		}
		/* can return error codes*/
		status = do_lseek(fd,0,0); /* 3rd argument SEEK_SET = 0 is not defined, not sure to do this as lseek.h not included */
		if(status<0)
		{
			curproc->p_files[fd] = NULL;
			fput(f);			
			return status;
		}
		return fd;
	}
	else
		return -EMFILE;
	/*NOT_YET_IMPLEMENTED("VFS: do_open");
        return -1;*/
}
