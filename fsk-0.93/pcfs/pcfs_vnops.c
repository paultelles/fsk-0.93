/*
 *
 *  Adapted for System V Release 4	(ESIX 4.0.4)	
 *
 *  Gerard van Dorth	(gdorth@nl.oracle.com)
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

#ident "@(#)pcfs_vnops.c	1.7 93/10/17 "

#include "pcfs.h"

/*
 *  Some general notes:
 *
 *  In the ufs filesystem the inodes, superblocks, and indirect
 *  blocks are read/written using the vnode for the filesystem.
 *  Blocks that represent the contents of a file are read/written
 *  using the vnode for the file (including directories when
 *  they are read/written as files).
 *  This presents problems for the dos filesystem because data
 *  that should be in an inode (if dos had them) resides in the
 *  directory itself.  Since we must update directory entries
 *  without the benefit of having the vnode for the directory
 *  we must use the vnode for the filesystem.  This means that
 *  when a directory is actually read/written (via read, write,
 *  or readdir, or seek) we must use the vnode for the filesystem
 *  instead of the vnode for the directory as would happen in ufs.
 *  This is to insure we retreive the correct block from the
 *  buffer cache since the hash value is based upon the vnode
 *  address and the desired block number.
 */

/*
 *  Create a regular file.
 *  On entry the directory to contain the file being
 *  created is locked.  We must release before we
 *  return.
 *  We must also free the pathname buffer pointed at
 *  by ndp->ni_pnbuf, always on error, or only if the
 *  SAVESTART bit in ni_nameiop is clear on success.
 */


int
pcfs_create ( vnode_t *dvp, char *name, struct vattr *vap, enum vcexcl excl,
			  int mode, vnode_t **vpp, cred_t *cr )
{
	denode_t	ndirent;
	dosdirent_t	*ndirp = &ndirent.de_de;
	denode_t	*dep;
	denode_t	*dp;
	vnode_t		*vp;
	denode_t	*pdep = VTODE(dvp);
	int			isadir;
	int			error = 0;

#ifdef PCFS_DEBUG
	printf ( "In pcfs_create\n" );
#endif

	/*
	Are we allowed to update the directory?
	*/

	if (  error = pcfs_access( dvp, VWRITE, 0, cr )  )
	{
		return error;
	}

	/*
	Check what they want us to do; we are not creating
	different things as files and directories.
	We return EINVAL on other stuff ... not according
	manual.
	*/

	switch ( vap->va_type )  {

	case  VREG:	
			isadir = 0;
			break;

	case  VDIR:	
			isadir = 1;
			break;

	default:	
			return EINVAL;

	}


	DELOCK ( pdep );

	/*
	Check whether the name exists,
	that might be the case.
	*/

	if ( error = pcfs_lookup(dvp, name, vpp, 0, GETFREESLOT, 0, NOCRED) )
	{
		if ( error != ENOENT )
		{
			DEUNLOCK ( pdep );
			return error;
		}
	}
	else
	{
		vp = *vpp;
		dp = VTODE ( vp );

		DELOCK ( dp );

		error = EEXIST;
	}

	/*
	We can accept non exclusive creates of
	existing files.
	*/

	if ( error == EEXIST )
	{
		if (excl == NONEXCL)
		{
			if ( ISADIR( dp ) && (mode & IWRITE) )
			{
				error = EISDIR;
			}
			else if (mode)
			{
				error = pcfs_access ( vp, mode, 0, cr );
			}
			else
			{
				error = 0;
			}
		}

		/*
		For regular files, we have to truncate the
		file to zero bytes, if specified.
		*/

		if ( !error && !ISADIR( dp ) && (vap->va_mask & AT_SIZE) &&
		     vap->va_size == 0 )
		{
			error = detrunc ( dp, 0, 0 );
		}

		if ( error )
		{
			deput( dp );
			DEUNLOCK ( pdep );
			return error;
		}

		DEUNLOCK ( dp );
		DEUNLOCK ( pdep );
		return 0;
	}

	/*
	Note that we call pcfs_mkdir in case of a creating
	directory command. This make . and .., which seems
	to be NONE standard for mknod ... but better for
	DOS.
	*/

	if ( isadir )
	{
		error = pcfs_mkdir ( dvp, name, vap, vpp, cr );

		DEUNLOCK ( pdep );
		return error;
	}

	/*
	Create a directory entry for the file, then call
	createde() to have it installed.
	NOTE: DOS files are always executable.  We use the
	absence of the owner write bit to make the file readonly.
	*/

	bzero((caddr_t)&ndirent, sizeof(ndirent));

	unix2dostime(&hrestime, (union dosdate *)&ndirp->deDate,
	    (union dostime *)&ndirp->deTime);
	unix2dosfn((u_char *)name, ndirp->deName, strlen(name));

	ndirp->deAttributes   = (vap->va_mode & VWRITE) ? 0 : ATTR_READONLY;
	ndirp->deStartCluster = 0;
	ndirp->deFileSize     = 0;

	ndirent.de_vfs   = pdep->de_vfs;
	ndirent.de_dev   = pdep->de_dev;
	ndirent.de_devvp = pdep->de_devvp;

	if ((error = createde(&ndirent, dvp, &dep)) == 0)
	{
		DEUNLOCK ( dep );
		*vpp = DETOV(dep);
	}

	DEUNLOCK ( pdep );
	return error;
}

/*
 *  Since DOS directory entries that describe directories
 *  have 0 in the filesize field, we take this opportunity (open)
 *  to find out the length of the directory and plug it
 *  into the denode structure.
 */
/*ARGSUSED*/
int
pcfs_open ( vnode_t **vpp, int flag, cred_t *cr )
{
	int			error = 0;
	u_long 		sizeinclusters;
	denode_t	*dep = VTODE(*vpp);

#ifdef PCFS_DEBUG
	printf ( "In pcfs_open\n" );
#endif
	if (  ISADIR( dep )  )
	{
		DELOCK ( dep );

		error = pcbmap(dep, 0xffff, 0, &sizeinclusters);

		if (error == E2BIG)
		{
			dep->de_FileSize = sizeinclusters * dep->de_vfs->vfs_bpcluster;
			error = 0;
		}
		else
		{
			cmn_err( CE_WARN, "pcfs_open: pcbmap returned %d\n", error);
		}

		DEUNLOCK ( dep );
	}

	return error;
}

/*ARGSUSED*/
int
pcfs_close ( vnode_t *vp, int flag, int count, off_t offset, cred_t *cr )
{
	struct denode *dep = VTODE(vp);

#ifdef PCFS_DEBUG
	printf ( "In pcfs_close\n" );
#endif
	DELOCK ( dep );
	DETIMES ( dep, &hrestime );
	cleanlocks ( vp, u.u_procp->p_epid, u.u_procp->p_sysid );
	DEUNLOCK ( dep );
	return 0;
}

