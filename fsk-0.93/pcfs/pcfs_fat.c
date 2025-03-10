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

#ident "@(#)pcfs_fat.c	1.6 93/10/17 "

#include "pcfs.h"

/*
 *  Fat cache stats.
 */

int fc_fileextends = 0;	/* # of file extends */
int fc_lfcempty    = 0;	/* # of time last file cluster cache entry was empty */
int fc_bmapcalls   = 0;	/* # of times pcbmap was called	*/
int fc_bmapnext    = 0;	/* # of times pcbmap was called	*/

#define	LMMAX	20

/*
Counters for how far off the
last cluster mapped entry was.
*/
int fc_lmdistance[LMMAX];
int fc_largedistance     = 0;	/* off by more than LMMAX		*/

/*
 *  Map the logical cluster number of a file into
 *  a physical disk sector that is filesystem relative.
 *  dep - address of denode representing the file of interest
 *  findcn - file relative cluster whose filesystem relative
 *    cluster number and/or block number are/is to be found
 *  bnp - address of where to place the file system relative
 *    block number.  If this pointer is null then don't return
 *    this quantity.
 *  cnp - address of where to place the file system relative
 *    cluster number.  If this pointer is null then don't return
 *    this quantity.
 *  NOTE:
 *    Either bnp or cnp must be non-null.
 *    This function has one side effect.  If the requested
 *    file relative cluster is beyond the end of file, then
 *    the actual number of clusters in the file is returned
 *    in *cnp.  This is useful for determining how long a
 *    directory is.
 */

