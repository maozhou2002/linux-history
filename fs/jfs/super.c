/*
 *   Copyright (c) International Business Machines Corp., 2000-2002
 *   Portions Copyright (c) Christoph Hellwig, 2001-2002
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
 */

#include <linux/fs.h>
#include <linux/config.h>
#include <linux/module.h>
#include <linux/completion.h>
#include <asm/uaccess.h>
#include "jfs_incore.h"
#include "jfs_filsys.h"
#include "jfs_metapage.h"
#include "jfs_superblock.h"
#include "jfs_dmap.h"
#include "jfs_imap.h"
#include "jfs_debug.h"

MODULE_DESCRIPTION("The Journaled Filesystem (JFS)");
MODULE_AUTHOR("Steve Best/Dave Kleikamp/Barry Arndt, IBM");
MODULE_LICENSE("GPL");

static kmem_cache_t * jfs_inode_cachep;

int jfs_stop_threads;
static pid_t jfsIOthread;
static pid_t jfsCommitThread;
static pid_t jfsSyncThread;
DECLARE_COMPLETION(jfsIOwait);

#ifdef CONFIG_JFS_DEBUG
int jfsloglevel = 1;
MODULE_PARM(jfsloglevel, "i");
MODULE_PARM_DESC(jfsloglevel, "Specify JFS loglevel (0, 1 or 2)");
#endif

/*
 * External declarations
 */
extern struct inode *jfs_iget(struct super_block *, ino_t);

extern int jfs_mount(struct super_block *);
extern int jfs_mount_rw(struct super_block *, int);
extern int jfs_umount(struct super_block *);
extern int jfs_umount_rw(struct super_block *);

extern int jfsIOWait(void *);
extern int jfs_lazycommit(void *);
extern int jfs_sync(void *);

extern void jfs_dirty_inode(struct inode *inode);
extern void jfs_delete_inode(struct inode *inode);
extern void jfs_write_inode(struct inode *inode, int wait);

extern struct dentry *jfs_get_parent(struct dentry *dentry);

#if defined(CONFIG_JFS_DEBUG) && defined(CONFIG_PROC_FS)
extern void jfs_proc_init(void);
extern void jfs_proc_clean(void);
#endif

extern wait_queue_head_t jfs_IO_thread_wait;
extern wait_queue_head_t jfs_commit_thread_wait;
extern wait_queue_head_t jfs_sync_thread_wait;

static struct inode *jfs_alloc_inode(struct super_block *sb)
{
	struct jfs_inode_info *jfs_inode;

	jfs_inode = kmem_cache_alloc(jfs_inode_cachep, GFP_NOFS);
	if (!jfs_inode)
		return NULL;
	return &jfs_inode->vfs_inode;
}

static void jfs_destroy_inode(struct inode *inode)
{
	kmem_cache_free(jfs_inode_cachep, JFS_IP(inode));
}

static int jfs_statfs(struct super_block *sb, struct statfs *buf)
{
	struct jfs_sb_info *sbi = JFS_SBI(sb);
	s64 maxinodes;
	imap_t *imap = JFS_IP(sbi->ipimap)->i_imap;

	jFYI(1, ("In jfs_statfs\n"));
	buf->f_type = JFS_SUPER_MAGIC;
	buf->f_bsize = sbi->bsize;
	buf->f_blocks = sbi->bmap->db_mapsize;
	buf->f_bfree = sbi->bmap->db_nfree;
	buf->f_bavail = sbi->bmap->db_nfree;
	/*
	 * If we really return the number of allocated & free inodes, some
	 * applications will fail because they won't see enough free inodes.
	 * We'll try to calculate some guess as to how may inodes we can
	 * really allocate
	 *
	 * buf->f_files = atomic_read(&imap->im_numinos);
	 * buf->f_ffree = atomic_read(&imap->im_numfree);
	 */
	maxinodes = min((s64) atomic_read(&imap->im_numinos) +
			((sbi->bmap->db_nfree >> imap->im_l2nbperiext)
			 << L2INOSPEREXT), (s64)0xffffffffLL);
	buf->f_files = maxinodes;
	buf->f_ffree = maxinodes - (atomic_read(&imap->im_numinos) -
				    atomic_read(&imap->im_numfree));

	buf->f_namelen = JFS_NAME_MAX;
	return 0;
}

