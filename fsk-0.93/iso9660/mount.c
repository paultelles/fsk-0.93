/*
 *
 *  Written for System V Release 4	(ESIX 4.0.4)	
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

#ident "@(#)mount.c	1.2 93/07/17 "

#include	<stdio.h>
#include	<string.h>
#include	<sys/signal.h>
#include	<unistd.h>
#include	<errno.h>
#include	<fcntl.h>
#include	<pwd.h>
#include	<grp.h>
#include	<limits.h>
#include	<sys/mnttab.h>
#include	<sys/vfstab.h>
#include	<sys/mount.h>
#include	<sys/vnode.h>
#include	<sys/types.h>
#include	"iso9660.h"


#define	TIMAX	16

#define WRONG	0
#define RDONLY	1
#define NORR	2
#define RAW		4
#define MASK	8
#define USER	16
#define GROUP	32
#define LOCK	64


#define CIOC		('C' << 8)		/* oops, hope this works ... */
#define	C_PREVMV	(CIOC | 0x07)	/* prevent medium removal    */
#define	C_ALLOMV	(CIOC | 0x08)	/* allow medium removal      */


char*	progname;
char*	fstype   = "iso9660";
char*	mounttab = "/etc/mnttab";
char*	vfstab   = "/etc/vfstab";

struct optarg {

	int		type;
	int		subarg;
	char*	string;

} optargs[] = {

	{ RDONLY,	0,	"ro"	   },
	{ NORR,		0,	"norridge" },
	{ RAW,		0,	"raw"      },
	{ MASK,		1,	"mask"     },
	{ USER,		1,	"user"     },
	{ GROUP,	1,	"group"    },
	{ LOCK,		0,	"lock"     },
	{ WRONG,	0,	NULL	   }

};

IsoArgs_t	iso9660_margs = { 0, 0, 0, DEFUSR, DEFGRP };
IsoArgs_t	*iso_mptr;



uid_t
getuserid ( string )

char*	string;
{
	char*			end;
	uid_t			uid;
	struct passwd*	pwdbuf;

	uid = (uid_t) strtol ( string, &end, 0 );

	if ( *end == '\0' && uid > 0 && uid < UID_MAX )
	{
		return uid;
	}

	pwdbuf = getpwnam ( string );

	if ( pwdbuf == NULL )
	{
		fprintf ( stderr, "%s: `%s' not an existing user.\n", progname, string );
		exit    ( 1 );
	}

	return pwdbuf->pw_uid;
}


gid_t
getgroupid ( string )

char*	string;
{
	char*			end;
	gid_t			gid;
	struct group*	grpbuf;

	gid = (gid_t) strtol ( string, &end, 0 );

	if ( *end == '\0' && gid > 0 && gid < UID_MAX )
	{
		return gid;
	}

	grpbuf = getgrnam ( string );

	if ( grpbuf == NULL )
	{
		fprintf ( stderr, "%s: `%s' not an existing group.\n", progname, string );
		exit    ( 1 );
	}

	return grpbuf->gr_gid;
}


main ( argc, argv )

