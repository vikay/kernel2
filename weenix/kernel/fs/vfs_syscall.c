/*
 *  FILE: vfs_syscall.c
 *  AUTH: mcc | jal
 *  DESC:
 *  DATE: Wed Apr  8 02:46:19 1998
 *  $Id: vfs_syscall.c,v 1.1 2012/10/10 20:06:46 william Exp $
 */

#include "kernel.h"
#include "errno.h"
#include "globals.h"
#include "fs/vfs.h"
#include "fs/file.h"
#include "fs/vnode.h"
#include "fs/vfs_syscall.h"
#include "fs/open.h"
#include "fs/fcntl.h"
#include "fs/lseek.h"
#include "mm/kmalloc.h"
#include "util/string.h"
#include "util/printf.h"
#include "fs/stat.h"
#include "util/debug.h"

/* To read a file:
 *      o fget(fd) (check)
 *      o call its virtual read f_op (check)
 *      o update f_pos (check)
 *      o fput() it (check)
 *      o return the number of bytes read, or an error (check)
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF (check)
 *        fd is not a valid file descriptor or is not open for reading.
 *      o EISDIR (check)
 *        fd refers to a directory.
 *
 * In all cases, be sure you do not leak file refcounts by returning before (check)
 * you fput() a file that you fget()'ed.
 */
int
do_read(int fd, void *buf, size_t nbytes)
{
	int result,status;	
	file_t *f;
	if(!curproc->p_files[fd])	
		return -EBADF;

	f= fget(fd);	
	if(f)
	{
		if(S_ISDIR(f->f_vnode->vn_mode))
		{
			fput(f);			
			return -EISDIR;
		}		
		if(f->f_mode & FMODE_READ)
		{		
			result = f->f_vnode->vn_ops->read(f->f_vnode, f->f_pos, buf, nbytes);
			if(result>=0)		
			{
				status = do_lseek(fd,result,SEEK_CUR); 
				if(status<0)
				{
					fput(f);			
					return status;
				}
			}
			fput(f);
			return result;
		}
		else
		{
			fput(f);
			return -EBADF;
		}
	}
	else
		return -EBADF;
        /*NOT_YET_IMPLEMENTED("VFS: do_read");
        return -1;*/
}

/* Very similar to do_read.  Check f_mode to be sure the file is writable.  If (check all)
 * f_mode & FMODE_APPEND, do_lseek() to the end of the file, call the write
 * f_op, and fput the file.  As always, be mindful of refcount leaks.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF (check)
 *        fd is not a valid file descriptor or is not open for writing.
 */
int
do_write(int fd, const void *buf, size_t nbytes)
{
	int result,status;	
	file_t *f;
	if(!curproc->p_files[fd])	
		return -EBADF;
	f= fget(fd);	
	if(f)
	{
		if(f->f_mode & FMODE_WRITE)
		{		
			if(f->f_mode & FMODE_APPEND)
			{			
				status = do_lseek(fd,0,SEEK_END); 
				if(status<0)
				{
					fput(f);			
					return status;
				}
			}
			result = f->f_vnode->vn_ops->write(f->f_vnode, f->f_pos, buf, nbytes);
			if(result>=0)		
			{
				status = do_lseek(fd,result,SEEK_CUR); 
				if(status<0)
				{
					fput(f);			
					return status;
				}
			}
			fput(f);
			return result;
		}
		else
		{
			fput(f);
			return -EBADF;
		}
	}
	else
		return -EBADF;
/*
        NOT_YET_IMPLEMENTED("VFS: do_write");
        return -1;*/
}

/*
 * Zero curproc->p_files[fd], and fput() the file. Return 0 on success (check)
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF (check)
 *        fd isn't a valid open file descriptor.
 */
int
do_close(int fd)
{
	file_t *f;	
	if(!curproc->p_files[fd])	
		return -EBADF; 
	f= fget(fd);	
	if(f)
	{
		curproc->p_files[fd] = 0;
		fput(f);
		fput(f);	
		return 0;
	}
	else
		return -EBADF;
	/*NOT_YET_IMPLEMENTED("VFS: do_close");
        return -1; */
}

/* To dup a file:
 *      o fget(fd) to up fd's refcount (check)
 *      o get_empty_fd() (check)
 *      o point the new fd to the same file_t* as the given fd (check)
 *      o return the new file descriptor (check)
 *
 * Don't fput() the fd unless something goes wrong.  Since we are creating
 * another reference to the file_t*, we want to up the refcount.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF (check)
 *        fd isn't an open file descriptor.
 *      o EMFILE (check)
 *        The process already has the maximum number of file descriptors open
 *        and tried to open a new one.
 */
