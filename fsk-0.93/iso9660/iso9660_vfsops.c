/*
 *
 *  Adapted for System V Release 4	(ESIX 4.0.4)	
 *
 *  Gerard van Dorth	(gdorth@nl.oracle.com)
 *  Paul Bauwens		(paul@pphbau.atr.bso.nl)
 *
 *  May 1993
 *
 *  This software is provided "as is".
 *
 *  The author supplies this software to be publicly
 *  redistributed on the understanding that the author
 *  is not responsible for the correct functioning of
 *  this software in any circumstances and is not liable
 *  for any damages caused by this software.
 *
 *
 */

#ident "@(#)iso9660_vfsops.c	1.3 93/09/16 "

/*
 * Copyright (c) 1989, 1991 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *  Started from
 *	ufs_vfsops.c	7.56 (Berkeley) 6/28/91
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/disp.h>
#include <sys/sysmacros.h>
#include <sys/kmem.h>
#include <sys/vnode.h>
#include <sys/pathname.h>
#include <sys/vfs.h>
#include <sys/mount.h>
#include <sys/buf.h>
#include <sys/file.h>
#include <sys/cred.h>
#include <sys/cmn_err.h>
#include <sys/errno.h>
#include <sys/debug.h>
#include <sys/statvfs.h>
#include <sys/uio.h>
#include <sys/conf.h>
#include <sys/swap.h>
#include <fs/fs_subr.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/flock.h>

#include "iso9660.h"
#include "iso9660_node.h"

static int IsoMountFs ( struct vfs*, dev_t, char*, cred_t*   );
static int IsoFhToVp  ( struct vfs*, struct fid*,  vnode_t** );


extern struct vfsops iso9660_vfsops;

/*
 * Called by vfs_mountroot when ufs is going to be mounted as root.
 *
 * Name is updated by mount(8) after booting.
 */
#define ROOTNAME	"root_device"

/*
 * Flag to allow forcible unmounting.
 */
int iso9660_doforce = 1;

/*
 * VFS Operations.
 *
 * mount system call
 */
int
IsoMount ( struct vfs* vfsp, vnode_t* mvp, struct mounta* uap, cred_t *cr )
{
	dev_t				dev;
	vnode_t				*bvp;
	struct pathname  	dpn;  /* directory point to mount */
	IsoArgs_t			args;
	struct vfs			*found;
	int					error;
	IsoVfs_t			*ivp;

#ifdef ISO9660_DEBUG
	printf ( "In mount\n" );
#endif
	/*
	Trivial checks first:
		- mount only allowed on directories
		- only super user can mount
		- mount on a not already mounted place
		- mount should be readonly
	*/

	if (mvp->v_type != VDIR)
	{
		return (ENOTDIR);
	}

	if (!suser(cr))
	{
		return (EPERM);
	}

    if ( (uap->flags & MS_REMOUNT) == 0 &&
	     (mvp->v_count != 1 || (mvp->v_flag & VROOT)))
	{
		return (EBUSY);
	}


	if ( !(uap->flags & MS_RDONLY) )
	{
		return (EROFS);
	}

	/*
	Get the mount directory; we need it for statvfs
	to display the pathname we're mounted on.
	*/

	if ( error = pn_get(uap->dir, UIO_USERSPACE, &dpn) )
	{
		return (error);
	}

	/*
    Get the path of the special file being mounted;
	it should be a block device. Pick-up the device
	type.
    */

    if (error = lookupname(uap->spec, UIO_USERSPACE, FOLLOW, NULLVPP, &bvp))
	{
		pn_free(&dpn);
        return (error);
    }

    if (bvp->v_type != VBLK)
	{
        VN_RELE(bvp);
		pn_free(&dpn);
		return (ENOTBLK);
	}

	dev = bvp->v_rdev;
    VN_RELE(bvp);

	/*
	Copy the readonly flag.
	*/

	vfsp->vfs_flag |= VFS_RDONLY;


	/*
	Check whether the device makes sense.
	*/

    if ( getmajor(dev) >= bdevcnt )
	{
        pn_free(&dpn);
        return (ENXIO);
    }

    /*
    A device can only be mounted once; however one
	is able to remount it - in which case we need
	to find the device on the same mountpoint.
	Note we expect the same filesystem on a remount
	so no actions are performed, just some checks.
    */

    found = vfs_devsearch(dev);

	if ( uap->flags & MS_REMOUNT )
	{
		pn_free(&dpn);

		if ( found != vfsp )
		{
            return EINVAL;
		}

		return 0;
	}
	else if ( found != NULL )
	{
		pn_free(&dpn);
		return EBUSY;
	}

	/*
	Well, it's not an update, it's a real mount request.
	Do the dirty part.
	*/


	error = IsoMountFs( vfsp, dev, dpn.pn_path, cr );

	pn_free ( &dpn );

	if ( error )
	{
		return error;
	}

	/*
	Copy in the args for the mount request, if there are some.
	Only actions till now: ignore RR-extensions or raw mode
	names.
	Note high sierra disks never contains RR extensions.
	*/

	ivp = VFSTOISO9660(vfsp);

	if ( uap->dataptr )
	{
		error = copyin(uap->dataptr, (caddr_t)&args, sizeof(IsoArgs_t));

		if ( error )
		{
			return error;
		}

		ivp->im_raw = args.raw;

		if ( ivp->im_raw || ivp->im_hsierra )
		{
			ivp->im_norr = 1;
		}
		else
		{
			ivp->im_norr = args.norr;
		}

		if ( args.uid >= MAXUID || args.uid < DEFUSR ||
		     args.gid >= MAXUID || args.gid < DEFGRP    )
		{
			return EINVAL;
		}

		ivp->im_mask = args.mask & 0777;
		ivp->im_uid  = args.uid;
		ivp->im_gid  = args.gid;
	}
	else
	{
		ivp->im_mask = 0;
		ivp->im_norr = ivp->im_hsierra;
		ivp->im_uid  = DEFUSR;
		ivp->im_gid  = DEFGRP;
	}

	return error;
}