int
pcbmap ( denode_t *dep, u_long findcn, u_long *bnp, u_long *cnp )
{
	int				error;
	u_long			i;
	u_long			cn;
	u_long			prevcn;
	u_long			bn;
	u_long			bo;
	lbuf_t			*bp0;
	u_long			bp0_bn;
	pcfs_vfs_t		*pvp;
	fattwiddle_t	x;

#ifdef PCFS_DEBUG
	printf ( "In pcbmap for ->%s<- frcn %x\n", dep->de_Name, findcn );
#endif
	fc_bmapcalls++;

	/*
	If they don't give us someplace to return a value
	then don't bother doing anything.
	*/

	if ( bnp == NULL && cnp == NULL )
	{
		return 0;
	}

	i   = 0;
	cn  = dep->de_StartCluster;
	pvp = dep->de_vfs;

	/*
	The "file" that makes up the root directory is contiguous,
	permanently allocated, of fixed size, and is not made up
	of clusters.  If the cluster number is beyond the end of
	the root directory, then return the number of clusters in
	the file.
	*/

	if (cn == PCFSROOT)
	{
		if ( ISADIR(dep) )
		{
			if ( findcn * pvp->vfs_SectPerClust > pvp->vfs_rootdirsize )
			{
				if (cnp)
				{
					*cnp = pvp->vfs_rootdirsize / pvp->vfs_SectPerClust;
				}

				return E2BIG;
			}

			if (bnp)
			{
				*bnp = pvp->vfs_rootdirblk + (findcn * pvp->vfs_SectPerClust);
			}

			if (cnp)
			{
				*cnp = PCFSROOT;
			}

			return 0;
		}
		else
		{
			/* Just an empty file. */
			if (cnp)
			{
				*cnp = 0;
			}

			return E2BIG;
		}
	}

	/*
	Rummage around in the fat cache, maybe we can avoid
	tromping thru every fat entry for the file.
	And, keep track of how far off the cache was from
	where we wanted to be.
	*/

	fc_lookup ( dep, findcn, &i, &cn );

	if ( (bn = findcn - i) >= LMMAX )
	{
		fc_largedistance++;
	}
	else
	{
		fc_lmdistance[bn]++;
	}

	/*
	Handle all other files or directories the normal way.
	*/

	bp0    = 0;
	bp0_bn = (u_long) ~0;	/* all ones */
	bn     = 0;

	if ( FAT12(pvp) )
	{
		lbuf_t	*bp1;

		bp1 = 0;

		for (  ; i < findcn; i++ )
		{
			if ( PCFSEOF(cn) )
			{
				goto hiteof;
			}

			bo  = cn + (cn >> 1);
			bn  = (bo >> pvp->vfs_bnshift) + pvp->vfs_fatblk;
			bo &= pvp->vfs_brbomask;

			if ( bn != bp0_bn )
			{
				if ( bp0 )
				{
					lbrelse(bp0);
				}

				bp0 = lbread ( pvp->vfs_dev, bn, pvp->vfs_BytesPerSec );

				if ( error = lgeterror(bp0) )
				{
					lbrelse(bp0);
					return error;
				}

				bp0_bn = bn;
			}

			x.byte[0] = bp0->b_un.b_addr[bo];

			/*
			If the first byte of the fat entry was the last byte
			in the block, then we must read the next block in the
			fat.  We hang on to the first block we read to insure
			that some other process doesn't update it while we
			are waiting for the 2nd block.
			Note that we free bp1 even though the next iteration of
			the loop might need it.
			*/

			if ( bo == pvp->vfs_BytesPerSec-1 )
			{
				bp1 = lbread ( pvp->vfs_dev, bn+1, pvp->vfs_BytesPerSec );

				if ( error = lgeterror(bp1) )
				{
					lbrelse(bp0);
					lbrelse(bp1);

					return error;
				}

				x.byte[1] = bp1->b_un.b_addr[0];

				lbrelse ( bp1 );
			}
			else
			{
				x.byte[1] = bp0->b_un.b_addr[bo+1];
			}

			if (cn & 1)
			{
				x.word >>= 4;
			}

			prevcn = cn;
			cn     = x.word & 0x0fff;

			/*
			Force the special cluster numbers in the range
			0x0ff0-0x0fff to be the same as for 16 bit cluster
			numbers to let the rest of pcfs think it is always
			dealing with 16 bit fats.
			*/

			if ((cn & 0x0ff0) == 0x0ff0)
			{
				cn |= 0xf000;
			}
		}
	}
	else				/* 16 bit fat */
	{
		u_long	fsrcn;
		u_long	frcn;
		int		ci;

		for (  ; i < findcn; i++ )
		{
			if (PCFSEOF(cn))
			{
				goto hiteof;
			}

			bo  = cn << 1;
			bn  = (bo >> pvp->vfs_bnshift) + pvp->vfs_fatblk;
			bo &= pvp->vfs_brbomask;

			if ( bn != bp0_bn )
			{
				if ( bp0 )
				{
					lbrelse ( bp0 );
				}

				bp0 = lbread ( pvp->vfs_dev, bn, pvp->vfs_BytesPerSec );

				if ( error = lgeterror(bp0) )
				{
					lbrelse(bp0);
					return error;
				}

				bp0_bn = bn;
			}

			prevcn = cn;
			cn     = *(u_short *)(bp0->b_un.b_addr+bo);
		}

		/*
		If we did I/O (bn == bp0_bn) and as long we are on the same block
		we try to fill the denode fat cache with next cluster numbers to
		come - hopefully this will speed-up stuff for the comming reads.
		This mechanism is especially useful when clusters are allocated
		close to each other (which is tried by clusteralloc()) and files.
		are read contiguously.
		*/

		fsrcn = cn;

		for ( frcn = i+1, ci = FC_NEXTMAP; ci < FC_LASTFC; ci++, frcn++ )
		{
			if ( PCFSEOF(fsrcn) )
			{
				break;
			}

			bo  = fsrcn << 1;
			bn  = (bo >> pvp->vfs_bnshift) + pvp->vfs_fatblk;
			bo &= pvp->vfs_brbomask;

			if ( bn != bp0_bn )
			{
				break;
			}

			fsrcn = *(u_short *)(bp0->b_un.b_addr+bo);
			
			fc_bmapnext++;
			fc_setcache ( dep, ci, frcn, fsrcn );
		}
	}

	if ( !PCFSEOF(cn) )
	{
		if (bp0)
		{
			lbrelse(bp0);
		}

		if (bnp)
		{
			*bnp = cntobn(pvp, cn);
		}

		if (cnp)
		{
			*cnp = cn;
		}

		fc_setcache ( dep, FC_LASTMAP, i, cn );
		return 0;
	}

hiteof:
	;
	if (cnp)
	{
		*cnp = i;
	}

	if (bp0)
	{
		lbrelse(bp0);
	}

	/*
	Update last file cluster entry in the
	fat cache.
	*/

	fc_setcache ( dep, FC_LASTFC, i-1, prevcn );
	return E2BIG;
}

