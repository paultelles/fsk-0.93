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

#ident "@(#)iso9660_lookup.c	1.3 93/09/16 "

/*
 * Copyright (c) 1989 The Regents of the University of California.
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
 * Started with
 *	ufs_lookup.c	7.33 (Berkeley) 5/19/91
 */

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/buf.h>
#include <sys/fbuf.h>
#include <sys/vnode.h>
#include <sys/vfs.h>
#include <sys/mount.h>
#include <sys/pathname.h>
#include <sys/cred.h>
#include <sys/dnlc.h>
#include <sys/cmn_err.h>
#include <sys/kmem.h>
#include <sys/utsname.h>
#include <sys/statvfs.h>
#include <sys/uio.h>
#include <sys/debug.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/flock.h>

#include <vm/seg.h>


#include "iso9660.h"
#include "iso9660_node.h"


static int IsoDirParse ( IsoDirInfo_t*, IsoDirectoryRecord_t*, int, int );


void
IsoDirOpen ( vnode_t* vp, IsoVfs_t* ivp, IsoFileId_t fileid,
			 IsoFileId_t max_fileid, int want_name,
			 int want_link, struct uio* uio, IsoDirInfo_t** dirpp )
{
	IsoDirInfo_t	*dirp;
	int				extra_space;
	char			*p;
	int				allocsize;

#ifdef ISO9660_DEBUG
	printf ( "In IsoDirOpen start %x, end %x\n", fileid, max_fileid );
#endif

	extra_space = 0;

	if (want_name)
	{
		extra_space += NAME_MAX + 1;
	}

	if (want_link)
	{
		extra_space += NAME_MAX + 1;
	}

	allocsize = sizeof *dirp + extra_space;
	dirp      = (IsoDirInfo_t*) kmem_zalloc(allocsize, KM_SLEEP);
	p         = (char *)dirp + sizeof *dirp;

	if (want_name)
	{
		dirp->name = p;
		dirp->name[0] = 0;
		p += NAME_MAX + 1;
	}

	if (want_link)
	{
		dirp->link = p;
		dirp->link[0] = 0;
		p += NAME_MAX + 1;
	}

	dirp->vp  = vp;
	dirp->uio = uio;
	dirp->ivp = ivp;

	dirp->allocsize   = allocsize;
	dirp->next_fileid = fileid;
	dirp->max_fileid  = max_fileid;

	if ( vp )
	{
		dirp->first_fileid = IsoLogBlkToSize( ivp, VTOI(vp)->extent );
	}

	*dirpp = dirp;
}