/*
 * Common code for mount and mountroot
 */
static int
IsoMountFs ( struct vfs* vfsp, dev_t dev, char* path, cred_t* cr )
{
	IsoVfs_t				*ivp;
	struct buf				*bp;
	vnode_t					*devvp;
	vnode_t					*vndep;
	int						error;
	int						needclose;
	extern int				iso9660type;
	int						iso9660_bsize;
	int						iso9660_blknum;
	IsoVolumeDescriptor_t	*vdp;
	HsaVolumeDescriptor_t	*hdp;
	IsoPrimaryDescriptor_t	*pri;
	HsaPrimaryDescriptor_t	*h_pri;
	IsoDirectoryRecord_t	*rootp;
	int						logblksz;
	int						volspcsz;
	int						extent;
	int						hsierra;

#ifdef ISO9660_DEBUG
	printf ( "In IsoMountFs\n" );
#endif
	bp        = NULL;
	ivp       = (struct iso9660_vfs *)0;
	needclose = 0;

	if ( !(vfsp->vfs_flag & VFS_RDONLY) )
	{
		return EROFS;
	}

	/*
	Get the vnode for the open of the device
	and check whether it's a swap device?!
	*/

	devvp = makespecvp ( dev, VBLK );

	if ( error = VOP_OPEN(&devvp, FREAD, cr) )
	{
		return (error);
	}

	needclose = 1;

	if ( IS_SWAPVP(devvp) )
	{
		error = EBUSY;
		goto out;
	}


	vfsp->vfs_bcount      = 0;
    vfsp->vfs_fstype      = iso9660type;
    vfsp->vfs_dev         = dev;
    vfsp->vfs_fsid.val[0] = dev;
    vfsp->vfs_fsid.val[1] = iso9660type;

	/*
	This is the "logical sector size".  The standard says this
	should be 2048 or the physical sector size on the device,
	whichever is greater.  For now, we'll just use a constant.
	*/

	iso9660_bsize = 2048;

	for (iso9660_blknum = 16; iso9660_blknum < 100; iso9660_blknum++)
	{
		bp = bread( dev, iso9660_blknum, iso9660_bsize );

		if ( error = geterror(bp) )
		{
			goto out;
		}

		vdp = (IsoVolumeDescriptor_t *)bp->b_un.b_addr;
		hdp = (HsaVolumeDescriptor_t *)bp->b_un.b_addr;

		if (bcmp(hdp->id, HSIERRA_STANDARD_ID, sizeof hdp->id) == 0)
		{
			if ( IsoNum711(hdp->type) == ISO9660_VD_END )
			{
				error = EINVAL;
				goto out;
			}

			hsierra = 1;
			h_pri   = (HsaPrimaryDescriptor_t *) hdp;

			if ( IsoNum711(hdp->type) == ISO9660_VD_PRIMARY )
				break;

		}
		else if (bcmp(vdp->id, ISO9660_STANDARD_ID, sizeof vdp->id) == 0)
		{
			if (IsoNum711(vdp->type) == ISO9660_VD_END)
			{
				error = EINVAL;
				goto out;
			}

			hsierra = 0;
			pri     = (IsoPrimaryDescriptor_t *) vdp;

			if (IsoNum711(vdp->type) == ISO9660_VD_PRIMARY)
				break;
		}
	}

	if ( iso9660_blknum == 100 )
	{
		error = EINVAL;
		goto out;
	}
	
	if ( hsierra )
	{
		logblksz = IsoNum723(h_pri->logical_block_size);
		volspcsz = IsoNum733(h_pri->volume_space_size);
		rootp    = (IsoDirectoryRecord_t *)h_pri->root_directory_record;
	}
	else
	{
		logblksz = IsoNum723(pri->logical_block_size);
		volspcsz = IsoNum733(pri->volume_space_size);
		rootp    = (IsoDirectoryRecord_t *)pri->root_directory_record;

	}

	if (logblksz < DEV_BSIZE || logblksz >= MAXBSIZE ||
	    (logblksz & (logblksz - 1)) != 0)
	{
		error = EINVAL;
		goto out;
	}

	ivp = (struct iso9660_vfs *) kmem_zalloc(sizeof *ivp, KM_SLEEP);

	ivp->logical_block_size = logblksz;
	ivp->volume_space_size  = volspcsz;

	bcopy((caddr_t)rootp, (caddr_t)ivp->root, sizeof ivp->root);

	ivp->im_bsize  = logblksz;
	ivp->im_bmask  = ~(ivp->im_bsize - 1);
	ivp->im_bshift = 0;

	while ((1 << ivp->im_bshift) < ivp->im_bsize)
	{
		ivp->im_bshift++;
	}

	extent           = IsoNum733(rootp->extent);
	ivp->root_fileid = IsoLogBlkToSize(ivp, (IsoFileId_t)extent);
	ivp->root_size   = IsoNum733(rootp->size);

	bp->b_flags |= B_INVAL;
	brelse(bp);
	bp = NULL;

	vfsp->vfs_bsize = logblksz;
	vfsp->vfs_data  = (caddr_t) ivp;
	vfsp->vfs_flag |= VFS_RDONLY;

	ivp->im_vfsp    = vfsp;
	ivp->im_dev     = dev;
	ivp->im_devvp   = devvp;
	ivp->im_hsierra = hsierra;

	if (  error = IsoRoot ( vfsp, &vndep )  )
	{
		goto out;
	}

	/*
	All fine; finish up.
	*/

	vndep->v_flag |= VROOT;

	ivp->im_root   = vndep;

	ivp->im_path[0] = '\0';
	strncat ( ivp->im_path, path, sizeof(ivp->im_path) );

	return 0;

out:
	if (bp)
	{
		brelse(bp);
	}

	if (needclose)
	{
		(void)VOP_CLOSE ( devvp, FREAD, 1, 0, cr );
	}

	if (ivp)
	{
		kmem_free((caddr_t)ivp, sizeof *ivp);
	}

	return error;
}

