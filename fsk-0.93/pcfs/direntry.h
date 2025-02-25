
#ident "@(#)direntry.h	1.5 93/10/17 "

/*
 *  Structure of a dos directory entry.
 */
struct direntry {
	u_char deName[8];			/* filename, blank filled	*/
#define	SLOT_EMPTY	0x00		/* slot has never been used	*/
#define	SLOT_E5		0x05		/* the real value is 0xe5	*/
#define	SLOT_DELETED	0xe5	/* file in this slot deleted	*/
	u_char deExtension[3];		/* extension, blank filled	*/
	u_char deAttributes;		/* file attributes		*/
#define	ATTR_NORMAL	0x00		/* normal file			*/
#define	ATTR_READONLY	0x01	/* file is readonly		*/
#define	ATTR_HIDDEN	0x02		/* file is hidden		*/
#define	ATTR_SYSTEM	0x04		/* file is a system file	*/
#define	ATTR_VOLUME	0x08		/* entry is a volume label	*/
#define	ATTR_DIRECTORY	0x10	/* entry is a directory name	*/
#define	ATTR_ARCHIVE	0x20	/* file is new or modified	*/
	char deReserved[10];		/* reserved			*/
	u_short deTime;				/* create/last update time	*/
	u_short deDate;				/* create/last update date	*/
	u_short deStartCluster;		/* starting cluster of file	*/
	u_long deFileSize;			/* size of file in bytes	*/
};

typedef struct direntry dosdirent_t;

#define DOSDIRSIZ	(sizeof((dosdirent_t*)0)->deName +\
			 sizeof((dosdirent_t*)0)->deExtension)

/*
 *  This is the format of the contents of the deTime
 *  field in the direntry structure.
 */
struct DOStime {
	u_short
	dt_2seconds:5,	/* seconds divided by 2		*/
	dt_minutes:6,	/* minutes			*/
	dt_hours:5;	/* hours			*/
};

/*
 *  This is the format of the contents of the deDate
 *  field in the direntry structure.
 */
struct DOSdate {
	u_short
	dd_day:5,	/* day of month			*/
	dd_month:4,	/* month			*/
	dd_year:7;	/* years since 1980		*/
};

union dostime {
	struct DOStime dts;
	u_short dti;
};

typedef union dostime	dostime_t;

union dosdate {
	struct DOSdate dds;
	u_short ddi;
};

typedef union dosdate	dosdate_t;

/*
 *  The following defines are used to rename fields in
 *  the ufs_specific structure in the nameidata structure
 *  in namei.h
 */
#define	ni_pcfs		ni_ufs

#if defined(_KERNEL)
void unix2dostime (timestruc_t *, union dosdate *, union dostime *);
void dos2unixtime (union dosdate *, union dostime *, struct timestruc *);
int  dos2unixfn (u_char *, u_char *);
void unix2dosfn (u_char *, u_char *, int);
#endif /* defined(_KERNEL) */
