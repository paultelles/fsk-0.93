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

#ident "@(#)iso9660_node.h	1.3 93/09/16 "

struct iso9660_node {

	struct	iso9660_node	*i_chain[2];/* hash chain, MUST be first */
	struct	vnode			i_vnode;	/* vnode associated with this inode */
	struct	vnode			*i_devvp;	/* vnode for block I/O */
	struct  iso9660_node	*i_freef;	/* forward pointer free list inodes */
	struct  iso9660_node	*i_freeb;	/* backward pointer free list inodes */
	u_long					i_flag;		/* see below */
	dev_t					i_dev;		/* device where inode resides */
	IsoVfs_t				*i_vfs;		/* filesystem associated with inode */
	long					i_owner;	/* owner of lock; if any */
	long					i_nrlocks;	/* # of locks owner has  */
	long					i_mapcnt;	/* number of blocks mapped */
	int						i_bias;		/* bias to compensate during I/O */
	int						i_size;		/* # bytes file (rnd block for dir) */

	IsoFileId_t				fileid;
	int						extent;

	u_char					iso9660_time[7];
	u_char					time_parsed;

	struct					iso9660_rr_info	rr;

	time_t					mtime;
	time_t					atime;
	time_t					ctime;
	
};


typedef struct iso9660_node IsoNode_t;


#define	i_forw		i_chain[0]
#define	i_back		i_chain[1]
#define i_total(ip)	((ip)->i_size + (ip)->i_bias)

/* flags */
#define	ILOCKED		0x0001		/* inode is locked */
#define	IWANT		0x0002		/* some process waiting on lock */
#define IREF		0x0004		/* someone refers this inode */


#define VTOI(vp) ((IsoNode_t*)(vp)->v_data)
#define ITOV(ip) ((vnode_t*)&((ip)->i_vnode))


#define ISOILOCK(ip)	IsoIlock(ip)
#define ISOIUNLOCK(ip)	IsoIunlock(ip)


#define ISOFILEMODE(ip)	(((ip)->rr.rr_flags & RR_PX) ? \
							((ip)->rr.rr_mode & 07777) : \
							((VREAD|VWRITE|VEXEC | VREAD|VWRITE|VEXEC >> 3 | \
							  VREAD|VWRITE|VEXEC >> 6) & ~(ip)->i_vfs->im_mask))

/*
 * Prototypes for ISO9660 vnode operations
 */
int IsoLookup ( vnode_t*, char*, vnode_t**, struct pathname*, int, vnode_t*,
				cred_t* );
int IsoOpen ( vnode_t**, int, cred_t* );
int IsoClose ( vnode_t*, int, int, off_t, cred_t* );
int IsoAccess ( vnode_t*, int, int, cred_t* );
int IsoGetAttr ( vnode_t*, struct vattr*, int, cred_t*  );
int IsoRead ( vnode_t*, struct uio*, int, cred_t* );
int IsoIoctl( vnode_t*, int, caddr_t, int, cred_t*, int*  );
int IsoMmap ( vnode_t*, uint, struct as*, addr_t*, uint, u_char, u_char,
															uint, cred_t* );
int IsoAddMap ( vnode_t*, uint, struct as*, addr_t, uint, u_char, u_char,
															uint, cred_t* );
int IsoDelMap ( vnode_t*, uint, struct as*, addr_t, uint, u_char, u_char,
															uint, cred_t* );
int IsoSeek  ( vnode_t*, off_t, off_t* );
int IsoReadDir ( vnode_t*, struct uio*, cred_t*, int* );
int IsoReadLink ( vnode_t*, struct uio*, cred_t* );
int	IsoFsync ( vnode_t*, cred_t* );
int IsoFid ( vnode_t*, struct fid** );
void IsoIlock ( IsoNode_t* );
void IsoIunlock ( IsoNode_t* );
void IsoLock ( vnode_t* );
void IsoUnlock ( vnode_t* );
int IsoIsLocked ( vnode_t* );
int IsoFrLock ( vnode_t*, int, struct flock*, int, off_t, cred_t* );
/*
int IsoGetPage ( vnode_t*, u_int, u_int, u_int*, page_t*, u_int, struct seg*,
												addr_t, enum seg_rw, cred_t* );
*/
void IsoInActive ( vnode_t*, cred_t* );
int IsoIget ( IsoNode_t*, IsoFileId_t, IsoNode_t**, IsoDirInfo_t* );
int IsoFlush ( struct vfs* );
int IsoDirGet ( IsoDirInfo_t* );
void IsoDirClose ( IsoDirInfo_t* );
void IsoPut ( IsoNode_t* );
void IsoUnHash ( IsoNode_t* );
void IsoDirOpen ( vnode_t*, IsoVfs_t*, IsoFileId_t, IsoFileId_t, int, int,
				 struct uio*, IsoDirInfo_t** );