/*
 *  Find the closest entry in the fat cache to the
 *  cluster we are looking for.
 */
void
fc_lookup ( denode_t *dep, u_long findcn, u_long *frcnp, u_long *fsrcnp )
{
	u_long		cn;
	fatcache_t	*closest;
	fatcache_t	*fcp;

#ifdef PCFS_DEBUG
	printf ( "In fc_lookup\n" );
#endif

	closest = 0;

	for ( fcp = dep->de_fc;  fcp < &dep->de_fc[FC_SIZE];  fcp++ )
	{
		cn = fcp->fc_frcn;

		if ( cn != FCE_EMPTY  &&  cn <= findcn )
		{
			if ( closest == 0  ||  cn > closest->fc_frcn )
			{
				if ( cn == findcn )
				{
					*frcnp  = fcp->fc_frcn;
					*fsrcnp = fcp->fc_fsrcn;

					return;
				}

				closest = fcp;

			}
		}
	}

	if (closest)
	{
		*frcnp  = closest->fc_frcn;
		*fsrcnp = closest->fc_fsrcn;
	}
}

/*
 *  Purge the fat cache in denode dep of all entries
 *  relating to file relative cluster frcn and beyond.
 */
void
fc_purge ( denode_t *dep, u_int frcn )
{
	fatcache_t	*fcp;

#ifdef PCFS_DEBUG
	printf ( "In fc_purge\n" );
#endif

	for ( fcp = dep->de_fc;  fcp < &dep->de_fc[FC_SIZE];  fcp++ )
	{
		if ( fcp->fc_frcn != FCE_EMPTY  &&  fcp->fc_frcn >= frcn )
		{
			fcp->fc_frcn = FCE_EMPTY;
		}
	}
}

/*
 *  Once the first fat is updated the other copies of
 *  the fat must also be updated.  This function does
 *  this.
 *  pvp - pcfs_vfs structure for filesystem to update
 *  bp0 - addr of modified fat block
 *  bp1 - addr of 2nd modified fat block (0 if not needed)
 *  fatbn - block number relative to begin of filesystem
 *    of the modified fat block.
 */
void
updateotherfats ( pcfs_vfs_t *pvp, lbuf_t *bp0, lbuf_t *bp1, u_long fatbn )
{
	int		i;
	lbuf_t	*bpn0;
	lbuf_t	*bpn1;

#ifdef PCFS_DEBUG
	printf ( "In updateotherfats\n" );
#endif

	/*
	Now copy the block(s) of the modified fat to the other
	copies of the fat and write them out.  This is faster
	than reading in the other fats and then writing them
	out.  This could tie up the fat for quite a while.
	Preventing others from accessing it.  To prevent us
	from going after the fat quite so much we use delayed
	writes, unless they specfied "synchronous" when the
	filesystem was mounted.  If synch is asked for then
	use lbwrite()'s and really slow things down.
	*/

	for (i = 1; i < (int) pvp->vfs_FATs; i++)
	{
		fatbn += pvp->vfs_FATsecs;
		bpn0   = lgetblk ( pvp->vfs_dev, fatbn, pvp->vfs_BytesPerSec );

		bcopy ( bp0->b_un.b_addr, bpn0->b_un.b_addr, pvp->vfs_BytesPerSec );

		if (pvp->vfs_waitonfat) lbwrite(bpn0);
		else					lbdwrite(bpn0);

		if (bp1)
		{
			bpn1 = lgetblk ( pvp->vfs_dev, fatbn+1, pvp->vfs_BytesPerSec );
			bcopy ( bp1->b_un.b_addr, bpn1->b_un.b_addr, pvp->vfs_BytesPerSec );

			if (pvp->vfs_waitonfat) lbwrite(bpn1);
			else					lbdwrite(bpn1);
		}
	}
}