/*ARGSUSED*/
int
pcfs_access ( vnode_t *vp, int mode, int flags, cred_t *cr )
{
	int			dosmode;
	uid_t		uid;
	gid_t		gid;
	denode_t	*dep;
	pcfs_vfs_t	*pvp;

#ifdef PCFS_DEBUG
	printf ( "In pcfs_access\n" );
#endif
	dep = VTODE(vp);
	pvp = dep->de_vfs;

	/*
	Root gets to do anything.  Even execute a file
	without the x-bit on?  But, for dos filesystems
	every file is executable.  I may regret this.
	*/

	if ( suser(cr) )
	{
		return 0;
	}

	if ( pvp->vfs_uid != DEFUSR )	uid = pvp->vfs_uid;
	else							uid = 0;

	if ( pvp->vfs_gid != DEFGRP )	gid = pvp->vfs_gid;
	else							gid = 0;

	if ( dep->de_Attributes & ATTR_READONLY )	dosmode = 0555;
	else										dosmode = 0777;

	dosmode &= ~pvp->vfs_mask;

	/*
	Access check is based on only one of owner, group, public.
	If not owner, then check group. If not a member of the
	group, then check public access.
	*/

	if (cr->cr_uid != uid)
	{
		mode >>= 3;

		if ( !groupmember(gid, cr) )
		{
			mode >>= 3;
		}
	}

	if ( (dosmode & mode) == mode )
		return 0;

	return EACCES;
}

/*ARGSUSED*/
int
pcfs_getattr ( vnode_t *vp, struct vattr *vap, int flags, cred_t *cr )
{
	u_int		cn;
	denode_t	*dep;
	pcfs_vfs_t	*pvp;

#ifdef PCFS_DEBUG
	printf ( "In pcfs_getattr\n" );
#endif
	dep = VTODE(vp);
	pvp = dep->de_vfs;

	DETIMES ( dep, &hrestime );

	/*
	The following computation of the fileid must be the
	same as that used in pcfs_readdir() to compute d_fileno.
	If not, pwd doesn't work.
	*/

	if (dep->de_Attributes & ATTR_DIRECTORY)
	{
		if ((cn = dep->de_StartCluster) == PCFSROOT)
			cn = 1;
	}
	else
	{
		if ((cn = dep->de_dirclust) == PCFSROOT)
			cn = 1;
		cn = (cn << 16) | (dep->de_diroffset & 0xffff);
	}

	vap->va_nodeid = cn;	/* ino like; identification of this file */
	vap->va_fsid   = dep->de_dev;
	vap->va_mode   = (dep->de_Attributes & ATTR_READONLY) ? 0555 : 0777;

	if (dep->de_Attributes & ATTR_DIRECTORY)
	{
		vap->va_mode |= S_IFDIR;
	}

	vap->va_mode &= ~pvp->vfs_mask;

	if ( pvp->vfs_uid != DEFUSR )	vap->va_uid = pvp->vfs_uid;
	else							vap->va_uid = 0;

	if ( pvp->vfs_gid != DEFGRP )	vap->va_gid = pvp->vfs_gid;
	else							vap->va_gid = 0;

	vap->va_nlink = 1;
	vap->va_rdev  = 0;
	vap->va_size  = dep->de_FileSize;

	dos2unixtime((union dosdate *)&dep->de_Date,
	    (union dostime *)&dep->de_Time, &vap->va_atime);

	vap->va_atime.tv_sec  = vap->va_atime.tv_sec;
	vap->va_atime.tv_nsec = 0;
	vap->va_mtime.tv_sec  = vap->va_atime.tv_sec;
	vap->va_mtime.tv_nsec = 0;
	vap->va_ctime.tv_sec  = vap->va_atime.tv_sec;
	vap->va_ctime.tv_nsec = 0;

	vap->va_type    = vp->v_type;
	vap->va_blksize = dep->de_vfs->vfs_bpcluster;
	vap->va_nblocks = (dep->de_FileSize + vap->va_blksize-1) / vap->va_blksize;

	return 0;
}


/*ARGSUSED*/
int
pcfs_setattr ( vnode_t *vp, struct vattr *vap, int flags, cred_t *cr )
{
	long		mask;
	int			error;
	denode_t	*dep;

#ifdef PCFS_DEBUG
	printf ( "In pcfs_setattr\n" );
#endif
	mask  = vap->va_mask;
	error = 0;
	dep   = VTODE(vp);

	/*
	Never allow nonsense ...
	*/

	if ( mask & AT_NOSET )
	{
		return EINVAL;
	}

	/*
	Setting uid/gid is defined as a no-op for pcfs.
	this allows you to use tar/cpio without massive
	warnings. 
	This is not ideal, but surely more useful.
	*/

	if ( mask & (AT_UID | AT_GID) )
	{
		return 0;
	}

	DELOCK ( dep );

	if ( mask & AT_MODE )
	{
		/* We ignore the read and execute bits */
		if (vap->va_mode & VWRITE)
		{
			dep->de_Attributes &= ~ATTR_READONLY;
		}
		else
		{
			dep->de_Attributes |= ATTR_READONLY;
		}

		dep->de_flag |= DEUPD;
	}

	if ( mask & AT_SIZE )
	{
		if (vp->v_type == VDIR)
		{
			error = EISDIR;
			goto out;
		}

		if ( IS_SWAPVP(vp) )
		{
			error = EBUSY;
			goto out;
		}

		if (error = detrunc(dep, vap->va_size, IO_SYNC))
			goto out;
	}

	if ( mask & (AT_ATIME | AT_MTIME) )
	{
		dep->de_flag |= DEUPD;
		error = deupdat ( dep, &vap->va_mtime, 0 );
	}
	else
	{
		error = deupdat ( dep, &hrestime, 0 );
	}

out:
	;
	DEUNLOCK ( dep );

	return error;
}


