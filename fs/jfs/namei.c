/*
 *
 *   Copyright (c) International Business Machines  Corp., 2000
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or 
 *   (at your option) any later version.
 * 
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software 
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Module: jfs/namei.c
 *
 */

/*
 * Change History :
 *
 */

#include <linux/fs.h>
#include <linux/locks.h>
#include "jfs_incore.h"
#include "jfs_inode.h"
#include "jfs_dinode.h"
#include "jfs_dmap.h"
#include "jfs_unicode.h"
#include "jfs_metapage.h"
#include "jfs_debug.h"

extern struct inode_operations jfs_file_inode_operations;
extern struct inode_operations jfs_symlink_inode_operations;
extern struct file_operations jfs_file_operations;
extern struct address_space_operations jfs_aops;

extern int jfs_fsync(struct file *, struct dentry *, int);
extern void jfs_truncate_nolock(struct inode *, loff_t);

/*
 * forward references
 */
struct inode_operations jfs_dir_inode_operations;
struct file_operations jfs_dir_operations;

s64 commitZeroLink(tid_t, struct inode *);

/*
 * NAME:	jfs_create(dip, dentry, mode)
 *
 * FUNCTION:	create a regular file in the parent directory <dip>
 *		with name = <from dentry> and mode = <mode>
 *
 * PARAMETER:	dip 	- parent directory vnode
 *		dentry	- dentry of new file
 *		mode	- create mode (rwxrwxrwx).
 *
 * RETURN:	Errors from subroutines
 *
 */
int jfs_create(struct inode *dip, struct dentry *dentry, int mode)
{
	int rc = 0;
	tid_t tid;		/* transaction id */
	struct inode *ip = NULL;	/* child directory inode */
	ino_t ino;
	component_t dname;	/* child directory name */
	btstack_t btstack;
	struct inode *iplist[2];
	tblock_t *tblk;

	jFYI(1, ("jfs_create: dip:0x%p name:%s\n", dip, dentry->d_name.name));

	IWRITE_LOCK(dip);

	/*
	 * search parent directory for entry/freespace
	 * (dtSearch() returns parent directory page pinned)
	 */
	if ((rc = get_UCSname(&dname, dentry, JFS_SBI(dip->i_sb)->nls_tab)))
		goto out1;

	/*
	 * Either iAlloc() or txBegin() may block.  Deadlock can occur if we
	 * block there while holding dtree page, so we allocate the inode &
	 * begin the transaction before we search the directory.
	 */
	ip = ialloc(dip, mode);
	if (ip == NULL) {
		rc = ENOSPC;
		goto out2;
	}

	tid = txBegin(dip->i_sb, 0);

	if ((rc = dtSearch(dip, &dname, &ino, &btstack, JFS_CREATE))) {
		jERROR(1, ("jfs_create: dtSearch returned %d\n", rc));
		ip->i_nlink = 0;
		iput(ip);
		txEnd(tid);
		goto out2;
	}

	tblk = tid_to_tblock(tid);
	tblk->xflag |= COMMIT_CREATE;
	tblk->ip = ip;

	iplist[0] = dip;
	iplist[1] = ip;

	/*
	 * initialize the child XAD tree root in-line in inode
	 */
	xtInitRoot(tid, ip);

	/*
	 * create entry in parent directory for child directory
	 * (dtInsert() releases parent directory page)
	 */
	ino = ip->i_ino;
	if ((rc = dtInsert(tid, dip, &dname, &ino, &btstack))) {
		jERROR(1, ("jfs_create: dtInsert returned %d\n", rc));
		/* discard new inode */
		ip->i_nlink = 0;
		iput(ip);

		if (rc == EIO)
			txAbort(tid, 1);	/* Marks Filesystem dirty */
		else
			txAbort(tid, 0);	/* Filesystem full */
		txEnd(tid);
		goto out2;
	}

	ip->i_op = &jfs_file_inode_operations;
	ip->i_fop = &jfs_file_operations;
	ip->i_mapping->a_ops = &jfs_aops;

	insert_inode_hash(ip);
	mark_inode_dirty(ip);
	d_instantiate(dentry, ip);

	dip->i_version = ++event;
	dip->i_ctime = dip->i_mtime = CURRENT_TIME;

	mark_inode_dirty(dip);

	rc = txCommit(tid, 2, &iplist[0], 0);
	txEnd(tid);

      out2:
	free_UCSname(&dname);

      out1:

	IWRITE_UNLOCK(dip);
	jFYI(1, ("jfs_create: rc:%d\n", -rc));
	return -rc;
}


/*
 * NAME:	jfs_mkdir(dip, dentry, mode)
 *
 * FUNCTION:	create a child directory in the parent directory <dip>
 *		with name = <from dentry> and mode = <mode>
 *
 * PARAMETER:	dip 	- parent directory vnode
 *		dentry	- dentry of child directory
 *		mode	- create mode (rwxrwxrwx).
 *
 * RETURN:	Errors from subroutines
 *
 * note:
 * EACCESS: user needs search+write permission on the parent directory
 */
