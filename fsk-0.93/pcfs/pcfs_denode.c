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

#ident "@(#)pcfs_denode.c	1.7 93/10/17 "

#include "pcfs.h"

#define	DEHSZ	512
#if ((DEHSZ & (DEHSZ-1)) == 0)
#define	DEHASH(dev, deno)	(((dev)+(deno)+((deno)>>16))&(DEHSZ-1))
#else
#define	DEHASH(dev, deno)	(((dev)+(deno)+((deno)>>16))%DEHSZ)
#endif /* ((DEHSZ & (DEHSZ-1)) == 0) */

union dehead {
	union dehead  *deh_head[2];
	struct denode *deh_chain[2];
} dehead[DEHSZ];



extern long int		ndenode;
int					pcfstype;
#ifndef NO_GENOFF
int					pcfs_shared; /* tells whether shared in kernel life */
#endif
struct denode*		denode;
struct denode*		dfreeh;		/* denode free head */
struct denode*		dfreet;		/* denode free tail */

static void
denodeinit(void)
{
	struct denode*	dp;
	union dehead*	deh;
	int				i;

	denode = (struct denode *) kmem_zalloc(ndenode*sizeof(*dp), KM_SLEEP);

	if ( denode == NULL )
	{
		cmn_err(CE_PANIC, "denodeinit: no memory for pcfs inodes");
	}

	dp = denode;

	for (i = DEHSZ, deh = dehead; --i >= 0; deh++)
	{
		deh->deh_head[0] = deh;
		deh->deh_head[1] = deh;
	}

	/*
	Setup free list of denodes, link all
	configured denodes and intialize them.
	*/

	dfreeh = dp;

	dp->de_freeb = NULL;

	for ( i = ndenode;  ;  )
	{
		dp->de_forw = dp;
		dp->de_back = dp;

		dp->de_vnode.v_data = (caddr_t) dp;
		dp->de_vnode.v_op   = &pcfs_vnodeops;

		if ( --i == 0 )
			break;

		dp->de_freef = dp+1;
		dp++;
		dp->de_freeb = dp-1;
	}

	dp->de_freef = NULL;
	dfreet = dp;

}

pcfsinit(struct vfssw *vswp, int fstype)
{
	extern struct vfsops pcfs_vfsops;

	denodeinit();

	vswp->vsw_vfsops = &pcfs_vfsops;
	pcfstype         = fstype;
#ifndef NO_GENOFF
	pcfs_shared		 = 0;
#endif
	return 0;
}

static void
dehash ( denode_t *dp, union dehead *deh )
{
	deh->deh_chain[0]->de_back = dp;
	dp->de_forw = deh->deh_chain[0];
	deh->deh_chain[0] = dp;
	dp->de_back = (denode_t *) deh;
}


void
deunhash ( denode_t *dp )
{
	dp->de_back->de_forw = dp->de_forw;
	dp->de_forw->de_back = dp->de_back;

	dp->de_forw = dp->de_back = dp;
}


/*
 *  pcfs_deflush:	called by pcfs_sync, till now write-out
 *					all updated denodes and sync the buffers.
 */

void
pcfs_deflush(void)
{
	register denode_t	*dp;
	register vnode_t	*vp;
	register int		i;

	/*
	Update active, nolocked, changed denodes,
	which reside on read/write filesystems
	(deupdat does a lot of the checking).
	*/

	for (i = 0, dp = denode; i < ndenode; i++, dp++)
	{
		if (  (dp->de_flag & DEREF) && !(dp->de_flag & DELOCKED)  )
		{
			vp = DETOV( dp );

			ASSERT ( vp->v_count != 0  &&  vp->v_vfsp != NULL );

			DELOCK  ( dp );
			VN_HOLD ( vp );

			(void)deupdat( dp, &hrestime, 0 );

			deput ( dp );
		}
	}

	/*
	Now flush all blocks with a delayed write
	pending, all should be written now.
	*/

	bflush ( NODEV );
}