/*ARGSUSED*/
int
pcfs_read ( vnode_t *vp, struct uio *uio, int ioflag, cred_t *cr )
{
	int			error;
	int			diff;
	int			isadir;
	long		n;
	long		on;
	u_long		lcn;
	u_long		lbn;
	lbuf_t		*bp;
	denode_t	*dep = VTODE(vp);
	pcfs_vfs_t	*pvp = dep->de_vfs;

#ifdef PCFS_DEBUG
	printf ( "In pcfs_read\n" );
#endif
	ASSERT ( dep->de_flag & DELOCKED );

	/*
	If they didn't ask for any data, then we
	are done.
	*/

	if (uio->uio_resid == 0)
		return 0;

	if (uio->uio_offset < 0)
		return EINVAL;

	isadir = ISADIR( dep );

	do {
		lcn = uio->uio_offset >> pvp->vfs_cnshift;
		on  = uio->uio_offset &  pvp->vfs_crbomask;
		n   = MIN((unsigned)(pvp->vfs_bpcluster - on), uio->uio_resid);

		diff = dep->de_FileSize - uio->uio_offset;

		if (diff <= 0)
			return 0;

		if (diff < n)
			n = diff;

		/*
		Convert cluster number to block number.
		*/
		if ( error = pcbmap(dep, lcn, &lbn, 0) )
			return error;

		/*
		If we are operating on a directory file then be
		sure to do i/o with the vnode for the filesystem
		instead of the vnode for the directory.
		*/

		if ( isadir )
		{
			bp    = lbread(pvp->vfs_dev, lbn, pvp->vfs_bpcluster);
			error = lgeterror(bp);
		}
		else
		{
			u_long		raclust;
			u_long		rablock;

			raclust = lcn + 1;

			if (dep->de_lastr + 1 == lcn  &&
			    raclust * pvp->vfs_bpcluster < dep->de_FileSize)
			{
				if ( error = pcbmap(dep, raclust, &rablock, 0) )
					return error;

				bp    = lbreada(vp->v_rdev, lbn, rablock, pvp->vfs_bpcluster);
				error = lgeterror(bp);
			}
			else
			{
				bp    = lbread ( vp->v_rdev, lbn, pvp->vfs_bpcluster );
				error = lgeterror(bp);
			}

			dep->de_lastr = lcn;
		}

		n = MIN(n, pvp->vfs_bpcluster);

		if (error)
		{
			lbrelse(bp);
			return error;
		}

		error = uiomove(bp->b_un.b_addr + on, (int)n, UIO_READ, uio);

		lbrelse(bp);

	} while (error == 0  &&  uio->uio_resid > 0  && n != 0);

	DETIMES ( dep, &hrestime );

	return error;
}

/*
 *  Write data to a file or directory.
 */

/*ARGSUSED*/
int
pcfs_write ( vnode_t *vp, struct uio *uio, int ioflag, cred_t *cr )
{
	int			n;
	int			croffset;
	int	 		error;
	u_long		bn;
	lbuf_t 		*bp;
	rlim_t		limit = uio->uio_limit;
	denode_t	*dep = VTODE(vp);
	pcfs_vfs_t	*pvp = dep->de_vfs;


#ifdef PCFS_DEBUG
	printf ( "In pcfs_write\n" );
#endif
	ASSERT ( dep->de_flag & DELOCKED );

	ASSERT ( vp->v_type == VREG );

	if (ioflag & IO_APPEND)
	{
		uio->uio_offset = dep->de_FileSize;
	}

	if (uio->uio_offset < 0)
	{
		return EINVAL;
	}

	if (uio->uio_resid == 0)
		return 0;

	/*
	If they've exceeded their filesize limit, tell them about it.
	*/

	if ( uio->uio_offset + uio->uio_resid > limit )
	{
		return EFBIG;
	}

	/*
	If attempting to write beyond the end of the root
	directory we stop that here because the root directory
	can not grow.
	*/

	if ( ISADIR(dep) && dep->de_StartCluster == PCFSROOT  &&
	    (uio->uio_offset+uio->uio_resid) > dep->de_FileSize)
	{
		return ENOSPC;
	}

	/*
	If the offset we are starting the write at is beyond the
	end of the file, then they've done a seek.  Unix filesystems
	allow files with holes in them, DOS doesn't so we must
	fill the hole with zeroed blocks.  We do this by calling
	our seek function.  This could probably be cleaned up
	someday.
	*/

	if (uio->uio_offset > dep->de_FileSize)
	{
		if (  error = pcfs_seek(vp, (off_t)0, (off_t *)uio->uio_offset)  )
		{
			return error;
		}
	}


	do {
		bn = uio->uio_offset >> pvp->vfs_cnshift;

		/*
		If we are appending to the file and we are on a
		cluster boundary, then allocate a new cluster
		and chain it onto the file.
		*/

		if (uio->uio_offset == dep->de_FileSize  &&
		    (uio->uio_offset & pvp->vfs_crbomask) == 0)
		{
			if (error = extendfile(dep, &bp, 0))
				break;
		}
		else
		{
			/*
			The block we need to write into exists,
			so just read it in.
			*/

			if ( error = pcbmap(dep, bn, &bn, 0) )
				return error;

			bp = lbread ( vp->v_rdev, bn, pvp->vfs_bpcluster );

			if ( error = lgeterror(bp) )
			{
				lbrelse(bp);
				return error;
			}
		}

		croffset = uio->uio_offset & pvp->vfs_crbomask;
		n        = MIN(uio->uio_resid, pvp->vfs_bpcluster-croffset);

		if (uio->uio_offset+n > dep->de_FileSize)
		{
			dep->de_FileSize = uio->uio_offset + n;
		}

		/*
		Copy the data from user space into the buf header.
		*/

		error = uiomove(bp->b_un.b_addr+croffset, n, UIO_WRITE, uio);

		/*
		If they want this synchronous then write it and wait
		for it.  Otherwise, if on a cluster boundary write it
		asynchronously so we can move on to the next block
		without delay.  Otherwise do a delayed write because
		we may want to write somemore into the block later.
		*/

		if ( (ioflag & IO_SYNC) || IS_SWAPVP(vp) )
		{
			lbwrite(bp);
		}
		else if (n + croffset == pvp->vfs_bpcluster)
		{
			lbawrite(bp);
		}
		else
		{
			lbdwrite(bp);
		}

		dep->de_flag |= DEUPD;

	} while (error == 0  &&  uio->uio_resid > 0);

	if ( !error && (ioflag & IO_SYNC) )
	{
		error = deupdat(dep, &hrestime, 1);
	}

	DETIMES ( dep, &hrestime );

	return error;
}

/*ARGSUSED*/
int
pcfs_ioctl ( vnode_t *vp, int com, caddr_t data, int fflag, cred_t *cr,
			 int *rvalp )
{
#ifdef PCFS_DEBUG
	PcfsCheckInodes();
#endif
	return ENOTTY;
}


/*ARGSUSED*/
int
pcfs_mmap ( vnode_t *vp, int fflags, cred_t *cred, struct proc *p )
{
	return EINVAL;
}

/*
 *  Flush the blocks of a file to disk.
 *
 *  This function is worthless for vnodes that represent
 *  directories.
 *  Maybe we could just do a sync if they try an fsync
 *  on a directory file.
 */

/*ARGSUSED*/
int
pcfs_fsync ( vnode_t *vp, cred_t *cr )
{
	struct denode *dep = VTODE(vp);

#ifdef PCFS_DEBUG
	printf ( "In pcfs_fsync\n" );
#endif
	dep->de_flag |= DEUPD;

	return deupdat ( dep, &hrestime, 1 );
}

/*
 *  Since the dos filesystem does not allow files with
 *  holes in them we must fill the file with zeroed
 *  blocks when a seek past the end of file happens.
 *
 *  It seems that nothing in the kernel calls the filesystem
 *  specific file seek functions.  And, someone on the
 *  net told me that NFS never sends announcements of
 *  seeks to the server.  So, if pcfs ever becomes
 *  NFS mountable it will have to use other means to
 *  fill in holes in what would be a sparse file.
 */