int jfs_mkdir(struct inode *dip, struct dentry *dentry, int mode)
{
	int rc = 0;
	tid_t tid;		/* transaction id */
	struct inode *ip = NULL;	/* child directory inode */
	ino_t ino;
	component_t dname;	/* child directory name */
	btstack_t btstack;
	struct inode *iplist[2];
	tblock_t *tblk;

	jFYI(1, ("jfs_mkdir: dip:0x%p name:%s\n", dip, dentry->d_name.name));

	IWRITE_LOCK(dip);

	/* link count overflow on parent directory ? */
	if (dip->i_nlink == JFS_LINK_MAX) {
		rc = EMLINK;
		goto out1;
	}

	/*
	 * search parent directory for entry/freespace
	 * (dtSearch() returns parent directory page pinned)
	 */
	if ((rc = get_UCSname(&dname, dentry, JFS_SBI(dip->i_sb)->nls_tab)))
		goto out1;

	/*
	 * Either iAlloc() or txBegin() may block.  Deadlock can occur if we
	 * block there while holding dtree page, so we allocate the inode &
	 * begin the transaction before we search the directory.
	 */
	ip = ialloc(dip, S_IFDIR | mode);
	if (ip == NULL) {
		rc = ENOSPC;
		goto out2;
	}

	tid = txBegin(dip->i_sb, 0);

	if ((rc = dtSearch(dip, &dname, &ino, &btstack, JFS_CREATE))) {
		jERROR(1, ("jfs_mkdir: dtSearch returned %d\n", rc));
		ip->i_nlink = 0;
		iput(ip);
		txEnd(tid);
		goto out2;
	}

	tblk = tid_to_tblock(tid);
	tblk->xflag |= COMMIT_CREATE;
	tblk->ip = ip;

	iplist[0] = dip;
	iplist[1] = ip;

	/*
	 * initialize the child directory in-line in inode
	 */
	dtInitRoot(tid, ip, dip->i_ino);

	/*
	 * create entry in parent directory for child directory
	 * (dtInsert() releases parent directory page)
	 */
	ino = ip->i_ino;
	if ((rc = dtInsert(tid, dip, &dname, &ino, &btstack))) {
		jERROR(1, ("jfs_mkdir: dtInsert returned %d\n", rc));
		/* discard new directory inode */
		ip->i_nlink = 0;
		iput(ip);

		if (rc == EIO)
			txAbort(tid, 1);	/* Marks Filesystem dirty */
		else
			txAbort(tid, 0);	/* Filesystem full */
		txEnd(tid);
		goto out2;
	}

	ip->i_nlink = 2;	/* for '.' */
	ip->i_op = &jfs_dir_inode_operations;
	ip->i_fop = &jfs_dir_operations;
	ip->i_mapping->a_ops = &jfs_aops;
	ip->i_mapping->gfp_mask = GFP_NOFS;

	insert_inode_hash(ip);
	mark_inode_dirty(ip);
	d_instantiate(dentry, ip);

	/* update parent directory inode */
	dip->i_nlink++;		/* for '..' from child directory */
	dip->i_version = ++event;
	dip->i_ctime = dip->i_mtime = CURRENT_TIME;
	mark_inode_dirty(dip);

	rc = txCommit(tid, 2, &iplist[0], 0);
	txEnd(tid);

      out2:
	free_UCSname(&dname);

      out1:

	IWRITE_UNLOCK(dip);

	jFYI(1, ("jfs_mkdir: rc:%d\n", -rc));
	return -rc;
}

/*
 * NAME:	jfs_rmdir(dip, dentry)
 *
 * FUNCTION:	remove a link to child directory
 *
 * PARAMETER:	dip 	- parent inode
 *		dentry	- child directory dentry
 *
 * RETURN:	EINVAL	- if name is . or ..
 *		EINVAL  - if . or .. exist but are invalid.
 *		errors from subroutines
 *
 * note:
 * if other threads have the directory open when the last link 
 * is removed, the "." and ".." entries, if present, are removed before 
 * rmdir() returns and no new entries may be created in the directory, 
 * but the directory is not removed until the last reference to 
 * the directory is released (cf.unlink() of regular file).
 */
int jfs_rmdir(struct inode *dip, struct dentry *dentry)
{
	int rc;
	tid_t tid;		/* transaction id */
	struct inode *ip = dentry->d_inode;
	ino_t ino;
	component_t dname;
	struct inode *iplist[2];
	tblock_t *tblk;

	jFYI(1, ("jfs_rmdir: dip:0x%p name:%s\n", dip, dentry->d_name.name));

	IWRITE_LOCK_LIST(2, dip, ip);

	/* directory must be empty to be removed */
	if (!dtEmpty(ip)) {
		IWRITE_UNLOCK(ip);
		IWRITE_UNLOCK(dip);
		rc = ENOTEMPTY;
		goto out;
	}

	if ((rc = get_UCSname(&dname, dentry, JFS_SBI(dip->i_sb)->nls_tab))) {
		IWRITE_UNLOCK(ip);
		IWRITE_UNLOCK(dip);
		goto out;
	}

	tid = txBegin(dip->i_sb, 0);

	iplist[0] = dip;
	iplist[1] = ip;

	tblk = tid_to_tblock(tid);
	tblk->xflag |= COMMIT_DELETE;
	tblk->ip = ip;

	/*
	 * delete the entry of target directory from parent directory
	 */
	ino = ip->i_ino;
	if ((rc = dtDelete(tid, dip, &dname, &ino, JFS_REMOVE))) {
		jERROR(1, ("jfs_rmdir: dtDelete returned %d\n", rc));
		if (rc == EIO)
			txAbort(tid, 1);
		txEnd(tid);

		IWRITE_UNLOCK(ip);
		IWRITE_UNLOCK(dip);

		goto out2;
	}

	/* update parent directory's link count corresponding
	 * to ".." entry of the target directory deleted
	 */
	dip->i_nlink--;
	dip->i_ctime = dip->i_mtime = CURRENT_TIME;
	dip->i_version = ++event;
	mark_inode_dirty(dip);

	/*
	 * OS/2 could have created EA and/or ACL
	 */
	/* free EA from both persistent and working map */
	if (JFS_IP(ip)->ea.flag & DXD_EXTENT) {
		/* free EA pages */
		txEA(tid, ip, &JFS_IP(ip)->ea, NULL);
	}
	JFS_IP(ip)->ea.flag = 0;

	/* free ACL from both persistent and working map */
	if (JFS_IP(ip)->acl.flag & DXD_EXTENT) {
		/* free ACL pages */
		txEA(tid, ip, &JFS_IP(ip)->acl, NULL);
	}
	JFS_IP(ip)->acl.flag = 0;

	/* mark the target directory as deleted */
	ip->i_nlink = 0;
	mark_inode_dirty(ip);

	rc = txCommit(tid, 2, &iplist[0], 0);

	txEnd(tid);

	IWRITE_UNLOCK(ip);

	/*
	 * Truncating the directory index table is not guaranteed.  It
	 * may need to be done iteratively
	 */
	if (test_cflag(COMMIT_Stale, dip)) {
		if (dip->i_size > 1)
			jfs_truncate_nolock(dip, 0);

		clear_cflag(COMMIT_Stale, dip);
	}

	IWRITE_UNLOCK(dip);

	d_delete(dentry);

      out2:
	free_UCSname(&dname);

      out:
	jFYI(1, ("jfs_rmdir: rc:%d\n", rc));
	return -rc;
}

