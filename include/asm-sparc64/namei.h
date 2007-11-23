/* $Id: namei.h,v 1.1 1996/12/26 13:25:22 davem Exp $
 * linux/include/asm-sparc64/namei.h
 *
 * Routines to handle famous /usr/gnemul/s*.
 * Included from linux/fs/namei.c
 */

#ifndef __SPARC64_NAMEI_H
#define __SPARC64_NAMEI_H

#define SPARC_BSD_EMUL "usr/gnemul/sunos/"
#define SPARC_SOL_EMUL "usr/gnemul/solaris/"

#define gnemul_namei(pathname, base, follow_links, res_inode) ({			\
 if ((current->personality & (PER_BSD|PER_SVR4)) && !base && *pathname == '/') {	\
	struct inode *emul_ino;								\
	int namelen;									\
	const char *name;								\
											\
	while (*pathname == '/')							\
		pathname++;								\
	current->fs->root->i_count++;							\
	if (dir_namei (current->personality & PER_BSD ? SPARC_BSD_EMUL : SPARC_SOL_EMUL,\
		&namelen, &name, current->fs->root, &emul_ino) >= 0 && emul_ino) {	\
			*res_inode = NULL;						\
			if (_namei (pathname, emul_ino, follow_links, res_inode) >= 0 &&\
					*res_inode) 					\
				return 0;						\
	}										\
	base = current->fs->root;							\
	base->i_count++;								\
  }											\
})

#define gnemul_open_namei(pathname, flag, mode, res_inode, base) ({			\
  if ((current->personality & (PER_BSD|PER_SVR4)) && !base && *pathname == '/') {	\
	struct inode *emul_ino;								\
	int namelen;									\
	const char *name;								\
											\
	while (*pathname == '/')							\
		pathname++;								\
	current->fs->root->i_count++;							\
	if (dir_namei (current->personality & PER_BSD ? SPARC_BSD_EMUL : SPARC_SOL_EMUL,\
		  &namelen, &name, current->fs->root, &emul_ino) >= 0 && emul_ino) {	\
		*res_inode = NULL;							\
		if (open_namei (pathname, flag, mode, res_inode, emul_ino) >= 0 && 	\
				*res_inode)						\
			return 0;							\
	}										\
	base = current->fs->root;							\
	base->i_count++;								\
  }											\
})

#endif /* __SPARC64_NAMEI_H */
