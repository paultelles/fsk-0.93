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

#ident "@(#)iso9660_node.c	1.3 93/09/16 "

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
 *	@(#)iso9660_inode.c
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/disp.h>
#include <sys/buf.h>
#include <sys/time.h>
#include <sys/errno.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/cmn_err.h>
#include <sys/statvfs.h>
#include <sys/kmem.h>
#include <sys/swap.h>
#include <sys/cred.h>
#include <sys/debug.h>
#include <sys/pathname.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/flock.h>

#include "iso9660.h"
#include "iso9660_node.h"

#define	INOHSZ	512
#if	((INOHSZ&(INOHSZ-1)) == 0)
#define	INOHASH(dev,ino)	(((dev)+(ino))&(INOHSZ-1))
#else
#define	INOHASH(dev,ino)	(((unsigned)((dev)+(ino)))%INOHSZ)
#endif

union iso9660_ihead {

	union  iso9660_ihead *ih_head[2];
	struct iso9660_node	 *ih_chain[2];

} iso9660_ihead[INOHSZ];


typedef union iso9660_ihead	IsoIhead_t;



int prtactive;	/* 1 => print out reclaim of active vnodes */


extern long					iso9660ninode;
int							iso9660type;
static struct iso9660_node*	inode;
static struct iso9660_node*	ifreeh;	/* inode free head */
static struct iso9660_node*	ifreet;	/* inode free tail */


extern struct vnodeops	iso9660_vnodeops;
extern struct vnodeops	spec_iso9660_vnodeops;

/*
 * Initialize hash links for inodes.
 */
void
IsoNodeInit()
{
	int			i;
	IsoIhead_t	*ih;
	IsoNode_t	*ip;


#ifdef ISO9660_DEBUG
#endif
	inode = (IsoNode_t*) kmem_zalloc(iso9660ninode*sizeof(*ip), KM_SLEEP);

	if ( inode == NULL )
	{
		cmn_err(CE_PANIC, "iso9660_init: no memory for inodes");
	}

	for ( i = INOHSZ, ih = iso9660_ihead; --i >= 0; ih++ )
	{
		ih->ih_head[0] = ih;
		ih->ih_head[1] = ih;
	}

	ip = inode;

	ifreeh      = ip;
	ip->i_freeb = NULL;

	for ( i = iso9660ninode;  ; )
	{
		ip->i_forw = ip;
		ip->i_back = ip;
		ip->i_vnode.v_data = (caddr_t) ip;

		if ( --i == 0 )
			break;
		
		ip->i_freef = ip+1;
		ip++;
		ip->i_freeb = ip-1;
	}

	ip->i_freef = NULL;
	ifreet      = ip;
}


iso9660init ( struct vfssw* vswp, int fstype )
{
	extern struct vfsops iso9660_vfsops;

#ifdef ISO9660_DEBUG
#endif
	IsoNodeInit();

	vswp->vsw_vfsops = &iso9660_vfsops;
	iso9660type	     = fstype;

	return 0;
}


void
IsoHash ( IsoNode_t* ip, IsoIhead_t* ih )
{
#ifdef ISO9660_DEBUG
#endif
	ih->ih_chain[0]->i_back = ip;
	ip->i_forw = ih->ih_chain[0];
	ih->ih_chain[0] = ip;
	ip->i_back = (IsoNode_t*) ih;
}


void
IsoUnHash ( IsoNode_t* ip )
{
#ifdef ISO9660_DEBUG
#endif
	ip->i_back->i_forw = ip->i_forw;
	ip->i_forw->i_back = ip->i_back;
	ip->i_forw = ip->i_back = ip;
}


int
IsoFlush ( struct vfs* vfsp )
{
	IsoNode_t	*ip;
	vnode_t		*vp;
	vnode_t		*rvp;
	int			inuse;
	int			i;
	dev_t		dev;

#ifdef ISO9660_DEBUG
	printf ( "In IsoFlush\n" );
#endif
	inuse = 0;
	dev   = vfsp->vfs_dev;
	rvp   = VFSTOISO9660(vfsp)->im_root;

	/*
	This search should run through the hash chains (rather
	than the entire inode table) so that we only examine
	inodes that we know are currently valid.
	*/

	for (i = 0, ip = inode; i < iso9660ninode; i++, ip++)
	{
		if (ip->i_dev != dev)
			continue;

		vp = ITOV(ip);

		if ( (ip->i_flag & IREF)  &&  (vp != rvp || vp->v_count > 1) )
		{
			inuse = 1;
			continue;
		}

		if ( ip->i_flag & ILOCKED )
		{
			cmn_err( CE_CONT, "lock left on \n" );
			inuse = 1;
			continue;
		}

		/*
		Thoroughly dispose of this denode.  Flush
		any associated pages and remove it from
		its hash chain.
		*/

		ISOILOCK(ip);	/* Won't sleep */

		if (ip->i_flag & IWANT)
		{
			ISOIUNLOCK(ip);
			inuse = 1;
			continue;
		}

		(void) IsoPutPage( vp, 0, 0, B_INVAL, (cred_t*) 0 );

		ISOIUNLOCK(ip);
		IsoUnHash(ip);
	}

	return inuse;
}



/*
 * IsoRejectInode:	A kind of IsoInActive() with a lock set
 *					and which is called when needed.
 */

void
IsoRejectInode ( IsoNode_t* ip )
{
	vnode_t*	vp;

	ISOIUNLOCK(ip);
	
	vp = ITOV(ip);

	if ( --vp->v_count == 0 )
	{
		IsoInActive ( vp, (cred_t*) 0 );
	}
}




/*
 * Look up a ISO9660 dinode number to find its incore vnode.
 * If it is not in core, read it in from the specified device.
 * If it is in core, wait for the lock bit to clear, then
 * return the inode locked. Detection and handling of mount
 * points must be done by the calling routine.
 */
int
IsoIget ( IsoNode_t* xp, IsoFileId_t fileid, IsoNode_t** ipp,
			   IsoDirInfo_t* dirp )
{
	dev_t		dev;
	IsoNode_t	*ip;
	vnode_t		*vp;
	IsoIhead_t	*ih;
	IsoVfs_t	*ivp;

#ifdef ISO9660_DEBUG
	printf ( "In IsoIget, xp = %x\n", xp );
#endif
	dev = xp->i_dev;
	ih  = &iso9660_ihead[INOHASH(dev, fileid)];

loop:
	for ( ip = ih->ih_chain[0]; ip != (IsoNode_t *)ih; ip = ip->i_forw )
	{
		if (fileid != ip->fileid || dev != ip->i_dev)
			continue;

		if ( (ip->i_flag & ILOCKED) && ip->i_owner != curproc->p_pid )
		{
			ip->i_flag |= IWANT;
			sleep((caddr_t)ip, PINOD);
			goto loop;
		}

		/*
		Got one; remove it from the freelist if it's on.
		*/

		if (  (ip->i_flag & IREF) == 0  )
		{
			if ( ip->i_freeb )	ip->i_freeb->i_freef = ip->i_freef;
			else				ifreeh				 = ip->i_freef;

			if ( ip->i_freef )	ip->i_freef->i_freeb = ip->i_freeb;
			else				ifreet				 = ip->i_freeb;

			ip->i_freef = NULL;
			ip->i_freeb = NULL;
		}

		ip->i_flag |= IREF;

		ISOILOCK ( ip );

		VN_HOLD ( ITOV(ip) );

		*ipp = ip;
		return 0;
	}

	/* 
	Not in cache, if no directory pointer given
	we must flag it as no entry as we have no
	way to create an entry.
	*/

	if (dirp == NULL)
	{
		*ipp = 0;
		return ENOENT;
	}

	/*
	Directory entry was not in cache, have to get
	one from the free list. If the list is empty
	try to purge the cache.
	Note that dnlc_purge1 can (indirectly) sleep
	so we have to check again and see what happened
	meanwhile (someone else could have "degotten"
	the same file).
	*/

	if ( ifreeh == NULL )
	{
		do {
			if (  !dnlc_purge1()  )
				break;
		} while ( ifreeh == NULL );

		if ( ifreeh == NULL )
		{
			cmn_err ( CE_WARN, "IsoIget - out of inodes" );
			return  ENFILE;
		}
		else
		{
			goto loop;
		}
	}

	ip = ifreeh;


	/*
	Got one, remove it from the freelist,
	the hash list and reinitialize it.
	*/

	ifreeh = ip->i_freef;


	if ( ifreeh )
	{
		ifreeh->i_freeb = NULL;
	}
	else
	{
		ifreet = NULL;
	}

	ip->i_freef = NULL;
	ip->i_freeb = NULL;

	/*
	Before we go on we need to check whether we took
	this inode already busy with a VOP_PUTPAGE if so
	discard this one and take another if possible.
	VOP_PUTPAGE performs an VN_HOLD as an indication
	no one else messes around with free vnode.
	If the freelist is empty it makes no sense to try
	another one - there won't be one. In this case we
	let other processes run by calling preempt() and
	hope the situaltion will relax, else we tail the
	inode - bet we don't need it the next iteraction.
	*/

	vp = ITOV(ip);

	if ( vp->v_count > 0 )
	{
		if ( ifreeh )
		{
			ip->i_freef     = NULL;
			ip->i_freeb     = ifreet;
			ifreet->i_freef = ip;
			ifreet          = ip;
		}
		else
		{
			ifreeh = ip;
			ifreet = ip;

			preempt();
		}

		goto loop;
	}

	IsoUnHash ( ip );

	ip->i_flag = IREF;

	/*
	Lock the inode so that other requests for this inode will
	block if they arrive while we are sleeping waiting for old
	data structures to be purged or for the contents of the
	disk portion of this inode to be read.
	*/

	ISOILOCK(ip);

	if ( IsoPutPage( vp, 0, 0, B_INVAL, (cred_t*) 0 ) || (ip->i_flag & IWANT) )
	{
		/*
		We came a long way ... and did not made it.
		*/

		VN_HOLD(vp);
		ISOIUNLOCK(ip);

		IsoRejectInode ( ip );
		goto loop;
	}


	/*
	Finally, we are okay; put it onto its
	hash chain and fill in the rest.
	*/

	ip->i_dev       = dev;
	ip->fileid      = fileid;
	
	IsoHash ( ip, ih );

	ip->extent      = dirp->extent;
	ip->i_size      = dirp->size;
	ip->i_bias      = dirp->bias;
	ip->time_parsed = 0;
	ip->mtime       = 0;
	ip->atime       = 0;
	ip->ctime       = 0;

	bcopy ( (caddr_t)dirp->iso9660_time, (caddr_t)ip->iso9660_time,
												sizeof ip->iso9660_time );
	ip->rr = dirp->rr;

	/*
	Initialize the associated vnode.
	*/

	if (dirp->rr.rr_flags & RR_PX)
	{
		switch (dirp->rr.rr_mode & 0170000) {

		case 0010000: vp->v_type = VFIFO; break;
		case 0020000: vp->v_type = VCHR;  break;
		case 0040000: vp->v_type = VDIR;  break;
		case 0060000: vp->v_type = VBLK;  break;
		case 0100000: vp->v_type = VREG;  break;
		case 0120000: vp->v_type = VLNK;  break;

		case 0140000: /*vp->v_type = VSOCK; break; ??? */
		default:      vp->v_type = VNON;  break;

		}
	}
	else if (dirp->iso9660_flags & ISO9660_DIR_FLAG)
	{
		vp->v_type = VDIR;
	}
	else
	{
		vp->v_type = VREG;
	}

	if (vp->v_type == VCHR || vp->v_type == VBLK)
	{
		vp->v_op = &spec_iso9660_vnodeops;
#ifdef NEVER
		if (nvp = checkalias(vp, ip->rr.rr_rdev, mntp))
		{
			IsoNode_t	*iq;

			/*
			Reinitialize aliased inode.
			*/

			vp = nvp;
			iq = VTOI(vp);
			iq->i_vnode = vp;
			iq->i_flag = 0;
			ISOILOCK(iq);
			iq->rr = ip->rr;
			iq->i_dev = dev;
			iq->fileid = fileid;
			insque(iq, ih);

			/*
			Discard unneeded vnode
			*/

			ip->deleted = 1;
			IsoIput(ip);
			ip = iq;
		}
#endif NEVER
	}
	else
	{
		vp->v_op = &iso9660_vnodeops;
	}

	ivp = xp->i_vfs;

	/*
	Finish inode initialization.
	*/

	ip->i_vfs   = ivp;
	ip->i_devvp = ivp->im_devvp;

    vp->v_count   = 1;
	vp->v_vfsp    = ivp->im_vfsp;
   	vp->v_stream  = NULL;
   	vp->v_pages   = NULL;
   	vp->v_filocks = NULL;
   	vp->v_rdev    = ivp->im_dev;
	vp->v_flag    = 0;

	if ( fileid == ivp->root_fileid )
	{
		vp->v_flag |= VROOT;
	}

	*ipp = ip;
	return 0;
}


/*
 * Unlock and decrement the reference count of an inode structure.
 */
void
IsoIput ( IsoNode_t* ip )
{
#ifdef ISO9660_DEBUG
#endif
	if ((ip->i_flag & ILOCKED) == 0)
	{
		cmn_err(CE_PANIC, "IsoIput: not locked" );
	}

	ISOIUNLOCK(ip);
	VN_RELE (ITOV(ip));
}


/*
 * Last reference to an inode, write the inode out and if necessary,
 * truncate and deallocate the file.
 */
void
IsoInActive ( vnode_t* vp, cred_t* cr )
{
	IsoNode_t	*ip;

#ifdef ISO9660_DEBUG
#endif
	ip = VTOI(vp);

	ASSERT ( ip->i_nrlocks == 0 );
	ASSERT ( ip->i_mapcnt  == 0 );

	ip->i_flag    = 0;
	ip->i_nrlocks = 0;

	/*
	Put the node at the end of the list.
	*/

	if ( ifreeh == NULL )
	{
		ip->i_freef = ifreeh;
		ip->i_freeb = NULL;

		if ( ifreeh == NULL )
		{
			ifreeh = ip;
			ifreet = ip;
		}
		else
		{
			ifreeh->i_freeb = ip;
			ifreeh          = ip;
		}
	}
	else
	{
		ip->i_freef     = NULL;
		ip->i_freeb     = ifreet;
		ifreet->i_freef = ip;
		ifreet			= ip;
	}
}

void
IsoPut ( IsoNode_t* ip )
{
	if ((ip->i_flag & ILOCKED) == 0)
		cmn_err ( CE_PANIC, "deput: inode not locked");

	ISOIUNLOCK( ip );

	VN_RELE( ITOV(ip) );
}


/*
 * Lock an inode. If its already locked, set the WANT bit and sleep.
 */

void
IsoIlock ( IsoNode_t* ip )
{
#ifdef ISO9660_DEBUG
#endif
	while ( (ip->i_flag & ILOCKED) && ip->i_owner != curproc->p_pid )
	{
		ip->i_flag |= IWANT;
		(void) sleep((caddr_t)ip, PINOD);
	}

	ip->i_owner = curproc->p_pid;
	ip->i_flag |= ILOCKED;

	ip->i_nrlocks++;

	ASSERT ( ip->i_nrlocks > 0 );
}

/*
 * Unlock an inode.  If WANT bit is on, wakeup.
 */
void
IsoIunlock ( IsoNode_t* ip )
{
#ifdef ISO9660_DEBUG
#endif
	ASSERT ( ip->i_flag & ILOCKED );

	ip->i_nrlocks--;

	ASSERT ( ip->i_nrlocks >= 0 );

	if ( ip->i_nrlocks == 0 )
	{
		ip->i_owner = 0;
		ip->i_flag &= ~ILOCKED;

		if ( ip->i_flag & IWANT )
		{
			ip->i_flag &= ~IWANT;
			wakeup ( (caddr_t) ip );
		}
	}
}