/*
 * unmount system call
 */
int
IsoUnMount ( struct vfs* vfsp, cred_t* cr )
{
	IsoVfs_t	*ivp;
	IsoNode_t	*rip;
	vnode_t		*rvp;
	vnode_t		*bvp;
	dev_t		dev;
	int			error;

#ifdef ISO9660_DEBUG
	printf ( "In IsoUnMount\n" );
#endif
	if ( !suser(cr) )
	{
		return EPERM;
	}

	if ( IsoFlush(vfsp) )
	{
		return EBUSY;
	}

	ivp = VFSTOISO9660(vfsp);
	dev = vfsp->vfs_dev;

	rvp = ivp->im_root;
	rip = VTOI(rvp);

	ISOILOCK(rip);

	bvp = ivp->im_devvp;

	(void) VOP_PUTPAGE(bvp, 0, 0, B_INVAL, cr);

	if (  error = VOP_CLOSE(bvp, FREAD, 1, 0, cr )  )
	{
		ISOIUNLOCK(rip);
		return error;
	}

	VN_RELE(bvp);
	binval(dev);

	IsoPut    ( rip );
	IsoUnHash ( rip );

	kmem_free((caddr_t)ivp, sizeof *ivp);
	
	rvp->v_vfsp = 0;

	return 0;
}