int
IsoDirGet ( IsoDirInfo_t* dirp )
{
	vnode_t					*vp;
	IsoVfs_t				*ivp;
	IsoDirectoryRecord_t	*ep;
	int						reclen;
	u_int					off;
	u_int					blk;
	int						error;

#ifdef ISO9660_DEBUG
	printf ( "In IsoDirGet\n" );
#endif

	if ( dirp->error )
	{
		return dirp->error;
	}

	dirp->fileid = dirp->next_fileid;
	vp			 = dirp->vp;
	ivp          = dirp->ivp;

again:
	if (dirp->fileid + sizeof(IsoDirectoryRecord_t) > dirp->max_fileid)
	{
		return (ENOENT);
	}

	if ( vp != NULL )
	{
		struct fbuf	*fbp;
		u_int		fileoff;

		fbp     = dirp->d_fbp;
		fileoff = dirp->fileid - dirp->first_fileid;
		off     = fileoff & PAGEOFFSET;

		if ( fbp == NULL || off == 0 )
		{
			if ( fbp != NULL )
			{
				fbrelse ( fbp, S_OTHER );
			}

			error = fbread ( vp, fileoff & PAGEMASK, PAGESIZE, S_OTHER, &fbp );

			if ( error )
			{
				dirp->error = error;
				dirp->d_fbp = NULL;

				return error;
			}

			dirp->d_fbp = fbp;
		}	

		ep = (IsoDirectoryRecord_t *) (fbp->fb_addr + off);
	}
	else
	{
		struct buf	*bp;

		bp  = dirp->d_bp;
		off = IsoBlkOff(ivp, dirp->fileid);

		if (bp == NULL || off == 0)
		{
			if (bp != NULL)
			{
				brelse(bp);
			}

			blk = IsoLogBlk(ivp, dirp->fileid);
			bp  = bread ( ivp->im_dev, blk, ivp->im_bsize );

			if ( error = geterror(bp) )
			{
				brelse(bp);

				dirp->error = error;
				dirp->d_bp    = NULL;

				return error;
			}

			dirp->d_bp = bp;
		}

		ep = (IsoDirectoryRecord_t *)(bp->b_un.b_addr + off);
	}


	reclen = IsoNum711(ep->length);

	if (reclen == 0)
	{
		/* 
		This is a marker that means skip to next block.  It
		shouldn't ever happen in the first byte of a block,
		but make sure we don't get stuck here if it does.
		*/

		if ((dirp->fileid % ivp->im_bsize) == 0)
		{
			dirp->fileid++;
		}

		dirp->fileid = ROUNDUP(dirp->fileid, ivp->im_bsize);
		goto again;
	}

	/*
	Directory entries do not cross block boundaries.

	if ( IsoBlkOff(ivp, off) + reclen > ivp->im_bsize )
	{
		goto bad_entry;
	}
	*/

	if (dirp->error = IsoDirParse(dirp, ep, reclen, ivp->im_hsierra))
	{
		goto bad_entry;
	}

	dirp->next_fileid = dirp->fileid + reclen;

	/*
	If the file doesn't start at a sector offset
	(due to an odd blocksize) we will get troubles,
	for this reason we introduce a bias to compensate
	for it.
	*/

	if ( ivp->im_bsize < SECTSIZE )
	{
		 IsoFileId_t	offset;

		 offset = IsoLogBlkToSize(ivp, dirp->extent);

		 if ( dirp->bias = offset & SECTOFFSET )
		 {
		 	dirp->extent = IsoLogBlk(ivp, offset & ~SECTOFFSET);
		 }
	}
	else
	{
		dirp->bias = 0;
	}

	return (0);

bad_entry:
	dirp->error = ENOENT;

	return dirp->error;
}


