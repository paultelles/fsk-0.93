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

#ident "@(#)iso9660_vnops.c	1.4 93/09/18 "

/*
 * Copyright (c) 1982, 1986, 1989 Regents of the University of California.
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
 * Started with
 *	ufs_vnops.c	7.64 (Berkeley) 5/16/91
 */
#include "sys/types.h"
#include "sys/systm.h"
#include "sys/buf.h"
#include "sys/proc.h"
#include "sys/disp.h"
#include "sys/cmn_err.h"
#include "sys/conf.h"
#include "sys/cred.h"
#include "sys/user.h"
#include "sys/debug.h"
#include "sys/dirent.h"
#include "sys/errno.h"
#include "sys/time.h"
#include "sys/fbuf.h"
#include "sys/fcntl.h"
#include "sys/file.h"
#include "sys/flock.h"
#include "sys/kmem.h"
#include "sys/immu.h"
#include "sys/inline.h"
#include "sys/mman.h"
#include "sys/open.h"
#include "sys/param.h"
#include "sys/pathname.h"
#include "sys/sysinfo.h"
#include "sys/uio.h"
#include "sys/vnode.h"
#include "sys/vfs.h"
#include "sys/dnlc.h"
#include "sys/stat.h"
#include "sys/swap.h"
#include "sys/resource.h"
#include "sys/sysmacros.h"
#include "sys/statvfs.h"
#include "sys/ioccom.h"
#include "fs/fs_subr.h"

#include "vm/hat.h"
#include "vm/page.h"
#include "vm/pvn.h"
#include "vm/as.h"
#include "vm/seg.h"
#include "vm/seg_map.h"
#include "vm/seg_vn.h"
#include "vm/rm.h"

#include "iso9660.h"
#include "iso9660_node.h"



extern struct vnodeops iso9660_vnodeops;
extern struct vnodeops spec_iso9660_vnodeops;


/*
 * Open called.
 *
 * Nothing to do.
 */
/* ARGSUSED */
int
IsoOpen ( vnode_t** vpp, int flag, cred_t* cr )
{
	return 0;
}

/*
 * Close called
 *
 * Clean all pending locks.
 */

/* ARGSUSED */
int
IsoClose ( vnode_t* vp, int flag, int count, off_t offset, cred_t* cr )
{
	IsoNode_t*	ip = VTOI(vp);

	ISOILOCK ( ip );
	cleanlocks ( vp, u.u_procp->p_epid, u.u_procp->p_sysid );
	ISOIUNLOCK ( ip );

	return 0;
}

/*
 * Check mode permission on inode pointer. Mode is READ, WRITE or EXEC.
 * The mode is shifted to select the owner/group/other fields. The
 * super user is granted all permissions.
 */
int
IsoAccess ( vnode_t* vp, int mode, int flags, cred_t* cr )
{
	IsoNode_t	*ip;
	IsoVfs_t	*ivp;
	int			fmode;
	uid_t		uid;
	gid_t		gid;

#ifdef ISO9660_DEBUG
	printf ( "In IsoAccess\n" );
#endif

	/*
	If you're the super-user, you always get access.
	*/

	if ( suser(cr) )
	{
		return (0);
	}

	ip = VTOI(vp);

	/*
	If Rock Ridge info, use that already biased data else
	pick it from the filesystem special structure.
	*/

	if (  ip->rr.rr_flags & RR_PX  )
	{
		fmode = ip->rr.rr_mode;
		uid   = ip->rr.rr_uid;
		gid   = ip->rr.rr_gid;
	}
	else
	{
		ivp   = ip->i_vfs;

		fmode = 0777 & ~ivp->im_mask;
		uid   = (ivp->im_uid == DEFUSR) ? 0 : ivp->im_uid;
		gid   = (ivp->im_gid == DEFGRP) ? 0 : ivp->im_gid;
	}

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

	if ( (fmode & mode) == mode )
		return 0;

	return EACCES;
}


