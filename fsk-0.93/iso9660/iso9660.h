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
 */

#ident "@(#)iso9660.h	1.3 93/09/16 "

/*
 * CD-ROM file system based on ISO-9660 : 1988 (E) with Rock Ridge Extensions
 *
 * When an on-disk data structure contains a number, it is encoded in
 * one of 8 ways, as described in sections 7.1.1 through 7.3.3 of
 * the ISO document.  In this file, a comment such as "711" which
 * follows a structure element tells how that particular number is
 * encoded.  See the file iso9660_util.c for more details.
 */

struct  iso9660_args {

	u_char	raw;	/* raw mode file names, don't translate anything    */
	u_char	norr;	/* ignore rock ridge, don't translate iso9660 names */
	int		mask;	/* mask used for files   */
	uid_t	uid;	/* set owner id of files */
	gid_t	gid;	/* set group id of files */

};

#define	DEFUSR	(-1)
#define DEFGRP	(-1)


typedef struct iso9660_args	IsoArgs_t;


#ifdef _KERNEL



#define ROUNDUP(x, y)   ((((x)+((y)-1))/(y))*(y))
#define NAME_MAX 255	/* ??? */
#define __P(a) ()

#define HSIERRA_STANDARD_ID	"CDROM"

struct hsierra_volume_descriptor {
	char	foo[8];
	u_char	type[1];
	char	id[5];
	u_char	version[1];
	char	data[2033];
};

typedef struct hsierra_volume_descriptor HsaVolumeDescriptor_t;


struct hsierra_primary_descriptor {
	char	foo						[8];
	u_char	type					[1];
	char	id						[5];
	u_char	version					[1];
	char	unused1					[1];
	char	system_id				[32];
	char	volume_id				[32];
	char	unused2					[8];
	u_char	volume_space_size		[8];
	char	unused3					[32];
	u_char	volume_set_size			[4];
	u_char	volume_sequence_number	[4];
	u_char	logical_block_size		[4];
	u_char	path_table_size			[8];
	char	type_l_path_table		[4];
	char	unused4					[28];
	char	root_directory_record	[34];
};

typedef struct hsierra_primary_descriptor HsaPrimaryDescriptor_t;


	
#define	ISO9660_STANDARD_ID "CD001"

struct iso9660_volume_descriptor {
	u_char	type[1]; /* 711 */
	char	id[5];
	u_char	version[1];
	char	data[2041];
};

typedef struct iso9660_volume_descriptor IsoVolumeDescriptor_t;

/* volume descriptor types */
#define	ISO9660_VD_PRIMARY 1
#define	ISO9660_VD_END 255

struct iso9660_primary_descriptor {
	u_char	type					[1]; /* 711 */
	char	id						[5];
	u_char	version					[1]; /* 711 */
	char	unused1					[1];
	char	system_id				[32]; /* achars */
	char	volume_id				[32]; /* dchars */
	char	unused2					[8];
	u_char	volume_space_size		[8]; /* 733 */
	char	unused3					[32];
	u_char	volume_set_size			[4]; /* 723 */
	u_char	volume_sequence_number	[4]; /* 723 */
	u_char	logical_block_size		[4]; /* 723 */
	u_char	path_table_size			[8]; /* 733 */
	u_char	type_l_path_table		[4]; /* 731 */
	u_char	opt_type_l_path_table	[4]; /* 731 */
	u_char	type_m_path_table		[4]; /* 732 */
	u_char	opt_type_m_path_table	[4]; /* 732 */
	u_char	root_directory_record	[34]; /* 9.1 */
	char	volume_set_id			[128]; /* dchars */
	char	publisher_id			[128]; /* achars */
	char	preparer_id				[128]; /* achars */
	char	application_id			[128]; /* achars */
	char	copyright_file_id		[37]; /* 7.5 dchars */
	char	abstract_file_id		[37]; /* 7.5 dchars */
	char	bibliographic_file_id	[37]; /* 7.5 dchars */
	u_char	creation_date			[17]; /* 8.4.26.1 */
	u_char	modification_date		[17]; /* 8.4.26.1 */
	u_char	expiration_date			[17]; /* 8.4.26.1 */
	u_char	effective_date			[17]; /* 8.4.26.1 */
	u_char	file_structure_version	[1]; /* 711 */
	char	unused4					[1];
	char	application_data		[512];
	char	unused5					[653];
};

typedef struct iso9660_primary_descriptor IsoPrimaryDescriptor_t;

#define	ISO9660_DIR_NAME	33 /* offset to start of name field */

#define	ISO9660_DIR_FLAG	2 /* bit in flags field */


/*
This following structure is the iso9660 directory structure,
for high sierra date is only six bytes (missing the timezone)
while flags is followed by a reserved byte.
We'll use the negated hsierra flag as an index to address flags.
*/