/*
 * NAME:	jfs_unlink(dip, dentry)
 *
 * FUNCTION:	remove a link to object <vp> named by <name> 
 *		from parent directory <dvp>
 *
 * PARAMETER:	dip 	- inode of parent directory
 *		dentry 	- dentry of object to be removed
 *
 * RETURN:	errors from subroutines
 *
 * note:
 * temporary file: if one or more processes have the file open
 * when the last link is removed, the link will be removed before
 * unlink() returns, but the removal of the file contents will be
 * postponed until all references to the files are closed.
 *
 * JFS does NOT support unlink() on directories.
 *
 */
int jfs_unlink(struct inode *dip, struct dentry *dentry)
{
	int rc;
	tid_t tid;		/* transaction id */
	struct inode *ip = dentry->d_inode;
	ino_t ino;
	component_t dname;	/* object name */
	struct inode *iplist[2];
	tblock_t *tblk;
	s64 new_size = 0;
	int commit_flag;

	jFYI(1, ("jfs_unlink: dip:0x%p name:%s\n", dip, dentry->d_name.name));

	if ((rc = get_UCSname(&dname, dentry, JFS_SBI(dip->i_sb)->nls_tab)))
		goto out;

	IWRITE_LOCK_LIST(2, ip, dip);

	tid = txBegin(dip->i_sb, 0);

	iplist[0] = dip;
	iplist[1] = ip;

	/*
	 * delete the entry of target file from parent directory
	 */
	ino = ip->i_ino;
	if ((rc = dtDelete(tid, dip, &dname, &ino, JFS_REMOVE))) {
		jERROR(1, ("jfs_unlink: dtDelete returned %d\n", rc));
		if (rc == EIO)
			txAbort(tid, 1);	/* Marks FS Dirty */
		txEnd(tid);
		IWRITE_UNLOCK(ip);
		IWRITE_UNLOCK(dip);
		goto out1;
	}

	ASSERT(ip->i_nlink);

	ip->i_ctime = dip->i_ctime = dip->i_mtime = CURRENT_TIME;
	dip->i_version = ++event;
	mark_inode_dirty(dip);

	/* update target's inode */
	ip->i_nlink--;
	mark_inode_dirty(ip);

	/*
	 *      commit zero link count object
	 */
	if (ip->i_nlink == 0) {
		assert(!test_cflag(COMMIT_Nolink, ip));
		/* free block resources */
		if ((new_size = commitZeroLink(tid, ip)) < 0) {
			txAbort(tid, 1);	/* Marks FS Dirty */
			txEnd(tid);
			IWRITE_UNLOCK(ip);
			IWRITE_UNLOCK(dip);
			rc = -new_size;		/* We return -rc */
			goto out1;
		}
		tblk = tid_to_tblock(tid);
		tblk->xflag |= COMMIT_DELETE;
		tblk->ip = ip;
	}

	/*
	 * Incomplete truncate of file data can
	 * result in timing problems unless we synchronously commit the
	 * transaction.
	 */
	if (new_size)
		commit_flag = COMMIT_SYNC;
	else
		commit_flag = 0;

	/*
	 * If xtTruncate was incomplete, commit synchronously to avoid
	 * timing complications
	 */
	rc = txCommit(tid, 2, &iplist[0], commit_flag);

	txEnd(tid);

	while (new_size && (rc == 0)) {
		tid = txBegin(dip->i_sb, 0);
		new_size = xtTruncate_pmap(tid, ip, new_size);
		if (new_size < 0) {
			txAbort(tid, 1);	/* Marks FS Dirty */
			rc = -new_size;		/* We return -rc */
		} else
			rc = txCommit(tid, 2, &iplist[0], COMMIT_SYNC);
		txEnd(tid);
	}

	if (!test_cflag(COMMIT_Holdlock, ip))
		IWRITE_UNLOCK(ip);

	/*
	 * Truncating the directory index table is not guaranteed.  It
	 * may need to be done iteratively
	 */
	if (test_cflag(COMMIT_Stale, dip)) {
		if (dip->i_size > 1)
			jfs_truncate_nolock(dip, 0);

		clear_cflag(COMMIT_Stale, dip);
	}

	IWRITE_UNLOCK(dip);

	d_delete(dentry);

      out1:
	free_UCSname(&dname);
      out:
	jFYI(1, ("jfs_unlink: rc:%d\n", -rc));
	return -rc;
}