static void jfs_put_super(struct super_block *sb)
{
	struct jfs_sb_info *sbi = JFS_SBI(sb);
	int rc;

	jFYI(1, ("In jfs_put_super\n"));
	rc = jfs_umount(sb);
	if (rc) {
		jERROR(1, ("jfs_umount failed with return code %d\n", rc));
	}
	unload_nls(sbi->nls_tab);
	sbi->nls_tab = NULL;

	/*
	 * We need to clean out the direct_inode pages since this inode
	 * is not in the inode hash.
	 */
	filemap_fdatawait(sbi->direct_inode->i_mapping);
	filemap_fdatawrite(sbi->direct_inode->i_mapping);
	filemap_fdatawait(sbi->direct_inode->i_mapping);
	truncate_inode_pages(sbi->direct_mapping, 0);
	iput(sbi->direct_inode);
	sbi->direct_inode = NULL;
	sbi->direct_mapping = NULL;

	kfree(sbi);
}

static int parse_options (char * options, struct jfs_sb_info *sbi)
{
	void *nls_map = NULL;
	char * this_char;
	char * value;

	if (!options)
		return 1;
	while ((this_char = strsep (&options, ",")) != NULL) {
		if (!*this_char)
			continue;
		if ((value = strchr (this_char, '=')) != NULL)
			*value++ = 0;
		if (!strcmp (this_char, "iocharset")) {
			if (!value || !*value)
				goto needs_arg;
			if (nls_map)	/* specified iocharset twice! */
				unload_nls(nls_map);
			nls_map = load_nls(value);
			if (!nls_map) {
				printk(KERN_ERR "JFS: charset not found\n");
				goto cleanup;
			}
		/* Silently ignore the quota options */
		} else if (!strcmp (this_char, "grpquota")
		         || !strcmp (this_char, "noquota")
		         || !strcmp (this_char, "quota")
		         || !strcmp (this_char, "usrquota"))
			/* Don't do anything ;-) */ ;
		else {
			printk ("jfs: Unrecognized mount option %s\n", this_char);
			goto cleanup;
		}
	}
	if (nls_map) {
		/* Discard old (if remount) */
		if (sbi->nls_tab)
			unload_nls(sbi->nls_tab);
		sbi->nls_tab = nls_map;
	}
	return 1;
needs_arg:
	printk(KERN_ERR "JFS: %s needs an argument\n", this_char);
cleanup:
	if (nls_map)
		unload_nls(nls_map);
	return 0;
}

int jfs_remount(struct super_block *sb, int *flags, char *data)
{
	struct jfs_sb_info *sbi = JFS_SBI(sb);

	if (!parse_options(data, sbi)) {
		return -EINVAL;
	}

	if ((sb->s_flags & MS_RDONLY) && !(*flags & MS_RDONLY)) {
		/*
		 * Invalidate any previously read metadata.  fsck may
		 * have changed the on-disk data since we mounted r/o
		 */
		truncate_inode_pages(sbi->direct_mapping, 0);

		return jfs_mount_rw(sb, 1);
	} else if ((!(sb->s_flags & MS_RDONLY)) && (*flags & MS_RDONLY))
		return jfs_umount_rw(sb);

	return 0;
}

static struct super_operations jfs_super_operations = {
	alloc_inode:	jfs_alloc_inode,
	destroy_inode:	jfs_destroy_inode,
	dirty_inode:	jfs_dirty_inode,
	write_inode:	jfs_write_inode,
	delete_inode:	jfs_delete_inode,
	put_super:	jfs_put_super,
	statfs:		jfs_statfs,
	remount_fs:	jfs_remount,
};

static struct export_operations jfs_export_operations = {
	get_parent:	jfs_get_parent,
};

