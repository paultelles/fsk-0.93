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

#ident "@(#)pcfs_lookup.c	1.6 93/10/17 "

#include "pcfs.h"

/*
 *  When we search a directory the blocks containing directory
 *  entries are read and examined.  The directory entries
 *  contain information that would normally be in the inode
 *  of a unix filesystem.  This means that some of a directory's
 *  contents may also be in memory resident denodes (sort of
 *  an inode).  This can cause problems if we are searching
 *  while some other process is modifying a directory.  To
 *  prevent one process from accessing incompletely modified
 *  directory information we depend upon being the soul owner
 *  of a directory block.  lbread/lbrelse provide this service.
 *  This being the case, when a process modifies a directory
 *  it must first acquire the disk block that contains the
 *  directory entry to be modified.  Then update the disk
 *  block and the denode, and then write the disk block out
 *  to disk.  This way disk blocks containing directory
 *  entries and in memory denode's will be in synch.
 */


/*ARGSUSED*/
int
pcfs_lookup ( vnode_t *dvp, char *name, vnode_t **vpp, struct pathname *pnp,
			  int flags, vnode_t *rdir, cred_t *cr )
{
#define	NONE	0
#define	FOUND	1

	u_long			bn;
	int				error;
	int				slotstatus;
	int				slotoffset;
	int				slotcluster;
	u_long			frcn;
	vnode_t			*vp;
	u_long			cluster;
	int				rootreloff;
	u_long			diroff;
	int				isadir;		/* ~0 if found direntry is a directory	*/
	u_long			scn;		/* starting cluster number		*/
	denode_t		*dp;
	denode_t		*tdp;
	pcfs_vfs_t		*pvp;
	lbuf_t			*bp;
	dosdirent_t		*dep;
	dosdirent_t		direntry;
	int				nlen;
	u_char			nm[DOSDIRSIZ+1];

#ifdef PCFS_DEBUG
	printf ( "In pcfs_lookup\n" );
#endif

	/*
	Be sure dp is a directory.  Since dos filesystems
	don't have the concept of execute permission anybody
	can search a directory.
	*/

	dp  = VTODE(dvp);
	pvp = dp->de_vfs;


	if ( !ISADIR(dp) )
	{
		return ENOTDIR;
	}


	nlen = strlen(name);

	if (  !nlen  ||  nlen == 1 && name[0] == '.'  )
	{
		/*
		No name or '.' equals this directory.
		*/

		VN_HOLD(dvp)
		*vpp = dvp;

		return 0;
	}

	/*
	We don't expect to be here for ".." in the
	root directory.
	*/

	ASSERT ( !(dvp->v_flag & VROOT) || nlen != 2 ||
	              name[0] != '.'  || name[1] != '.' );

	if ( flags & GETFREESLOT )
	{
		slotstatus = NONE;
		slotoffset = -1;
	}
	else
	{
		slotstatus = FOUND;
	}

	unix2dosfn ( (unsigned char *)name, nm, nlen );
	nm[DOSDIRSIZ] = '\0';

	/*
	See if the component of the pathname we are looking for
	is in the directory cache.  If so then do a few things
	and return.
	*/

	if ( vp = dnlc_lookup(dvp, (char*)nm, NOCRED) )
	{
		VN_HOLD(vp);

		*vpp = vp;

		return 0;
	}



	/*
	Search the directory pointed at by dvp for the
	name pointed at by nm.
	*/

	tdp = NULL;

	/*
	The outer loop ranges over the clusters that make
	up the directory.  Note that the root directory is
	different from all other directories.  It has a
	fixed number of blocks that are not part of the
	pool of allocatable clusters.  So, we treat it a
	little differently.
	The root directory starts at "cluster" 0.
	*/

	rootreloff = 0;
	bp         = 0;

	DELOCK ( dp );

	for (frcn = 0; ; frcn++)
	{
		if (error = pcbmap(dp, frcn, &bn, &cluster))
		{
			if (error == E2BIG)
				break;

			DEUNLOCK ( dp );
			return error;
		}

		bp = lbread ( pvp->vfs_dev, bn, pvp->vfs_bpcluster );

		if ( error = lgeterror(bp) )
		{
			lbrelse   ( bp );
			DEUNLOCK ( dp );

			return error;
		}

		dep = (struct direntry *)bp->b_un.b_addr;

		for ( diroff = 0;  diroff < pvp->vfs_depclust;  diroff++, dep++ )
		{

			/*
			If the slot is empty and we are still looking for
			an empty then remember this one.  If the slot is
			not empty then check to see if it matches what we
			are looking for.  If the slot has never been filled
			with anything, then the remainder of the directory
			has never been used, so there is no point in searching
			it.
			*/

			if (dep->deName[0] == SLOT_EMPTY || dep->deName[0] == SLOT_DELETED)
			{
				if (slotstatus != FOUND)
				{
					slotstatus = FOUND;

					if (cluster == PCFSROOT)
					{
						slotoffset = rootreloff;
					}
					else
					{
						slotoffset = diroff;
					}

					slotcluster = cluster;
				}

				if (dep->deName[0] == SLOT_EMPTY)
				{
					lbrelse(bp);
					goto notfound;
				}
			}
			else
			{
				/*
				Ignore volume labels (anywhere, not
				just the root directory).
				*/

				if ((dep->deAttributes & ATTR_VOLUME) == 0  &&
				    bcmp((char *)nm, (char *)dep->deName, 11) == 0)
				{
					/*
					Remember where this directory entry came from
					for whoever did this lookup.
					If this is the root directory we are interested
					in the offset relative to the beginning of the
					directory (not the beginning of the cluster).
					*/

					if (cluster == PCFSROOT)
						diroff = rootreloff;

					goto found;
				}
			}
			rootreloff++;
		}

		/*
		Release the buffer holding the directory cluster
		just searched.
		*/

		lbrelse(bp);
	}


notfound:
	;

	/*
	If we get here we didn't find the entry we were looking
	for.  But that's ok if we are creating or renaming and
	are at the end of the pathname and the directory hasn't
	been removed.
	Note that we hold no disk buffers at this point.
	*/

	if ( flags & GETFREESLOT )
	{
		if ( slotstatus == NONE )
		{
			dp->de_froffset  = 0;
			dp->de_frcluster = 0;
			dp->de_frspace   = 0;
		}
		else
		{
			dp->de_froffset  = slotoffset;
			dp->de_frcluster = slotcluster;
			dp->de_frspace   = 1;
		}
	}

	DEUNLOCK ( dp );

	return ENOENT;


found:
	;

	/*
	NOTE:  We still have the buffer with matched
	directory entry at this point. As deget may
	sleep we copy the entry and release the buf.
	*/

	direntry = *dep;
	isadir   = dep->deAttributes & ATTR_DIRECTORY;
	scn      = dep->deStartCluster;

	lbrelse(bp);

	error = deget(pvp, isadir, cluster, diroff, scn, &direntry, &tdp);

	if ( !error )
	{
		DEUNLOCK(tdp);

		vp = DETOV(tdp);
		dnlc_enter ( dvp, (char*) nm, vp, NOCRED );
		*vpp = vp;
	}

	DEUNLOCK ( dp );

	return error;
}