/*ARGSUSED*/
int
pcfs_seek ( vnode_t *vp, off_t oldoff, off_t *newoff )
{
	int			error = 0;
	off_t		foff;
	lbuf_t		*bp;
	denode_t	*dep = VTODE(vp);
	pcfs_vfs_t	*pvp = dep->de_vfs;

#ifdef PCFS_DEBUG
	printf ( "In pcfs_seek\n" );
#endif
	if ( *newoff < 0 )
	{
		return EINVAL;
	}

	/*
	Compute the offset of the first byte after the
	last block in the file.
	If seeking beyond the end of file then fill the
	file with zeroed blocks up to the seek address.
	*/

	foff = (dep->de_FileSize + (pvp->vfs_bpcluster-1)) & ~pvp->vfs_crbomask;

	if (*newoff > foff)
	{
		/*
		If this is the root directory and we are
		attempting to seek beyond the end disallow
		it.  DOS filesystem root directories can
		not grow.
		*/

		if (vp->v_flag & VROOT)
		{
			return EINVAL;
		}

		/*
		If this is a directory and the caller is not
		root, then do not let them seek beyond the end
		of file.  If we allowed this then users could
		cause directories to grow.  Is this really that
		important?
		*/

		if ( ISADIR(dep) && !suser(u.u_cred) )
		{
			return EPERM;
		}

		/*
		Allocate and chain together as many clusters as
		are needed to get to *newoff.
		*/

		DELOCK ( dep );

		while (foff < *newoff)
		{
			if (error = extendfile(dep, &bp, 0))
			{
				DEUNLOCK ( dep );
				break;
			}

			dep->de_flag |= DEUPD;
			lbdwrite(bp);

			foff             += pvp->vfs_bpcluster;
			dep->de_FileSize += pvp->vfs_bpcluster;
		}

		dep->de_FileSize = *newoff;
		error = deupdat(dep, &hrestime, 1);

		DEUNLOCK ( dep );
	}

	return error;
}

/*ARGSUSED*/
int
pcfs_remove ( vnode_t *dvp, char *name, cred_t *cr )
{
	int			error;
	vnode_t		*vp;
	denode_t	*pdep;
	char		nm[DOSDIRSIZ+1];

#ifdef PCFS_DEBUG
	printf ( "In pcfs_remove\n" );
#endif
	/*
	Are we allowed to update the directory?
	*/

	if (  error = pcfs_access( dvp, VWRITE, 0, cr )  )
	{
		return error;
	}

	pdep = VTODE ( dvp );

	DELOCK ( pdep );

	if (  error = pcfs_lookup(dvp, name, &vp, 0, 0, 0, cr)  )
	{
		DEUNLOCK ( pdep );
		return error;
	}

	/*
	No mount points can be removed this way.
	Don't remove files on used for swapping;
	who is goin to use dos-files as swap-area?
	*/

	if ( vp->v_vfsmountedhere != NULL || IS_SWAPVP(vp) )
	{
		VN_RELE  ( vp   );
		DEUNLOCK ( pdep );
		return EBUSY;
	}

	/*
	Only super users can unlink directories.
	*/

	if ( vp->v_type == VDIR && !suser(cr) )
	{
		VN_RELE  ( vp   );
		DEUNLOCK ( pdep );

		return EPERM;
	}

	strncpy ( nm, (char *)VTODE(vp)->de_Name, DOSDIRSIZ );
	nm[DOSDIRSIZ] = '\0';

	dnlc_remove ( dvp, nm );
	error = removede ( vp );

	DEUNLOCK ( pdep );
	return error;
}

/*
 *  DOS filesystems don't know what links are.
 *  We can make link by pointing to the same
 *  cluster start, but we have no link count,
 *  so removal will destroy all links.
 */
/*ARGSUSED*/
int
pcfs_link ( vnode_t *tdvp, vnode_t *svp, char *tname, cred_t *cr )
{
	return EINVAL;
}

/*
 *  Renames on files require moving the denode to
 *  a new hash queue since the denode's location is
 *  used to compute which hash queue to put the file in.
 *  Unless it is a rename in place.  For example "mv a b".
 *
 *  What follows is the basic algorithm:
 */
/*
 *	if (file move) {
 *		if (dest file exists) {
 *			remove dest file
 *		}
 *		if (dest and src in same directory) {
 *			rewrite name in existing directory slot
 *		} else {
 *			write new entry in dest directory
 *			update offset and dirclust in denode
 *			move denode to new hash chain
 *			clear old directory entry
 *		}
 */
/*
 *	} else {  directory move
 *		if (dest directory exists) {
 *			if (dest is not empty) {
 *				return ENOTEMPTY
 *			}
 *			remove dest directory
 *		}
 *		if (dest and src in same directory) {
 *			rewrite name in existing entry
 *		} else {
 *			be sure dest is not a child of src directory
 *			write entry in dest directory
 *			update "." and ".." in moved directory
 *			update offset and dirclust in denode
 *			move denode to new hash chain
 *			clear old directory entry for moved directory
 *		}
 *	}
 *
 */
/*
 *  On entry:
 *    nothing is locked
 *
 *  On exit:
 *    all denodes should be released
 *
 *  Parameters:
 *    fdvp  - from directory vnode pointer
 *    fname - from entry name
 *    tdvp  - to directory vnode pointer
 *    tname - to entry name
 *    cr    - credentials
 */