struct iso9660_directory_record {
	u_char	length					[1]; /* 711 */
	u_char	ext_attr_length			[1]; /* 711 */
	u_char	extent					[8]; /* 733 */
	u_char	size					[8]; /* 733 */
	u_char	date					[7]; /* 7 by 711 */
	u_char	flags					[1];
	u_char	file_unit_size			[1]; /* 711 */
	u_char	interleave				[1]; /* 711 */
	u_char	volume_sequence_number	[4]; /* 723 */
	u_char	name_len				[1]; /* 711 */
	/* name follows */
};

typedef struct iso9660_directory_record IsoDirectoryRecord_t;

#define	IsoDirName(dp)	((char *)dp + ISO9660_DIR_NAME)

/* must fit in uio.uio_offset and vattr.va_fileid */
typedef u_int IsoFileId_t;

struct ifid {

	u_short		len;
	IsoFileId_t	fileid;

};

typedef struct ifid	IsoFid_t;


#define	ISO9660_MAX_FILEID	UINT_MAX

struct iso9660_vfs {

	int					logical_block_size;
	int					volume_space_size;
	struct vfs			*im_vfsp;
	vnode_t				*im_devvp;
	vnode_t				*im_root;

	char				im_path[32];
	dev_t				im_dev;

	int					im_bshift;
	int					im_bmask;
	int					im_bsize;

	u_char				root[34];
	IsoFileId_t			root_fileid;
	int					root_size;

	int					im_raw;  /* raw mode:  don't translate iso9660 names */
	int					im_norr; /* ignore rr, don't translate iso9660 names */
	int					im_mask; /* forced file mask */
	uid_t				im_uid;  /* forced user-id  */
	gid_t				im_gid;	 /* forced group-id */

	int					im_hsierra;					/* high sierra fs */
	int					im_have_rock_ridge;
	int					im_rock_ridge_skip;			/* rock ridge fs */
};

typedef struct iso9660_vfs	IsoVfs_t;


#define	VFSTOISO9660(vfsp)	((IsoVfs_t *)((vfsp)->vfs_data))

#define	IsoRndOff(imp, loc) 	((loc) & (imp)->im_bmask)
#define	IsoRndUpOff(imp, loc) 	(((loc)+(imp)->im_bsize-1) & (imp)->im_bmask)
#define	IsoBlkOff(imp, loc) 	((loc) & ~(imp)->im_bmask)
#define	IsoLogBlk(imp, loc) 	((loc) >> (imp)->im_bshift)
#define	IsoBlkSize(imp)			((imp)->im_bsize)
#define	IsoPhysBlk(ip, ivp, loc, bsize)	\
	( ((ip)->extent + IsoLogBlk( (ivp), (loc) )) * ((bsize) >> SCTRSHFT) )
#define	IsoLogBlkToSize(imp, blk) ((blk) << (imp)->im_bshift)

/* time stamp info - we only care about modify, access and creation */
#define	RR_TIMESTAMPS		7
#define	RR_LONG_FORM		0x80
#define	RR_LONG_TIMESTAMP_LEN	17
#define	RR_SHORT_TIMESTAMP_LEN	7

#define	RR_CREATION		0
#define	RR_MODIFY		1
#define	RR_ACCESS		2
#define	RR_ATTRIBUTES		3
#define	RR_BACKUP		4
#define	RR_EXPIRATION		5
#define	RR_EFFECTIVE		6

#define	TS_INDEX(val) ((val) - RR_MODIFY)

struct iso9660_rr_info {

	int		rr_flags;
	int		rr_mode;
	int		rr_nlink;
	int		rr_uid;
	int		rr_gid;
	int		rr_dev_high;
	int		rr_dev_low;

	int		rr_rdev;

	u_char	rr_have_rr;
	u_char	rr_rr;

	u_char	rr_time[3][RR_LONG_TIMESTAMP_LEN];
	u_char	rr_time_format[3];

};

struct iso9660_dir_info {

	vnode_t		*vp;
	IsoVfs_t	*ivp;
	IsoFileId_t fileid;
	IsoFileId_t translated_fileid;
	IsoFileId_t first_fileid;
	IsoFileId_t next_fileid;
	IsoFileId_t max_fileid;
	IsoFileId_t extent;
	int			bias;

	int			error;

	int 		allocsize;
	int			namelen;
	char		*name;

	union {

		struct fbuf	*dirp_fbp;
		struct buf	*dirp_bp;
	
	} bio;

	int			size;
	u_char		iso9660_time[7];
	int			iso9660_flags;

	struct	iso9660_rr_info rr;

	char		*link;
	int			link_used;
	int			link_separator;

	struct uio	*uio;	/* used to implement ISO9660GETSUSP ioctl */
};

typedef struct iso9660_dir_info IsoDirInfo_t;

#define d_fbp	bio.dirp_fbp
#define d_bp	bio.dirp_bp


/*
 * Each of these is a piece of Rock Ridge information that may be present
 * for a particular file.
 */
#define	RR_PX	0x01
#define	RR_PN	0x02
#define	RR_SL	0x04
#define	RR_NM	0x08
#define	RR_CL	0x10
#define	RR_PL	0x20
#define	RR_RE	0x40
#define	RR_TF	0x80


#define	SUSP_CODE(a,b) ((((a) & 0xff) << 8) | ((b) & 0xff))

