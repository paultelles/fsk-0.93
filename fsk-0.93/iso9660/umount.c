/*
 *
 *  Written for System V Release 4	(ESIX 4.0.4)	
 *
 *  Gerard van Dorth	(gdorth@nl.oracle.com)
 *  Paul Bauwens	(paul@pphbau.atr.bso.nl)
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

#ident "@(#)umount.c	1.1 93/05/20 "

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
#define RAW	4
#define REMOUNT	8
#define USER	16
#define GROUP	32
#define LOCK	64


#define CIOC		('C' << 8)		/* oops, hope this works ... */
#define	C_PREVMV	(CIOC | 0x07)	/* prevent medium removal    */
#define	C_ALLOMV	(CIOC | 0x08)	/* allow medium removal      */


char*	progname;
char*	mounttab = "/etc/mnttab";
char*	vfstab   = "/etc/vfstab";
char	temp[30];


main ( argc, argv )

int	argc;
char**	argv;
{
	char*			special;
	char*			ptr;
	FILE*			fp;
	FILE*			ftp;
	int				stat;
	int				dounlock;
	int				goteinval;
	struct mnttab	mget;
	struct mnttab	msearch;


	dounlock  = 0;
	goteinval = 0;
	special   = NULL;
	ftp       = NULL;

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
		switch ( *ptr )  {

		case '-':
			if ( ptr[1] == 'o' )
			{
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

				if (  strcmp( ptr, "unlock" )  )
				{
					usage();
				}
				else
				{
					dounlock = 1;
				}

			}
			else
			{
				usage();
			}

			break;

		default:
			if ( special )
			{
				usage();
			}
			else
			{
				special = ptr;
			}

			break;
		}
	}

	if ( special == NULL )
	{
		usage();
	}

	if ( getuid() != 0 )
	{
		fprintf ( stderr, "%s: permission denied\n", progname );
	}

	/*
	Everything okay now; do the umount.
	Note that the umount option might
	be a special as well as a mount
	point.
	*/

	fp = fopen ( mounttab, "r" );

	if ( fp == NULL )
	{
		/*
		No mounttab; assume special ...
		*/

		fprintf ( stderr, "%s: warning cannot open %s\n", progname, mounttab );
		mget.mnt_special = mget.mnt_mountp = special;
	}
	else
	{
		/*
		First try as mount point; if not okay
		try as special.
		*/

		mntnull ( &msearch );
		msearch.mnt_mountp = special;

		stat = getmntany ( fp, &mget, &msearch );

		if ( stat == -1 )
		{
			msearch.mnt_special = special;
			msearch.mnt_mountp  = NULL;

			rewind ( fp );

			stat = getmntany ( fp, &mget, &msearch );

			if ( stat == -1 )
			{
				fprintf ( stderr, "%s: warning: %s not in mnttab\n", progname,
																	 special  );
				mntnull ( &mget );
				mget.mnt_special = mget.mnt_mountp = special;
			}
		}

		fclose ( fp );

		if ( stat > 0 )
		{
			mounterror ( stat );
		}
	}

	stat = umount ( mget.mnt_mountp );

	if ( stat < 0  &&  errno != EBUSY )
	{
		stat = umount ( mget.mnt_special );
	}

	/*
	If we fail and the errno equals EINVAL
	we have some junk in mnttab - let's
	remove it.
	*/

	if ( stat < 0 )
	{
		error ( mget.mnt_special, mget.mnt_mountp );

		if ( errno != EINVAL )
		{
			exit ( 1 );
		}

		goteinval = 1;
	}

	/*
	Check whether an unlock is needed.
	*/

	for (  ptr = mget.mnt_mntopts;  !dounlock && ptr != NULL;
												ptr = strchr ( ptr+1, ',' )  )
	{
		if ( *ptr == ',' )
		{
			ptr++;
		}

		if ( !strncmp ( ptr, "lock", 4 ) )
		{
			dounlock = 1;
		}
	}

	/*
	Do the unlock if requested or needed.
	*/

	if ( dounlock )
	{
		int				fd;
		FILE*			vfsfp;
		struct vfstab	vtab;

		if ( (vfsfp = fopen ( vfstab, "r" )) == NULL         ||
		     getvfsspec ( vfsfp, &vtab, mget.mnt_special )   ||
			 (fd = open ( vtab.vfs_fsckdev, O_RDONLY )) < 0  ||
		     ioctl ( fd, C_ALLOMV, 0 ) < 0                      )
		{
			fprintf ( stderr, "%s: warning can't unlock %s\n", progname,
															mget.mnt_special );
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


	/*
	Remove the mount table entry.
	Update the mount table file, do it
	locked to prevent concurrent updates.
	*/

	fp = fopen ( mounttab, "r+" );

	sprintf ( temp, "%s.%d", mounttab, getpid() );

	ftp = fopen ( temp, "w" );

	special = (char*) malloc ( strlen(mget.mnt_special) + 1 );

	if ( fp == NULL  || lockf ( fileno(fp), F_LOCK, 0 ) < 0  || ftp == NULL ||
		 special == NULL )
	{
		if ( fp  != NULL )	fclose ( fp  );

		if ( ftp != NULL )
		{
			unlink ( temp );
			fclose ( ftp  );
		}

		fprintf ( stderr, "%s: can't update %s\n", progname, mounttab );
		exit    ( 3 );
	}

	strcpy ( special, mget.mnt_special );

	/*
	Don't get interrupted while mounting
	and updating the mount table.
	*/

	signal ( SIGHUP,  SIG_IGN );
	signal ( SIGINT,  SIG_IGN );
	signal ( SIGQUIT, SIG_IGN );


	while ( (stat = getmntent(fp, &msearch)) != -1 )
	{
		if ( stat == 0 && strcmp ( special, msearch.mnt_special ) )
		{
			putmntent ( ftp, &msearch );
		}
	}

	fclose ( ftp );
	rename ( temp, mounttab );

	fclose ( fp );

	return ( goteinval );
}


usage()
{
	fprintf ( stderr, "Usage: %s [-o unlock] {special | mount-point}\n",
																	progname );
	exit    ( 1 );
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
		fprintf( stderr, "%s: %s is not a mounted iso9660 file system.\n",
							progname, special );
		break;

	case EBUSY:
		fprintf(stderr, "%s: %s is busy.\n", progname, special);
		break;

	default:
		perror(progname);
		fprintf( stderr, "%s: cannot mount %s\n", progname, special );
	}
}

mounterror ( flag )

int     flag;
{
	switch (flag) {

	case MNT_TOOLONG:
			fprintf(stderr, "%s: line in mnttab exceeds %d characters\n",
													progname, MNT_LINE_MAX-2);
			break;
	case MNT_TOOFEW:
			fprintf(stderr, "%s: line in mnttab has too few entries\n",
																	progname);
			break;
	default:
			break;
	}

	exit ( 1 );
}