/*
 *  Updating entries in 12 bit fats is a pain in the butt.
 *  So, we have a function to hide this ugliness.
 *
 *  The following picture shows where nibbles go when
 *  moving from a 12 bit cluster number into the appropriate
 *  bytes in the FAT.
 *
 *      byte m        byte m+1      byte m+2
 *    +----+----+   +----+----+   +----+----+
 *    |  0    1 |   |  2    3 |   |  4    5 |   FAT bytes
 *    +----+----+   +----+----+   +----+----+
 *
 *       +----+----+----+ +----+----+----+
 *       |  3    0    1 | |  4    5    2 |
 *       +----+----+----+ +----+----+----+
 *         cluster n        cluster n+1
 *
 *    Where n is even.
 *    m = n + (n >> 2)
 *
 *  This function is written for little endian machines.
 *  least significant byte stored into lowest address.
 */
void
setfat12slot ( lbuf_t *bp0, lbuf_t *bp1, u_long oddcluster, u_int byteoffset,
			   u_long newvalue )
{
	u_char			*b0;
	u_char			*b1;
	fattwiddle_t	x;

#ifdef PCFS_DEBUG
	printf ( "In setfat12slot\n" );
#endif
	/*
	If we have a 2nd buf header and the byte offset is not the
	last byte in the buffer, then something is fishy.  Better
	tell someone.
	*/

	if ( bp1  &&  byteoffset != 511 )
	{
		cmn_err (CE_WARN, "setfat12slot: bp1 %08lx, byteoffset %d incorrect",
		    (long)bp1, byteoffset);
	}

	/*
	Get address of 1st byte and 2nd byte setup
	so we don't worry about which buf header to
	be poking around in.
	*/

	b0 = (u_char *)&bp0->b_un.b_addr[byteoffset];

	if (bp1)
	{
		b1 = (unsigned char *)&bp1->b_un.b_addr[0];
	}
	else
	{
		b1 = b0 + 1;
	}

#ifdef PCFS_DEBUG
	printf("setfat12: offset %d, old %02x%02x, new %04x\n", byteoffset,
														*b0, *b1, newvalue ); 
#endif

	if (oddcluster)
	{
		x.word = newvalue << 4;
		*b0    = (*b0 & 0x0f) | x.byte[0];
		*b1    = x.byte[1];
	}
	else
	{
		x.word = newvalue & 0x0fff;
		*b0    = x.byte[0];
		*b1    = (*b1 & 0xf0) | x.byte[1];
	}
#ifdef PCFS_DEBUG
	printf("setfat12(): result %02x%02x\n", *b0, *b1);
#endif
}

int
clusterfree ( pcfs_vfs_t *pvp, u_long cluster )
{
	int error;

#ifdef PCFS_DEBUG
	printf ( "In clusterfree\n" );
#endif

	error = fatentry ( FAT_SET, pvp, cluster, 0, PCFSFREE );

	if ( !error )
	{
		/*
		If the cluster was successfully marked free,
		then update the count of free clusters, and
		turn off the "allocated" bit in the "in use"
		cluster bit map.
		*/
		pvp->vfs_freeclustercount++;
		pvp->vfs_inusemap[cluster >> 3] &= ~(1 << (cluster & 0x07));
	}

	return error;
}