int
do_dup(int fd)
{
	file_t *f;
	int new_fd;
		
	if(!curproc->p_files[fd])	
		return -EBADF;
	f = fget(fd);
	if(f)
	{
		new_fd = get_empty_fd(curproc);
		if(new_fd!=EMFILE)
		{
			curproc->p_files[new_fd] = f;
			return new_fd;
		}
		else
		{
			fput(f);
			return -EMFILE;
		}
	}
	else
		return -EBADF;  		      
	
	/*NOT_YET_IMPLEMENTED("VFS: do_dup");
        return -1;*/
}

/* Same as do_dup, but insted of using get_empty_fd() to get the new fd, (check)
 * they give it to us in 'nfd'.  If nfd is in use (and not the same as ofd)
 * do_close() it first.  Then return the new file descriptor.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF (check)
 *        ofd isn't an open file descriptor, or nfd is out of the allowed
 *        range for file descriptors.
 */
int
do_dup2(int ofd, int nfd)
{
	file_t *f;
	int new_fd;
		
	if(!curproc->p_files[ofd] || (nfd < 0 || nfd >= NFILES))	
		return -EBADF;
	f = fget(ofd);
	if(f)
	{
		if(curproc->p_files[nfd] && ofd!=nfd)
		{
			if(!(do_close(nfd)))
			{
				fput(f);				
				return -EBADF;
			}
		}
		new_fd = nfd;
		curproc->p_files[new_fd] = f;
		return new_fd;
	}
	else
		return -EBADF;  		      

        /*NOT_YET_IMPLEMENTED("VFS: do_dup2");
        return -1;*/
}

/*
 * This routine creates a special file of the type specified by 'mode' at
 * the location specified by 'path'. 'mode' should be one of S_IFCHR or
 * S_IFBLK (you might note that mknod(2) normally allows one to create
 * regular files as well-- for simplicity this is not the case in Weenix).
 * 'devid', as you might expect, is the device identifier of the device
 * that the new special file should represent.
 *
 * You might use a combination of dir_namev, lookup, and the fs-specific
 * mknod (that is, the containing directory's 'mknod' vnode operation).
 * Return the result of the fs-specific mknod, or an error.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EINVAL
 *        mode requested creation of something other than a device special
 *        file.
 *      o EEXIST
 *        path already exists.
 *      o ENOENT
 *        A directory component in path does not exist.
 *      o ENOTDIR
 *        A component used as a directory in path is not, in fact, a directory.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 */
/*
 * Author : Vishwanath Eswarakrishnan 
*/

int
do_mknod(const char *path, int mode, unsigned devid)
{
      /* NOT_YET_IMPLEMENTED("VFS: do_mknod");*/
	vnode_t *nvnode;
	vnode_t *altvnode;
	
	const char *name;
	size_t namelen;
	

	if(S_ISCHR(mode) || S_ISBLK(mode))
	{
		
		int retDir=dir_namev(path,&namelen,&name,NULL,&nvnode);
		if(retDir==-ENOTDIR)
			return -ENOENT;	
		
		if(strlen(name)>NAME_LEN) 
		{
			vput(nvnode);
			return -ENAMETOOLONG;
		}
		
		int lookupResult=lookup(nvnode,name,namelen,&altvnode);
		if(lookupResult==-ENOTDIR)	
		{
			vput(nvnode);
		/*	vput(altvnode);		*/
			return -ENOTDIR;	
		}
		if(lookupResult==0)
		{
			vput(nvnode);
		/*	vput(altvnode);		*/
			return -EEXIST;
		}	

		KASSERT(NULL!=nvnode->vn_ops->mknod);
		int toRet=nvnode->vn_ops->mknod(nvnode,name,namelen,mode,devid);
		/* MKMOD(2)	*/
		return toRet;				
	}
	else
	{
		return -EINVAL;
	}
		
		
        return -1;
}

/* Use dir_namev() to find the vnode of the dir we want to make the new
 * directory in.  Then use lookup() to make sure it doesn't already exist.
 * Finally call the dir's mkdir vn_ops. Return what it returns.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EEXIST
 *        path already exists.
 *      o ENOENT
 *        A directory component in path does not exist.
 *      o ENOTDIR
 *        A component used as a directory in path is not, in fact, a directory.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 */