/*
 * NAME:	commitZeroLink()
 *
 * FUNCTION:    for non-directory, called by jfs_remove(),
 *		truncate a regular file, directory or symbolic
 *		link to zero length. return 0 if type is not 
 *		one of these.
 *
 *		if the file is currently associated with a VM segment
 *		only permanent disk and inode map resources are freed,
 *		and neither the inode nor indirect blocks are modified
 *		so that the resources can be later freed in the work
 *		map by ctrunc1.
 *		if there is no VM segment on entry, the resources are
 *		freed in both work and permanent map.
 *		(? for temporary file - memory object is cached even 
 *		after no reference:
 *		reference count > 0 -   )
 *
 * PARAMETERS:	cd	- pointer to commit data structure.
 *			  current inode is the one to truncate.
 *
 * RETURN :	Errors from subroutines
 */
s64 commitZeroLink(tid_t tid, struct inode *ip)
{
	int filetype, committype;
	tblock_t *tblk;

	jFYI(1, ("commitZeroLink: tid = %d, ip = 0x%p\n", tid, ip));

	filetype = ip->i_mode & S_IFMT;
	switch (filetype) {
	case S_IFREG:
		break;
	case S_IFLNK:
		/* fast symbolic link */
		if (ip->i_size <= 256) {
			ip->i_size = 0;
			return 0;
		}
		break;
	default:
		assert(filetype != S_IFDIR);
		return 0;
	}

#ifdef _STILL_TO_PORT
	/*
	 *      free from block allocation map:
	 *
	 * if there is no cache control element associated with 
	 * the file, free resources in both persistent and work map;
	 * otherwise just persistent map. 
	 */
	if (ip->i_cacheid) {
		committype = COMMIT_PMAP;

		/* mark for iClose() to free from working map */
		set_cflag(COMMIT_Freewmap, ip);
	} else
		committype = COMMIT_PWMAP;
#else				/* _STILL_TO_PORT */

	set_cflag(COMMIT_Freewmap, ip);
	committype = COMMIT_PMAP;
#endif				/* _STILL_TO_PORT */

	/* mark transaction of block map update type */
	tblk = tid_to_tblock(tid);
	tblk->xflag |= committype;

	/*
	 * free EA
	 */
	if (JFS_IP(ip)->ea.flag & DXD_EXTENT) {
#ifdef _STILL_TO_PORT
		/* free EA pages from cache */
		if (committype == COMMIT_PWMAP)
			bmExtentInvalidate(ip, addressDXD(&ip->i_ea),
					   lengthDXD(&ip->i_ea));
#endif				/* _STILL_TO_PORT */

		/* acquire maplock on EA to be freed from block map */
		txEA(tid, ip, &JFS_IP(ip)->ea, NULL);

		if (committype == COMMIT_PWMAP)
			JFS_IP(ip)->ea.flag = 0;
	}

	/*
	 * free ACL
	 */
	if (JFS_IP(ip)->acl.flag & DXD_EXTENT) {
#ifdef _STILL_TO_PORT
		/* free ACL pages from cache */
		if (committype == COMMIT_PWMAP)
			bmExtentInvalidate(ip, addressDXD(&ip->i_acl),
					   lengthDXD(&ip->i_acl));
#endif				/* _STILL_TO_PORT */

		/* acquire maplock on EA to be freed from block map */
		txEA(tid, ip, &JFS_IP(ip)->acl, NULL);

		if (committype == COMMIT_PWMAP)
			JFS_IP(ip)->acl.flag = 0;
	}

	/*
	 * free xtree/data (truncate to zero length):
	 * free xtree/data pages from cache if COMMIT_PWMAP, 
	 * free xtree/data blocks from persistent block map, and
	 * free xtree/data blocks from working block map if COMMIT_PWMAP;
	 */
	if (ip->i_size)
		return xtTruncate_pmap(tid, ip, 0);

	return 0;
}


/*
 * NAME:	freeZeroLink()
 *
 * FUNCTION:    for non-directory, called by iClose(),
 *		free resources of a file from cache and WORKING map 
 *		for a file previously committed with zero link count
 *		while associated with a pager object,
 *
 * PARAMETER:	ip	- pointer to inode of file.
 *
 * RETURN:	0 -ok
 */
int freeZeroLink(struct inode *ip)
{
	int rc = 0;
	int type;

	jFYI(1, ("freeZeroLink: ip = 0x%p\n", ip));

	/* return if not reg or symbolic link or if size is
	 * already ok.
	 */
	type = ip->i_mode & S_IFMT;

	switch (type) {
	case S_IFREG:
		break;
	case S_IFLNK:
		/* if its contained in inode nothing to do */
		if (ip->i_size <= 256)
			return 0;
		break;
	default:
		return 0;
	}

	/*
	 * free EA
	 */
	if (JFS_IP(ip)->ea.flag & DXD_EXTENT) {
		s64 xaddr;
		int xlen;
		maplock_t maplock;	/* maplock for COMMIT_WMAP */
		pxdlock_t *pxdlock;	/* maplock for COMMIT_WMAP */

		/* free EA pages from cache */
		xaddr = addressDXD(&JFS_IP(ip)->ea);
		xlen = lengthDXD(&JFS_IP(ip)->ea);
#ifdef _STILL_TO_PORT
		bmExtentInvalidate(ip, xaddr, xlen);
#endif

		/* free EA extent from working block map */
		maplock.index = 1;
		pxdlock = (pxdlock_t *) & maplock;
		pxdlock->flag = mlckFREEPXD;
		PXDaddress(&pxdlock->pxd, xaddr);
		PXDlength(&pxdlock->pxd, xlen);
		txFreeMap(ip, pxdlock, 0, COMMIT_WMAP);
	}

	/*
	 * free ACL
	 */
	if (JFS_IP(ip)->acl.flag & DXD_EXTENT) {
		s64 xaddr;
		int xlen;
		maplock_t maplock;	/* maplock for COMMIT_WMAP */
		pxdlock_t *pxdlock;	/* maplock for COMMIT_WMAP */

		/* free ACL pages from cache */
		xaddr = addressDXD(&JFS_IP(ip)->acl);
		xlen = lengthDXD(&JFS_IP(ip)->acl);
#ifdef _STILL_TO_PORT
		bmExtentInvalidate(ip, xaddr, xlen);
#endif

		/* free ACL extent from working block map */
		maplock.index = 1;
		pxdlock = (pxdlock_t *) & maplock;
		pxdlock->flag = mlckFREEPXD;
		PXDaddress(&pxdlock->pxd, xaddr);
		PXDlength(&pxdlock->pxd, xlen);
		txFreeMap(ip, pxdlock, 0, COMMIT_WMAP);
	}

	/*
	 * free xtree/data (truncate to zero length):
	 * free xtree/data pages from cache, and
	 * free xtree/data blocks from working block map;
	 */
	if (ip->i_size)
		rc = xtTruncate(0, ip, 0, COMMIT_WMAP);

	return rc;
}

