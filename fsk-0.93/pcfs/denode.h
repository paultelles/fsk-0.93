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

#ident "@(#)denode.h	1.5 93/10/17 "

/*
 *  This is the pc filesystem specific portion of the
 *  vnode structure.
 *  To describe a file uniquely the de_dirclust, de_diroffset,
 *  and de_de.deStartCluster fields are used.  de_dirclust
 *  contains the cluster number of the directory cluster containing
 *  the entry for a file or directory.  de_diroffset is the
 *  index into the cluster for the entry describing a file
 *  or directory.  de_de.deStartCluster is the number of the
 *  first cluster of the file or directory.  Now to describe the
 *  quirks of the pc filesystem.
 *  - Clusters 0 and 1 are reserved.
 *  - The first allocatable cluster is 2.
 *  - The root directory is of fixed size and all blocks that
 *    make it up are contiguous.
 *  - Cluster 0 refers to the root directory when it is found
 *    in the startcluster field of a directory entry that points
 *    to another directory.
 *  - Cluster 0 implies a 0 length file when found in the start
 *    cluster field of a directory entry that points to a file.
 *  - You can't use the cluster number 0 to derive
 *    the address of the root directory.
 *  - Multiple directory entries can point to a directory.
 *    The entry in the parent directory points to a child
 *    directory.  Any directories in the child directory contain
 *    a ".." entry that points back to the child.  The child
 *    directory itself contains a "." entry that points to
 *    itself.
 *  - The root directory does not contain a "." or ".." entry.
 *  - Directory entries for directories are never changed once
 *    they are created (except when removed).  The size stays
 *    0, and the last modification time is never changed.  This
 *    is because so many directory entries can point to the physical
 *    clusters that make up a directory.  It would lead to an update
 *    nightmare.
 *  - The length field in a directory entry pointing to a directory
 *    contains 0 (always).  The only way to find the end of a directory
 *    is to follow the cluster chain until the "last cluster"
 *    marker is found.
 *  My extensions to make this house of cards work.  These apply
 *  only to the in memory copy of the directory entry.
 *  - A reference count for each denode will be kept since dos doesn't
 *    keep such things.
 */


struct dfid {
	u_short dfid_len;        /* length of rest structure */
 	u_short dfid_gen;        /* generation number of file for NFS */
	u_short dfid_dirclust;   /* cluster # directory */
	u_short dfid_diroffset;  /* diroffset | PCFS_FID_DIRFLAG for dirs */
	u_short dfid_startclust; /* cluster start of entry */
};

typedef struct dfid dfid_t;

#define PCFS_FID_DIRFLAG        (1<<15)   /* id < sizeof(dosdirent) */

/*
 *  The fat cache structure.
 *  fc_fsrcn is the filesystem relative cluster number
 *  that corresponds to the file relative cluster number
 *  in this structure (fc_frcn).
 */

struct fatcache {
	u_short fc_frcn;	/* file relative cluster number	*/
	u_short fc_fsrcn;	/* filesystem relative cluster number */
};

typedef struct fatcache	fatcache_t;


/*
 *  The fat entry cache as it stands helps make extending
 *  files a "quick" operation by avoiding having to scan
 *  the fat to discover the last cluster of the file.
 *  The cache also helps sequential reads by remembering
 *  the last cluster read from the file.  This also prevents
 *  us from having to rescan the fat to find the next cluster
 *  to read. This cache is probably pretty worthless if a
 *  file is opened by multiple processes.
 *  Note that each entry stands for ``cluster-size'' bytes
 *  so enlarging the cache might NOT be very useful (and is
 *  useless for small files).
 */
#define	FC_SIZE		6			/* number of entries in the cache	*/
#define	FC_LASTMAP	0			/* entry the last call pcbmap() resolved to */
#define	FC_NEXTMAP	1			/* start of the next clusters in the file */
#define	FC_LASTFC	(FC_SIZE-1)	/* entry for the last cluster in the file */

#define	FCE_EMPTY	0xffff	/* doesn't represent an actual cluster # */

/*
 *  Set a slot in the fat cache.
 */
#define	fc_setcache(dep, slot, frcn, fsrcn) \
	(dep)->de_fc[slot].fc_frcn = frcn; \
	(dep)->de_fc[slot].fc_fsrcn = fsrcn;

/*
 *  This is the in memory variant of a dos directory
 *  entry.  It is usually contained within a vnode.
 */
struct denode {
	struct denode *de_chain[2];	/* hash chain ptrs		  */
	struct denode *de_freef;	/* forward in the freelist        */
	struct denode *de_freeb;	/* backward in the freelist       */
	struct vnode de_vnode;		/* associated vnode of the denode */
	struct vnode *de_devvp;		/* vnode of blk dev we live on	  */
	u_long de_flag;				/* flag bits			  */
	dev_t de_dev;				/* device where direntry lives	  */
	u_long de_dirclust;			/* cluster of the directory file
						 		 *  containing this entry	  */
	u_long de_diroffset;		/* ordinal of this entry in the
						 		 *  directory			  */
	long de_refcnt;				/* reference count		  */
	struct pcfs_vfs *de_vfs;	/* addr of our mount struct	  */
	struct lockf *de_lockf;		/* byte level lock list		  */
	long de_owner;				/* current lock holder		  */
	long de_nrlocks;			/* number of locks owner has      */
	struct direntry de_de;		/* the actual directory entry	  */
	fatcache_t de_fc[FC_SIZE];	/* fat cache			  */
	u_long	de_lastr;			/* last read block; readahead     */
	int	de_frspace;				/* indicate whether we found free */
	u_long	de_froffset;		/* space at this offset and in    */
	u_long	de_frcluster;		/* this cluster, for directories  */
};