static int
IsoDirParse ( IsoDirInfo_t* dirp, IsoDirectoryRecord_t* ep,
			  int reclen, int hsierra )
{
	IsoVfs_t			*ivp;
	vnode_t				*vp;
	IsoNode_t			*ip;
	int					namelen;
	int					suoffset;
	int					sulimit;
	int					susp_len;
	u_int				extra;
	struct susp			*susp;
	struct susp_px		*susp_px;
	struct susp_pn		*susp_pn;
	struct susp_rr		*susp_rr;
	struct susp_tf		*susp_tf;
	struct susp_nm		*susp_nm;
	struct susp_ce		*susp_ce;
	struct susp_cl		*susp_cl;
	struct susp_pl		*susp_pl;
	struct susp_sl		*susp_sl;
	struct susp_slc		*slc;
	int					rr_wanted;
	char				*subase;
	int					dot;
	int					dotdot;
	int					ce_block;
	int					ce_offset;
	int					ce_length;
	int					rr_name_offset;
	int					block;
	int					off;
	struct buf			*bp;
	struct fbuf			*fbp;
	int					error;
	int					len;
	int					offset;
	int					ts;
	int					flags;
	char				*np;
	char				*name;
	char				*p;
	char				*q;
	int					cflags;
	int					doing_relocated;
	int					cl_block;

#ifdef ISO9660_DEBUG
	printf ( "In IsoParse\n" );
#endif

	bp    = NULL;
	fbp   = NULL;
	error = 0;
	vp    = dirp->vp;
	ivp   = dirp->ivp;

	if ( vp )
	{
		ip = VTOI(vp);
	}

	bzero ((caddr_t) &dirp->rr, sizeof dirp->rr);

	name      = NULL;
	rr_wanted = 0;

	if (dirp->link)
	{
		rr_wanted |= RR_SL;
	}

	if (dirp->name)
	{
		rr_wanted |= RR_NM;
	}

	name    = IsoDirName(ep);
	namelen = IsoNum711(ep->name_len);

	dot    = 0;
	dotdot = 0;

	if (namelen == 1)
	{
		/*
		iso9660 uses '\000' for . and '\001' for ..
		*/

		if (name[0] == 0)
		{
			dot = 1;
		}
		else if (name[0] == 1)
		{
			dotdot = 1;
		}
	}

	doing_relocated = 0;

do_relocated:

	subase   = (char *)ep;
	suoffset = ROUNDUP(ISO9660_DIR_NAME + IsoNum711(ep->name_len), 2);

	if (dirp->fileid != ivp->root_fileid)
	{
		suoffset += ivp->im_rock_ridge_skip;
	}

	sulimit = IsoNum711( ep->length );
	extra   = IsoNum711( ep->ext_attr_length );
	
	if ( ivp->im_norr )
	{
		rr_wanted = 0;
	}
	else
	{
		rr_wanted |= RR_PX | RR_PN | RR_TF;
	}

	bcopy(ep->date, dirp->iso9660_time, sizeof dirp->iso9660_time);

	dirp->iso9660_flags = IsoNum711 ( ep->flags - hsierra  ); /* oops */
	dirp->extent        = IsoNum733 ( ep->extent ) + extra;
	dirp->size          = IsoNum733 ( ep->size );


	/*
	Rock Ridge does not support hard links, except the special
	cases of dot and dotdot.  Therefore, we can use the address
	of the directory entry as the fileid.  To handle the special
	case, we always use the address of the "." entry for directories.
	*/

	if (dirp->iso9660_flags & ISO9660_DIR_FLAG)
	{
		dirp->translated_fileid = IsoLogBlkToSize(ivp, dirp->extent);
	}
	else
	{
		dirp->translated_fileid = dirp->fileid;
	}

	cl_block       = 0;
	ce_block       = 0;
	ce_offset      = 0;
	ce_length      = 0;
	rr_name_offset = 0;

	while (rr_wanted || dirp->uio)
	{
		if (suoffset + SUSP_MIN_SIZE > sulimit)
		{
			paddr_t		phaddr;

			/*
			Used up current system usage area.  If we
			know a valid continuation, then do it now.
			*/

			if (ce_length < SUSP_MIN_SIZE)
			{
				if (cl_block == 0)
				{
					break;
				}

				ce_block  = cl_block;
				ce_offset = 0;
				ce_length = ivp->im_bsize;
			}

			block = ce_block + (ce_offset / ivp->im_bsize);
			off   = ce_offset & (ivp->im_bsize - 1);

			if ( vp )
			{
				uint	fileoff;

				if ( fbp )
				{
					fbrelse ( fbp, S_OTHER );
				}

				/*
				This is a completely file unassociated access ... better
				have a proper file offset here (we might get troubles if
				we try this PAGESIZED the end address might be illegal).
				*/

				fileoff = IsoLogBlkToSize( ivp, block - ip->extent );

				error = fbread ( vp, fileoff, ivp->im_bsize, S_OTHER, &fbp );

				if ( error )
				{
					return error;
				}

				phaddr = (paddr_t) fbp->fb_addr;
			}
			else
			{
				if ( bp )
				{
					brelse(bp);
				}

				bp = bread ( ivp->im_dev, block, ivp->im_bsize );

				if ( error = geterror(bp) )
				{
					goto done;
				}

				phaddr = paddr(bp);
			}

			/*
			Ignore continuation area if it claims to
			be bigger than a block.
			XXX This is wrong.  I've since learned that
			continuation area may span block boundaries.
			I think it can only happen with ridiculously
			long symbolic links, so I'll put off fixing
			it for a while.
			*/

			if (off + ce_length > ivp->im_bsize)
			{
				error = ENOENT;
				goto done;
			}

			if (cl_block)
			{
				ep              = (IsoDirectoryRecord_t *) phaddr;
				reclen          = IsoNum711(ep->length);
				cl_block        = 0;
				doing_relocated = 1;

				goto do_relocated;
			}

			subase    = (char*) phaddr;
			suoffset  = off;
			sulimit   = off + ce_length;
			ce_length = 0;
		}

		susp     = (struct susp *)(subase + suoffset);
		susp_len = IsoNum711(susp->length);
			
		if (susp_len < SUSP_MIN_SIZE || suoffset + susp_len > sulimit)
		{
			break;
		}

		if (dirp->uio)
		{
			uiomove (susp, susp_len, UIO_READ, dirp->uio);
		}

		switch (SUSP_CODE(susp->code[0], susp->code[1])) {

		case SUSP_CODE('R','R'):
			/*
			This field provides a bit map of which other
			fields are present.
			*/

			if (susp_len < SUSP_RR_LEN)
			{
				break;
			}

			susp_rr = (struct susp_rr *)susp;
			
			dirp->rr.rr_have_rr = 1;
			dirp->rr.rr_rr = IsoNum711(susp_rr->flags);
			
			/* Stop looking for fields we will never find.  */

			rr_wanted &= dirp->rr.rr_rr;
			break;
			
		case SUSP_CODE('P','X'):
			if (susp_len < SUSP_PX_LEN)
			{
				break;
			}

			susp_px = (struct susp_px *)susp;
			
			if ((rr_wanted & RR_PX) == 0)
			{
				break;
			}

			/* Only one PX is allowed. */

			rr_wanted &= ~RR_PX;
			
			dirp->rr.rr_flags |= RR_PX;
			dirp->rr.rr_mode   = IsoNum733(susp_px->mode) & ~ivp->im_mask;
			dirp->rr.rr_nlink  = IsoNum733(susp_px->nlink);

			if ( ivp->im_uid != DEFUSR )
			{
				dirp->rr.rr_uid = ivp->im_uid;
			}
			else
			{
				dirp->rr.rr_uid = IsoNum733(susp_px->uid);
			}

			if ( ivp->im_gid != DEFGRP )
			{
				dirp->rr.rr_gid = ivp->im_gid;
			}
			else
			{
				dirp->rr.rr_gid = IsoNum733(susp_px->gid);
			}

			break;
			
		case SUSP_CODE('P','N'):
			if (susp_len < SUSP_PN_LEN)
			{
				break;
			}

			susp_pn = (struct susp_pn *)susp;
			
			if ((rr_wanted & RR_PN) == 0)
			{
				break;
			}

			/* Only one PN is allowed. */

			rr_wanted &= ~RR_PN;
			
			dirp->rr.rr_flags   |= RR_PN;
			dirp->rr.rr_dev_high = IsoNum733(susp_pn->dev_high);
			dirp->rr.rr_dev_low  = IsoNum733(susp_pn->dev_low);

			if (dirp->rr.rr_dev_high)
			{
				/* ???
				dirp->rr.rr_rdev=makedev(dirp->rr.rr_dev_high,
													dirp->rr.rr_dev_low)*/;
			}
			else
			{
				dirp->rr.rr_rdev = dirp->rr.rr_dev_low;
			}
			break;
			
		case SUSP_CODE('T','F'):
			if (susp_len < SUSP_TF_LEN)
			{
				break;
			}

			susp_tf = (struct susp_tf *)susp;
			
			if ((rr_wanted & RR_TF) == 0)
			{
				break;
			}

			/* 
			There may be more than one TF field,
			so just because we found this one, we
			can't stop looking for more.
			*/

			flags = IsoNum711(susp_tf->flags);
			len   = (flags & RR_LONG_FORM) ?
			    		RR_LONG_TIMESTAMP_LEN : RR_SHORT_TIMESTAMP_LEN;
			
			offset = SUSP_TF_LEN;

			for (ts = 0; ts < RR_TIMESTAMPS; ts++)
			{
				if (offset + len > susp_len)
				{
					break;
				}

				if ((flags & (1 << ts)) == 0)
				{
					continue;
				}

				if (ts >= RR_MODIFY && ts <= RR_ATTRIBUTES)
				{
					bcopy((char *)susp+offset, dirp->rr.rr_time[TS_INDEX(ts)],
					    												   len);
					dirp->rr.rr_time_format[TS_INDEX(ts)] = len;
					dirp->rr.rr_flags |= RR_TF;
				}

				offset += len;
			}
			break;
			
		case SUSP_CODE('N','M'):
			if (susp_len < SUSP_NM_LEN)
			{
				break;
			}

			susp_nm = (struct susp_nm *)susp;
			
			if ((rr_wanted & RR_NM) == 0)
			{
				break;
			}
			
			dirp->rr.rr_flags |= RR_NM;

			/* spec says ignore NM for . and .. */
			if (dot || dotdot)
				break;
			
			flags = susp_nm->flags[0];
			
			if (flags & RR_NAME_CURRENT)
			{
				np = ".";
				len = 1;
			}
			else if (flags & RR_NAME_PARENT)
			{
				np = "..";
				len = 2;
			}
			else if (flags & RR_NAME_HOST)
			{
				if (*utsname.nodename)
				{
					np = utsname.nodename;
				}
				else
				{
					np = "localhost";
				}

				len = strlen(np);
			}
			else
			{
				np  = SUSP_NM_NAME(susp_nm);
				len = susp_len - SUSP_NM_LEN;
			}
			
			if (rr_name_offset + len > NAME_MAX)
			{
				error = ENOENT;
				goto done;
			}

			bcopy ( np, dirp->name + rr_name_offset, len );
			
			rr_name_offset += len;

			dirp->name[rr_name_offset] = 0;
			dirp->namelen              = rr_name_offset;

			if ((flags & RR_NAME_CONTINUE) == 0)
			{
				rr_wanted &= ~RR_NM;
			}
			
			break;
			
		case SUSP_CODE('S','L'):
			if (susp_len < SUSP_SL_LEN)
			{
				break;
			}

			susp_sl = (struct susp_sl *)susp;

			if ((rr_wanted & RR_SL) == 0)
			{
				break;
			}
			
			flags = IsoNum711(susp_sl->flags);

			if ((flags & RR_NAME_CONTINUE) == 0)
			{
				rr_wanted &= ~RR_SL;
			}
			
			offset = SUSP_SL_LEN;

			while (offset + SUSP_SLC_LEN < susp_len)
			{
				if (dirp->link_separator)
				{
					if (dirp->link_used + 1 >= NAME_MAX)
					{
						goto symlink_too_long;
					}

					dirp->link[dirp->link_used++] = '/';
				}

				slc    = (struct susp_slc *)((char *)susp+offset);
				cflags = IsoNum711(slc->flags);
				len    = -1;

				if (cflags & RR_NAME_CURRENT)
				{
					np = ".";
				}
				else if (cflags & RR_NAME_PARENT)
				{
					np = "..";
				}
				else if (cflags & RR_NAME_ROOT)
				{
					dirp->link_used = 0;
					np = "/";
				}
				else if (cflags & RR_NAME_VOLROOT)
				{
					np = ivp->im_path;
				}
				else if (cflags & RR_NAME_HOST)
				{
					if (*utsname.nodename)
					{
						np = utsname.nodename;
					}
					else
					{
						np = "localhost";
					}
				}
				else
				{
					np  = SLC_NAME(slc);
					len = IsoNum711(slc->length);
				}

				if (len == -1)
				{
					len = strlen (np);
				}

				if (dirp->link_used + len > NAME_MAX)
				{
					goto symlink_too_long;
				}

				bcopy(np, dirp->link + dirp->link_used, len);
				dirp->link_used += len;

				if ( cflags & (RR_NAME_CONTINUE|RR_NAME_ROOT) )
				{
					dirp->link_separator = 0;
				}
				else
				{
					dirp->link_separator = 1;
				}

				offset += SUSP_SLC_LEN + IsoNum711(slc->length);
			}
					
			dirp->link[dirp->link_used] = 0;
			break;

		symlink_too_long:
			rr_wanted &= ~RR_SL;
			dirp->link = NULL;
			break;

		case SUSP_CODE('S','T'):
			/* stop looking at this area */
			suoffset = sulimit;
			break;
			
		case SUSP_CODE('C','E'):
			/* Save pointer to continuation area. */
			if (susp_len < SUSP_CE_LEN)
			{
				break;
			}

			susp_ce = (struct susp_ce *)susp;
			
			ce_block  = IsoNum733(susp_ce->block);
			ce_offset = IsoNum733(susp_ce->offset);
			ce_length = IsoNum733(susp_ce->ce_length);
			break;

		case SUSP_CODE('C','L'):
			/*
			This file represents a directory that has been
			relocated.  Only believe the NM field here -
			get the rest of the information from the
			real "." entry.
			*/

			if (doing_relocated)
			{
				break; /* illegal CL - avoid infinite loop */
			}

			if (susp_len < SUSP_CL_LEN)
			{
				break;
			}

			susp_cl = (struct susp_cl *)susp;

			cl_block = IsoNum733(susp_cl->loc);
			break;

		case SUSP_CODE('P','L'):
			/*
			This code only appears in the .. entry of a 
			directory that has been relocated.  The only
			special handling needed is to pick up the 
			"extent" from this entry, instead of from the
			iso9660 entry.
			*/

			if (susp_len < SUSP_PL_LEN)
			{
				break;
			}

			susp_pl                 = (struct susp_pl *)susp;
			dirp->extent            = IsoNum733(susp_pl->loc);
			dirp->translated_fileid = IsoLogBlkToSize(ivp,
												(IsoFileId_t)dirp->extent);
			break;

		case SUSP_CODE('R','E'):
			/* 
			Hidden directory containing relocated children.
			No special action needed, though we may want
			to hide this entry someday.
			*/

			break;
		}
		
		suoffset += susp_len;
	}

	if (dirp->name && (dirp->rr.rr_flags & RR_NM) == 0)
	{
		/* Didn't find Rock Ridge name, so use iso9660 name. */

		if ( sizeof(IsoDirectoryRecord_t) + namelen > reclen ||
			 namelen > NAME_MAX )
		{
			error = ENOENT;
			goto done;
		}
		
		if (dirp->name)
		{
			if (dot)
			{
				strcpy (dirp->name, ".");
				dirp->namelen = 1;
			}
			else if (dotdot)
			{
				strcpy (dirp->name, "..");
				dirp->namelen = 2;
			}
			else if (ivp->im_raw)
			{
				bcopy(name, dirp->name, namelen);
				dirp->name[namelen] = 0;
				dirp->namelen = namelen;
			}
			else
			{
				/* 
				Truncate at the semicolon delete trailing dot
				translate to lower case
				*/

				for ( len = 0, p = name, q = dirp->name; len < namelen && *p;
				    											len++, p++, q++)
				{
					if (*p == ';')
						break;

					if (*p == '.' && (p[1] == ';' || p[1] == 0))
						break;

					if (*p >= 'A' && *p <= 'Z')
						*q = *p - 'A' + 'a';
					else
						*q = *p;
				}

				*q            = 0;
				dirp->namelen = len;
			}
		}
	}
	
done:
	if ( fbp )
	{
		fbrelse ( fbp, S_OTHER );
	}
	else if ( bp )
	{
		brelse ( bp );
	}

	return error;
}


