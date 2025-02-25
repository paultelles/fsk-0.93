/*
 *
 *  Adapted for System V Release 4	(ESIX 4.0.4)	
 *
 *  Gerard van Dorth	(gdorth@nl.oracle.com)
 *  Paul Bauwens		(paul@pphbau.atr.bso.nl)
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

#ident "@(#)pcfs_vfsops.c	1.7 93/10/17 "

#include "pcfs.h"
#ifdef SYSVR42
#include "sys/fs/snode.h"        /* for makespecvp */
#endif

int pcfs_mount(struct vfs *, vnode_t *, struct mounta *, cred_t *);
int pcfs_root(struct vfs *, vnode_t **);
int pcfs_statvfs(struct vfs *, struct statvfs *);
int pcfs_sync (struct vfs *, int, cred_t *);
int pcfs_vget(struct vfs *, vnode_t **, struct fid *);
int pcfs_unmount(struct vfs *, cred_t *);
int pcfs_mountroot(struct vfs *, enum whymountroot);

static int mountpcfs(struct vfs *, dev_t, char *, cred_t *);


int
pcfs_mount ( struct vfs *vfsp, vnode_t *mvp, struct mounta *uap, cred_t *cr )
{
	dev_t				dev;
	struct pathname		dpn;	/* directory point to mount */
	vnode_t				*bvp;	/* vnode for blk device to mount */
	struct vfs			*found;
	pcfsmnt_args_t		args;	/* arguments for this mount call */
	pcfs_vfs_t*			pvp;
	int					error;

	/*
	Trivial checks first:
		- mount only allowed on directories
		- only super user can mount
		- mount on a not already mounted place
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

	if (uap->flags & MS_RDONLY)
	{
		vfsp->vfs_flag |= VFS_RDONLY;
	}

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
	Time to get dirty.
	*/

	error = mountpcfs ( vfsp, dev, dpn.pn_path, cr );

	pn_free( &dpn );

	if ( error )
	{
		return error;
	}

	/*
	Copy in the args for the mount request,
	if there are some. The following actions
	have been defined:
		- set uid
		- set gid
		- set mode mask
	*/

	pvp = PCFS_VFS ( vfsp );

	if ( uap->dataptr )
	{
		error = copyin ( uap->dataptr, (caddr_t)&args, sizeof args );

		if ( error )
		{
			return error;
		}

		if ( args.uid >= MAXUID || args.uid < DEFUSR ||
		     args.gid >= MAXUID || args.gid < DEFGRP    )
		{
			return EINVAL;
		}

		pvp->vfs_uid  = args.uid;
		pvp->vfs_gid  = args.gid;
		pvp->vfs_mask = args.mask & 0777;
	}
	else
	{
		pvp->vfs_uid  = DEFUSR;
		pvp->vfs_gid  = DEFGRP;
		pvp->vfs_mask = 0;
	}

	return error;
}