typedef struct denode denode_t;


/*
 *  Values for the de_flag field of the denode.
 */
#define	DELOCKED	0x0001		/* directory entry is locked	*/
#define	DEWANT		0x0002		/* someone wants this de	*/
#define	DERENAME	0x0004		/* de is being renamed		*/
#define	DEUPD		0x0008		/* file has been modified	*/
#define	DESHLOCK	0x0010		/* file has shared lock		*/
#define	DEEXLOCK	0x0020		/* file has exclusive lock	*/
#define	DELWAIT		0x0040		/* someone waiting on file lock	*/
#define	DEMOD		0x0080		/* denode wants to be written back
					 			 *  to disk			*/
#define DEREF		0x0100		/* someone refers this denode   */


/*
Flags for pcfs_lookup:
*/

#define	GETFREESLOT	0x01		/* record empty slots during search */


/*
 *  Shorthand macros used to reference fields in the direntry
 *  contained in the denode structure.
 */
#define	de_Name			de_de.deName
#define	de_Extension	de_de.deExtension
#define	de_Attributes	de_de.deAttributes
#define	de_Reserved		de_de.deReserved
#define	de_Time			de_de.deTime
#define	de_Date			de_de.deDate
#define	de_StartCluster	de_de.deStartCluster
#define	de_FileSize		de_de.deFileSize
#define	de_forw			de_chain[0]
#define	de_back			de_chain[1]


/*

The following macro uses a part of the directory reserved area
as a generation number.

This directory reserved area is only affected when exporting pcfs
filesystems using NFS.

This number is only used for NFS mounts and is incremented whenever
the file on disk is removed while the pcfs filesystem is shared,
this serves to detect stale NFS handles. The number must be stored
on disk to survive a reboot (hopefully...)

The user of this filesystem is free to select GENOFF: 0 <= GENOFF <= 8

Define NO_GENOFF if you do not want pcfs to use the reserved area,
in which case the NFS specs are slightly violated by an inability
to detect stale NFS handles.

*/

#ifndef GENOFF
#define GENOFF	0
#endif

#define DE_GEN(dp)		((u_short*) (&(dp)->de_Reserved[GENOFF]))[0]
#define DIR_GEN(dir)	((u_short*) (&(dir)->deReserved[GENOFF]))[0]

/*
Shorthand macro's of denode attribures.
*/

#define ISADIR(dp)		((dp)->de_Attributes & ATTR_DIRECTORY)
#define ISVLABEL(dp)	((dp)->de_Attributes & ATTR_VOLUME)


#if defined(_KERNEL)

#define	VTODE(vp)	((struct denode *)(vp)->v_data)
#define	DETOV(de)	((struct vnode *)&(de)->de_vnode)

#define	DELOCK(de)		delock(de)
#define	DEUNLOCK(de)	deunlock(de)

#define	DEUPDAT(dep, t, waitfor) \
	if ( dep->de_flag & (DEUPD|DEMOD) ) \
		(void) deupdat(dep, t, waitfor);

#define	DETIMES(dep, t) \
	if (dep->de_flag & DEUPD) { \
		(dep)->de_flag |= DEMOD; \
		unix2dostime(t, (union dosdate *)&dep->de_Date, \
			(union dostime *)&dep->de_Time); \
		(dep)->de_flag &= ~DEUPD; \
	}

/*
 * Prototypes for PCFS vnode operations
 */
int pcfs_lookup (vnode_t*, char*, vnode_t**, struct pathname*, int, vnode_t*,
				 cred_t*);
int pcfs_create (vnode_t*, char*, struct vattr*, enum vcexcl, int, vnode_t**,
				 cred_t*);
int pcfs_open (vnode_t**, int, cred_t*);
int pcfs_close (vnode_t*, int, int, off_t, cred_t*);
int pcfs_access (vnode_t*, int, int, cred_t*);
int pcfs_getattr (vnode_t*, struct vattr*, int, cred_t*);
int pcfs_setattr (vnode_t*, struct vattr*, int, cred_t*);
int pcfs_read (vnode_t*, struct uio*, int, cred_t*);
int pcfs_write (vnode_t*, struct uio*, int, cred_t*);
int pcfs_ioctl (vnode_t*, int, caddr_t, int, cred_t*, int*);
int pcfs_mmap (vnode_t*, int, cred_t*, proc_t*);
int pcfs_fsync (vnode_t*, cred_t*);
int pcfs_seek (vnode_t*, off_t, off_t*);
int pcfs_remove (vnode_t*, char*, cred_t*);
int pcfs_link (vnode_t*t, vnode_t*, char*, cred_t*);
int pcfs_rename (vnode_t*, char*, vnode_t*, char*, cred_t*);
int pcfs_mkdir (vnode_t*, char*, struct vattr*, vnode_t**, cred_t*);
int pcfs_rmdir (vnode_t*, char*, vnode_t*, cred_t*);
int pcfs_symlink (vnode_t*, char*, struct vattr*, char*, cred_t*);
int pcfs_readdir (vnode_t*, struct uio*, cred_t*, int*);
int pcfs_readlink (vnode_t*, struct uio*, cred_t*);
void pcfs_inactive (vnode_t*, cred_t*);
void pcfs_lock (vnode_t*);
void pcfs_unlock (vnode_t*);
int pcfs_islocked (vnode_t*);

/*
 *  Internal service routine prototypes.
 */
int deget (struct pcfs_vfs*, int, u_long, u_long, u_long, dosdirent_t*,
		   denode_t**);

#endif /* defined(_KERNEL) */