/*
 *  Get or Set or 'Get and Set' the cluster'th entry in the
 *  fat.
 *  function - whether to get or set a fat entry
 *  pvp - address of the pcfs_vfs structure for the
 *    filesystem whose fat is to be manipulated.
 *  cluster - which cluster is of interest
 *  oldcontents - address of a word that is to receive
 *    the contents of the cluster'th entry if this is
 *    a get function
 *  newcontents - the new value to be written into the
 *    cluster'th element of the fat if this is a set
 *    function.
 *
 *  This function can also be used to free a cluster
 *  by setting the fat entry for a cluster to 0.
 *
 *  All copies of the fat are updated if this is a set
 *  function.
 *  NOTE:
 *    If fatentry() marks a cluster as free it does not
 *    update the inusemap in the pcfs_vfs structure.
 *    This is left to the caller.
 */
int
fatentry ( int function, pcfs_vfs_t *pvp, u_long cluster, u_long *oldcontents,
		   u_long newcontents )
{
	int				error;
	u_long			whichbyte;
	u_long			whichblk;
	lbuf_t			*bp0 = 0;
	lbuf_t			*bp1 = 0;
	fattwiddle_t	x;

#ifdef PCFS_DEBUG
	printf ( "In fatentry\n" );
#endif

	/*
	Be sure they asked us to do something.
	*/

	if ((function & (FAT_SET | FAT_GET)) == 0)
	{
#ifdef PCFS_DEBUG
		printf("fatentry(): function code doesn't specify get or set\n");
#endif
		return EINVAL;
	}

	/*
	If they asked us to return a cluster number
	but didn't tell us where to put it, give them
	an error.
	*/

	if ((function & FAT_GET)  &&  oldcontents == NULL)
	{
#ifdef PCFS_DEBUG
		printf("fatentry(): get function with no place to put result\n");
#endif
		return EINVAL;
	}

	/*
	Be sure the requested cluster is in the filesystem.
	*/

	if (cluster < CLUST_FIRST || cluster > pvp->vfs_maxcluster)
	{
		return EINVAL;
	}

	if (FAT12(pvp))
	{
		whichbyte  = cluster + (cluster >> 1);
		whichblk   = (whichbyte >> pvp->vfs_bnshift) + pvp->vfs_fatblk;
		whichbyte &= pvp->vfs_brbomask;

		/*
		Read in the fat block containing the entry of interest.
		If the entry spans 2 blocks, read both blocks.
		*/

		bp0 = lbread ( pvp->vfs_dev, whichblk, pvp->vfs_BytesPerSec );

		if ( error = lgeterror(bp0) )
		{
			lbrelse(bp0);
			return error;
		}

		if ( whichbyte == (pvp->vfs_BytesPerSec-1) )
		{
			bp1 = lbread ( pvp->vfs_dev, whichblk+1, pvp->vfs_BytesPerSec );

			if ( error = lgeterror(bp1) )
			{
				lbrelse(bp1);
				return error;
			}
		}

		if (function & FAT_GET)
		{
			x.byte[0] = bp0->b_un.b_addr[whichbyte];
			x.byte[1] = bp1 ? bp1->b_un.b_addr[0] :
							  bp0->b_un.b_addr[whichbyte+1];
			if (cluster & 1)
			{
				x.word >>= 4;
			}

			x.word &= 0x0fff;
			/* map certain 12 bit fat entries to 16 bit */
			if ((x.word & 0x0ff0) == 0x0ff0)
			{
				x.word |= 0xf000;
			}
			*oldcontents = x.word;
		}

		if (function & FAT_SET)
		{
			setfat12slot    ( bp0, bp1, cluster & 1, whichbyte, newcontents );
			updateotherfats ( pvp, bp0, bp1, whichblk );

			/*
			 *  Write out the first fat last.
			 */
			if (pvp->vfs_waitonfat)	lbwrite(bp0);
			else					lbdwrite(bp0);

			bp0 = NULL;

			if (bp1)
			{
				if (pvp->vfs_waitonfat) lbwrite(bp1);
				else					lbdwrite(bp1);

				bp1 = NULL;
			}

			pvp->vfs_fmod++;
		}
	}
	else	/* fat16 */
	{
		whichbyte  = cluster << 1;
		whichblk   = (whichbyte >> pvp->vfs_bnshift) + pvp->vfs_fatblk;
		whichbyte &= pvp->vfs_brbomask;

		bp0 = lbread ( pvp->vfs_dev, whichblk, pvp->vfs_BytesPerSec );

		if ( error = lgeterror(bp0) )
		{
			lbrelse(bp0);
			return error;
		}

		if (function & FAT_GET)
		{
			*oldcontents = *((u_short *)(bp0->b_un.b_addr + whichbyte));
		}

		if (function & FAT_SET)
		{
			*(u_short *)(bp0->b_un.b_addr+whichbyte) = newcontents;
			updateotherfats ( pvp, bp0, (lbuf_t *)0, whichblk );

			if (pvp->vfs_waitonfat) lbwrite(bp0);
			else					lbdwrite(bp0);

			bp0 = NULL;

			pvp->vfs_fmod++;
		}
	}

	if (bp0)
	{
		lbrelse(bp0);
	}

	if (bp1)
	{
		lbrelse(bp1);
	}

	return 0;
}