/*
 *  CREATEDE:	Create a new denode as indicated by the first argument
 *				in the directory passed as 2nd argument. Return an error
 *				or zero - if all went fine. If depp not equal NULL, the
 *				address of the new created denode in filled.
 */

int
createde ( denode_t *dep, vnode_t *dvp, denode_t **depp )
{
	int				bn;
	int				error;
	int				theoff;
	int				isadir = ISADIR(dep);
	u_long			newcluster;
	dosdirent_t		*ndep;
	denode_t		*ddep = VTODE(dvp);	/* directory to add to */
	pcfs_vfs_t		*pvp = dep->de_vfs;
	lbuf_t			*bp;

#ifdef PCFS_DEBUG
	printf ( "In createde\n" );
	printf ( "Create entry %s in %s - clust %d (%d)\n", dep->de_Name,
	    ddep->de_Name, ddep->de_StartCluster, ddep->de_frcluster );
#endif
	/*
	If no space left in the directory then allocate
	another cluster and chain it onto the end of the
	file.  There is one exception to this.  That is,
	if the root directory has no more space it can NOT
	be expanded.  extendfile() checks for and fails attempts to
	extend the root directory.  We just return an error
	in that case.
	*/

	if ( !ddep->de_frspace )
	{
		if (error = extendfile(ddep, 0, &newcluster))
		{
			return error;
		}

		/*
		If they want us to return with the denode gotten.
		*/

		if (depp)
		{
			error = deget(pvp, isadir, newcluster, 0,
						  (u_long)dep->de_StartCluster, &dep->de_de, depp);
			if (error)
			{
				return error;
			}
		}

		bp = lgetblk(pvp->vfs_dev, cntobn(pvp,newcluster), pvp->vfs_bpcluster);
		lclrbuf(bp);

		ndep  = (dosdirent_t *) paddr(bp);
#ifndef NO_GENOFF
		DE_GEN( *depp ) = DIR_GEN( ndep );
#endif
		*ndep = dep->de_de;

		if (error = lbwritewait(bp))
		{
			if ( depp )
			{
				deput ( *depp );
			}

			return error;
		}

		/*
		Let caller know where we put the directory entry. who ???
		*/

		ddep->de_frcluster = newcluster;
		ddep->de_froffset  = 0;

		return 0;
	}

	/*
	There is space in the existing directory.  So,
	we just read in the cluster with space.  Copy
	the new directory entry in.  Then write it to
	disk.
	NOTE:  DOS directories do not get smaller as
	clusters are emptied.
	*/

	if ( ddep->de_frcluster == PCFSROOT )
	{
		bn     = pvp->vfs_rootdirblk + (ddep->de_froffset / pvp->vfs_depclust);
		theoff = ddep->de_froffset % pvp->vfs_depclust;
	}
	else
	{
		bn     = cntobn(pvp, ddep->de_frcluster);
		theoff = ddep->de_froffset;
	}

	/*
	If they want us to return with the denode gotten.
	*/

	if ( depp )
	{
		error = deget(pvp, isadir, ddep->de_frcluster, ddep->de_froffset,
		                   (u_long)dep->de_StartCluster, &dep->de_de, depp);
		if (error)
		{
			return error;
		}
	}

	bp = lbread ( pvp->vfs_dev, bn, pvp->vfs_bpcluster );

	if ( error = lgeterror(bp) )
	{
		if ( depp )
		{
			deput ( *depp );
		}

		lbrelse(bp);
		return error;
	}

	ndep  = (dosdirent_t *)(bp->b_un.b_addr) + theoff;
	*ndep = dep->de_de;

	if ( error = lbwritewait(bp) )
	{
		if ( depp )
		{
			deput ( *depp );
		}
	}

	return error;
}