int
pcfs_rename (vnode_t *fdvp, char *fname, vnode_t *tdvp, char *tname, cred_t *cr)
{
	u_char		toname[DOSDIRSIZ+1];
	u_char		oldname[DOSDIRSIZ+1];
	int			error;
	int			newparent;
	int			srcisdir;
	u_long		to_dirclust;
	u_long		to_diroffset;
	u_long		cn;
	u_long		bn;
	denode_t	*fddep;	/* from file's parent directory	*/
	denode_t	*fdep;	/* from file or directory	*/
	denode_t	*tddep;	/* to file's parent directory	*/
	denode_t	*tdep;	/* to file or directory		*/
	vnode_t		*fvp;	/* from file vnode		*/
	vnode_t		*tvp;	/* to file vnode		*/
	pcfs_vfs_t	*pvp;
	dosdirent_t	*dotdotp;
	dosdirent_t	*ep;
	lbuf_t		*bp;

#ifdef PCFS_DEBUG
	printf ( "In pcfs_rename fdir %s, fname %s; tdir %s, tname %s\n" ,
	    VTODE(fdvp)->de_Name, fname,
	    VTODE(tdvp)->de_Name, tname );
#endif

	/*
	Ensure there's something like a source with
	can be renamed.
	*/

	if (  error = pcfs_access( fdvp, VWRITE, 0, cr )  )
	{
		return error;
	}

	if ( tdvp != fdvp )
	{
		if (  error = pcfs_access( tdvp, VWRITE, 0, cr )  )
		{
			return error;
		}
	}
	else if ( !strcmp( fname, tname ) )
	{
		return 0;	/* source == destiny; for sure */
	}

	fddep = VTODE( fdvp );
	DELOCK ( fddep );

	if (  error = pcfs_lookup(fdvp, fname, &fvp, 0, 0, 0, cr)  )
	{
		DELOCK ( fddep );
		return error;
	}

	tddep = VTODE( tdvp );
	fdep  = VTODE( fvp  );

	oldname[0] = '\0';
	strncat ( (char *)oldname, (char *)fdep->de_Name, DOSDIRSIZ );

	pvp = fddep->de_vfs;

	if ( tdvp != fdvp )
	{
		DELOCK ( tddep );
	}

	/*
	Check the destination, it may not exist.
	*/

	if (  error = pcfs_lookup(tdvp, tname, &tvp, 0, GETFREESLOT, 0, cr)  )
	{
		if ( error != ENOENT )
		{
			DEUNLOCK ( fddep );
			DEUNLOCK ( tddep );

			return error;
		}

		tdep = NULL;
		tvp  = NULL;

		/*
		Convert the filename in tdnp into a dos filename.
		We copy this into the denode and directory entry
		for the destination file/directory.
		*/

		unix2dosfn ( (u_char*)tname, toname, strlen(tname) );

	}
	else
	{
		/*
		Both sides exist, they must be
		the same type.
		*/

		tdep = VTODE( tvp );

		if ( ISADIR(fdep) && !ISADIR(tdep) )
		{
			error = ENOTDIR;
			goto out1;
		}

		if ( !ISADIR(fdep) && ISADIR(tdep) )
		{
			error = EISDIR;
			goto out1;
		}

		/*
		The destination should neither be
		a mount point, nor a swap file.
		*/

		if ( tvp->v_vfsmountedhere != NULL || IS_SWAPVP(tvp) )
		{
			error = EBUSY;
			goto out1;
		}

		toname[0] = '\0';
		strncat ( (char *)toname, (char *)tdep->de_Name, DOSDIRSIZ );
	}

	/*
	Be sure we are not renaming ".", "..", or an alias of ".".
	This leads to a crippled directory tree.  It's pretty tough
	to do a "ls" or "pwd" with the "." directory entry missing,
	and "cd .." doesn't work if the ".." entry is missing.
	*/

	if ( ISADIR(fdep) )
	{
		if ( !strcmp(fname, ".")  ||  !strcmp(fname,"..") )
		{
			error = EINVAL;
			goto out1;
		}

		srcisdir = 1;
	}
	else
	{
		srcisdir = 0;
	}

	/*
	If we are renaming a directory, and the directory
	is being moved to another directory, then we must
	be sure the destination directory is not in the
	subtree of the source directory.  This could orphan
	everything under the source directory.
	*/

	DELOCK ( fdep );

	if (tdep)
	{
		DELOCK ( tdep );
	}

	/*
	fddep != tddep will do??? we don't have
	links. What happens with "." entries???
	*/

	newparent = fddep->de_StartCluster != tddep->de_StartCluster;

	if (srcisdir && newparent)
	{
		if (  error = doscheckpath ( fdep, tddep )  )
		{
			goto out;
		}
	}

	/*
	If the destination exists it will be
	removed; if it is a directory make
	sure it is empty.
	*/

	if ( tdep )
	{
		if ( srcisdir && !dosdirempty(tdep) )
		{
			error = ENOTEMPTY;
			goto out;
		}

		to_dirclust  = tdep->de_dirclust;
		to_diroffset = tdep->de_diroffset;

		DEUNLOCK ( tdep );

		dnlc_remove ( tdvp, (char*) toname );

		if (error = removede(tvp))
		{
			goto out;
		}

		tdep = NULL;
		tvp  = NULL;

		/*
		Remember where the slot was for createde().
		*/

		tddep->de_frspace   = 1;
		tddep->de_froffset  = to_dirclust;
		tddep->de_frcluster = to_diroffset;
	}

	/*
	If the source and destination are in the same
	directory then just read in the directory entry,
	change the name in the directory entry and
	write it back to disk.
	*/

	if ( !newparent )
	{
		if ( error = readde(fdep, &bp, &ep) )
		{
			goto out;
		}

		strncpy ((char *)ep->deName, (char *)toname, DOSDIRSIZ);

		if ( error = lbwritewait(bp) )
		{
			goto out;
		}

		strncpy ((char *)fdep->de_Name, (char *)toname, DOSDIRSIZ);

		dnlc_remove ( fdvp, (char*) oldname );
		dnlc_enter  ( fdvp, (char*) toname, fvp, NOCRED );
	}
	else
	{
		/*
		If the source and destination are in different
		directories, then mark the entry in the source
		directory as deleted and write a new entry in the
		destination directory. Move the denode to the
		correct hash chain for its new location in
		the filesystem if we moved a file, else update
		its .. entry to point to the new parent directory.
		Note: we reuse the old denode (fdep) and adjust
			  the entries which are changed.
		*/

		if ( error = readde(fdep, &bp, &ep) )
		{
			goto out;
		}

		ep->deName[0] = SLOT_DELETED;

		if ( error = lbwritewait(bp) )
		{
			goto out;
		}

		strncpy ((char *)fdep->de_Name, (char *)toname, DOSDIRSIZ);

		if ( error = createde(fdep, tdvp, NULL) )
		{
			strncpy ((char *)fdep->de_Name, (char *)oldname, DOSDIRSIZ);
			goto out;
		}

		fdep->de_dirclust  = tddep->de_frcluster;
		fdep->de_diroffset = tddep->de_froffset;

		if ( srcisdir )
		{
			/*
			If we moved a directory to a new parent directory,
			then we must fixup the ".." entry in the moved
			directory.
			*/

			cn = fdep->de_StartCluster;

			ASSERT ( cn != PCFSROOT );

			bn = cntobn ( pvp, cn );
			bp = lbread ( pvp->vfs_dev, bn, pvp->vfs_bpcluster );

			/*
			Better succeed else should really
			panic here, fs is corrupt.
			*/

			if ( error = lgeterror(bp) )
			{
				lbrelse(bp);
				goto out;
			}

			dotdotp = (dosdirent_t *)bp->b_un.b_addr + 1;
			dotdotp->deStartCluster = tddep->de_StartCluster;

			if ( error = lbwritewait(bp) )
			{
				goto out;
			}
		}
		else
		{
			reinsert ( fdep );
		}

		dnlc_remove ( fdvp, (char*) oldname );
		dnlc_enter  ( tdvp, (char*) toname, fvp, NOCRED );
	}

out:
	if ( fdep )	 DEUNLOCK ( fdep );
	if ( tdep )	 DEUNLOCK ( tdep );

out1:
	DEUNLOCK ( fddep );

	if ( tdvp != fdvp )
	{
		DEUNLOCK ( tddep );
	}

	if ( fvp )	VN_RELE ( fvp );
	if ( tvp )	VN_RELE ( tvp );

	return error;
}