/*
 *  Allocate a free cluster.
 *  pvp - 
 *  retcluster - put the allocated cluster's number here.
 *  fillwith - put this value into the fat entry for the
 *     allocated cluster.
 */
int
clusteralloc ( pcfs_vfs_t *pvp, u_long *retcluster, u_long fillwith )
{
	int		error;
	u_long	cn;
	u_char*	useptr;
	u_char*	endptr;
	u_long*	quickptr;

#ifdef PCFS_DEBUG
	printf ( "In clusteralloc\n" );
#endif

	/*
	Quick search for an empty place in the
	cluster; do it per 32.
	*/

	useptr = pvp->vfs_inusemap;
	endptr = useptr + pvp->vfs_maxcluster / NBBY;

	for ( quickptr = (u_long*) useptr; quickptr < (u_long*) endptr; quickptr++ )
	{
		if ( *quickptr != 0xffffffff )
		{
			break;
		}
	}

	cn     = ((u_char*) quickptr - useptr) * NBBY;
	useptr = (u_char*)  quickptr;

	for (  ; cn <= pvp->vfs_maxcluster; cn += NBBY, useptr++ )
	{
		if ( *useptr != 0xff )
		{
			u_long	end_cn;
			int		i;

			end_cn = cn + NBBY;

			for ( i = 1;  cn < end_cn;  cn++, i <<= 1 )
			{
				if ( *useptr & i )
				{
					continue;
				}

				/*
				Found one fill the entry and return whether
				we did succeed.
				fatentry() might sleep; prevent others from
				selecting the same cluster by marking this
				one used already.
				*/

				*useptr |= i;

				error = fatentry ( FAT_SET, pvp, cn, (u_long*) NULL, fillwith );

				if ( !error )
				{
					pvp->vfs_freeclustercount--;
					pvp->vfs_fmod++;
					*retcluster = cn;
				}
				else
				{
					*useptr &= ~i;
				}

				return error;
			}
		}
	}

	return ENOSPC;
}

/*
 *  Free a chain of clusters.
 *  pvp - address of the pcfs mount structure for the
 *    filesystem containing the cluster chain to be freed.
 *  startcluster - number of the 1st cluster in the chain
 *    of clusters to be freed.
 */