#define	SUSP_MIN_SIZE	4

struct susp {
	u_char	code[2];
	u_char	length[1];
	u_char	version[1];
};

#define	SUSP_SP_LEN	7
struct susp_sp {
	u_char	code[2];
	u_char	length[1];
	u_char	version[1];
	u_char	check[2];
	u_char	len_skp[1];
};

#define	SUSP_PX_LEN	36
struct susp_px {
	u_char	code[2];
	u_char	length[1];
	u_char	version[1];
	u_char	mode[8];
	u_char	nlink[8];
	u_char	uid[8];
	u_char	gid[8];
};

#define	SUSP_PN_LEN	20
struct susp_pn {
	u_char	code[2];
	u_char	length[1];
	u_char	version[1];
	u_char	dev_high[8];
	u_char	dev_low[8];
};

#define	SUSP_RR_LEN	5
struct susp_rr {
	u_char	code[2];
	u_char	length[1];
	u_char	version[1];
	u_char	flags[1];
};

#define	SUSP_TF_LEN	5
struct susp_tf {
	u_char	code[2];
	u_char	length[1];
	u_char	version[1];
	u_char	flags[1];
};

#define	SUSP_NM_LEN	5
struct susp_nm {
	u_char	code[2];
	u_char	length[1];
	u_char	version[1];
	u_char	flags[1];
};
#define	SUSP_NM_NAME(sp) ((char *)sp + SUSP_NM_LEN)

#define	SUSP_CE_LEN	28
struct susp_ce {
	u_char	code[2];
	u_char	length[1];
	u_char	version[1];
	u_char	block[8];
	u_char	offset[8];
	u_char	ce_length[8];
};

#define	SUSP_SL_LEN	5
struct susp_sl {
	u_char	code[2];
	u_char	length[1];
	u_char	version[1];
	u_char	flags[1];
};

#define	SUSP_SLC_LEN	2
struct susp_slc {
	u_char	flags[1];
	u_char	length[1];
};
#define	SLC_NAME(slc) ((char *)slc + 2)

#define	SUSP_ER_LEN	8
struct susp_er {
	u_char	code[2];
	u_char	length[1];
	u_char	version[1];
	u_char	len_id[1];
	u_char	len_des[1];
	u_char	len_src[1];
	u_char	ext_ver[1];
};
#define	SUSP_ER_ID(sp)	((char *)sp + SUSP_ER_LEN)
#define	SUSP_ER_DES(sp)	(SUSP_ER_ID(sp) + IsoNum711(sp->len_id))
#define	SUSP_ER_SRC(sp) (SUSP_ER_DES(sp) + IsoNum711(sp->len_des))

#define	SUSP_CL_LEN 12
struct susp_cl {
	u_char	code[2];
	u_char	length[1];
	u_char	version[1];
	u_char	loc[8];
};

#define	SUSP_PL_LEN	12
struct susp_pl {
	u_char	code[2];
	u_char	length[1];
	u_char	version[1];
	u_char	loc[8];
};

/* name flags */
#define	RR_NAME_CONTINUE	0x01
#define	RR_NAME_CURRENT		0x02
#define	RR_NAME_PARENT		0x04
#define	RR_NAME_ROOT		0x08
#define	RR_NAME_VOLROOT		0x10
#define	RR_NAME_HOST		0x20

struct iso9660_getsusp {
	char	*buf;
	int		buflen;
};

typedef struct iso9660_getsusp IsoGetSusp_t;


/*
The following macro is used to support unaligned
reads on a device: if a read is on a non-sector
offset (which will only occur if the block size is
smaller than SECTSIZE) we will compensate for this
(the bias fields are used for this).
*/

#define SECTSIZE	2048
#define SECTOFFSET	(SECTSIZE-1)


#define	ISO9660GETSUSP _IOWR('i', 213, struct iso9660_getsusp)

#ifdef _KERNEL
int IsoMount ( struct vfs*, vnode_t*, struct mounta*, cred_t* );
int IsoUnMount ( struct vfs*, cred_t* );
int IsoRoot ( struct vfs*, vnode_t** );
int IsoStatVfs ( struct vfs*, struct statvfs* );
int IsoSync ( void );
int IsoVget ( struct vfs*, vnode_t**, struct fid* );

int IsoNum711 (u_char *p);
int IsoNum712 (u_char *p);
int IsoNum721 (u_char *p);
int IsoNum722 (u_char *p);
int IsoNum723 (u_char *p);
int IsoNum731 (u_char *p);
int IsoNum732 (u_char *p);
int IsoNum733 (u_char *p);
	
int Isodir_parse __P((struct Isodir_info *dirp,
	   struct Isodirectory_record *ep, int reclen));

int Isodiropen __P((struct Isomnt *imp, Isofileid_t fileid,
	 Isofileid_t max_fileid, int want_name, int want_link,
	 struct uio *uio, struct Isodir_info **dirpp));
int Isodirget __P((struct Isodir_info *dirp));
void Isodirclose __P((struct Isodir_info *));
#endif

#endif _KERNEL