/*
 * NAME:	jfs_link(vp, dvp, name, crp)
 *
 * FUNCTION:	create a link to <vp> by the name = <name>
 *		in the parent directory <dvp>
 *
 * PARAMETER:	vp 	- target object
 *		dvp	- parent directory of new link
 *		name	- name of new link to target object
 *		crp	- credential
 *
 * RETURN:	Errors from subroutines
 *
 * note:
 * JFS does NOT support link() on directories (to prevent circular
 * path in the directory hierarchy);
 * EPERM: the target object is a directory, and either the caller
 * does not have appropriate privileges or the implementation prohibits
 * using link() on directories [XPG4.2].
 *
 * JFS does NOT support links between file systems:
 * EXDEV: target object and new link are on different file systems and
 * implementation does not support links between file systems [XPG4.2].
 */
int jfs_link(struct dentry *old_dentry,
	     struct inode *dir, struct dentry *dentry)
{
	int rc;
	tid_t tid;
	struct inode *ip = old_dentry->d_inode;
	ino_t ino;
	component_t dname;
	btstack_t btstack;
	struct inode *iplist[2];

	jFYI(1,
	     ("jfs_link: %s %s\n", old_dentry->d_name.name,
	      dentry->d_name.name));

	IWRITE_LOCK_LIST(2, dir, ip);

	tid = txBegin(ip->i_sb, 0);

	if (ip->i_nlink == JFS_LINK_MAX) {
		rc = EMLINK;
		goto out;
	}

	/*
	 * scan parent directory for entry/freespace
	 */
	if ((rc = get_UCSname(&dname, dentry, JFS_SBI(ip->i_sb)->nls_tab)))
		goto out;

	if ((rc = dtSearch(dir, &dname, &ino, &btstack, JFS_CREATE)))
		goto out;

	/*
	 * create entry for new link in parent directory
	 */
	ino = ip->i_ino;
	if ((rc = dtInsert(tid, dir, &dname, &ino, &btstack)))
		goto out;

	dir->i_version = ++event;

	/* update object inode */
	ip->i_nlink++;		/* for new link */
	ip->i_ctime = CURRENT_TIME;
	mark_inode_dirty(dir);
	atomic_inc(&ip->i_count);
	d_instantiate(dentry, ip);

	iplist[0] = ip;
	iplist[1] = dir;
	rc = txCommit(tid, 2, &iplist[0], 0);

      out:
	IWRITE_UNLOCK(dir);
	IWRITE_UNLOCK(ip);

	txEnd(tid);

	jFYI(1, ("jfs_link: rc:%d\n", rc));
	return -rc;
}

/*
 * NAME:	jfs_symlink(dip, dentry, name)
 *
 * FUNCTION:	creates a symbolic link to <symlink> by name <name>
 *		        in directory <dip>
 *
 * PARAMETER:	dip	    - parent directory vnode
 *		        dentry 	- dentry of symbolic link
 *		        name    - the path name of the existing object 
 *			              that will be the source of the link
 *
 * RETURN:	errors from subroutines
 *
 * note:
 * ENAMETOOLONG: pathname resolution of a symbolic link produced
 * an intermediate result whose length exceeds PATH_MAX [XPG4.2]
*/