int
freeclusterchain ( pcfs_vfs_t *pvp, u_long startcluster )
{
	u_long	nextcluster;
	int		error = 0;

#ifdef PCFS_DEBUG
	printf ( "In freeclusterchain\n" );
#endif

	while (startcluster >= CLUST_FIRST  && startcluster <= pvp->vfs_maxcluster)
	{
		error = fatentry ( FAT_GET_AND_SET, pvp, startcluster, &nextcluster,
															 (u_long)PCFSFREE );
		if (error)
		{
			cmn_err (CE_WARN, "freeclusterchain: free failed, cluster %ld",
			    												startcluster);
			break;
		}

		/*
		If the cluster was successfully marked free,
		then update the count of free clusters, and
		turn off the "allocated" bit in the "in use"
		cluster bit map.
		*/

		pvp->vfs_freeclustercount++;
		pvp->vfs_inusemap[startcluster >> 3] &= ~(1 << (startcluster & 0x07));
		startcluster = nextcluster;
		pvp->vfs_fmod++;
	}

	return error;
}

/*
 *  Read in fat blocks looking for free clusters.
 *  For every free cluster found turn off its
 *  corresponding bit in the vfs_inusemap.
 */
int
fillinusemap ( pcfs_vfs_t *pvp )
{
	lbuf_t			*bp0 = 0;
	u_long			bp0_blk = (u_long) ~0;
	lbuf_t			*bp1 = 0;
	u_long			cn;
	u_long			whichblk;
	u_long			whichbyte;
	int				error;
	fattwiddle_t	x;

#ifdef PCFS_DEBUG
	printf ( "In fillinusemap\n" );
#endif

	/*
	Mark all clusters in use, we mark the free ones in the
	fat scan loop further down.
	*/

	for ( cn = 0;  cn < (pvp->vfs_maxcluster >> 3) + 1;  cn++ )
	{
		pvp->vfs_inusemap[cn] = 0xff;
	}

	/*
	Figure how many free clusters are in the filesystem
	by ripping thougth the fat counting the number of
	entries whose content is zero.  These represent free
	clusters.
	*/

	pvp->vfs_freeclustercount = 0;

	for ( cn = CLUST_FIRST; cn <= pvp->vfs_maxcluster; cn++ )
	{
		if (FAT12(pvp))
		{
			whichbyte  = cn + (cn >> 1);
			whichblk   = (whichbyte >> pvp->vfs_bnshift) + pvp->vfs_fatblk;
			whichbyte &= pvp->vfs_brbomask;

			if (whichblk != bp0_blk)
			{
				if (bp0)
				{
					lbrelse(bp0);
				}

				bp0 = lbread ( pvp->vfs_dev, whichblk, pvp->vfs_BytesPerSec );

				if ( error = lgeterror(bp0) )
				{
					goto error_exit;
				}

				bp0_blk = whichblk;
			}

			x.byte[0] = bp0->b_un.b_addr[whichbyte];

			if (whichbyte == (pvp->vfs_BytesPerSec-1))
			{
				bp1 = lbread ( pvp->vfs_dev, whichblk+1, pvp->vfs_BytesPerSec );

				if ( error = lgeterror(bp1) )
				{
					goto error_exit;
				}

				x.byte[1] = bp1->b_un.b_addr[0];
				lbrelse(bp0);
				bp0 = bp1;
				bp1 = NULL;
				bp0_blk++;
			}
			else
			{
				x.byte[1] = bp0->b_un.b_addr[whichbyte + 1];
			}

			if (cn & 1)
			{
				x.word >>= 4;
			}

			x.word &= 0x0fff;
		}
		else	/* 16 bit fat	*/
		{
			whichbyte  = cn << 1;
			whichblk   = (whichbyte >> pvp->vfs_bnshift) + pvp->vfs_fatblk;
			whichbyte &= pvp->vfs_brbomask;

			if (whichblk != bp0_blk)
			{
				if (bp0)
				{
					lbrelse(bp0);
				}

				bp0 = lbread ( pvp->vfs_dev, whichblk, pvp->vfs_BytesPerSec );

				if ( error = lgeterror(bp0) )
				{
					goto error_exit;
				}

				bp0_blk = whichblk;
			}

			x.byte[0] = bp0->b_un.b_addr[whichbyte];
			x.byte[1] = bp0->b_un.b_addr[whichbyte+1];
		}

		if (x.word == 0)
		{
			pvp->vfs_freeclustercount++;
			pvp->vfs_inusemap[cn >> 3] &= ~(1 << (cn & 0x07));
		}
	}

	lbrelse(bp0);
	return 0;

error_exit:
	;
	if (bp0)
	{
		lbrelse(bp0);
	}

	if (bp1)
	{
		lbrelse(bp1);
	}

	return error;
}