struct {
	dosdirent_t dot;
	dosdirent_t dotdot;
} dosdirtemplate = {
		".       ", "   ",		/* the . entry */
		ATTR_DIRECTORY,			/* file attribute */
		0,0,0,0,0,0,0,0,0,0,	/* resevered */
		1234, 1234,				/* time and date */
		0,						/* startcluster */
		0,						/* filesize */
		"..      ", "   ",		/* the .. entry */
		ATTR_DIRECTORY,			/* file attribute */
		0,0,0,0,0,0,0,0,0,0,	/* resevered */
		1234, 1234,				/* time and date */
		0,						/* startcluster */
		0,						/* filesize */
};



/*ARGSUSED*/
int
pcfs_mkdir ( vnode_t *dvp, char *name, struct vattr *vap, vnode_t **vpp,
			 cred_t *cr )
{
	int			bn;
	int			error;
	u_long		newcluster;
	denode_t	*pdep;
	denode_t	*ndep;
	dosdirent_t *denp;
	denode_t	ndirent;
	pcfs_vfs_t	*pvp;
	lbuf_t		*bp;

#ifdef PCFS_DEBUG
	printf ( "In pfcs_mkdir\n" );
#endif
	/*
	Are we allowed to update the directory?
	*/

	if (  error = pcfs_access( dvp, VWRITE, 0, cr )  )
	{
		return error;
	}

	pdep = VTODE(dvp);
	DELOCK ( pdep );

	if ( error = pcfs_lookup(dvp, name, vpp, 0, GETFREESLOT, 0, NOCRED) )
	{
		if ( error != ENOENT )
		{
			DEUNLOCK ( pdep );
			return error;
		}
	}
	else
	{
		DEUNLOCK ( pdep );
		return EEXIST;
	}

	/*
	If this is the root directory and there is no space left
	we can't do anything.  This is because the root directory
	can not change size.
	*/

	if ( pdep->de_StartCluster == PCFSROOT  &&  !pdep->de_frspace )
	{
		DEUNLOCK(pdep);
		return ENOSPC;
	}

	pvp = pdep->de_vfs;

	/*
	Allocate a cluster to hold the about to be created directory.
	*/

	if (error = clusteralloc(pvp, &newcluster, CLUST_EOFE))
	{
		DEUNLOCK(pdep);
		return error;
	}

	/*
	Now fill the cluster with the "." and ".." entries.
	And write the cluster to disk.  This way it is there
	for the parent directory to be pointing at if there
	were a crash.
	*/

	bn = cntobn(pvp, newcluster);
	bp = lgetblk(pvp->vfs_dev, bn, pvp->vfs_bpcluster);

	bzero(bp->b_un.b_addr, pvp->vfs_bpcluster);
	bcopy((caddr_t)&dosdirtemplate, bp->b_un.b_addr, sizeof dosdirtemplate);
	denp = (dosdirent_t *)bp->b_un.b_addr;
	denp->deStartCluster = newcluster;
	unix2dostime(&hrestime, (union dosdate *)&denp->deDate,
	    (union dostime *)&denp->deTime);
	denp++;
	denp->deStartCluster = pdep->de_StartCluster;
	unix2dostime(&hrestime, (union dosdate *)&denp->deDate,
	    (union dostime *)&denp->deTime);

	if ( error = lbwritewait(bp) )
	{
		(void)clusterfree(pvp, newcluster);
		DEUNLOCK(pdep);
		return error;
	}

	/*
	Now build up a directory entry pointing to the newly
	allocated cluster.  This will be written to an empty
	slot in the parent directory.
	*/

	ndep = &ndirent;
	bzero((caddr_t)ndep, sizeof(*ndep));
	unix2dosfn( (u_char *)name, ndep->de_Name, strlen(name) );
	unix2dostime(&hrestime, (union dosdate *)&ndep->de_Date,
	    (union dostime *)&ndep->de_Time);

	ndep->de_StartCluster = newcluster;
	ndep->de_Attributes   = ATTR_DIRECTORY;
	ndep->de_vfs          = pvp;	/* createde() needs this	*/

	if ( error = createde(ndep, dvp, &ndep) )
	{
		(void)clusterfree(pvp, newcluster);
	}
	else
	{
		DEUNLOCK ( ndep );
		*vpp = DETOV( ndep );
	}

	DEUNLOCK ( pdep );

	return error;
}

/*ARGSUSED*/
int
pcfs_rmdir ( vnode_t *dvp, char *name, vnode_t *cdir, cred_t *cr )
{
	denode_t	*pdep;
	denode_t	*dep;
	vnode_t		*vp;
	char		nm[DOSDIRSIZ+1];
	int			error;

#ifdef PCFS_DEBUG
	printf ( "In pcfs_rmdir\n" );
#endif

	/*
	Are we allowed to update the directory?
	*/

	if (  error = pcfs_access( dvp, VWRITE, 0, cr )  )
	{
		return error;
	}

	pdep = VTODE(dvp);	/* parent dir of dir to delete	*/
	DELOCK ( pdep );

	if (  error = pcfs_lookup(dvp, name, &vp, 0, 0, 0, cr)  )
	{
		DEUNLOCK ( pdep );
		return error;
	}

	dep  = VTODE(vp);	/* directory to delete	*/

	/*
	It should be a directory.
	*/

	if (  !ISADIR(dep)  )
	{
		VN_RELE ( vp );
		DEUNLOCK ( pdep );
		return ENOTDIR;
	}

	/*
	Don't remove a mount-point.
	*/

	if ( vp->v_vfsmountedhere != NULL )
	{
		VN_RELE ( vp );
		DEUNLOCK ( pdep );
		return EBUSY;
	}

	/*
	Don't let "rmdir ." go thru.
	*/

	if ( dvp == vp )
	{
		VN_RELE ( vp );
		DEUNLOCK ( pdep );
		return EINVAL;
	}

	/*
	Be sure the directory being deleted is empty.
	*/

	if ( !dosdirempty(dep) )
	{
		VN_RELE ( vp );
		DEUNLOCK ( pdep );
		return ENOTEMPTY;
	}

	strncpy ( nm, (char *)dep->de_Name, DOSDIRSIZ );
	nm[DOSDIRSIZ] = '\0';

	dnlc_remove ( dvp, nm );

	/*
	Delete the entry from the directory.  For dos filesystems
	this gets rid of the directory entry on disk, the in memory
	copy still exists but the de_refcnt is <= 0.  This prevents
	it from being found by deget().  When the deput() on dep is
	done we give up access.
	*/

	error = removede(vp);

	DEUNLOCK ( pdep );
	return error;
}

/*
 *  DOS filesystems don't know what symlinks are.
 */
/*ARGSUSED*/
int
pcfs_symlink ( vnode_t *dvp, char *linkname, struct vattr *vap, char *target,
				cred_t *cr )
{
	return EINVAL;
}



