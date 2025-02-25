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

#ident "@(#)pcfs_lbuf.h	1.6 93/10/17 "

#include <sys/kmem.h>

/*
 * logical block I/O layer 
 * saves mapping junk for the moment
 */

/*
 * reserve pointers to handle 16K clustersize
 */

#ifndef MAXCLUSTBLOCKS
#define MAXCLUSTBLOCKS	32
#endif

typedef struct	lbuf {
	union {
		caddr_t b_addr;	/* pointer to logical buffer */
	} b_un;
	size_t	b_bsize;	/* size of logical buffer    */
	buf_t	*bp[MAXCLUSTBLOCKS];	/* pointers to physical buffers */
} lbuf_t;


/* prototypes for pcfs low-level interface functions */

extern	lbuf_t *lbread(dev_t, daddr_t, long int);
extern	lbuf_t *lbreada(dev_t, daddr_t, daddr_t, long int);
extern	lbuf_t *lgetblk(dev_t, daddr_t, long int);
extern	void	lbwrite(lbuf_t*);
extern	void	lbawrite(lbuf_t*);
extern	void	lbdwrite(lbuf_t*);
extern	void	lbrelse(lbuf_t*);
extern	void	lclrbuf(lbuf_t*);
extern	int		lbiowait(lbuf_t*);
extern	int		lbwritewait(lbuf_t*);
extern	void	lbiodone(lbuf_t*);
extern	int		lgeterror(lbuf_t*);