/*
 *  Read in a directory entry and mark it as being deleted.
 */
static int
markdeleted ( pcfs_vfs_t *pvp, u_long dirclust, u_long diroffset )
{
	int			offset;
	int			error;
	u_long		bn;
	dosdirent_t *ep;
	lbuf_t		*bp;

#ifdef PCFS_DEBUG
	printf ( "In markdeleted\n" );
#endif
	if (dirclust == PCFSROOT)
	{
		bn     = pvp->vfs_rootdirblk + (diroffset / pvp->vfs_depclust);
		offset = diroffset % pvp->vfs_depclust;
	}
	else
	{
		bn     = cntobn(pvp, dirclust);
		offset = diroffset;
	}

	bp = lbread( pvp->vfs_dev, bn, pvp->vfs_bpcluster );

	if ( error = lgeterror(bp) )
	{
		lbrelse(bp);
		return error;
	}

	ep = (dosdirent_t *)bp->b_un.b_addr + offset;
	ep->deName[0] = SLOT_DELETED;

	/*
	Might do lbdwrite here; the reference
	is set to zero (i.e: no one will deget
	it anymore). inactive will de the rest.
	*/

	lbdwrite ( bp );

	return 0;
}

/*
 *  Remove a directory entry.
 *  At this point the file represented by the directory
 *  entry to be removed is still full length until no
 *  one has it open.  When the file no longer being
 *  used pcfs_inactive() is called and will truncate
 *  the file to 0 length.
 */