/*
 * Return root of a filesystem vnode.
 */
int
IsoRoot ( struct vfs* vfsp, vnode_t** vpp )
{
	IsoFid_t			 ifid;
	IsoVfs_t			 *ivp;
	int					 error;
	int					 suoffset;
	struct buf			 *bp;
	struct susp_sp		 *sp;
	IsoDirectoryRecord_t *ep;
	int					 reclen;

#ifdef ISO9660_DEBUG
	printf ( "In IsoRoot\n" );
#endif
	ivp = VFSTOISO9660( vfsp );

	/*
	If the root vnode is known, we've
	been here before; fill the pointer
	mark it used and return success.
	*/

	if ( ivp->im_root )
	{
		VN_HOLD ( ivp->im_root );
		*vpp = ivp->im_root;

		return 0;
	}

	ifid.len    = sizeof ifid;
	ifid.fileid = ivp->root_fileid;

	if ( error = IsoFhToVp(vfsp, (struct fid *)&ifid, vpp) )
	{
		return error;
	}

	ISOIUNLOCK ( VTOI(*vpp) );

	/*
	If we won't RR or we are HS we're done.
	*/

	if ( ivp->im_norr || ivp->im_hsierra )
	{
		return 0;
	}

	bp = bread ( ivp->im_dev, 0, ivp->im_bsize );

	if ( error = geterror(bp) )
	{
		return error;
	}

	ep       = (IsoDirectoryRecord_t *) bp->b_un.b_addr;
	reclen   = IsoNum711(ep->length);
	suoffset = ROUNDUP(ISO9660_DIR_NAME + IsoNum711(ep->name_len), 2);

	if (suoffset + SUSP_SP_LEN > reclen)
	{
		brelse ( bp );
		return 0;
	}

	sp = (struct susp_sp *)(bp->b_un.b_addr + suoffset);

	if (sp->code[0] == 'S' && sp->code[1] == 'P' &&
	    IsoNum711(sp->length) >= SUSP_SP_LEN &&
	    sp->check[0] == 0xbe && sp->check[1] == 0xef)
	{
		ivp->im_have_rock_ridge = 1;	/* for sure; seems no standard ??? */
		ivp->im_rock_ridge_skip = IsoNum711(sp->len_skp);
	}

	brelse ( bp );
	return 0;
}


int
IsoSync()
{
	return 0;
}


int IsoVget ( struct vfs* vfsp, vnode_t** vpp, struct fid* fidp )
{ 
	int	error;

#ifdef ISO9660_DEBUG
	printf ( "In IsoVget\n" );
#endif

	if (  error = IsoFhToVp ( vfsp, fidp, vpp )  )
	{
		return error;
	}

	ISOIUNLOCK( VTOI(*vpp) );
	return 0;
}



int IsoSwapVp() { return 0; }
int IsoMountRoot() { return EINVAL; }