static int jfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct jfs_sb_info *sbi;
	struct inode *inode;
	int rc;

	jFYI(1, ("In jfs_read_super: s_flags=0x%lx\n", sb->s_flags));

	sbi = kmalloc(sizeof(struct jfs_sb_info), GFP_KERNEL);
	if (!sbi)
		return -ENOSPC;
	memset(sbi, 0, sizeof(struct jfs_sb_info));
	sb->u.generic_sbp = sbi;

	if (!parse_options((char *)data, sbi)) {
		kfree(sbi);
		return -EINVAL;
	}

	/*
	 * Initialize blocksize to 4K.
	 */
	sb_set_blocksize(sb, PSIZE);

	/*
	 * Set method vectors.
	 */
	sb->s_op = &jfs_super_operations;
	sb->s_export_op = &jfs_export_operations;

	/*
	 * Initialize direct-mapping inode/address-space
	 */
	inode = new_inode(sb);
	if (inode == NULL)
		goto out_kfree;
	inode->i_ino = 0;
	inode->i_nlink = 1;
	inode->i_size = 0x0000010000000000LL;
	inode->i_mapping->a_ops = &direct_aops;
	inode->i_mapping->gfp_mask = GFP_NOFS;

	sbi->direct_inode = inode;
	sbi->direct_mapping = inode->i_mapping;

	rc = jfs_mount(sb);
	if (rc) {
		if (!silent) {
			jERROR(1,
			       ("jfs_mount failed w/return code = %d\n",
				rc));
		}
		goto out_mount_failed;
	}
	if (sb->s_flags & MS_RDONLY)
		sbi->log = 0;
	else {
		rc = jfs_mount_rw(sb, 0);
		if (rc) {
			if (!silent) {
				jERROR(1,
				       ("jfs_mount_rw failed w/return code = %d\n",
					rc));
			}
			goto out_no_rw;
		}
	}

	sb->s_magic = JFS_SUPER_MAGIC;

	inode = jfs_iget(sb, ROOT_I);
	if (!inode || is_bad_inode(inode))
		goto out_no_root;
	sb->s_root = d_alloc_root(inode);
	if (!sb->s_root)
		goto out_no_root;

	if (!sbi->nls_tab)
		sbi->nls_tab = load_nls_default();

	sb->s_maxbytes = ((u64) sb->s_blocksize) << 40;
#if BITS_PER_LONG == 32
	sb->s_maxbytes = min((u64)PAGE_CACHE_SIZE << 32, sb->s_maxbytes);
#endif

	return 0;

out_no_root:
	jEVENT(1, ("jfs_read_super: get root inode failed\n"));
	if (inode)
		iput(inode);

out_no_rw:
	rc = jfs_umount(sb);
	if (rc) {
		jERROR(1, ("jfs_umount failed with return code %d\n", rc));
	}
out_mount_failed:
	filemap_fdatawait(sbi->direct_inode->i_mapping);
	filemap_fdatawrite(sbi->direct_inode->i_mapping);
	filemap_fdatawait(sbi->direct_inode->i_mapping);
	truncate_inode_pages(sbi->direct_mapping, 0);
	make_bad_inode(sbi->direct_inode);
	iput(sbi->direct_inode);
	sbi->direct_inode = NULL;
	sbi->direct_mapping = NULL;
out_kfree:
	if (sbi->nls_tab)
		unload_nls(sbi->nls_tab);
	kfree(sbi);
	return -EINVAL;
}

static struct super_block *jfs_get_sb(struct file_system_type *fs_type,
		int flags, char *dev_name, void *data)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, jfs_fill_super);
}

static struct file_system_type jfs_fs_type = {
	owner:		THIS_MODULE,
	name:		"jfs",
	get_sb:		jfs_get_sb,
	kill_sb:	kill_block_super,
	fs_flags:	FS_REQUIRES_DEV,
};

extern int metapage_init(void);
extern int txInit(void);
extern void txExit(void);
extern void metapage_exit(void);