int jfs_symlink(struct inode *dip, struct dentry *dentry, const char *name)
{
	int rc;
	tid_t tid;
	ino_t ino = 0;
	component_t dname;
	int ssize;		/* source pathname size */
	btstack_t btstack;
	struct inode *ip = dentry->d_inode;
	unchar *i_fastsymlink;
	s64 xlen = 0;
	int bmask = 0, xsize;
	s64 xaddr;
	metapage_t *mp;
	struct super_block *sb;
	tblock_t *tblk;

	struct inode *iplist[2];

	jFYI(1, ("jfs_symlink: dip:0x%p name:%s\n", dip, name));

	IWRITE_LOCK(dip);

	ssize = strlen(name) + 1;

	tid = txBegin(dip->i_sb, 0);

	/*
	 * search parent directory for entry/freespace
	 * (dtSearch() returns parent directory page pinned)
	 */

	if ((rc = get_UCSname(&dname, dentry, JFS_SBI(dip->i_sb)->nls_tab)))
		goto out1;

	if ((rc = dtSearch(dip, &dname, &ino, &btstack, JFS_CREATE)))
		goto out2;



	/*
	 * allocate on-disk/in-memory inode for symbolic link:
	 * (iAlloc() returns new, locked inode)
	 */

	ip = ialloc(dip, S_IFLNK | 0777);
	if (ip == NULL) {
		BT_PUTSEARCH(&btstack);
		rc = ENOSPC;
		goto out2;
	}

	tblk = tid_to_tblock(tid);
	tblk->xflag |= COMMIT_CREATE;
	tblk->ip = ip;

	/*
	 * create entry for symbolic link in parent directory
	 */

	ino = ip->i_ino;



	if ((rc = dtInsert(tid, dip, &dname, &ino, &btstack))) {
		jERROR(1, ("jfs_symlink: dtInsert returned %d\n", rc));
		/* discard ne inode */
		ip->i_nlink = 0;
		iput(ip);
		goto out2;

	}

	/* fix symlink access permission
	 * (dir_create() ANDs in the u.u_cmask, 
	 * but symlinks really need to be 777 access)
	 */
	ip->i_mode |= 0777;

	/*
	   *       write symbolic link target path name
	 */
	xtInitRoot(tid, ip);

	/*
	 * write source path name inline in on-disk inode (fast symbolic link)
	 */

	if (ssize <= IDATASIZE) {
		ip->i_op = &jfs_symlink_inode_operations;

		i_fastsymlink = JFS_IP(ip)->i_inline;
		memcpy(i_fastsymlink, name, ssize);
		ip->i_size = ssize - 1;
		jFYI(1,
		     ("jfs_symlink: fast symlink added  ssize:%d name:%s \n",
		      ssize, name));
	}
	/*
	 * write source path name in a single extent
	 */
	else {
		jFYI(1, ("jfs_symlink: allocate extent ip:0x%p\n", ip));

		ip->i_op = &page_symlink_inode_operations;
		ip->i_mapping->a_ops = &jfs_aops;

		/*
		 * even though the data of symlink object (source 
		 * path name) is treated as non-journaled user data,
		 * it is read/written thru buffer cache for performance.
		 */
		sb = ip->i_sb;
		bmask = JFS_SBI(sb)->bsize - 1;
		xsize = (ssize + bmask) & ~bmask;
		xaddr = 0;
		xlen = xsize >> JFS_SBI(sb)->l2bsize;
		if ((rc = xtInsert(tid, ip, 0, 0, xlen, &xaddr, 0)) == 0) {
			ip->i_size = ssize - 1;
			while (ssize) {
				int copy_size = min(ssize, PSIZE);

				mp = get_metapage(ip, xaddr, PSIZE, 1);

				if (mp == NULL) {
					dtDelete(tid, dip, &dname, &ino,
						 JFS_REMOVE);
					ip->i_nlink = 0;
					iput(ip);
					rc = EIO;
					goto out2;
				}
				memcpy(mp->data, name, copy_size);
				flush_metapage(mp);
#if 0
				mark_buffer_uptodate(bp, 1);
				mark_buffer_dirty(bp, 1);
				if (IS_SYNC(dip)) {
					ll_rw_block(WRITE, 1, &bp);
					wait_on_buffer(bp);
				}
				brelse(bp);
#endif				/* 0 */
				ssize -= copy_size;
				xaddr += JFS_SBI(sb)->nbperpage;
			}
			ip->i_blocks = LBLK2PBLK(sb, xlen);
		} else {
			dtDelete(tid, dip, &dname, &ino, JFS_REMOVE);
			ip->i_nlink = 0;
			iput(ip);
			rc = ENOSPC;
			goto out2;
		}
	}
	dip->i_version = ++event;

	insert_inode_hash(ip);
	mark_inode_dirty(ip);
	d_instantiate(dentry, ip);

	/*
	 * commit update of parent directory and link object
	 *
	 * if extent allocation failed (ENOSPC),
	 * the parent inode is committed regardless to avoid
	 * backing out parent directory update (by dtInsert())
	 * and subsequent dtDelete() which is harmless wrt 
	 * integrity concern.  
	 * the symlink inode will be freed by iput() at exit
	 * as it has a zero link count (by dtDelete()) and 
	 * no permanant resources. 
	 */

	iplist[0] = dip;
	if (rc == 0) {
		iplist[1] = ip;
		rc = txCommit(tid, 2, &iplist[0], 0);
	} else
		rc = txCommit(tid, 1, &iplist[0], 0);

      out2:

	free_UCSname(&dname);
      out1:
	IWRITE_UNLOCK(dip);

	txEnd(tid);

	jFYI(1, ("jfs_symlink: rc:%d\n", -rc));
	return -rc;
}


/*
 * NAME:        jfs_rename
 *
 * FUNCTION:    rename a file or directory
 */
int jfs_rename(struct inode *old_dir, struct dentry *old_dentry,
	       struct inode *new_dir, struct dentry *new_dentry)
{
	btstack_t btstack;
	ino_t ino;
	component_t new_dname;
	struct inode *new_ip;
	component_t old_dname;
	struct inode *old_ip;
	int rc;
	tid_t tid;
	tlock_t *tlck;
	dtlock_t *dtlck;
	lv_t *lv;
	int ipcount;
	struct inode *iplist[4];
	tblock_t *tblk;
	s64 new_size = 0;
	int commit_flag;


	jFYI(1,
	     ("jfs_rename: %s %s\n", old_dentry->d_name.name,
	      new_dentry->d_name.name));

	old_ip = old_dentry->d_inode;
	new_ip = new_dentry->d_inode;

	if (old_dir == new_dir) {
		if (new_ip)
			IWRITE_LOCK_LIST(3, old_dir, old_ip, new_ip);
		else
			IWRITE_LOCK_LIST(2, old_dir, old_ip);
	} else {
		if (new_ip)
			IWRITE_LOCK_LIST(4, old_dir, new_dir, old_ip,
					 new_ip);
		else
			IWRITE_LOCK_LIST(3, old_dir, new_dir, old_ip);
	}

	if ((rc = get_UCSname(&old_dname, old_dentry,
			      JFS_SBI(old_dir->i_sb)->nls_tab)))
		goto out1;