int
do_mkdir(const char *path)
{
        NOT_YET_IMPLEMENTED("VFS: do_mkdir");
        return -1;
}

/* Use dir_namev() to find the vnode of the directory containing the dir to be
 * removed. Then call the containing dir's rmdir v_op.  The rmdir v_op will
 * return an error if the dir to be removed does not exist or is not empty, so
 * you don't need to worry about that here. Return the value of the v_op,
 * or an error.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EINVAL
 *        path has "." as its final component.
 *      o ENOTEMPTY
 *        path has ".." as its final component.
 *      o ENOENT
 *        A directory component in path does not exist.
 *      o ENOTDIR
 *        A component used as a directory in path is not, in fact, a directory.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 */
int
do_rmdir(const char *path)
{
        NOT_YET_IMPLEMENTED("VFS: do_rmdir");
        return -1;
}

/*
 * Same as do_rmdir, but for files.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EISDIR
 *        path refers to a directory.
 *      o ENOENT
 *        A component in path does not exist.
 *      o ENOTDIR
 *        A component used as a directory in path is not, in fact, a directory.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 */
int
do_unlink(const char *path)
{
        NOT_YET_IMPLEMENTED("VFS: do_unlink");
        return -1;
}

/* To link:
 *      o open_namev(from)
 *      o dir_namev(to)
 *      o call the destination dir's (to) link vn_ops.
 *      o return the result of link, or an error
 *
 * Remember to vput the vnodes returned from open_namev and dir_namev.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EEXIST
 *        to already exists.
 *      o ENOENT
 *        A directory component in from or to does not exist.
 *      o ENOTDIR
 *        A component used as a directory in from or to is not, in fact, a
 *        directory.
 *      o ENAMETOOLONG
 *        A component of from or to was too long.
 */
int
do_link(const char *from, const char *to)
{
        NOT_YET_IMPLEMENTED("VFS: do_link");
        return -1;
}

/*      o link newname to oldname
 *      o unlink oldname
 *      o return the value of unlink, or an error
 *
 * Note that this does not provide the same behavior as the
 * Linux system call (if unlink fails then two links to the
 * file could exist).
 */
int
do_rename(const char *oldname, const char *newname)
{
        NOT_YET_IMPLEMENTED("VFS: do_rename");
        return -1;
}

/* Make the named directory the current process's cwd (current working
 * directory).  Don't forget to down the refcount to the old cwd (vput()) and
 * up the refcount to the new cwd (open_namev() or vget()). Return 0 on
 * success.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o ENOENT
 *        path does not exist.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 *      o ENOTDIR
 *        A component of path is not a directory.
 */
int
do_chdir(const char *path)
{
       /* NOT_YET_IMPLEMENTED("VFS: do_chdir");*/
	vnode_t *dirname;
	vnode_t *altdirname;
 	
 	vnode_t *olddir=curproc->p_cwd;
	
	const char *name;
	size_t namelen;
		
	int nameret=dir_namev(path,&namelen,&name,NULL,&dirname);
	if(nameret==-ENOTDIR)
		return -ENOENT;		
	if(strlen(name)>NAME_LEN) 
	{
		vput(dirname);
		return -ENAMETOOLONG;
	}

	int lookupResult=lookup(dirname,name,namelen,&altdirname);
	if(lookupResult==-ENOENT)
	{
		vput(dirname);
		return -ENOENT;
	}	
	if(lookupResult==-ENOTDIR)	
	{
		vput(dirname);
		/*vput(altdirname);		*/
		return -ENOTDIR;	
	}

	curproc->p_cwd=altdirname;
	vput(olddir);
	vput(dirname);
        return 0;
}

/* Call the readdir f_op on the given fd, filling in the given dirent_t*.
 * If the readdir f_op is successful, it will return a positive value which
 * is the number of bytes copied to the dirent_t.  You need to increment the
 * file_t's f_pos by this amount.  As always, be aware of refcounts, check
 * the return value of the fget and the virtual function, and be sure the
 * virtual function exists (is not null) before calling it.
 *
 * Return either 0 or sizeof(dirent_t), or -errno.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        Invalid file descriptor fd.
 *      o ENOTDIR
 *        File descriptor does not refer to a directory.
 */