void
IsoDirClose ( IsoDirInfo_t* dirp )
{

#ifdef ISO9660_DEBUG
	printf ( "In IsoDirClose\n" );
#endif

	if ( dirp->vp != NULL )
	{
		if (dirp->d_fbp)
		{
			fbrelse(dirp->d_fbp, S_OTHER);
		}
	}
	else
	{
		if (dirp->d_bp)
		{
			brelse(dirp->d_bp);
		}
	}

	kmem_free ((caddr_t)dirp, dirp->allocsize);
}


int
IsoLookup ( vnode_t* dvp, char* name, vnode_t** vpp, struct pathname* pnp,
			int flags, vnode_t* rdir, cred_t* cr )
{
	IsoDirInfo_t	*dirp;
	IsoVfs_t		*ivp;
	IsoNode_t		*dp;
	IsoNode_t		*tdp;
	vnode_t			*vp;
	int				nlen;
	int				error;
	IsoFileId_t		start_fileid;
	IsoFileId_t		end_fileid;
	IsoFileId_t		fileid;

#ifdef ISO9660_DEBUG
	printf ( "In IsoLookup for ->%s<-\n", name );
#endif

	dp  = VTOI(dvp);
	ivp = dp->i_vfs;

	if (dvp->v_type != VDIR)
	{
		return (ENOTDIR);
	}

	if (  error = IsoAccess ( dvp, VEXEC, 0, cr )  )
	{
		return error;
	}

	/*
	No name equals '.', check it.
	*/

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

	if ( vp = dnlc_lookup(dvp, name, NOCRED) )
	{
		VN_HOLD ( vp );

		*vpp = vp;
		return 0;
	}

	start_fileid = IsoLogBlkToSize (ivp, dp->extent) + dp->i_bias;
	end_fileid   = start_fileid + i_total(dp);

	IsoDirOpen ( dvp, ivp, start_fileid, end_fileid, 1, 0, NULL, &dirp );

	while ( dirp->next_fileid < end_fileid && !(error = IsoDirGet(dirp)) )
	{
		if ( dirp->namelen == nlen && !bcmp(name, dirp->name, nlen) )
		{
			goto found;
		}
	}

	if (error == 0)
	{
		error = ENOENT;
	}

	goto done;

found:
	fileid = dirp->translated_fileid;

	if (  error = IsoIget(dp, fileid, &tdp, dirp)  )
	{
		goto done;
	}

	vp = ITOV(tdp);
	dnlc_enter ( dvp, name, vp, NOCRED );
	*vpp = vp;

	ISOIUNLOCK(tdp);

done:
	IsoDirClose(dirp);

	return error;
}
