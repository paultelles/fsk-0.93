#ifndef PCFS_H
#define PCFS_H

/*
 *
 *  Adapted for System V Release 4	(ESIX 4.0.4)	
 *
 *  Gerard van Dorth	(gdorth@oracle.nl.oracle.com)
 *  Paul Bauwens	(paul@pphbau.atr.bso.nl)
 *
 *  May 1993
 *
 *  Originally written by Paul Popelka (paulp@uts.amdahl.com)
 *
 *  You can do anything you want with this software,
 *    just don't say you wrote it,
 *    and don't remove this notice.
 *
 *  This software is provided "as is".
 *
 *  The author supplies this software to be publicly
 *  redistributed on the understanding that the author
 *  is not responsible for the correct functioning of
 *  this software in any circumstances and is not liable
 *  for any damages caused by this software.
 *
 *  October 1992
 *
 */

#ident "@(#)pcfs.h	1.3 93/10/17 "

#include "sys/types.h"
#include "sys/param.h"
#include "sys/fcntl.h"
#include "sys/proc.h"
#include "sys/user.h"
#include "sys/stat.h"
#include "sys/statvfs.h"
#include "sys/dirent.h"
#include "sys/sysmacros.h"
#include "sys/vfs.h"
#include "sys/vnode.h"
#include "sys/buf.h"
#include "sys/cmn_err.h"
#include "sys/conf.h"
#include "sys/cred.h"
#include "sys/ddi.h"
#include "sys/debug.h"
#include "sys/disp.h"
#include "sys/dnlc.h"
#include "sys/errno.h"
#include "sys/fbuf.h"
#include "sys/file.h"
#include "sys/flock.h"
#include "sys/immu.h"
#include "sys/inode.h"
#include "sys/kmem.h"
#include "sys/mkdev.h"
#include "sys/mman.h"
#include "sys/mount.h"
#include "sys/open.h"
#include "sys/pathname.h"
#include "sys/resource.h"
#include "sys/swap.h"
#include "sys/sysinfo.h"
#include "sys/systm.h"
#include "sys/time.h"
#include "sys/uio.h"

#include "fs/fs_subr.h"

/* pcfs includes */
#include "bootsect.h"
#include "bpb.h"
#include "direntry.h"
#include "denode.h"
#include "fat.h"
#include "pcfs_filsys.h"
#include "pcfs_lbuf.h"

#ifdef _KERNEL

/* pcfs prototypes */

int createde(denode_t*, vnode_t*, denode_t**);
int deflush(struct vfs*, int);
int detrunc(denode_t*, u_long, int);
int deupdat(denode_t*, timestruc_t*, int);
int doscheckpath(denode_t*, denode_t*);
int dosdirempty(denode_t*);
int extendfile(denode_t*, lbuf_t**, u_long*);
int fillinusemap(pcfs_vfs_t*);
int get_direntp(pcfs_vfs_t*, u_long, u_long, dosdirent_t*);
int readde(denode_t*, lbuf_t**, dosdirent_t**);
int removede(vnode_t*);
int strcpy(char*, const char*);
int strlen(const char*);
void cleanlocks();
void fc_purge(denode_t*, u_int);
void fc_lookup(denode_t*, u_long, u_long*, u_long*);
void delock (denode_t*);
void deput(denode_t*);
void dereject (denode_t*);
void deunhash(denode_t*);
void deunlock (denode_t*);
void pcfs_deflush(void);
void reinsert(denode_t*);

extern struct vnodeops pcfs_vnodeops;

#ifndef NO_GENOFF
extern int pcfs_shared;
#endif

#endif /* _KERNEL */

#endif /* PCFS_H */