int
IsoStatVfs ( struct vfs* vfsp, struct statvfs* sp )
{
	IsoVfs_t	*ivp;
	int			cdmax;
	int			cdfree;

#ifdef ISO9660_DEBUG
	printf ( "In IsoStatVfs\n" );
#endif
	/*
	What fits on a cd, how many blocks will fit?
	74 minutes * 60 sec/min * 75 blocks/sec.
	*/

	ivp   = (struct iso9660_vfs *)(vfsp->vfs_data);
	cdmax = 74 * 60 * 75;

	if (cdmax < ivp->volume_space_size)
	{
		cdmax = ivp->volume_space_size;
	}

	cdfree = cdmax - ivp->volume_space_size;

	sp->f_bsize   = ivp->logical_block_size;
	sp->f_frsize  = ivp->logical_block_size;
	sp->f_blocks  = cdmax;
	sp->f_bfree   = cdfree;
	sp->f_bavail  = cdfree;
	sp->f_files   = 0;
	sp->f_ffree   = 0;
	sp->f_favail  = 0;
	sp->f_fsid    = ivp->im_dev;
	sp->f_namemax = NAME_MAX;      /* about */
	sp->f_flag    = vfsp->vfs_flag;

	sp->f_fstr[0] = '\0';
	strncat ( sp->f_fstr, ivp->im_path, sizeof(sp->f_fstr) );

	strcpy  ( sp->f_basetype, "iso9660" );

	return 0;
}

static int
IsoFhToVp ( struct vfs* vfsp, struct fid* fhp, vnode_t** vpp )
{
	struct ifid	 *ifhp;
	IsoVfs_t	 *ivp;
	IsoNode_t	 inode;
	IsoNode_t	 *nip;
	int			 error;
	IsoDirInfo_t *dirp;

#ifdef ISO9660_DEBUG
	printf ( "In IsoFhToVp\n" );
#endif
	dirp = NULL;

	ifhp = (struct ifid *)fhp;
	ivp  = VFSTOISO9660(vfsp);

	if ( ifhp->fileid >= IsoLogBlkToSize(ivp, ivp->volume_space_size) )
	{
		return EINVAL;
	}

	inode.i_vnode.v_vfsp = vfsp;
	inode.i_dev          = ivp->im_dev;
	inode.i_vfs		     = ivp;

	/* this just does a lookup in the cache */
	if ( !(error = IsoIget(&inode, ifhp->fileid, &nip, NULL)) )
	{
		*vpp = ITOV(nip);
		return (0);
	}

	if ( error != ENOENT )
	{
		return error;
	}

	/* not in inode cache ... must get directory info */

	IsoDirOpen ( (vnode_t*) NULL, ivp, ifhp->fileid, ISO9660_MAX_FILEID,
														0, 0, NULL, &dirp );

	if ( error = IsoDirGet (dirp) )
	{
		goto ret;
	}

	if (dirp->translated_fileid != ifhp->fileid)
	{
		error = EINVAL;
		goto ret;
	}

	if (error = IsoIget(&inode, ifhp->fileid, &nip, dirp))
	{
		goto ret;
	}

	*vpp = ITOV(nip);

ret:
	if (dirp)
	{
		IsoDirClose(dirp);
	}

	return (error);
}


/*
 * Vnode pointer to File handle
 *	NOT USED

ARGSUSED
static int
IsoVpToFh ( vnode_t* vp, struct fid* fhp )
{
	IsoNode_t	*ip;
	IsoFid_t	*ifhp;

#ifdef ISO9660_DEBUG
	printf ( "In IsoVpToFh\n" );
#endif
	ip           = VTOI(vp);
	ifhp         = (struct ifid *)fhp;
	ifhp->len    = sizeof(struct ifid);
	ifhp->fileid = ip->fileid;

	return (0);
}
*/


struct vfsops iso9660_vfsops = {
	IsoMount,
    IsoUnMount,
    IsoRoot,
    IsoStatVfs,
    IsoSync,
    IsoVget,
    IsoMountRoot,
    IsoSwapVp,     /* XXX - swapvp */
    fs_nosys,      /* filler */
    fs_nosys,
    fs_nosys,
    fs_nosys,
    fs_nosys,
    fs_nosys,
    fs_nosys,
    fs_nosys
};