/*
 * pcfs_readdir: convert a dos directory to a fs-independent type
 *		 and pass it to the user.
 */

/*ARGSUSED*/
int
pcfs_readdir( vnode_t *vp, struct uio *uio, cred_t *cr, int *eofflagp )
{
	int			error;
	int			diff;
	long		n;
	long		on;
	long		count;
	int			len;		/* lenght of entry name */
	int 		lastslot;	/* end of directory indication */
	off_t		offset;		/* directory offset in bytes */
	u_long		cn;
	u_long		fileno;		/* kind of ino for pcfs */
	long		bias;		/* for handling "." and ".." */
	u_long		bn;
	u_long		lbn;
	int			fixlen;		/* fixed length of dirent   */
	int			maxlen;		/* maximum length of dirent */
	addr_t		end;
	lbuf_t		*bp;
	denode_t	*dep;
	pcfs_vfs_t	*pvp;
	dosdirent_t	*dentp;
	dirent_t	*crnt;
	u_char		dirbuf[512];	/* holds converted dos directories */


#ifdef PCFS_DEBUG
	printf ( "In readdir\n" );
#endif

	dep = VTODE(vp);
	pvp = dep->de_vfs;

	ASSERT ( dep->de_flag & DELOCKED );

	/*
	pcfs_readdir() won't operate properly on regular files
	since it does i/o only with the the filesystem vnode,
	and hence can retrieve the wrong block from the buffer
	cache for a plain file.  So, fail attempts to readdir()
	on a plain file.
	*/

	if (  !ISADIR(dep)  )
		return ENOTDIR;

	/*
	If the user buffer is smaller than the size of one dos
	directory entry or the file offset is not a multiple of
	the size of a directory entry, then we fail the read.
	*/

	bp     = NULL;
	offset = uio->uio_offset;
	count  = uio->uio_resid;
	fixlen = (int) ((struct dirent *) 0)->d_name;
	maxlen = fixlen + DOSDIRSIZ + 1;

	if ( offset & (sizeof(dosdirent_t)-1) )
	{
		return ENOENT;
	}

	if ( count < maxlen )
	{
		return EINVAL;
	}

	bias 		      = 0;
	lastslot 	      = 0;
	error		      = 0;

	uio->uio_iov->iov_len = count; /* does this assume vector big enough? */
	crnt  		          = (dirent_t *)dirbuf;

	/*
	If they are reading from the root directory then,
	we simulate the . and .. entries since these don't
	exist in the root directory.  We also set the offset
	bias to make up for having to simulate these entries.
	By this I mean that at file offset 64 we read the first entry
	in the root directory that lives on disk.
	*/

	if (dep->de_StartCluster == PCFSROOT)
	{
		bias = 2 * sizeof(dosdirent_t);

		if ( offset < bias )
		{
			/*
			Write the "." entry ?
			*/

			if ( offset < sizeof(dosdirent_t) )
			{
				offset         = sizeof(dosdirent_t);
				crnt->d_ino    = 1;
				crnt->d_reclen = fixlen + 2;
				crnt->d_off    = offset;

				strcpy ( crnt->d_name, "." );

				count -= crnt->d_reclen;
				crnt   = (dirent_t *)((char*)crnt+crnt->d_reclen);
			}

			if ( count >= maxlen )
			{
				/*
				Write the ".." entry.
				*/

				offset        += sizeof(dosdirent_t);
				crnt->d_ino    = 0;
				crnt->d_reclen = fixlen + 3;
				crnt->d_off    = offset;

				strcpy ( crnt->d_name, ".." );

				count -= crnt->d_reclen;
				crnt   = (dirent_t *)((char*)crnt+crnt->d_reclen);
			}
		}
	}

	do {
		/*
		Stop if we did do everything.
		*/

		if (  (diff = dep->de_FileSize - (offset - bias)) <= 0  )
		{
			break;
		}

		lbn = (offset - bias) >> pvp->vfs_cnshift;
		on  = (offset - bias) &  pvp->vfs_crbomask;
		n   = (uint)(pvp->vfs_bpcluster - on);

		if (diff < n)
		{
			n = diff;
		}

		if (  error = pcbmap(dep, lbn, &bn, &cn)  )
			break;

		bp = lbread ( pvp->vfs_dev, bn, pvp->vfs_bpcluster );

		if ( error = lgeterror(bp) )
		{
			lbrelse(bp);
			break;
		}

		n = MIN(n, pvp->vfs_bpcluster);

		/*
		We assume that an integer number of dosentries
		fits in a cluster block.
		*/

		if ( n < sizeof(dosdirent_t)  )
		{
			lbrelse(bp);
			error = EIO;
			break;
		}

		/*
		Code to convert from dos directory entries to
		fs-independent directory entries.
		*/

		dentp = (dosdirent_t *)(bp->b_un.b_addr + on);
		end   = bp->b_un.b_addr + on + n;

		for (  ; (addr_t)dentp < end && count >= maxlen; dentp++,
		    offset += sizeof(dosdirent_t) )
		{
			/*
			If we have a slot from a deleted file, or a volume
			label entry just skip it.
			If the entry is empty then set a flag saying finish
			in the directory.
			*/

			if ( dentp->deName[0] == SLOT_EMPTY )
			{
				lastslot = 1;
				break;
			}

			if (dentp->deName[0] == SLOT_DELETED  ||
			    (dentp->deAttributes & ATTR_VOLUME))
			{
				continue;
			}

			/*
			This computation of d_fileno must match the
			computation of va_fileid in pcfs_getattr
			*/

			if (dentp->deAttributes & ATTR_DIRECTORY)
			{
				/*
				If this is the root directory
				*/
				fileno = dentp->deStartCluster;
				if ( fileno == PCFSROOT)
					fileno = 1;
			}
			else
			{
				/*
				If the file's dirent lives in root dir
				*/
				if ((fileno = cn) == PCFSROOT)
					fileno = 1;
				fileno = (fileno << 16) | ((dentp -
				    (dosdirent_t *)bp->b_un.b_addr) & 0xffff);
			}

			len = dos2unixfn(dentp->deName, (u_char *)crnt->d_name);

			crnt->d_ino    = fileno;
			crnt->d_reclen = fixlen + len + 1;
			crnt->d_off    = offset + sizeof(dosdirent_t);

			count -= crnt->d_reclen;

			crnt = (dirent_t *)((char *) crnt + crnt->d_reclen);

			/*
			If our intermediate buffer is full then copy
			its contents to user space.  I would just
			use the buffer the buf header points to but,
			I'm afraid that when we lbrelse() it someone else
			might find it in the cache and think its contents
			are valid.  Maybe there is a way to invalidate
			the buffer before lbrelse()'ing it.
			*/

			if ( ((u_char *)crnt)+maxlen >= &dirbuf[sizeof dirbuf] )
			{
				n     = (u_char*) crnt - dirbuf;
				error = uiomove(dirbuf, n, UIO_READ, uio);
				if (error)
					break;
				crnt = (struct dirent *)dirbuf;
			}
		}

		lbrelse(bp);

	} while ( !lastslot  &&  error == 0  &&  count >= maxlen );

	/*
	Move the rest of the directory
	entries to the user.
	*/

	if (  !error  &&  (n = (u_char*) crnt - dirbuf)  )
	{
		error = uiomove(dirbuf, n, UIO_READ, uio);
	}
out:
	;
	uio->uio_offset = offset;

	/*
	I don't know why we bother setting this eofflag, getdents()
	doesn't bother to look at it when we return.
	*/

	if (lastslot || ((dep->de_FileSize - (uio->uio_offset - bias)) <= 0))
	{
		*eofflagp = 1;
	}
	else
	{
		*eofflagp = 0;
	}

	return error;
}