int
deflush ( struct vfs *vfsp, int force )
{
	register denode_t	*dp;
	register vnode_t	*vp;
	register vnode_t	*rvp;
	register int		i;
	dev_t				dev;

	dev = vfsp->vfs_dev;
	rvp = PCFS_VFS(vfsp)->vfs_root;

	/*
	This search should run through the hash chains (rather
	than the entire inode table) so that we only examine
	inodes that we know are currently valid.
	*/

	for (i = 0, dp = denode; i < ndenode; i++, dp++)
	{
		if (dp->de_dev != dev)
			continue;

		vp = DETOV(dp);
		if (vp == rvp)
		{
			if (vp->v_count > 1 && force == 0)
				return -1;
			continue;
		}

		if (vp->v_count == 0)
		{
			if (vp->v_vfsp == 0)
				continue;

			if ( dp->de_flag & DELOCKED )
			{
				if (force)
					continue;
#ifdef PCFS_DEBUG
#endif
				cmn_err( CE_CONT, "lock left on %s\n", dp->de_Name );
				return -1;
			}

			/*
			Thoroughly dispose of this denode.  Flush
			any associated pages and remove it from
			its hash chain.
			*/

			DELOCK(dp);	/* Won't sleep */

			if (dp->de_flag & DEWANT)
			{
				DEUNLOCK(dp);
				if (force)
					continue;
				return -1;
			}

			DEUNLOCK(dp);
			deunhash(dp);
		}
		else if (force == 0)
		{
#ifdef PCFS_DEBUG
#endif
			cmn_err( CE_CONT, "vnode count not null %s (%d)\n",
			    dp->de_Name, vp->v_count );
			return -1;
		}
	}

	return 0;
}



/*
 *  If deget() succeeds it returns with the gotten denode
 *  locked().
 *  pvp - address of pcfs_vfs structure of the filesystem
 *    containing the denode of interest.  The vfs_dev field
 *    and the address of the pcfs_vfs structure are used. 
 *  isadir - a flag used to indicate whether the denode of
 *    interest represents a file or a directory.
 *  dirclust - which cluster bp contains, if dirclust is 0
 *    (root directory) diroffset is relative to the beginning
 *    of the root directory, otherwise it is cluster relative.
 *  diroffset - offset past begin of cluster of denode we
 *    want
 *  startclust - number of 1st cluster in the file the
 *    denode represents.  Similar to an inode number.
 *  direntp - pointer to the dos dir entry.
 *  depp - returns the address of the gotten denode.
 */