int
removede ( vnode_t *vp )
{
	denode_t	*dp;
	pcfs_vfs_t	*pvp;
	int			error;

#ifdef PCFS_DEBUG
	printf ( "In removede\n" );
#endif

	dp  = VTODE(vp);		/* the file being removed */
	pvp = dp->de_vfs;

	DELOCK ( dp );

	/*
	Read the directory block containing the directory
	entry we are to make free.
	*/

	error = markdeleted ( pvp, dp->de_dirclust, dp->de_diroffset );

	dp->de_refcnt--;

	deput ( VTODE(vp) );

	return error;
}

/*
 *  Be sure a directory is empty except for "." and "..".
 *  Return 1 if empty, return 0 if not empty or error.
 */
int
dosdirempty ( denode_t *dep )
{
	int			dei;
	int			error;
	u_long		cn;
	u_long		bn;
	lbuf_t		*bp;
	pcfs_vfs_t	*pvp;
	dosdirent_t	*dentp;

#ifdef PCFS_DEBUG
	printf ( "In dosdirempty\n" );
#endif

	pvp = dep->de_vfs;

	/*
	Since the filesize field in directory entries for a directory
	is zero, we just have to feel our way through the directory
	until we hit end of file.
	*/

	for ( cn = 0;  ; cn++ )
	{
		error = pcbmap(dep, cn, &bn, 0);

		if (error == E2BIG)
		{
			return 1;	/* it's empty */
		}

		bp = lbread(pvp->vfs_dev, bn, pvp->vfs_bpcluster );

		if ( error = lgeterror(bp) )
		{
			lbrelse(bp);
			return error;
		}

		dentp = (dosdirent_t *) bp->b_un.b_addr;

		for ( dei = 0; dei < pvp->vfs_depclust; dei++ )
		{
			if (dentp->deName[0] != SLOT_DELETED)
			{
				/*
				In dos directories an entry whose name starts with SLOT_EMPTY
				starts the beginning of the unused part of the directory, so
				we can just return that it is empty.
				*/

				if (dentp->deName[0] == SLOT_EMPTY)
				{
					lbrelse(bp);
					return 1;
				}

				/*
				Any names other than "." and ".." in a directory
				mean it is not empty.
				*/

				if (bcmp((char *)dentp->deName, ".          ", 11)  &&
				    bcmp((char *)dentp->deName, "..         ", 11))
				{
					lbrelse(bp);
#ifdef PCFS_DEBUG
					printf("dosdirempty(): entry %d found %02x, %02x\n", dei,
											dentp->deName[0], dentp->deName[1]);
#endif
					return 0;	/* not empty */
				}
			}

			dentp++;
		}

		lbrelse(bp);
	}

	/*NOTREACHED*/
}

/*
 *  Check to see if the directory described by target is
 *  in some subdirectory of source.  This prevents something
 *  like the following from succeeding and leaving a bunch
 *  or files and directories orphaned.
 *	mv /a/b/c /a/b/c/d/e/f
 *  Where c and f are directories.
 *  source - the inode for /a/b/c
 *  target - the inode for /a/b/c/d/e/f
 *  Returns 0 if target is NOT a subdirectory of source.
 *  Otherwise returns a non-zero error number.
 *  The target inode is expected to be locked on entrance;
 *  it is (re-)locked at return.
 *  This routine never crosses devices (mountpoints).
 */