static int
mountpcfs ( struct vfs *vfsp, dev_t dev, char *path, cred_t *cr )
{
	int					i;
	int					bpc;
	int					bit;
	int					error;
	int					needclose;
	int					ronly;
	union bootsector	*bsp;
	lbuf_t				*bp0;
	struct byte_bpb33	*b33;
	struct byte_bpb50	*b50;
	vnode_t				*devvp;
	pcfs_vfs_t*			pvp;		/* private pcfs info */
	denode_t*			ndep;
	vnode_t*			vndep;
	extern int			pcfstype;

	/*
	Get the device vnode and check whether it's
	not used as swap device.
	*/

	devvp = makespecvp ( dev, VBLK );
	ronly = (vfsp->vfs_flag & VFS_RDONLY) != 0;

	if ( error = VOP_OPEN(&devvp, ronly ? FREAD : FREAD|FWRITE, cr) )
	{
		return (error);
	}

	needclose = 1;

	if ( IS_SWAPVP(devvp) )
	{
		error = EBUSY;
		goto error_exit;
	}

	vfsp->vfs_bcount      = 0;
	vfsp->vfs_fstype      = pcfstype;
	vfsp->vfs_dev         = dev;
	vfsp->vfs_fsid.val[0] = dev;
	vfsp->vfs_fsid.val[1] = pcfstype;

	/*
	Flush back any dirty pages on the block device to
	try and keep the buffer cache in sync with the page
	cache if someone is trying to use block devices when
	they really should be using the raw device.
	*/

	(void) VOP_PUTPAGE(devvp, 0, 0, B_INVAL, cr);
	binval(dev);

	/*
	Read the boot sector of the filesystem, and then
	check the boot signature.  If not a dos boot sector
	then error out.  We could also add some checking on
	the bsOemName field.  So far I've seen the following
	values:
		"IBM  3.3"
		"MSDOS3.3"
		"MSDOS5.0"
	*/

	bp0 = lbread( dev, 0, 512 );

	if ( error = lgeterror(bp0) )
	{
		goto error_exit;
	}

	bsp = (union bootsector *)bp0->b_un.b_addr;
	b33 = (struct byte_bpb33 *)bsp->bs33.bsBPB;
	b50 = (struct byte_bpb50 *)bsp->bs50.bsBPB;

	if (bsp->bs50.bsBootSectSig != BOOTSIG)
	{
		error = EINVAL;
		goto error_exit;
	}

	pvp = (struct pcfs_vfs *)kmem_zalloc(sizeof *pvp, KM_SLEEP);

	if (pvp == NULL)
	{
		error = ENOMEM;
		goto error_exit;
	}

	vfsp->vfs_data = (caddr_t) pvp;

	if ( ronly )
	{
		vfsp->vfs_flag |= VFS_RDONLY;
	}

	pvp->vfs_vfsp     = vfsp;
	pvp->vfs_inusemap = NULL;

	/*
	Compute several useful quantities from the bpb in
	the bootsector.  Copy in the dos 5 variant of the
	bpb then fix up the fields that are different between
	dos 5 and dos 3.3.
	*/

	pvp->vfs_BytesPerSec  = getushort(b50->bpbBytesPerSec);
	pvp->vfs_SectPerClust = b50->bpbSecPerClust;
	pvp->vfs_ResSectors   = getushort(b50->bpbResSectors);
	pvp->vfs_FATs         = b50->bpbFATs;
	pvp->vfs_RootDirEnts  = getushort(b50->bpbRootDirEnts);
	pvp->vfs_Sectors      = getushort(b50->bpbSectors);
	pvp->vfs_Media        = b50->bpbMedia;
	pvp->vfs_FATsecs      = getushort(b50->bpbFATsecs);
	pvp->vfs_SecPerTrack  = getushort(b50->bpbSecPerTrack);
	pvp->vfs_Heads        = getushort(b50->bpbHeads);

	if (bsp->bs50.bsOemName[5] == '5')
	{
		pvp->vfs_HiddenSects = getulong(b50->bpbHiddenSecs);
		pvp->vfs_HugeSectors = getulong(b50->bpbHugeSectors);
	}
	else
	{
		pvp->vfs_HiddenSects = getushort(b33->bpbHiddenSecs);
		pvp->vfs_HugeSectors = 0;
	}

	if (pvp->vfs_Sectors != 0)
		pvp->vfs_HugeSectors = pvp->vfs_Sectors;

	pvp->vfs_fatblk     = pvp->vfs_ResSectors;
	pvp->vfs_rootdirblk = pvp->vfs_fatblk +
	    (pvp->vfs_FATs * pvp->vfs_FATsecs);

	/*
	Size in sectors.
	*/

	pvp->vfs_rootdirsize = (pvp->vfs_RootDirEnts * sizeof(struct direntry))
	    												/ pvp->vfs_BytesPerSec;
	pvp->vfs_firstcluster = pvp->vfs_rootdirblk + pvp->vfs_rootdirsize;
	pvp->vfs_nmbrofclusters = (pvp->vfs_HugeSectors - pvp->vfs_firstcluster)
														/ pvp->vfs_SectPerClust;
	pvp->vfs_maxcluster = pvp->vfs_nmbrofclusters + 1;

	/*
	360K PC floppies have a 7-sector root directory of all things!
	so skip sanity check in this case.
	*/	

	if ( pvp->vfs_rootdirsize != 7 &&
		 pvp->vfs_rootdirsize % (long)pvp->vfs_SectPerClust )
	{
		cmn_err ( CE_WARN, "mountpcfs: root directory is not "
			"a multiple of the clustersize in length\n" );
	}	

	/*
	Compute mask and shift value for isolating cluster relative
	byte offsets and cluster numbers from a file offset.
	*/

	bpc                = pvp->vfs_SectPerClust * pvp->vfs_BytesPerSec;
	pvp->vfs_bpcluster = bpc;
	pvp->vfs_depclust  = bpc / sizeof(struct direntry);
	pvp->vfs_crbomask  = bpc - 1;

	if (bpc == 0)
	{
		error = EINVAL;
		goto error_exit;
	}

	for (bit = 1, i = 0; i < 32; i++)
	{
		if (bit & bpc)
		{
			if (bit ^ bpc)
			{
				error = EINVAL;
				goto error_exit;
			}
			pvp->vfs_cnshift = i;
			break;
		}
		bit <<= 1;
	}

	vfsp->vfs_bsize = bpc;

	pvp->vfs_brbomask = 0x01ff; /* 512 byte blocks only (so far) */
	pvp->vfs_bnshift  = 9;	    /* shift right 9 bits to get bn  */

	/*
	Release the bootsector buffer.
	*/

	lbrelse(bp0);
	bp0 = NULL;

	/*
	Allocate memory for the bitmap of allocated clusters,
	and then fill it in.
	*/

	pvp->vfs_inusemap = kmem_alloc((pvp->vfs_maxcluster>>3) + 1, KM_SLEEP);

	if (!pvp->vfs_inusemap)
	{
		error = ENOMEM;
		goto error_exit;
	}

	/*
	fillinusemap() needs vfs_dev.
	*/

	pvp->vfs_devvp = devvp;
	pvp->vfs_dev   = dev;

	/*
	Have the inuse map filled in.
	*/

	error = fillinusemap(pvp);

	if (error)
	{
		goto error_exit;
	}

	pvp->vfs_ronly = ronly;

	if (ronly == 0)
	{
		pvp->vfs_fmod = 1;
	}


	/*
	Finish up.
	*/

	if ( error = deget(pvp, ATTR_DIRECTORY, 0, 0, PCFSROOT, 0, &ndep) )
	{
		goto error_exit;
	}


	vndep = DETOV( ndep );
	vndep->v_flag |= VROOT;

	pvp->vfs_root  = vndep;

	pvp->path[0] = '\0';
	strncat ( pvp->path, path, sizeof(pvp->path) );

	DEUNLOCK ( ndep );

	return 0;

error_exit:
	;
	if (bp0)
	{
		lbrelse(bp0);
	}

	if ( needclose )
	{
		(void) VOP_CLOSE(devvp, ronly ? FREAD : FREAD|FWRITE, 1, 0, cr);
		binval(dev);
	}

	if (pvp)
	{
		if (pvp->vfs_inusemap)
		{
			kmem_free((caddr_t)pvp->vfs_inusemap,
			    (pvp->vfs_maxcluster >> 3) + 1);
		}

		kmem_free((caddr_t)pvp, sizeof *pvp);
	}

	return error;
}