int
do_getdent(int fd, struct dirent *dirp)
{
        /*NOT_YET_IMPLEMENTED("VFS: do_getdent");*/
	file_t *f;
 	f=fget(fd);
	
	if(f && curproc->p_files[fd])
	{	
		if(S_ISDIR(f->f_vnode->vn_mode))
			return -ENOTDIR;			
		
		int retRead=f->f_vnode->vn_ops->readdir(f->f_vnode,f->f_pos,dirp);	
		if(retRead)
		{
			if(retRead==f->f_vnode->vn_len)
				return 0;

			f->f_pos=f->f_pos+retRead; 		
		}
	}
	else
	{
		return -EBADF;
	}
	return sizeof(dirent_t);
}

/*
 * Modify f_pos according to offset and whence.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        fd is not an open file descriptor.
 *      o EINVAL
 *        whence is not one of SEEK_SET, SEEK_CUR, SEEK_END; or the resulting
 *        file offset would be negative.
 */
int
do_lseek(int fd, int offset, int whence)
{
        /*NOT_YET_IMPLEMENTED("VFS: do_lseek");*/
	file_t *f=NULL;
	f=curproc->p_files[fd];
	
	if(fd<0 || f==NULL || S_ISCHR(f->f_vnode->vn_mode) || S_ISBLK(f->f_vnode->vn_mode))	
		return -EBADF;
	
	if(whence!=SEEK_SET && whence!=SEEK_CUR && whence!=SEEK_END)
		return -EINVAL;
	int newpos=0;
	switch(whence)
	{
		case SEEK_SET:
				f->f_pos=offset;
				newpos=f->f_pos;
				break;
		case SEEK_END:
				f->f_pos=f->f_vnode->vn_len+offset;
				newpos=f->f_pos;
				break;
		default:
				f->f_pos+=offset;
				newpos=f->f_pos;						
	}
	
	if(newpos<0)
		return -EINVAL;

        return newpos;
}

/*
 * Find the vnode associated with the path, and call the stat() vnode operation.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o ENOENT
 *        A component of path does not exist.
 *      o ENOTDIR
 *        A component of the path prefix of path is not a directory.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 */
int
do_stat(const char *path, struct stat *buf)
{
       /* NOT_YET_IMPLEMENTED("VFS: do_stat");*/
	vnode_t *nvnode;
	vnode_t *altvnode;
	
	const char *name;
	size_t namelen;
	
	int retDir=dir_namev(path,&namelen,&name,NULL,&nvnode);
	if(retDir==-ENOTDIR)
		return -ENOENT;

	if(strlen(name)>NAME_LEN) 
	{
		vput(nvnode);
		return -ENAMETOOLONG;
	}

	vput(nvnode);
	int lookupResult=lookup(nvnode,name,namelen,&altvnode);
	if(lookupResult==-ENOTDIR)	
	{		
		/*vput(altvnode);		*/
		return -ENOTDIR;	
	}
	if(lookupResult==-ENOENT) 
	{
		/*vput(altvnode);	*/
		return -ENOENT;
	}
	if(lookupResult==0)
	{
		vput(altvnode);			
	}	

	KASSERT(nvnode->vn_ops->stat);
	int statret=nvnode->vn_ops->stat(altvnode, buf);
		/*HANLDE 2 ?*/
	
        return statret;
}

#ifdef __MOUNTING__
/*
 * Implementing this function is not required and strongly discouraged unless
 * you are absolutely sure your Weenix is perfect.
 *
 * This is the syscall entry point into vfs for mounting. You will need to
 * create the fs_t struct and populate its fs_dev and fs_type fields before
 * calling vfs's mountfunc(). mountfunc() will use the fields you populated
 * in order to determine which underlying filesystem's mount function should
 * be run, then it will finish setting up the fs_t struct. At this point you
 * have a fully functioning file system, however it is not mounted on the
 * virtual file system, you will need to call vfs_mount to do this.
 *
 * There are lots of things which can go wrong here. Make sure you have good
 * error handling. Remember the fs_dev and fs_type buffers have limited size
 * so you should not write arbitrary length strings to them.
 */
int
do_mount(const char *source, const char *target, const char *type)
{
        NOT_YET_IMPLEMENTED("MOUNTING: do_mount");
        return -EINVAL;
}

/*
 * Implementing this function is not required and strongly discouraged unless
 * you are absolutley sure your Weenix is perfect.
 *
 * This function delegates all of the real work to vfs_umount. You should not worry
 * about freeing the fs_t struct here, that is done in vfs_umount. All this function
 * does is figure out which file system to pass to vfs_umount and do good error
 * checking.
 */
int
do_umount(const char *target)
{
        NOT_YET_IMPLEMENTED("MOUNTING: do_umount");
        return -EINVAL;
}
#endif