static time_t
IsoParseTime ( IsoNode_t* ip, int ts, int hsierra )
{
	int		 year;
	unsigned month;
	unsigned day;
	unsigned hour;
	unsigned minute;
	unsigned second;
	unsigned days;
	int		 tz;
	time_t	 t;
	u_char	 *buf;
	int		 len;

#ifdef ISO9660_DEBUG
#endif
	if ((len = ip->rr.rr_time_format[TS_INDEX(ts)]) == 0)
	{
		buf = ip->iso9660_time;
		len = RR_SHORT_TIMESTAMP_LEN;
	}
	else
	{
		buf = ip->rr.rr_time[TS_INDEX(ts)];
		len = ip->rr.rr_time_format[TS_INDEX(ts)];
	}

	if (len == RR_LONG_TIMESTAMP_LEN)
	{
		year   = (buf[0] - '0') * 1000 + (buf[1] - '0') * 100 +
							(buf[2] - '0') * 10 + (buf[3] - '0') - 1970;
		month  = (buf[4] - '0') * 10 + (buf[5] - '0');
		day    = (buf[6] - '0') * 10 + (buf[7] - '0');
		hour   = (buf[8] - '0') * 10 + (buf[9] - '0');
		minute = (buf[10] - '0') * 10 + (buf[11] - '0');
		second = (buf[12] - '0') * 10 + (buf[13] - '0');

		tz     = IsoNum712(buf + 16);
	}
	else
	{
		year   = buf[0] - 70;
		month  = buf[1];
		day    = buf[2];
		hour   = buf[3];
		minute = buf[4];
		second = buf[5];

		if ( hsierra )	tz = 0;
		else			tz = IsoNum712(buf + 6);
	}

	if (year < 0 || month > 12)
	{
		t = 0;
	}
	else
	{
		/* monlen[X] is number of days before month X */
		static int monlen[12] =
		    {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

		days = year * 365;

		/* 
		add a day for each leap year since 1970 (since
		2000 is a leap year, this will work through 2100)
		*/

		if (year > 2)
		{
			days += (year+2) / 4;
		}

		days += monlen[month-1];

		/* 
		subtract a day if this is a leap year, 
		but we aren't past February yet
		*/

		if (((year+2) % 4) == 0 && month <= 2)
		{
			days--;
		}

		days += day - 1;

		t = ((((days * 24) + hour) * 60 + minute) * 60) + second;
		
		if (-48 <= tz && tz <= 52)
		{
			t -= tz * 15 * 60;
		}
	}

	return (t);
}


/* ARGSUSED */
int
IsoGetAttr ( vnode_t* vp, struct vattr* vap, int flags, cred_t* cr )
{
	IsoNode_t	*ip;

#ifdef ISO9660_DEBUG
	printf ( "In IsoGetAttr\n" );
#endif

	ip  = VTOI(vp);

	if (!ip->time_parsed)
	{
		ip->time_parsed = 1;

		/*
		Parse the time; note that RR may have different
		time values while ISO9660 has one value. Also
		note that RR is never hsierra.
		*/

		if (ip->rr.rr_flags & RR_PX)
		{
			ip->mtime = IsoParseTime(ip, RR_MODIFY,     0);
			ip->atime = IsoParseTime(ip, RR_ACCESS,     0);
			ip->ctime = IsoParseTime(ip, RR_ATTRIBUTES, 0);
		}
		else
		{
			ip->mtime = IsoParseTime(ip, RR_MODIFY, ip->i_vfs->im_hsierra);
			ip->atime = ip->mtime;
			ip->ctime = ip->mtime;
		}
	}

	vap->va_mtime.tv_sec = ip->mtime;
	vap->va_mtime.tv_nsec = 0;

	vap->va_atime.tv_sec = ip->atime;
	vap->va_atime.tv_nsec = 0;

	vap->va_ctime.tv_sec = ip->ctime;
	vap->va_ctime.tv_nsec = 0;

	if (ip->rr.rr_flags & RR_PX)
	{
		vap->va_mode  = ip->rr.rr_mode & 07777;
		vap->va_uid   = ip->rr.rr_uid;
		vap->va_gid   = ip->rr.rr_gid;
		vap->va_nlink = ip->rr.rr_nlink;
	}
	else
	{
		vap->va_mode  = VREAD|VWRITE|VEXEC;
		vap->va_mode |= (vap->va_mode >> 3) | (vap->va_mode >> 6);
		vap->va_mode &= ~ip->i_vfs->im_mask;

		if ( ip->i_vfs->im_uid != DEFUSR )
		{
			vap->va_uid = ip->i_vfs->im_uid;
		}
		else
		{
			vap->va_uid = 0;
		}

		if ( ip->i_vfs->im_gid != DEFGRP )
		{
			vap->va_gid = ip->i_vfs->im_gid;
		}
		else
		{
			vap->va_gid = 0;
		}

		vap->va_nlink = 1;
	}

	if (ip->rr.rr_flags & RR_PN)
	{
		vap->va_rdev = ip->rr.rr_rdev;
	}
	else
	{
		vap->va_rdev = 0;
	}

	vap->va_fsid    = ip->i_dev;
	vap->va_nodeid  = ip->fileid;
	vap->va_size    = ip->i_size;
	vap->va_blksize = ip->i_vfs->logical_block_size;
	vap->va_nblocks = (ip->i_size + vap->va_blksize - 1) / vap->va_blksize;
	vap->va_type    = vp->v_type;

	return 0;
}



/*
 * Vnode op for reading.
 */
/* ARGSUSED */
int
IsoRead ( vnode_t* vp, struct uio* uio, int ioflag, cred_t* cr )
{
	IsoNode_t	*ip;
	addr_t		base;
	off_t		offset;
	uint		mapoff;
	uint		mapblk;
	int			flags;
	int			diff;
	int			bias;
	int			error;
	long		n;
	long		on;

#ifdef ISO9660_DEBUG
	printf ( "In IsoRead\n" );
#endif

	if ( uio->uio_offset < 0  ||  uio->uio_offset + uio->uio_resid < 0 )
	{
		return EINVAL;
	}

	ip = VTOI(vp);

	if ( MANDLOCK( vp, ISOFILEMODE(ip) ) )
	{
		error = chklock ( vp, FREAD, uio->uio_offset, uio->uio_resid,
															uio->uio_fmode );
		return error;
	}

	if ( uio->uio_resid == 0 )
	{
		return 0;
	}

	bias = ip->i_bias;

	do {
		diff = ip->i_size - uio->uio_offset;

		if (diff <= 0)
		{
			return 0;
		}

		offset = uio->uio_offset + bias;
		mapblk = offset & MAXBMASK;
		mapoff = offset & MAXBOFFSET;

		on = offset & PAGEOFFSET;
		n  = MIN((PAGESIZE - on), uio->uio_resid);

		if (diff < n)
		{
			n = diff;
		}

		base = segmap_getmap ( segkmap, vp, mapblk );

		error = uiomove ( base + mapoff, (long)n, UIO_READ, uio );

		if ( error )
		{
			/*
			An error occurred, invalidate any
			pages that has been allocated yet.
			*/

			(void) segmap_release ( segkmap, base, 0 );
		}
		else
		{
			if ( n + on == PAGESIZE || uio->uio_offset == ip->i_size )
			{
				flags = SM_DONTNEED;	/* Don't need this buffer anymore */
			}
			else
			{
				flags = 0;
			}

			error = segmap_release ( segkmap, base, flags );
		}

	} while ( !error && uio->uio_resid > 0 && n != 0 );

	return error;
}

/* ARGSUSED */
int
IsoIoctl( vnode_t* vp, int com, caddr_t data, int flag, cred_t* cr, int* rvalp )
{
	IsoGetSusp_t	suspbuf;
	struct iovec	iov;
	struct uio		uio;
	int				error;
	IsoNode_t		*ip;
	IsoVfs_t		*ivp;
	IsoDirInfo_t	*dirp;

	switch (com) {

	case ISO9660GETSUSP:

		if ( copyin(data, (caddr_t)&suspbuf, sizeof suspbuf) )
		{
			return EFAULT;
		}

		if (vp->v_op != &iso9660_vnodeops && vp->v_op != &spec_iso9660_vnodeops)
		{
			return EINVAL;
		}

		ip  = VTOI(vp);
		ivp = ip->i_vfs;

		iov.iov_base   = suspbuf.buf;
		iov.iov_len    = suspbuf.buflen;
		uio.uio_iov    = &iov;
		uio.uio_iovcnt = 1;
		uio.uio_offset = 0;
		uio.uio_segflg = UIO_USERSPACE;
		uio.uio_resid  = suspbuf.buflen;

		IsoDirOpen (vp, ivp, ip->fileid, ISO9660_MAX_FILEID, 0, 0, &uio, &dirp);

		if ( error = IsoDirGet(dirp) )
		{
			return error;
		}

		IsoDirClose(dirp);

		if ( uio.uio_resid )
		{
			suspbuf.buflen -= uio.uio_resid;

			if ( copyout(data, (caddr_t)&suspbuf, sizeof suspbuf) )
			{
				return EFAULT;
			}
		}

		return 0;

	default:
		return (ENOTTY);
	}
}


/*
 * Seek on a file
 */
/* ARGSUSED */
int
IsoSeek  ( vnode_t* vp, off_t oldoff, off_t* newoff )
{
	return *newoff < 0 ? EINVAL : 0;
}


/*
 * Vnode op for readdir
 */
int
IsoReadDir ( vnode_t* vp, struct uio* uio, cred_t* cr, int* eofflagp )
{
	int				error;
	IsoDirInfo_t	*dirp;
	struct dirent	*direntp;
	char			direntbuf[(int)((struct dirent*) 0)->d_name+NAME_MAX+1];
	IsoNode_t		*ip;
	IsoVfs_t		*ivp;
	IsoFileId_t		dir_first_fileid;
	IsoFileId_t		dir_last_fileid;
	IsoFileId_t		fileid;

#ifdef ISO9660_DEBUG
	printf ( "In IsoReadDir\n" );
#endif

	ip      = VTOI(vp);
	ivp     = ip->i_vfs;
	direntp = (struct dirent*) direntbuf;

	ASSERT ( ip->i_flag & ILOCKED );

	if ( vp->v_type != VDIR )
	{
		return ENOTDIR;
	}

	if ( uio->uio_resid == 0 )
	{
		return 0;
	}

	if ( uio->uio_resid < sizeof( struct dirent ) )
	{
		return EINVAL;
	}

	dir_first_fileid = IsoLogBlkToSize(ivp, (IsoFileId_t) ip->extent);
	dir_last_fileid  = dir_first_fileid + i_total(ip);

	fileid = uio->uio_offset;

	if (fileid == 0)
	{
		fileid = dir_first_fileid;
	}

	fileid += ip->i_bias;

	IsoDirOpen ( vp, ivp, fileid, dir_last_fileid, 1, 0, NULL, &dirp );

	while (uio->uio_resid)
	{
		if (error = IsoDirGet(dirp))
		{
			break;
		}

		direntp->d_ino    = dirp->translated_fileid;
		direntp->d_off    = dirp->next_fileid;
		bcopy(dirp->name, direntp->d_name, dirp->namelen);
		direntp->d_name[dirp->namelen] = 0;
		direntp->d_reclen = (int)((struct dirent*) 0)->d_name + dirp->namelen+1;

		if (uio->uio_resid < (int) direntp->d_reclen)
			break;
		
		if (error = uiomove(direntp, direntp->d_reclen, UIO_READ, uio))
			break;

		uio->uio_offset = dirp->next_fileid;
	}

	IsoDirClose(dirp);

	/*
	The eof-flag is not used so an extra
	getdents() call will be made ...
	*/

	if ( error == ENOENT )
	{
		error = 0;
		*eofflagp = 1;
	}
	else
	{
		*eofflagp = 0;
	}

	return error;
}


/*
 * Return target name of a symbolic link
 */
int
IsoReadLink ( vnode_t* vp, struct uio* uiop, cred_t* cr )
{
	IsoNode_t	 *ip;
	IsoVfs_t	 *ivp;
	IsoDirInfo_t *dirp;
	int			 error;

	if ( vp->v_type != VLNK )
	{
		return EINVAL;
	}

	ip  = VTOI(vp);
	ivp = ip->i_vfs;

	IsoDirOpen ( vp, ivp, ip->fileid, ISO9660_MAX_FILEID, 0, 1, NULL, &dirp );

	if (error = IsoDirGet(dirp))
	{
		goto ret;
	}

	if (dirp->link == NULL || *dirp->link == 0)
	{
		error = EINVAL;
		goto ret;
	}

	error = uiomove(dirp->link, dirp->link_used, UIO_READ, uiop);

ret:
	IsoDirClose(dirp);
	return error;
}


int
IsoFsync ( vnode_t* vp, cred_t* cr )
{
	return 0;
}


/*
 * IsoFid:	Export a fileid to enable identification;
 *			beware for structure alignment: do zalloc.
 */

int
IsoFid ( vnode_t* vp, struct fid** fidpp )
{
	IsoNode_t	*ip;
	IsoFid_t	*ifid;

#ifdef ISO9660_DEBUG
	printf ( "In IsoFid\n" );
#endif

	ip   = VTOI(vp);

	/*
	If the blocksize is smaller than the
	sector size we might expect unaligned
	reads. NFS won't be able to deal with
	this, so disallow shares.
	*/

	if ( IsoBlkSize(ip->i_vfs) < SECTSIZE )
	{
		return EINVAL;
	}

	ifid = (IsoFid_t *) kmem_zalloc ( sizeof (IsoFid_t), KM_SLEEP );

	ifid->len    = sizeof ( IsoFid_t ) - sizeof ( ifid->len );
	ifid->fileid = ip->fileid;

	*fidpp = (struct fid *) ifid;
	return 0;
}


int IsoAllocStore()
{
	return 0;
}



/*
 * Lock an inode.
 */
void
IsoLock ( vnode_t* vp )
{
	ISOILOCK ( VTOI(vp) );
}


/*
 * Unlock an inode.
 */
void
IsoUnlock ( vnode_t* vp )
{
	IsoNode_t *ip = VTOI(vp);

#ifdef ISO9660_DEBUG
#endif
	if (!(ip->i_flag & ILOCKED))
		cmn_err ( CE_PANIC, "IsoUnlock: inode not locked" );

	ISOIUNLOCK(ip);
}

/*
 * Check for a locked inode.
 */
int
IsoIsLocked ( vnode_t* vp )
{
#ifdef ISO9660_DEBUG
#endif
	return  (VTOI(vp)->i_flag & ILOCKED) != 0;
}


int
IsoFrLock ( vnode_t* vp, int cmd, struct flock* flp, int flag,
			off_t offset, cred_t* cr )
{
	IsoNode_t*	ip;
	
#ifdef ISO9660_DEBUG
#endif
	ip = VTOI(vp);

	/*
	No locking on mapped files; no way to check.
	*/

	if ( ip->i_mapcnt > 0 && MANDLOCK( vp, ISOFILEMODE(ip) ) )
	{
		return EAGAIN;
	}

    return fs_frlock ( vp, cmd, flp, flag, offset, cr );
}


/*
 * Mmap a file
 */

int
IsoMmap ( vnode_t* vp, uint off, struct as *as, addr_t *addrp, uint len,
			u_char prot, u_char maxprot, uint flags, cred_t *cr )
{
	struct segvn_crargs	mapargs;
	IsoNode_t*  		ip;
	int					error;

#ifdef ISO9660_DEBUG
	printf ( "In IsoMmap on offset %x\n", off );
#endif
	
	/*
	Filter out the nonsense: mapping must be allowed,
	the offset/length must be okay, it should be a
	plain file with no locks pending.
	*/

	if ( vp->v_flag & VNOMAP )
	{
	    return ENOSYS;
	}

	if ( (int) off < 0  ||  (int) (off + len) < 0 )
	{
		return EINVAL;
	}

	if ( vp->v_type != VREG )
	{
		return ENODEV;
	}

	ip = VTOI(vp);

	/*
	Mapping not implemented for biased objects;
	there is a easy way to remap from aligned
	to unaligned by adding the bias to the address
	as well as the lenght but in this case the
	user can't unmap anymore as the real mapped
	address is addr - bias.
	*/

	if ( ip->i_bias )
	{
		return ENOSYS;
	}

	if ( vp->v_filocks != NULL && MANDLOCK( vp, ISOFILEMODE( ip ) ) )
	{
		return EAGAIN;
	}

	/*
	If the user specified an address, invalidate
	all previous mapping.
	*/

	if ( flags & MAP_FIXED )
	{
		(void) as_unmap ( as, *addrp, len );
	}
	else
	{
		map_addr ( addrp, len, (off_t) off, 1 );

		if ( *addrp == NULL )
		{	
			return ENOMEM;
		}
	}

	ISOILOCK(ip);

	mapargs.vp      = vp;
	mapargs.offset  = off;
	mapargs.type    = flags & MAP_TYPE;
	mapargs.prot    = prot;
	mapargs.maxprot = maxprot;
	mapargs.cred    = cr;
	mapargs.amp     = NULL;

	error = as_map ( as, *addrp, len, segvn_create, (caddr_t)&mapargs );

	ISOIUNLOCK(ip);

	return error;
}



/* ARGUSED */

int
IsoAddMap ( vnode_t *vp, uint off, struct as *as, addr_t addr, uint len,
						u_char prot, u_char maxprot, uint flags, cred_t *cr )
{
#ifdef ISO9660_DEBUG
	printf ( "In IsoAddMap\n" );
#endif

	if ( vp->v_flag & VNOMAP )
	{
		return ENOSYS;
	}

	VTOI(vp)->i_mapcnt += btopr( len );
	return 0;
}


int
IsoDelMap ( vnode_t *vp, uint off, struct as *as, addr_t addr, uint len,
						u_char prot, u_char maxprot, uint flags, cred_t *cr )
{
	IsoNode_t*	ip;
#ifdef ISO9660_DEBUG
	printf ( "In IsoDelMap, len %x\n", len );
#endif

	if ( vp->v_flag & VNOMAP )
	{
		return ENOSYS;
	}

	ip = VTOI(vp);

	ip->i_mapcnt -= btopr( len );

	ASSERT ( ip->i_mapcnt >= 0 );
	return 0;
}




static int
IsoGetaPage ( vnode_t* vp, uint off, uint* protp, page_t* pl[], uint plsz,
			  struct seg* seg, addr_t addr, enum seg_rw rw, cred_t* cr )
{
	IsoNode_t*  ip;
	IsoVfs_t*   ivp;
	struct buf*	bp;
	vnode_t*	devvp;
	dev_t		dev;
	uint		bsize;
	uint		io_len;
	uint		io_off;
	int			total;
	page_t*		pp;
	page_t**	ppp;
	int			error;

#ifdef ISO9660_DEBUG
	printf( "In getapage: off %x\n", off );
#endif

	if ( pl != NULL )
	{
		pl[0] = NULL;
	}


retry:
	if (  (pp = page_find ( vp, off )) != NULL  )
	{
		/*
		If we found the page we can be in a crooked situation:
		we might have lost it due to interrupts as the page
		might have been on the free-list.
		*/

		int	s;


		s = splvm();

		if ( pp->p_vnode == vp && pp->p_offset == off )
		{
			/*
			If the page is still intransit or if it is on the free list
			re-call page_lookup to try and wait for/reclaim the page.
			*/

			if ( pp->p_intrans || pp->p_free )
			{
				pp = page_lookup ( vp, off );
			}
		}

		(void) splx(s);

		/*
		Now check whether we lost the page, if do we have
		to restart the whole process again.
		*/

		if ( pp == NULL || pp->p_offset != off ||
			 pp->p_vnode != vp || pp->p_gone      )
		{
			goto retry;
		}

		if ( pl != NULL )
		{
			PAGE_HOLD(pp);

			pl[0] = pp;
			pl[1] = NULL;
		}

		return 0;
	}

	/*
	The real work: we need to do the I/O now.
	Setup the page-list and call the strategy
	to get the blocks needed.
	*/

	pp = pvn_kluster ( vp, off, seg, addr, &io_off, &io_len,
										off & PAGEOFFSET, PAGESIZE, 0 );

	/*
	If someone was ahead of us we retry for
	the page, else we must do the disk I/O
	to get the pages filled.
	*/

	if ( pp == NULL )
	{
		goto retry;
	}

	if ( pl != NULL )
	{
		page_t*		ppt;
		int			size;

		ppt = pp;

		if ( plsz >= io_len )
		{
			/*
			Everything fits, get ready for the
			load and hold all gotten pages.
			*/

			size = io_len;
		}
		else
		{
			/*
			Need to load plsz; find the needed
			offset to do so.
			*/

			size = plsz;

			for (  ; ppt->p_offset != off; ppt = ppt->p_next ) ;
		}

		/*
		Now fill the asked list, hold all
		pages and make it NULL terminated.
		*/

		ppp = pl;

		do {
				PAGE_HOLD ( ppt );

				*ppp++ = ppt;
				ppt    = ppt->p_next;
				size  -= PAGESIZE;

		} while ( size > 0 );

		*ppp = NULL;
	}


	/*
	Now recalculate the io_len once more: we want to read a
	PAGESIZE but if the file is smaller or if the read is
	associated (which occurs for RR only) we must adjust here.
	*/

	ip     = VTOI(vp);
	ivp    = ip->i_vfs;

	bsize  = IsoBlkSize ( ivp );
	devvp  = ip->i_devvp;
	dev    = devvp->v_rdev;

	total  = i_total(ip) - io_off;

	if ( total < 0 )
	{
		io_len = bsize;
	}
	else if ( total < io_len )
	{
		io_len = total;
	}

	bp = pageio_setup ( pp, io_len, devvp, pl == NULL ?
									(B_ASYNC | B_READ) : B_READ );

	bp->b_edev  = dev;
	bp->b_dev   = (o_dev_t) cmpdev(dev);
	bp->b_blkno = IsoPhysBlk ( ip, ivp, io_off, bsize );

	ASSERT (  !(bp->b_blkno & SECTOFFSET)  );

	/*
	Clean the part we are not going to
	read in this routines and read in
	the stuff for this block.
	*/

	total = io_len & PAGEOFFSET;

	if ( total )
	{
		pagezero ( pp->p_prev, total, PAGESIZE - total );
	}

	(*bdevsw[getmajor(dev)].d_strategy)(bp);

	/*
	Keep the paging info upto date.
	*/

	vminfo.v_pgin++;
	vminfo.v_pgpgin += btopr(io_len);
	

	/*
	We did an I/O so we need to wait for it and release it.
	*/

	error = biowait ( bp );

	pageio_done ( bp );

	/*
	Clear the page list and release all holded
	pages if something went wrong on our way.
	*/

	if ( error && pl != NULL )
	{
		for ( ppp = pl; *ppp != NULL; *ppp++ = NULL )
		{
			PAGE_RELE ( *ppp );
		}
	}

	return error;

}


int
IsoGetPage ( vnode_t* vp, uint off, uint len, uint* protp, page_t* pl[],
			 uint plsz, struct seg* seg, addr_t addr, enum seg_rw rw,
			 cred_t* cr )
{
	IsoNode_t*	ip;
	int			error;

#ifdef ISO9660_DEBUG
	printf ( "In IsoGetPage: plsz %x, off %x, len %x\n", plsz, off, len );
#endif

	if (vp->v_flag & VNOMAP)
	{
		return ENOSYS;
	}

	ip = VTOI(vp);
	ISOILOCK(ip);

	/*
	Check whether one requests beyond EOF; return EFAULT if so.
	*/

	if (off + len > i_total(ip)+PAGEOFFSET && (seg != segkmap || rw != S_OTHER))
	{
		ISOIUNLOCK(ip);
		return EFAULT;
	}

	/*
	Indicate that everything is allowed with these pages.
	*/

	if ( protp != NULL )
	{
		*protp = PROT_ALL;	/* write too ??? */
	}

	/*
	Now get the page, if more than one page is needed we let pvn_getpages
	iterate for it using the page read-in function, else we call this
	function directly.
	*/

	if ( len <= PAGESIZE )
	{
		error = IsoGetaPage ( vp, off, protp, pl, plsz, seg, addr, rw, cr );
	}
	else
	{
		error = pvn_getpages ( IsoGetaPage, vp, off, len, protp, pl, plsz,
														seg, addr, rw, cr );
	}

	ISOIUNLOCK(ip);

	return error;
}


int
IsoPutPage ( vnode_t *vp, uint off, uint len, int flags, cred_t *cr )
{
	IsoNode_t*	ip;
	IsoVfs_t	*ivp;
	int			error;
	int			orgvcount;

#ifdef ISO9660_DEBUG
	printf ( "In IsoPutPage len %x, off %x\n", len, off );
#endif

	if ( vp->v_flag & VNOMAP )
	{
	    return ENOSYS;
	}

	ip  = VTOI ( vp );
	ivp = ip->i_vfs;


	/*
	We are done if there are no pages or
	the offset is  no part of the file.
	*/

	if ( vp->v_pages == NULL || off >= i_total(ip) )
	{
		return 0;
	}

	/*
	Prevent others from inactiving the vnode,
	and remember whether we should do it.
	*/

	error     = 0;
	orgvcount = vp->v_count;
	VN_HOLD(vp);


	if ( len == 0 )
	{
		/*
		Search the entire vp list for pages >= off
		and perform the action indicated by flags.
		*/

		pvn_vplist_dirty ( vp, off, flags );
	}
	else
	{
		/*
		Range from [off ... off+len) and check for
		dirty pages, which we can't handle as we
		are a read-only fs.
		*/

		uint	endoff;
		page_t*	dirtypages;

		endoff     = MIN ( off+len, ((i_total(ip) + MAXBOFFSET) & MAXBMASK) );
		dirtypages = pvn_range_dirty ( vp, off, endoff, IsoRndOff(ivp, off),
										    IsoRndUpOff(ivp, endoff), flags );
		if ( dirtypages != NULL )
		{
			pvn_fail ( dirtypages, B_WRITE | flags );
			error = EROFS;
		}
	}

	/*
	Now check whether we need to inactivate the
	vnode; we should if the count goes to zero
	and the original count was bigger than zero.
	*/

	if ( --vp->v_count == 0 && orgvcount > 0 )
	{
		IsoInActive ( vp, cr );
	}

	return error;

}



/*
 * Global vfs data structures for iso9660
 */
struct vnodeops iso9660_vnodeops = {

		IsoOpen,
        IsoClose,
        IsoRead,
        fs_nosys,			/* write */
        IsoIoctl,
        fs_setfl,
        IsoGetAttr,
        fs_nosys,			/* setattr */
        IsoAccess,
        IsoLookup,
        fs_nosys,			/* create */
        fs_nosys,			/* remove */
        fs_nosys,			/* link   */
        fs_nosys,			/* rename */
        fs_nosys,			/* mkdir  */
        fs_nosys,			/* rmdir  */
        IsoReadDir,
        fs_nosys,			/* fsync  */
        IsoReadLink,
        IsoFsync,
        IsoInActive,
        IsoFid,
		IsoLock,
        IsoUnlock,
        IsoSeek,
        fs_cmp,
        IsoFrLock,
        fs_nosys,       /* iso9660_space, */
        fs_nosys,       /* realvp */
        IsoGetPage,
        IsoPutPage,
        IsoMmap,
        IsoAddMap,
        IsoDelMap,
        fs_poll,
        fs_nosys,       /* dump */
        fs_pathconf,
        IsoAllocStore,
        fs_nosys,       /* filler */
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
        fs_nosys
};

struct vnodeops spec_iso9660_vnodeops = {

		IsoOpen,
        IsoClose,
        IsoRead,
        fs_nosys,
        IsoIoctl,
        fs_setfl,
        IsoGetAttr,
        fs_nosys,
        IsoAccess,
        IsoLookup,
        fs_nosys,
        fs_nosys,
        fs_nosys,
        fs_nosys,
        fs_nosys,
        fs_nosys,
        IsoReadDir,
        fs_nosys,
        IsoReadLink,
        IsoFsync,
        IsoInActive,
        IsoFid,
		IsoLock,
        IsoUnlock,
        IsoSeek,
        fs_cmp,
        IsoFrLock,
        fs_nosys,       /* iso9660_space, */
        fs_nosys,       /* realvp */
        IsoGetPage,
        IsoPutPage,
        fs_nosys,       /* iso9660_map, */
        fs_nosys,       /* iso9660_addmap, */
        fs_nosys,       /* iso9660_delmap, */
        fs_poll,
        fs_nosys,       /* dump */
        fs_pathconf,
        IsoAllocStore,
        fs_nosys,       /* filler */
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
        fs_nosys
};