/*
 *  DOS filesystems don't know what symlinks are.
 */

/*ARGSUSED*/
int
pcfs_readlink ( vnode_t *vp, struct uio *uio, cred_t *cred )
{
	return EINVAL;
}


void
pcfs_lock ( vnode_t *vp )
{
	struct denode *dep = VTODE(vp);

#ifdef PCFS_DEBUG
	printf ( "In pcfs_lock for ->%s<-\n", dep->de_Name );
#endif
	DELOCK(dep);
}

void
pcfs_unlock ( vnode_t *vp )
{
	struct denode *dep = VTODE(vp);

	if (!(dep->de_flag & DELOCKED))
		cmn_err ( CE_PANIC, "pcfs_unlock: denode not locked" );

	DEUNLOCK(dep);
}

int
pcfs_islocked ( vnode_t *vp )
{
	return VTODE(vp)->de_flag & DELOCKED ? 1 : 0;
}



int
pcfs_frlock ( vnode_t *vp, int cmd, flock_t *flp, int flag, off_t offset,
			  cred_t *cr )
{
	return fs_frlock ( vp, cmd, flp, flag, offset, cr );
}


int
pcfs_space(void)
{
#ifdef PCFS_DEBUG
	cmn_err ( CE_CONT, "pcfs_space() not yet implemented.\n" );
#endif
	return ENOSYS;
}

int
pcfs_fid ( vnode_t *vp, struct fid **fidpp )
{
	denode_t* dp;
	dfid_t*	  dfid;

#ifdef PCFS_DEBUG
	printf ( "In pcfs_fid\n" );
#endif

#ifndef NO_GENOFF
	pcfs_shared = 1;
#endif

	dp   = VTODE(vp);
	dfid = (dfid_t *) kmem_alloc ( sizeof (dfid_t), KM_SLEEP );

	dfid->dfid_len        = sizeof ( dfid_t ) - sizeof ( dfid->dfid_len );
	dfid->dfid_gen        = DE_GEN(dp);
	dfid->dfid_dirclust   = dp->de_dirclust;
	dfid->dfid_diroffset  = dp->de_diroffset;
	dfid->dfid_startclust = dp->de_StartCluster;

	if (  ISADIR( dp )  )
	{
		if ( dfid->dfid_diroffset > (u_short)(PCFS_FID_DIRFLAG - 1) )
		{ 
			cmn_err( CE_WARN, "pcfs_fid: subdirectory %s has too many entries",
	    								dp->de_Name);
		}
		dfid->dfid_diroffset  |= PCFS_FID_DIRFLAG;
	}

	*fidpp = (struct fid *) dfid;
	return 0;
}

int
pcfs_putpage(void)
{
#ifdef PCFS_DEBUG
	cmn_err ( CE_CONT, "pcfs_putpage() not yet implemented.\n" );
#endif
	return ENOSYS;
}

int
pcfs_allocstore(void)
{
#ifdef PCFS_DEBUG
	cmn_err ( CE_CONT, "pcfs_allocstore() not yet implemented.\n" );
#endif
	return ENOSYS;
}

int
pcfs_getpage(void)
{
#ifdef PCFS_DEBUG
	cmn_err ( CE_CONT, "pcfs_getpage() not yet implemented.\n" );
#endif
	return ENOSYS;
}

int
pcfs_map(void)
{
#ifdef PCFS_DEBUG
	cmn_err ( CE_CONT, "pcfs_map() not yet implemented.\n" );
#endif
	return ENOSYS;
}

int
pcfs_addmap(void)
{
#ifdef PCFS_DEBUG
	cmn_err ( CE_CONT, "pcfs_addmap() not yet implemented.\n" );
#endif
	return ENOSYS;
}

int
pcfs_delmap(void)
{
#ifdef PCFS_DEBUG
	cmn_err ( CE_CONT, "pcfs_delmap() not yet implemented.\n" );
#endif
	return ENOSYS;
}

int
realvp(void)
{
#ifdef PCFS_DEBUG
	cmn_err ( CE_CONT, "realvp() not yet implemented.\n" );
#endif
	return ENOSYS;
}


struct vnodeops pcfs_vnodeops = {
		pcfs_open,
		pcfs_close,
		pcfs_read,
		pcfs_write,
		pcfs_ioctl,
		fs_setfl,
		pcfs_getattr,
		pcfs_setattr,
		pcfs_access,
		pcfs_lookup,
		pcfs_create,
		pcfs_remove,
		pcfs_link,
		pcfs_rename,
		pcfs_mkdir,
		pcfs_rmdir,
		pcfs_readdir,
		pcfs_symlink,
		pcfs_readlink,
		pcfs_fsync,
		pcfs_inactive,
		pcfs_fid,
		pcfs_lock,
		pcfs_unlock,
		pcfs_seek,
		fs_cmp,
		pcfs_frlock,
		pcfs_space,		/* pcfs_space, */
		realvp, 		/* realvp */
		pcfs_getpage,	/* pcfs_getpage, */
		pcfs_putpage,	/* pcfs_putpage, */
		pcfs_map,		/* pcfs_map, */
		pcfs_addmap,	/* pcfs_addmap, */
		pcfs_delmap,	/* pcfs_delmap, */
		fs_poll,
		fs_nosys,		/* dump */
		fs_pathconf,
		pcfs_allocstore,
		fs_nosys,		/* filler */
		fs_nosys,
		fs_nosys,
		fs_nosys,
		fs_nosys,
		fs_nosys,
		fs_nosys,
		fs_nosys,
		fs_nosys,
		fs_nosys,
		fs_nosys,
		fs_nosys,
		fs_nosys,
		fs_nosys,
		fs_nosys,
		fs_nosys,
		fs_nosys,
		fs_nosys,
		fs_nosys,
		fs_nosys,
		fs_nosys,
		fs_nosys,
		fs_nosys,
		fs_nosys,
		fs_nosys,
		fs_nosys,
		fs_nosys,
		fs_nosys,
		fs_nosys,
		fs_nosys,
#ifndef SYSVR42
		fs_nosys,
#endif
};