	if ((rc = get_UCSname(&new_dname, new_dentry,
			      JFS_SBI(old_dir->i_sb)->nls_tab)))
		goto out2;

	/*
	 * Make sure source inode number is what we think it is
	 */
	rc = dtSearch(old_dir, &old_dname, &ino, &btstack, JFS_LOOKUP);
	if (rc || (ino != old_ip->i_ino)) {
		rc = ENOENT;
		goto out3;
	}

	/*
	 * Make sure dest inode number (if any) is what we think it is
	 */
	rc = dtSearch(new_dir, &new_dname, &ino, &btstack, JFS_LOOKUP);
	if (rc == 0) {
		if ((new_ip == 0) || (ino != new_ip->i_ino)) {
			rc = ESTALE;
			goto out3;
		}
	} else if (rc != ENOENT)
		goto out3;
	else if (new_ip) {
		/* no entry exists, but one was expected */
		rc = ESTALE;
		goto out3;
	}

	if (S_ISDIR(old_ip->i_mode)) {
		if (new_ip) {
			if (!dtEmpty(new_ip)) {
				rc = ENOTEMPTY;
				goto out3;
			}
		} else if ((new_dir != old_dir) &&
			   (new_dir->i_nlink == JFS_LINK_MAX)) {
			rc = EMLINK;
			goto out3;
		}
	}

	/*
	 * The real work starts here
	 */
	tid = txBegin(new_dir->i_sb, 0);

	if (new_ip) {
		/*
		 * Change existing directory entry to new inode number
		 */
		ino = new_ip->i_ino;
		rc = dtModify(tid, new_dir, &new_dname, &ino,
			      old_ip->i_ino, JFS_RENAME);
		if (rc)
			goto out4;
		new_ip->i_nlink--;
		if (S_ISDIR(new_ip->i_mode)) {
			new_ip->i_nlink--;
			assert(new_ip->i_nlink == 0);
			tblk = tid_to_tblock(tid);
			tblk->xflag |= COMMIT_DELETE;
			tblk->ip = new_ip;
		} else if (new_ip->i_nlink == 0) {
			assert(!test_cflag(COMMIT_Nolink, new_ip));
			/* free block resources */
			if ((new_size = commitZeroLink(tid, new_ip)) < 0) {
				txAbort(tid, 1);	/* Marks FS Dirty */
				rc = -new_size;		/* We return -rc */
				goto out4;
			}
			tblk = tid_to_tblock(tid);
			tblk->xflag |= COMMIT_DELETE;
			tblk->ip = new_ip;
		} else {
			new_ip->i_ctime = CURRENT_TIME;
			mark_inode_dirty(new_ip);
		}
	} else {
		/*
		 * Add new directory entry
		 */
		rc = dtSearch(new_dir, &new_dname, &ino, &btstack,
			      JFS_CREATE);
		if (rc) {
			jERROR(1,
			       ("jfs_rename didn't expect dtSearch to fail w/rc = %d\n",
				rc));
			goto out4;
		}

		ino = old_ip->i_ino;
		rc = dtInsert(tid, new_dir, &new_dname, &ino, &btstack);
		if (rc) {
			jERROR(1,
			       ("jfs_rename: dtInsert failed w/rc = %d\n",
				rc));
			goto out4;
		}
		if (S_ISDIR(old_ip->i_mode))
			new_dir->i_nlink++;
	}
	/*
	 * Remove old directory entry
	 */

	ino = old_ip->i_ino;
	rc = dtDelete(tid, old_dir, &old_dname, &ino, JFS_REMOVE);
	if (rc) {
		jERROR(1,
		       ("jfs_rename did not expect dtDelete to return rc = %d\n",
			rc));
		txAbort(tid, 1);	/* Marks Filesystem dirty */
		goto out4;
	}
	if (S_ISDIR(old_ip->i_mode)) {
		old_dir->i_nlink--;
		if (old_dir != new_dir) {
			/*
			 * Change inode number of parent for moved directory
			 */

			JFS_IP(old_ip)->i_dtroot.header.idotdot =
				cpu_to_le32(new_dir->i_ino);

			/* Linelock header of dtree */
			tlck = txLock(tid, old_ip,
				      (metapage_t *) & JFS_IP(old_ip)->bxflag,
				      tlckDTREE | tlckBTROOT);
			dtlck = (dtlock_t *) & tlck->lock;
			ASSERT(dtlck->index == 0);
			lv = (lv_t *) & dtlck->lv[0];
			lv->offset = 0;
			lv->length = 1;
			dtlck->index++;
		}
	}

	/*
	 * Update ctime on changed/moved inodes & mark dirty
	 */
	old_ip->i_ctime = CURRENT_TIME;
	mark_inode_dirty(old_ip);

	new_dir->i_version = ++event;
	new_dir->i_ctime = CURRENT_TIME;
	mark_inode_dirty(new_dir);

	/* Build list of inodes modified by this transaction */
	ipcount = 0;
	iplist[ipcount++] = old_ip;
	if (new_ip)
		iplist[ipcount++] = new_ip;
	iplist[ipcount++] = old_dir;

	if (old_dir != new_dir) {
		iplist[ipcount++] = new_dir;
		old_dir->i_version = ++event;
		old_dir->i_ctime = CURRENT_TIME;
		mark_inode_dirty(old_dir);
	}

	/*
	 * Incomplete truncate of file data can
	 * result in timing problems unless we synchronously commit the
	 * transaction.
	 */
	if (new_size)
		commit_flag = COMMIT_SYNC;
	else
		commit_flag = 0;

	rc = txCommit(tid, ipcount, iplist, commit_flag);