/*
 *  Allocate a new cluster and chain it onto the end of the
 *  file.
 *  dep - the file to extend
 *  bpp - where to return the address of the buf header for the
 *        new file block
 *  ncp - where to put cluster number of the newly allocated file block
 *        If this pointer is 0, do not return the cluster number.
 *
 *  NOTE:
 *   This function is not responsible for turning on the DEUPD
 *   bit if the de_flag field of the denode and it does not
 *   change the de_FileSize field.  This is left for the caller
 *   to do.
 */
int
extendfile ( denode_t *dep, lbuf_t **bpp, u_long *ncp )
{
	int			error;
	u_long		frcn;
	u_long		cn;
	pcfs_vfs_t	*pvp;

#ifdef PCFS_DEBUG
	printf ( "In extendfile\n" );
#endif

	/*
	Don't try to extend the root directory
	*/

	if ( DETOV(dep)->v_flag & VROOT )
	{
#ifdef PCFS_DEBUG
		printf("extendfile: attempt to extend root directory\n");
#endif
		return ENOSPC;
	}

	/*
	If the "file's last cluster" cache entry is empty,
	and the file is not empty,
	then fill the cache entry by calling pcbmap().
	*/

	fc_fileextends++;

	if ( dep->de_fc[FC_LASTFC].fc_frcn == FCE_EMPTY && dep->de_StartCluster )
	{
		fc_lfcempty++;

		error = pcbmap(dep, (u_long)0xffff, (u_long *)0, &cn);

		if (error != E2BIG)		/* we expect E2BIG */
		{
			return error;
		}

		error = 0;
	}

	/*
	Allocate another cluster and chain onto the end of the file.
	If the file is empty we make de_StartCluster point to the
	new block.  Note that de_StartCluster being 0 is sufficient
	to be sure the file is empty since we exclude attempts to
	extend the root directory above, and the root dir is the
	only file with a startcluster of 0 that has blocks allocated
	(sort of).
	*/

	pvp = dep->de_vfs;

	if ( error = clusteralloc(pvp, &cn, (u_long)CLUST_EOFE) )
	{
		return error;
	}

	if (dep->de_StartCluster == 0)
	{
		dep->de_StartCluster = cn;
		frcn = 0;
	}
	else
	{
		error = fatentry(FAT_SET, pvp, (u_long)dep->de_fc[FC_LASTFC].fc_fsrcn,
		    												             0, cn);
		if (error)
		{
			(void)clusterfree(pvp, cn);
			return error;
		}

		frcn = dep->de_fc[FC_LASTFC].fc_frcn + 1;
	}

	/*
	Update the "last cluster of the file" entry in the denode's
	fat cache.
	*/

	fc_setcache ( dep, FC_LASTFC, frcn, cn );

	/*
	Get the buf header for the new block of the
	file, if wanted.
	*/

	if ( bpp )
	{
		*bpp = lgetblk(pvp->vfs_dev, cntobn(pvp,cn), pvp->vfs_bpcluster);
		lclrbuf(*bpp);
	}

	/*
	Give them the filesystem relative cluster number
	if they want it.
	*/

	if (ncp)
	{
		*ncp = cn;
	}

	return 0;
}