int
deget ( pcfs_vfs_t *pvp, int isadir, u_long dirclust, u_long diroffset,
		u_long startclust, dosdirent_t *direntp, struct denode **depp )
{
	dev_t		  dev;
	union dehead* deh;
	denode_t*	  ldep;
	vnode_t*	  nvp;

	/*
	See if the denode is in the denode cache.
	If the denode is for a directory then use the
	startcluster in computing the hash value.  If
	a regular file then use the location of the directory
	entry to compute the hash value.  We use startcluster
	for directories because several directory entries
	may point to the same directory.  For files
	we use the directory entry location because
	empty files have a startcluster of 0, which
	is non-unique and because it matches the root
	directory too.  I don't think the dos filesystem
	was designed.
	
	NOTE: The check for de_refcnt > 0 below insures the denode
	being examined does not represent an unlinked but
	still open file.  These files are not to be accessible
	even when the directory entry that represented the
	file happens to be reused while the deleted file is still
	open.
	*/

	dev = pvp->vfs_dev;

	if (isadir)
	{
		deh = &dehead[DEHASH(dev, startclust)];
	}
	else
	{
		deh = &dehead[DEHASH(dev, dirclust+diroffset)];
	}

loop:
	for ( ldep = deh->deh_chain[0]; ldep != (struct denode *)deh;
	      ldep = ldep->de_forw )
	{
		if (dev == ldep->de_dev  &&  ldep->de_refcnt > 0)
		{
			if ( ISADIR(ldep) )
			{
				if ( ldep->de_StartCluster != startclust  || !isadir )
				{
					continue;
				}
			}
			else
			{
				if ( isadir                          ||
				     dirclust  != ldep->de_dirclust  ||
				     diroffset != ldep->de_diroffset    )
				{
					continue;
				}
			}

			if ((ldep->de_flag & DELOCKED) && ldep->de_owner != curproc->p_pid)
			{
				ldep->de_flag |= DEWANT;
				sleep((caddr_t)ldep, PINOD);
				goto loop;
			}

			/*
			Remove from freelist if it's on.
			*/

			if ( (ldep->de_flag & DEREF) == 0 )
			{
				if ( ldep->de_freeb ) ldep->de_freeb->de_freef = ldep->de_freef;
				else				  dfreeh                   = ldep->de_freef;

				if ( ldep->de_freef ) ldep->de_freef->de_freeb = ldep->de_freeb;
				else				  dfreet                   = ldep->de_freeb;

				ldep->de_freef = NULL;
				ldep->de_freeb = NULL;
			}

			ldep->de_flag |= DEREF;
			DELOCK ( ldep );

			VN_HOLD ( DETOV(ldep) );

			*depp = ldep;
			return 0;
		}
	}


	/*
	We can enter with direntp == NULL in case of a vget;
	read in the dirent, cleanup and reloop as we might
	fall asleep along this way.
	*/

	if ( direntp == NULL  &&  (startclust != PCFSROOT  ||  !isadir) )
	{
		dosdirent_t		dirent;

		if ( get_direntp ( pvp, dirclust, diroffset, &dirent ) ||
							  dirent.deName[0] == SLOT_DELETED ||
						      dirent.deName[0] == SLOT_EMPTY       )
		{
			return ENOENT;
		}

		direntp = &dirent;

		goto loop;
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

	if ( dfreeh == NULL )
	{
		do {
			if (  !dnlc_purge1()  )
				break;
		} while ( dfreeh == NULL );

		if ( dfreeh == NULL )
		{
			cmn_err ( CE_WARN, "deget: out of pcfs inodes" );
			return  ENFILE;
		}
		else
		{
			goto loop;
		}
	}

	ldep = dfreeh;

	/*
	Got one, remove it from the freelist,
	the hash list and reinitialize it.
	*/

	dfreeh = ldep->de_freef;

	if ( dfreeh )
	{
		dfreeh->de_freeb = NULL;
	}
	else
	{
		dfreet = NULL;
	}

	ldep->de_freef = NULL;
	ldep->de_freeb = NULL;

	deunhash ( ldep );

	ldep->de_flag  = DEREF;
	ldep->de_devvp = 0;
	ldep->de_lockf = 0;
	ldep->de_dev   = dev;

	fc_purge ( ldep, 0 );	/* init the fat cache for this denode */

	/*
	Insert the denode into the hash queue and lock the
	denode so it can't be accessed until we've read it
	in and have done what we need to it.
	*/

	dehash ( ldep, deh );

	DELOCK ( ldep );

	/*
	Copy the directory entry into the denode area of the
	vnode.  If they are going after the directory entry
	for the root directory, there isn't one so we manufacture
	one.
	We should probably rummage through the root directory and
	find a label entry (if it exists), and then use the time
	and date from that entry as the time and date for the
	root denode.
	*/

	if ( startclust == PCFSROOT  &&  isadir )
	{
		ldep->de_Attributes   = ATTR_DIRECTORY;
		ldep->de_StartCluster = PCFSROOT;
		ldep->de_FileSize     = 0;

		/*
		Fill in time and date so that dos2unixtime() doesn't split
		up when called from pcfs_getattr() with root denode. Fill
		in name ROOT (no use but handy for debugging) and leave
		out other fields.
		*/

		ldep->de_Time = 0x0000;							/* 00:00:00	*/
		ldep->de_Date = (0 << 9) | (1 << 5) | (1 << 0); /* Jan 1, 1980	*/
		ldep->de_vfs  = pvp;

		strcpy ( (char*) ldep->de_Name, "/ROOT" );
	}
	else
	{
		ldep->de_de = *direntp;
	}

	/*
	Fill in a few fields of the vnode and finish filling
	in the denode.  Then return the address of the found
	denode.
	*/

	nvp = DETOV(ldep);

	if (ldep->de_Attributes & ATTR_DIRECTORY)
	{
		nvp->v_type = VDIR;
		if (startclust == PCFSROOT)
			nvp->v_flag = VROOT | VNOMAP;
	}
	else
	{
		nvp->v_type = VREG;
	    nvp->v_flag = VNOMAP;
	}

	nvp->v_count   = 1;
	nvp->v_vfsp    = pvp->vfs_vfsp;
	nvp->v_stream  = NULL;
	nvp->v_pages   = NULL;
	nvp->v_filocks = NULL;
	nvp->v_rdev    = pvp->vfs_dev;

	ldep->de_vfs       = pvp;
	ldep->de_devvp     = pvp->vfs_devvp;
	ldep->de_refcnt    = 1;
	ldep->de_dirclust  = dirclust;
	ldep->de_diroffset = diroffset;

	*depp = ldep;

	return 0;
}


/*
 *  dereject:	kind of inactivate without having been active.
 */

void
dereject ( denode_t *dp )
{
	vnode_t*	vp = DETOV ( dp );

#ifdef PCFS_DEBUG
	printf ( "In dereject\n" );
#endif

	ASSERT ( dp->de_flag & DELOCKED );

	if ( --vp->v_count == 0 )
	{
		dp->de_flag    = 0;
		dp->de_nrlocks = 0;

		dp->de_freef = dfreeh;
		dp->de_freeb = NULL;

		if ( dfreeh == NULL )
		{
			dfreeh = dp;
			dfreet = dp;
		}
		else
		{
			dfreeh->de_freeb = dp;
			dfreeh           = dp;
		}
		deunhash(dp);
	}
	else
	{
		DEUNLOCK ( dp );
	}
}


void
deput ( denode_t *dep )
{
	if ((dep->de_flag & DELOCKED) == 0)
		cmn_err ( CE_PANIC, "deput: denode not locked");

	DEUNLOCK(dep);

	VN_RELE(DETOV(dep));
}

int
deupdat ( denode_t *dep, timestruc_t *tp, int waitfor )
{
	lbuf_t*			bp;
	dosdirent_t*	dirp;
	vnode_t*	  	vp;
	int				error;

#ifdef PCFS_DEBUG
	printf ( "In deupdate\n" );
#endif

	vp = DETOV(dep);

	/*
	If the update bit is off, or this denode is from
	a readonly filesystem, or this denode is for a
	directory, or the denode represents an open but
	unlinked file then don't do anything.  DOS directory
	entries that describe a directory do not ever
	get updated.  This is the way dos treats them.
	*/

	if ( (dep->de_flag & (DEUPD|DEMOD)) == 0   ||
	     (vp->v_vfsp->vfs_flag & VFS_RDONLY)   ||
	     (dep->de_Attributes & ATTR_DIRECTORY)     )
	{
		return 0;
	}

	/*
	Read in the cluster containing the directory entry
	we want to update.
	*/

	if (error = readde(dep, &bp, &dirp))
	{
		return error;
	}

	/*
	Put the passed in time into the directory entry.
	*/

	unix2dostime(tp, (dosdate_t *)&dep->de_Date, (dostime_t *)&dep->de_Time);

	dep->de_flag &= ~(DEUPD|DEMOD);

	/*
	Copy the directory entry out of the denode into
	the cluster it came from.
	*/

	*dirp = dep->de_de;	/* structure copy */

	/*
	Write the cluster back to disk.  If they asked
	for us to wait for the write to complete, then
	use lbwritewait() otherwise use lbdwrite().
	*/

	if (waitfor)
	{
		error = lbwritewait(bp);
	}
	else
	{
		lbdwrite(bp);
	}

	return error;
}

/*
 *  Truncate the file described by dep to the length
 *  specified by length.
 */
int
detrunc ( denode_t *dp, u_long length, int flags )
{
	int			error;
	int			allerror;
	u_long 		eofentry;
	u_long 		chaintofree;
	u_long 		bn;
	int 		boff;
	int 		isadir;
	lbuf_t*		bp;
	pcfs_vfs_t*	pvp;

#ifdef PCFS_DEBUG
	printf ( "In detrunc\n" );
#endif

	pvp    = dp->de_vfs;
	isadir = dp->de_Attributes & ATTR_DIRECTORY;

	/*
	Disallow attempts to truncate the root directory
	since it is of fixed size.  That's just the way
	dos filesystems are.  We use the VROOT bit in the
	vnode because checking for the directory bit and
	a startcluster of 0 in the denode is not adequate
	to recognize the root directory at this point in
	a file or directory's life.
	*/

	if (DETOV(dp)->v_flag & VROOT)
	{
		cmn_err ( CE_WARN, "detrunc: can't truncate root directory, "
						   "clust %ld, offset %ld\n",
		    						dp->de_dirclust, dp->de_diroffset );
		return EINVAL;
	}

	/*
	If we are going to truncate a directory then we better
	find out how long it is.  DOS doesn't keep the length of
	a directory file in its directory entry.
	Note: pcbmap() returns the # of clusters in the file.
	*/

	if (isadir)
	{
		error = pcbmap(dp, 0xffff, 0, &eofentry);

		if (error != 0  &&  error != E2BIG)
		{
			return error;
		}

		dp->de_FileSize = eofentry << pvp->vfs_cnshift;
	}

	if (dp->de_FileSize <= length)
	{
		dp->de_flag |= DEUPD;

		error = deupdat(dp, &hrestime, 1);
		return error;
	}

	/*
	If the desired length is 0 then remember the starting
	cluster of the file and set the StartCluster field in
	the directory entry to 0.  If the desired length is
	not zero, then get the number of the last cluster in
	the shortened file.  Then get the number of the first
	cluster in the part of the file that is to be freed.
	Then set the next cluster pointer in the last cluster
	of the file to CLUST_EOFE.
	*/

	if (length == 0)
	{
		chaintofree = dp->de_StartCluster;
		dp->de_StartCluster = 0;
		eofentry = (u_long) ~0;
	}
	else
	{
		error = pcbmap ( dp, (u_long)((length-1) >> pvp->vfs_cnshift), 0,
																  &eofentry );
		if (error)
		{
			return error;
		}
	}

	fc_purge ( dp, (length + pvp->vfs_crbomask) >> pvp->vfs_cnshift );

	/*
	If the new length is not a multiple of the cluster size
	then we must zero the tail end of the new last cluster in case
	it becomes part of the file again because of a seek.
	*/

	if ( (boff = length & pvp->vfs_crbomask) != 0 )
	{
		bn = cntobn(pvp, eofentry);
		bp = lbread(pvp->vfs_dev, bn, pvp->vfs_bpcluster);

		if ( error = lgeterror(bp) )
		{
			lbrelse(bp);
			return error;
		}

		bzero(bp->b_un.b_addr + boff, pvp->vfs_bpcluster - boff);

		if (flags & IO_SYNC)
		{
			lbwrite(bp);
		}
		else
		{
			lbdwrite(bp);
		}
	}

	/*
	Write out the updated directory entry.  Even
	if the update fails we free the trailing clusters.
	*/

	dp->de_FileSize  = length;
	dp->de_flag     |= DEUPD;

	allerror = deupdat(dp, &hrestime, 1);

	/*
	If we need to break the cluster chain for the file
	then do it now.
	*/

	if ( eofentry != (u_long) ~0 )
	{
		error = fatentry ( FAT_GET_AND_SET, pvp, eofentry,
		    						&chaintofree, CLUST_EOFE );
		if (error)
		{
			return error;
		}

		fc_setcache(dp, FC_LASTFC, (length - 1) >> pvp->vfs_cnshift, eofentry);
	}

	/*
	Now free the clusters removed from the file because
	of the truncation.
	*/

	if ( chaintofree != 0  &&  !PCFSEOF(chaintofree) )
	{
		(void)freeclusterchain(pvp, chaintofree);
	}

	return allerror;
}

/*
 *  Move a denode to its correct hash queue after
 *  the file it represents has been moved to a new
 *  directory.
 */
void
reinsert ( denode_t *dep )
{
	pcfs_vfs_t   *pvp = dep->de_vfs;
	union dehead *deh;

#ifdef PCFS_DEBUG
	printf ( "In reinsert\n" );
#endif
	/*
	Fix up the denode cache.  If the denode is
	for a directory, there is nothing to do since the
	hash is based on the starting cluster of the directory
	file and that hasn't changed.  If for a file the hash
	is based on the location
	of the directory entry, so we must remove it from the
	cache and re-enter it with the hash based on the new
	location of the directory entry.
	*/

	if ((dep->de_Attributes & ATTR_DIRECTORY) == 0)
	{
		deunhash(dep);
		deh = &dehead[DEHASH(pvp->vfs_dev, dep->de_dirclust+dep->de_diroffset)];
		dehash(dep, deh);
	}
}


/*
 *  pcfs_inactive:	Called when the last reference of the file
 *		   			is released. If the file has been removed
 *					meanwhile this removal gets active now.
 */

/*ARGSUSED*/
void
pcfs_inactive ( vnode_t *vp, cred_t *cr )
{
	denode_t *dep = VTODE(vp);

#ifdef PCFS_DEBUG
	printf ( "In inactive for ->%s<-\n", dep->de_Name );
#endif

	/*
	If the file has been deleted then truncate the file.
	(This may not be necessary for the dos filesystem.)
	*/

	DELOCK(dep);

	if ( dep->de_refcnt <= 0 )
	{
#ifndef NO_GENOFF
		/*
		Fill in a new generation number for the
		next occupying this denode.
		*/

		if ( pcfs_shared )
		{
			DE_GEN(dep)++;
		}
#endif
		dep->de_Name[0] = SLOT_DELETED;
		(void) detrunc(dep, (u_long)0, 0);
		deunhash(dep);
	}
	else
	{
		DEUPDAT(dep, &hrestime, 0);
	}

	DEUNLOCK(dep);

	if ( dep->de_nrlocks != 0 )
	{
		cmn_err (CE_WARN, "pcfs_inactive: locks still set %s\n", dep->de_Name );
	}

	dep->de_flag    = 0;
	dep->de_nrlocks = 0;

	/*
	If we are done with the denode, then insert
	it so that it can be reused now, else put on
	the end of the queue.
	*/

	if ( dep->de_refcnt <= 0 || dfreeh == NULL )
	{
		dep->de_freef = dfreeh;
		dep->de_freeb = NULL;

		if ( dfreeh == NULL )
		{
			dfreeh = dep;
			dfreet = dep;
		}
		else
		{
			dfreeh->de_freeb = dep;
			dfreeh           = dep;
		}
	}
	else
	{
		dep->de_freef    = NULL;
		dep->de_freeb    = dfreet;
		dfreet->de_freef = dep;
		dfreet           = dep;
	}
}

void
delock ( denode_t *dep )
{
#ifdef PCFS_DEBUG
	printf ( "In delock\n" );
#endif

	while ( (dep->de_flag & DELOCKED) && dep->de_owner != curproc->p_pid )
	{
		dep->de_flag  |= DEWANT;
		(void) sleep((caddr_t)dep, PINOD);
	}

	dep->de_owner = curproc->p_pid;
	dep->de_flag |= DELOCKED;

	dep->de_nrlocks++;

	ASSERT ( dep->de_nrlocks > 0 );
}

void
deunlock ( denode_t *dep )
{
#ifdef PCFS_DEBUG
	printf ( "In deunlock\n" );
#endif

	ASSERT ( dep->de_flag & DELOCKED );

	dep->de_nrlocks--;

	ASSERT ( dep->de_nrlocks >= 0 );

	if ( dep->de_nrlocks == 0 )
	{
		dep->de_owner = 0;
		dep->de_flag &= ~DELOCKED;

		if (dep->de_flag & DEWANT)
		{
			dep->de_flag &= ~DEWANT;
			wakeup((caddr_t)dep);
		}
	}
}

#ifdef PCFS_DEBUG
PcfsCheckInodes()
{
	denode_t*	dp;
	int			i;

	for ( i = 0, dp = denode; i < ndenode; i++, dp++ )
	{
		if ( dp->de_flag & DEREF )
		{
			printf ( "->%s<- vcnt %d", dp->de_Name, DETOV(dp)->v_count );

			if ( dp->de_flag & DELOCKED )	printf ( " (lock left)\n" );
			else							printf ( "\n" );
		}
	}
}
#endif /* PCFS_DEBUG */