int
pcfs_root ( struct vfs *vfsp, vnode_t **vpp )
{
	denode_t	*ndep;
	pcfs_vfs_t	*pvp;
	int			error;

	pvp   = PCFS_VFS(vfsp);
	error = deget(pvp, ATTR_DIRECTORY, 0, 0, PCFSROOT, 0, &ndep);

	if ( error == 0 )
	{
		*vpp = DETOV(ndep);
		DEUNLOCK ( ndep );
	}

	return error;
}


int
pcfs_statvfs ( struct vfs *vfsp, struct statvfs *sp )
{
	pcfs_vfs_t *pvp;

	pvp = PCFS_VFS(vfsp);

	sp->f_bsize   = pvp->vfs_bpcluster;
	sp->f_frsize  = pvp->vfs_bpcluster;
	sp->f_blocks  = pvp->vfs_nmbrofclusters;
	sp->f_bfree   = pvp->vfs_freeclustercount;
	sp->f_bavail  = pvp->vfs_freeclustercount;
	sp->f_files   = pvp->vfs_nmbrofclusters/2;
	sp->f_ffree   = pvp->vfs_freeclustercount/2; /* what to put here? !0 */
	sp->f_favail  = pvp->vfs_freeclustercount/2; /* what to put here? !0 */
	sp->f_fsid    = pvp->vfs_dev;
	sp->f_namemax = DOSDIRSIZ;	/* about */

	sp->f_flag    = vfsp->vfs_flag;

	sp->f_fstr[0] = '\0';
	strncat ( sp->f_fstr, pvp->path, sizeof(sp->f_fstr) );

	strcpy  ( sp->f_basetype, "pcfs" );

	return 0;
}



static	int	pcfs_snclck;	/* set if sync locked */
static	int	pcfs_sncwnt;	/* set if sync wanted */