	/*
	 * Don't unlock new_ip if COMMIT_HOLDLOCK is set
	 */
	if (new_ip && test_cflag(COMMIT_Holdlock, new_ip))
		new_ip = 0;

      out4:
	txEnd(tid);

	while (new_size && (rc == 0)) {
		tid = txBegin(new_ip->i_sb, 0);
		new_size = xtTruncate_pmap(tid, new_ip, new_size);
		if (new_size < 0) {
			txAbort(tid, 1);
			rc = -new_size;		/* We return -rc */
		} else
			rc = txCommit(tid, 1, &new_ip, COMMIT_SYNC);
		txEnd(tid);
	}
      out3:
	free_UCSname(&new_dname);
      out2:
	free_UCSname(&old_dname);
      out1:
	IWRITE_UNLOCK(old_ip);
	if (old_dir != new_dir)
		IWRITE_UNLOCK(new_dir);
	if (new_ip)
		IWRITE_UNLOCK(new_ip);

	/*
	 * Truncating the directory index table is not guaranteed.  It
	 * may need to be done iteratively
	 */
	if (test_cflag(COMMIT_Stale, old_dir)) {
		if (old_dir->i_size > 1)
			jfs_truncate_nolock(old_dir, 0);

		clear_cflag(COMMIT_Stale, old_dir);
	}

	IWRITE_UNLOCK(old_dir);

	jFYI(1, ("jfs_rename: returning %d\n", rc));
	return -rc;
}


/*
 * NAME:        jfs_mknod
 *
 * FUNCTION:    Create a special file (device)
 */
int jfs_mknod(struct inode *dir, struct dentry *dentry, int mode, int rdev)
{
	btstack_t btstack;
	component_t dname;
	ino_t ino;
	struct inode *ip;
	struct inode *iplist[2];
	int rc;
	tid_t tid;
	tblock_t *tblk;

	jFYI(1, ("jfs_mknod: %s\n", dentry->d_name.name));

	if ((rc = get_UCSname(&dname, dentry, JFS_SBI(dir->i_sb)->nls_tab)))
		goto out;

	IWRITE_LOCK(dir);

	ip = ialloc(dir, mode);
	if (ip == NULL) {
		rc = ENOSPC;
		goto out1;
	}

	tid = txBegin(dir->i_sb, 0);

	if ((rc = dtSearch(dir, &dname, &ino, &btstack, JFS_CREATE))) {
		ip->i_nlink = 0;
		iput(ip);
		txEnd(tid);
		goto out1;
	}

	tblk = tid_to_tblock(tid);
	tblk->xflag |= COMMIT_CREATE;
	tblk->ip = ip;

	ino = ip->i_ino;
	if ((rc = dtInsert(tid, dir, &dname, &ino, &btstack))) {
		ip->i_nlink = 0;
		iput(ip);
		txEnd(tid);
		goto out1;
	}

	if (S_ISREG(ip->i_mode)) {
		ip->i_op = &jfs_file_inode_operations;
		ip->i_fop = &jfs_file_operations;
		ip->i_mapping->a_ops = &jfs_aops;
	} else
		init_special_inode(ip, ip->i_mode, rdev);

	insert_inode_hash(ip);
	mark_inode_dirty(ip);
	d_instantiate(dentry, ip);

	dir->i_version = ++event;
	dir->i_ctime = dir->i_mtime = CURRENT_TIME;

	mark_inode_dirty(dir);

	iplist[0] = dir;
	iplist[1] = ip;
	rc = txCommit(tid, 2, iplist, 0);
	txEnd(tid);

      out1:
	IWRITE_UNLOCK(dir);
	free_UCSname(&dname);

      out:
	jFYI(1, ("jfs_mknod: returning %d\n", rc));
	return -rc;
}

static struct dentry *jfs_lookup(struct inode *dip, struct dentry *dentry)
{
	btstack_t btstack;
	ino_t inum;
	struct inode *ip;
	component_t key;
	const char *name = dentry->d_name.name;
	int len = dentry->d_name.len;
	int rc;

	jFYI(1, ("jfs_lookup: name = %s\n", name));


	if ((name[0] == '.') && (len == 1))
		inum = dip->i_ino;
	else if (strcmp(name, "..") == 0)
		inum = PARENT(dip);
	else {
		if ((rc =
		     get_UCSname(&key, dentry, JFS_SBI(dip->i_sb)->nls_tab)))
			return ERR_PTR(-rc);
		IREAD_LOCK(dip);
		rc = dtSearch(dip, &key, &inum, &btstack, JFS_LOOKUP);
		IREAD_UNLOCK(dip);
		free_UCSname(&key);
		if (rc == ENOENT) {
			d_add(dentry, NULL);
			return ERR_PTR(0);
		} else if (rc) {
			jERROR(1,
			       ("jfs_lookup: dtSearch returned %d\n", rc));
			return ERR_PTR(-rc);
		}
	}

	ip = iget(dip->i_sb, inum);
	if (ip == NULL) {
		jERROR(1,
		       ("jfs_lookup: iget failed on inum %d\n",
			(uint) inum));
		return ERR_PTR(-EACCES);
	}

	d_add(dentry, ip);

	return ERR_PTR(0);
}

struct inode_operations jfs_dir_inode_operations = {
	create:		jfs_create,
	lookup:		jfs_lookup,
	link:		jfs_link,
	unlink:		jfs_unlink,
	symlink:	jfs_symlink,
	mkdir:		jfs_mkdir,
	rmdir:		jfs_rmdir,
	mknod:		jfs_mknod,
	rename:		jfs_rename,
};

struct file_operations jfs_dir_operations = {
	read:		generic_read_dir,
	readdir:	jfs_readdir,
	fsync:		jfs_fsync,
};