int
doscheckpath ( denode_t *source, denode_t *target )
{
	u_long		scn;
	pcfs_vfs_t	*pvp;
	dosdirent_t	direntry;
	denode_t	*newdep;
	denode_t	*dep;
	lbuf_t		*bp;
	int			error;

#ifdef PCFS_DEBUG
	printf ( "In doscheckpath\n" );
#endif

	ASSERT ( target->de_flag & DELOCKED );

	if ( !ISADIR(target) || !ISADIR(source) )
	{
		return ENOTDIR;
	}

	if ( target->de_StartCluster == source->de_StartCluster )
	{
		return EEXIST;
	}

	if ( target->de_StartCluster == PCFSROOT )
	{
		return 0;
	}

	dep = target;
	pvp = dep->de_vfs;

	for (;;)
	{
		scn = dep->de_StartCluster;
		bp  = lbread(pvp->vfs_dev, cntobn(pvp, scn), pvp->vfs_bpcluster);

		if ( error = lgeterror(bp) )
		{
			lbrelse(bp);
			break;
		}

		/*
		Go up by using the .. entry. Copy that
		entry and release the buffer. We keep
		the directory locked till we don't need
		the buffer info anymore.
		*/

		direntry = *((dosdirent_t *) paddr(bp) + 1);
		lbrelse(bp);

		if ((direntry.deAttributes & ATTR_DIRECTORY) == 0  ||
		    bcmp((char *)direntry.deName, "..         ", 11) != 0)
		{
			error = ENOTDIR;
			break;
		}

		if (direntry.deStartCluster == source->de_StartCluster)
		{
			error = EINVAL;
			break;
		}

		if (direntry.deStartCluster == PCFSROOT)
		{
			break;
		}

		/*
		First deget the .. entry then deput the current
		one to keep locked.
		NOTE: deget() clears dep on error.
		*/

		error = deget ( pvp, ATTR_DIRECTORY, scn, 1,
						(u_long)direntry.deStartCluster, &direntry, &newdep);
		/*
		Never deput the target, others could
		reuse the denode. We just unlock it
		and relock it at the end.
		*/

		if ( dep == target )
		{
			DEUNLOCK ( dep );
		}
		else
		{
			deput ( dep );
		}

		if (error)
		{
			break;
		}

		dep = newdep;

		if ( !ISADIR(dep) )
		{
			error = ENOTDIR;
			break;
		}

	}

	/*
	Now we most check whether we need to
	relock the target - the target should
	be locked at the end. If the current
	dep is the target, we are done else
	we need to relock it and check whether
	it still exist.
	*/

	if ( dep != target )
	{
		if (dep != NULL)
		{
			deput(dep);
		}

		DELOCK ( target );

		if ( error == 0 && target->de_Name[0] == SLOT_DELETED )
			error = ENOENT;
	}

	return error;
}

/*
 *  Read in the disk block containing the directory entry
 *  dep came from and return the address of the buf header,
 *  and the address of the directory entry within the block.
 */
int
readde ( denode_t *dep, lbuf_t **bpp, dosdirent_t **epp )
{
	int 		error;
	u_long		bn;
	u_long		theoff;
	pcfs_vfs_t	*pvp = dep->de_vfs;

#ifdef PCFS_DEBUG
	printf ( "In readde\n" );
#endif

	if (dep->de_dirclust == PCFSROOT)
	{
		bn     = pvp->vfs_rootdirblk + (dep->de_diroffset / pvp->vfs_depclust);
		theoff = dep->de_diroffset % pvp->vfs_depclust;
	}
	else
	{
		bn     = cntobn(pvp, dep->de_dirclust);
		theoff = dep->de_diroffset;
	}

	*bpp = lbread(pvp->vfs_dev, bn, pvp->vfs_bpcluster );

	if ( error = lgeterror(*bpp) )
	{
		lbrelse(*bpp);
		*bpp = NULL;
		return error;
	}

	if (epp)
	{
		*epp = (struct direntry *)((*bpp)->b_un.b_addr) + theoff;
	}

	return 0;
}


/*
 * get_direntp: Like readde, other arguments, slightly different
 *				results.
 */

int
get_direntp ( pcfs_vfs_t *pvp, u_long dirclust, u_long diroffset,
			  dosdirent_t *dep )
{
	int		error;
	u_long	bn;
	lbuf_t*	bp;
	u_long	theoff;

#ifdef PCFS_DEBUG
	printf ( "In get_direntp\n" );
#endif

	if ( dirclust == PCFSROOT )
	{
		bn     = pvp->vfs_rootdirblk + (diroffset / pvp->vfs_depclust);
		theoff = diroffset % pvp->vfs_depclust;
	}
	else
	{
		bn     = cntobn(pvp, dirclust);
		theoff = diroffset;
	}

	bp = lbread ( pvp->vfs_dev, bn, pvp->vfs_bpcluster );

	error = lgeterror(bp);

	if ( !error )
	{
		*dep = *((dosdirent_t*) bp->b_un.b_addr + theoff);
	}

	lbrelse(bp);
	return error;
}