static void init_once(void * foo, kmem_cache_t * cachep, unsigned long flags)
{
	struct jfs_inode_info *jfs_ip = (struct jfs_inode_info *) foo;

	if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
	    SLAB_CTOR_CONSTRUCTOR) {
		INIT_LIST_HEAD(&jfs_ip->anon_inode_list);
		INIT_LIST_HEAD(&jfs_ip->mp_list);
		RDWRLOCK_INIT(&jfs_ip->rdwrlock);
		inode_init_once(&jfs_ip->vfs_inode);
	}
}

static int __init init_jfs_fs(void)
{
	int rc;

	jfs_inode_cachep =
	    kmem_cache_create("jfs_ip",
			    sizeof(struct jfs_inode_info),
			    0, 0, init_once, NULL);
	if (jfs_inode_cachep == NULL)
		return -ENOMEM;

	/*
	 * Metapage initialization
	 */
	rc = metapage_init();
	if (rc) {
		jERROR(1, ("metapage_init failed w/rc = %d\n", rc));
		goto free_slab;
	}

	/*
	 * Transaction Manager initialization
	 */
	rc = txInit();
	if (rc) {
		jERROR(1, ("txInit failed w/rc = %d\n", rc));
		goto free_metapage;
	}

	/*
	 * I/O completion thread (endio)
	 */
	jfsIOthread = kernel_thread(jfsIOWait, 0,
				    CLONE_FS | CLONE_FILES |
				    CLONE_SIGHAND);
	if (jfsIOthread < 0) {
		jERROR(1,
		       ("init_jfs_fs: fork failed w/rc = %d\n",
			jfsIOthread));
		goto end_txmngr;
	}
	wait_for_completion(&jfsIOwait);	/* Wait until IO thread starts */

	jfsCommitThread = kernel_thread(jfs_lazycommit, 0,
					CLONE_FS | CLONE_FILES |
					CLONE_SIGHAND);
	if (jfsCommitThread < 0) {
		jERROR(1,
		       ("init_jfs_fs: fork failed w/rc = %d\n",
			jfsCommitThread));
		goto kill_iotask;
	}
	wait_for_completion(&jfsIOwait);	/* Wait until IO thread starts */

	jfsSyncThread = kernel_thread(jfs_sync, 0,
				      CLONE_FS | CLONE_FILES |
				      CLONE_SIGHAND);
	if (jfsSyncThread < 0) {
		jERROR(1,
		       ("init_jfs_fs: fork failed w/rc = %d\n",
			jfsSyncThread));
		goto kill_committask;
	}
	wait_for_completion(&jfsIOwait);	/* Wait until IO thread starts */

#if defined(CONFIG_JFS_DEBUG) && defined(CONFIG_PROC_FS)
	jfs_proc_init();
#endif

	return register_filesystem(&jfs_fs_type);


kill_committask:
	jfs_stop_threads = 1;
	wake_up(&jfs_commit_thread_wait);
	wait_for_completion(&jfsIOwait);	/* Wait until Commit thread exits */
kill_iotask:
	jfs_stop_threads = 1;
	wake_up(&jfs_IO_thread_wait);
	wait_for_completion(&jfsIOwait);	/* Wait until IO thread exits */
end_txmngr:
	txExit();
free_metapage:
	metapage_exit();
free_slab:
	kmem_cache_destroy(jfs_inode_cachep);
	return -rc;
}

static void __exit exit_jfs_fs(void)
{
	jFYI(1, ("exit_jfs_fs called\n"));

	jfs_stop_threads = 1;
	txExit();
	metapage_exit();
	wake_up(&jfs_IO_thread_wait);
	wait_for_completion(&jfsIOwait);	/* Wait until IO thread exits */
	wake_up(&jfs_commit_thread_wait);
	wait_for_completion(&jfsIOwait);	/* Wait until Commit thread exits */
	wake_up(&jfs_sync_thread_wait);
	wait_for_completion(&jfsIOwait);	/* Wait until Sync thread exits */
#if defined(CONFIG_JFS_DEBUG) && defined(CONFIG_PROC_FS)
	jfs_proc_clean();
#endif
	unregister_filesystem(&jfs_fs_type);
	kmem_cache_destroy(jfs_inode_cachep);
}

module_init(init_jfs_fs)
module_exit(exit_jfs_fs)