/*ARGSUSED*/
int 
pcfs_sync ( struct vfs *vfsp, int flag, cred_t *cr )
{
	while ( pcfs_snclck )
	{
		pcfs_sncwnt = 1;
		sleep ( (caddr_t)&pcfs_snclck, PINOD );
	}

	pcfs_snclck = 1;
	pcfs_deflush();
	pcfs_snclck = 0;

	if ( pcfs_sncwnt )
	{
		pcfs_sncwnt = 0;
		wakeup ( (caddr_t)&pcfs_snclck );
	}

	return 0;
}


int
pcfs_vget ( struct vfs *vfsp, vnode_t **vpp, struct fid *fidp )
{
	dfid_t*		 dfid;
	denode_t*	 dp;
	pcfs_vfs_t*  pvp;
	int			 isadir;


#ifdef PCFS_DEBUG
	printf ( "In pcfs_vget\n" );
#endif

	dfid = (dfid_t *) fidp;
	pvp  = PCFS_VFS ( vfsp );

	if ( dfid->dfid_diroffset & PCFS_FID_DIRFLAG )
	{
		isadir	= 1;
	}
	else 
	{
		isadir	= 0;
	}

	if ( deget(pvp, isadir, (u_long)dfid->dfid_dirclust,
	    (u_long)dfid->dfid_diroffset & ~PCFS_FID_DIRFLAG,
		(u_long)dfid->dfid_startclust, NULL, &dp) )
	{
		*vpp = NULL;
		return 0;
	}

	/*
	If the generation number changed
	we know we have an improper file.
	Reject the denode but don't use
	the inactivate path here!
	*/

	if ( DE_GEN(dp) != dfid->dfid_gen )
	{
		dereject ( dp );

		*vpp = NULL;
		return 0;
	}

	DEUNLOCK(dp);

	*vpp = DETOV ( dp );
	return 0;
}



/*
 *  Unmount the filesystem described by mp.
 */
int
pcfs_unmount ( struct vfs *vfsp, cred_t *cr )
{
	int			error;
	dev_t		dev;
	pcfs_vfs_t	*pvp;	/* private pcfs info */
	vnode_t	 	*bvp;	/* blk dev vnode pointer */
	vnode_t	 	*rvp;	/* root vnode pointer */
	denode_t	*rdp;	/* inode root dir */

	if ( !suser(cr) )
	{
		return EPERM;
	}

	if ( deflush(vfsp,0) < 0  )
	{
		return EBUSY;
	}

	pvp = PCFS_VFS(vfsp);
	dev = vfsp->vfs_dev;

	/*
	Flush the root inode to disk.
	*/

	rvp = pvp->vfs_root;
	rdp = VTODE(rvp);

	DELOCK(rdp);

	/*
	At this point there should be no active files on the
	file system, and the super block should not be locked.
	Break the connections.
	*/

	if ( !(vfsp->vfs_flag & VFS_RDONLY) )
	{
		bflush ( dev );
		bdwait();
	}

	bvp = pvp->vfs_devvp;

	(void) VOP_PUTPAGE(bvp, 0, 0, B_INVAL, cr);

	if (error = VOP_CLOSE(bvp, (vfsp->vfs_flag & VFS_RDONLY) ?
	    FREAD : FREAD|FWRITE, 1, (off_t) 0, cr))
	{
		DEUNLOCK(rdp);

		return error;
	}

	VN_RELE(bvp);
	binval(dev);

	deput(rdp);
	deunhash(rdp);

	kmem_free((caddr_t)pvp->vfs_inusemap, (pvp->vfs_maxcluster >> 3) + 1);
	kmem_free((caddr_t)pvp, sizeof(struct pcfs_vfs));

	rvp->v_vfsp = 0;

	return 0;
}

/*ARGSUSED*/
int
pcfs_mountroot ( struct vfs *vfsp, enum whymountroot why )
{
	return EINVAL;
}


struct vfsops pcfs_vfsops = {
	        pcfs_mount,
	        pcfs_unmount,
	        pcfs_root,
	        pcfs_statvfs,
	        pcfs_sync,
	        pcfs_vget,
	        pcfs_mountroot,
	        fs_nosys,       /* swapvp */
	        fs_nosys,       /* filler */
	        fs_nosys,
	        fs_nosys,
	        fs_nosys,
	        fs_nosys,
	        fs_nosys,
	        fs_nosys,
	        fs_nosys,
};
