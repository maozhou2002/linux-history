/*
 * linux/arch/x86_64/kernel/sys_x86_64.c
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/stat.h>
#include <linux/mman.h>
#include <linux/file.h>
#include <linux/utsname.h>
#include <linux/personality.h>

#include <asm/uaccess.h>
#include <asm/ipc.h>
#include <asm/ia32.h>

/*
 * sys_pipe() is the normal C calling standard for creating
 * a pipe. It's not the way Unix traditionally does this, though.
 */
asmlinkage long sys_pipe(int *fildes)
{
	int fd[2];
	int error;

	error = do_pipe(fd);
	if (!error) {
		if (copy_to_user(fildes, fd, 2*sizeof(int)))
			error = -EFAULT;
	}
	return error;
}

long sys_mmap(unsigned long addr, unsigned long len, unsigned long prot, unsigned long flags,
	unsigned long fd, unsigned long off)
{
	long error;
	struct file * file;

	error = -EINVAL;
	if (off & ~PAGE_MASK)
		goto out;

	error = -EBADF;
	file = NULL;
	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	if (!(flags & MAP_ANONYMOUS)) {
		file = fget(fd);
		if (!file)
			goto out;
	}
	down_write(&current->mm->mmap_sem);
	error = do_mmap_pgoff(file, addr, len, prot, flags, off >> PAGE_SHIFT);
	up_write(&current->mm->mmap_sem);

	if (file)
		fput(file);
out:
	return error;
}

static void find_start_end(unsigned long flags, unsigned long *begin,
			   unsigned long *end)
{
#ifdef CONFIG_IA32_EMULATION
	if (test_thread_flag(TIF_IA32)) { 
		*begin = TASK_UNMAPPED_32;
		*end = IA32_PAGE_OFFSET; 
	} else 
#endif
	if (flags & MAP_32BIT) { 
		/* This is usually used needed to map code in small
		   model, so it needs to be in the first 31bit. Limit
		   it to that.  This means we need to move the
		   unmapped base down for this case. This can give
		   conflicts with the heap, but we assume that glibc
		   malloc knows how to fall back to mmap. Give it 1GB
		   of playground for now. -AK */ 
		*begin = 0x40000000; 
		*end = 0x80000000;		
	} else { 
		*begin = TASK_UNMAPPED_64; 
		*end = TASK_SIZE; 
		}
} 

unsigned long
arch_get_unmapped_area(struct file *filp, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long start_addr;
	unsigned long begin, end;
	
	find_start_end(flags, &begin, &end); 

	if (len > end)
		return -ENOMEM;

	if (addr) {
		addr = PAGE_ALIGN(addr);
		vma = find_vma(mm, addr);
		if (end - len >= addr &&
		    (!vma || addr + len <= vma->vm_start))
			return addr;
	}
	addr = mm->free_area_cache;
	if (addr < begin) 
		addr = begin; 
	start_addr = addr;

full_search:
	for (vma = find_vma(mm, addr); ; vma = vma->vm_next) {
		/* At this point:  (!vma || addr < vma->vm_end). */
		if (end - len < addr) {
			/*
			 * Start a new search - just in case we missed
			 * some holes.
			 */
			if (start_addr != begin) {
				start_addr = addr = begin;
				goto full_search;
			}
			return -ENOMEM;
		}
		if (!vma || addr + len <= vma->vm_start) {
			/*
			 * Remember the place where we stopped the search:
			 */
			mm->free_area_cache = addr + len;
			return addr;
		}
		addr = vma->vm_end;
	}
}

asmlinkage long sys_uname(struct new_utsname * name)
{
	int err;
	down_read(&uts_sem);
	err = copy_to_user(name, &system_utsname, sizeof (*name));
	up_read(&uts_sem);
	if (personality(current->personality) == PER_LINUX32) 
		err |= copy_to_user(&name->machine, "i686", 5); 		
	return err ? -EFAULT : 0;
}

asmlinkage long wrap_sys_shmat(int shmid, char *shmaddr, int shmflg)
{
	unsigned long raddr;
	return sys_shmat(shmid,shmaddr,shmflg,&raddr) ?: (long)raddr;
} 

asmlinkage long sys_time64(long * tloc)
{
	struct timeval now; 
	int i; 

	do_gettimeofday(&now);
	i = now.tv_sec;
	if (tloc) {
		if (put_user(i,tloc))
			i = -EFAULT;
	}
	return i;
}