int	argc;
char**	argv;
{
	unsigned		typeflag;
	int				flags;
	char*			special;
	char*			mpoint;
	char			timbuf[TIMAX];
	char*			ptr;
	FILE*			fp;
	int				mask;
	uid_t			uid;
	gid_t			gid;
	struct optarg*	argptr;
	struct mnttab	mtab;


	typeflag = 0;
	flags    = MS_RDONLY;
	special  = NULL;
	mpoint   = NULL;

	progname = strrchr ( argv[0], '/' );

	if ( progname )
	{
		progname++;
	}
	else
	{
		progname = argv[0];
	}

	while ( ptr = *++argv )
	{
		char*	nextp;

		switch ( *ptr )  {

		case '-':
			if ( !strcmp( ptr, "-r" ) )
			{
				typeflag |= RDONLY;
			}
			else if ( ptr[1] == 'o' )
			{
				char*	subarg;
				char*	eql;

				if ( ptr[2] == '\0' )
				{
					if ( (ptr = *++argv) == NULL )
					{
						usage();
					}
				}
				else
				{
					ptr = &ptr[2];
				}

				do {

					if ( nextp = strchr ( ptr, ',' ) )
					{
						*nextp = '\0';
					}

					for ( argptr=optargs; argptr->type != WRONG; argptr++ )
					{
						if ( argptr->subarg )
						{
							eql = strchr ( ptr, '=' );

							if ( eql == NULL )
							{
								continue;
							}

							*eql   = '\0';
							subarg = eql + 1;
						}

						if ( !strcmp( argptr->string, ptr ) )
							break;

						if ( argptr->subarg )
						{
							*eql = '=';
						}
					}

					switch ( argptr->type )  {

					case WRONG:   usage();
							  	  break;

					case LOCK:
					case RDONLY:  
					case NORR:    
					case RAW:     typeflag |= argptr->type;
							  	  break;

					case USER:    uid = getuserid( subarg );

							  	  typeflag |= argptr->type;
							  	  break;

					case GROUP:   gid = getgroupid( subarg );

							  	  typeflag |= argptr->type;
							  	  break;

					case MASK:	  mask = strtol( subarg, NULL, 0 ) & 0777;

								  typeflag |= argptr->type;
								  break;
					}

					ptr = nextp + 1;

				} while ( nextp != NULL );
			}
			else
			{
				usage();
			}

			break;

		default:
			if ( special )
			{
				if ( mpoint )
				{
					usage();
				}
				else
				{
					mpoint = ptr;
				}
			}
			else
			{
				special = ptr;
			}

			break;
		}
	}

	if ( mpoint == NULL )
	{
		usage();
	}

	/*
	Everything okay now; copy the flags
	suitable for the mount systemcall.
	*/

	if ( typeflag & (RAW | NORR | MASK | USER | GROUP) )
	{
		if ( typeflag & RAW   )	 iso9660_margs.raw  = 1;
		if ( typeflag & NORR  )	 iso9660_margs.norr = 1;
		if ( typeflag & MASK  )	 iso9660_margs.mask = mask;
		if ( typeflag & USER  )	 iso9660_margs.uid  = uid;
		if ( typeflag & GROUP )	 iso9660_margs.gid  = gid;

		iso_mptr = &iso9660_margs;
	}
	else
	{
		iso_mptr = NULL;
	}

	/*
	Make a new mount table entry.
	*/

	mtab.mnt_special = special;
	mtab.mnt_mountp  = mpoint;
	mtab.mnt_fstype  = fstype;

	if ( typeflag & LOCK )
	{
		mtab.mnt_mntopts = "ro,lock";
	}
	else
	{
		mtab.mnt_mntopts = "ro";
	}

	sprintf ( timbuf, "%ld", time(0) );

	mtab.mnt_time = timbuf;

	/*
	Update the mount table file, do it
	locked to prevent concurrent updates.
	*/

	fp = fopen ( mounttab, "r+" );

	if ( fp == NULL || lockf (fileno(fp), F_LOCK, 0) < 0 || fseek (fp, 0, 2) )
	{
		if ( fp  != NULL )	fclose ( fp  );

		fprintf ( stderr, "%s: can't update %s\n", progname, mounttab );
		exit    ( 3 );
	}

	/*
	Don't get interrupted while mounting
	and updating the mount table.
	*/

	signal ( SIGHUP,  SIG_IGN );
	signal ( SIGINT,  SIG_IGN );
	signal ( SIGQUIT, SIG_IGN );

	if (  !doit ( special, mpoint, flags, iso_mptr )  )
	{
		if ( fp  != NULL )	fclose ( fp  );

		exit ( 2 );
	}


	if ( typeflag & LOCK )
	{
		int				fd;
		FILE*			vfsfp;
		struct vfstab	vtab;

		if ( (vfsfp = fopen ( vfstab, "r" )) == NULL         ||
		     getvfsspec ( vfsfp, &vtab, special )            ||
			 (fd = open ( vtab.vfs_fsckdev, O_RDONLY )) < 0  ||
		     ioctl ( fd, C_PREVMV, 0 ) < 0                      )
		{
			fprintf( stderr, "%s: warning can't lock %s\n", progname, special );
			mtab.mnt_mntopts = "ro";
		}

		if ( fd >= 0 )
		{
			close ( fd );
		}

		if ( vfsfp != NULL )
		{
			fclose ( vfsfp );
		}

	}


	putmntent ( fp, &mtab );

	fclose ( fp );

	return ( 0 );

}


usage()
{
	fprintf ( stderr, "Usage: %s [-r] special mount-dir\n", progname );
	fprintf ( stderr, "\t\t[-o {ro|norridge|raw|lock|mask=<mask>}]\n" );
	fprintf ( stderr, "\t\t[-o {user={user|uid}|group={group|gid}}]\n" );
	exit    ( 1 );
}


conflict()
{
	fprintf ( stderr, "%s: conflicting arguments passed.\n", progname );
	exit    ( 1 );
}


doit ( special, mpoint, flags, iso_mptr )

char*				special;
char*				mpoint;
int					flags;
struct iso9660_args	*iso_mptr;
{
	int	datalen;

	datalen = (iso_mptr != NULL) ? sizeof *iso_mptr : 0;
	flags  |= MS_DATA;

	if (  mount ( special, mpoint, flags, fstype, iso_mptr, datalen )  )
	{
		error  ( special, mpoint );
		return ( 0 );
	}

	return ( 1 );
}


error ( special, mpoint )

char*	special;
char*	mpoint;
{
	switch ( errno ) {

	case ENXIO:
		fprintf( stderr, "%s: %s no such device\n", progname, special );
		break;

	case ENOTDIR:
		fprintf( stderr, "%s: %s not a directory\n", progname, mpoint );
		fprintf( stderr, "\tor a component of %s is not a directory\n",
							special );
		break;

	case EPERM:
		fprintf ( stderr, "%s: not super user\n", progname );
		break;

	case ENOTBLK:
		fprintf(stderr, "%s: %s not a block device\n", progname,
								    special);
		break;

	case ENOENT:
		fprintf( stderr, "%s: %s or %s, no such file or directory\n",
						    progname, special, mpoint );
		break;

	case EINVAL:
		fprintf( stderr, "%s: %s is not an iso9660 file system.\n",
							progname, special );
		break;

	case EBUSY:
		fprintf(stderr, "%s: %s is already mounted", progname, special);
		fprintf(stderr, ", %s is busy,\n\tor allowable", mpoint);
		fprintf(stderr, " number of mount points exceeded\n");
		break;

	case EROFS:
		fprintf(stderr, "%s: %s is write protected\n", progname, 
								    special);
		break;

	case ENOSPC:
		fprintf(stderr, "%s: %s status not okay\n", progname, 
								   special);
		break;

	default:
		perror(progname);
		fprintf( stderr, "%s: cannot mount %s\n", progname, special );
	}
}
