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

#ident "@(#)iso9660_util.c	1.2 93/07/17 "

#include <sys/param.h>

/*
 * These routines decode numbers that are stored in various formats
 * on the disk.  The last part of the function name (e.g. 733) tells
 * the chapter and section number of the ISO-9660 spec where that
 * format is defined.
 *
 * Note that there are no alignment guarantees - a 32 bit number 
 * could start at any address.
 */

/* unsigned 8 bit */
int
IsoNum711 ( u_char* p )
{
	return (*p & 0xff);
}

/* signed 8 bit */
int
IsoNum712 ( u_char* p )
{
	int val;

	val = *p;
	if (val & 0x80)
		val |= (-1 << 8);
	return (val);
}

/* unsigned 16 bit, little endian */
int
IsoNum721 ( u_char* p )
{
	return ((p[0] & 0xff) | ((p[1] & 0xff) << 8));
}

/* unsigned 16 bit, big endian */
int
IsoNum722 ( u_char* p )
{
	return (((p[0] & 0xff) << 8) | (p[1] & 0xff));
}

/* unsigned 16 bit, little endian, followed by big endian */
int
IsoNum723 ( u_char* p )
{
	return ((p[0] & 0xff) | ((p[1] & 0xff) << 8));
}

/* signed 32 bit little endian */
int
IsoNum731 ( u_char* p)
{
	return ((p[0] & 0xff) | ((p[1] & 0xff) << 8) |
	    ((p[2] & 0xff) << 16) | ((p[3] & 0xff) << 24));
}

/* signed 32 bit big endian */
int
IsoNum732 ( u_char* p )
{
	return (((p[0] & 0xff) << 24) | ((p[1] & 0xff) << 16) |
	    ((p[2] & 0xff) << 8) | (p[3] & 0xff));
}

/* signed 32 bit little endian, followed by big endian */
int
IsoNum733 ( u_char* p )
{
	return ((p[0] & 0xff)
		| ((p[1] & 0xff) << 8)
		| ((p[2] & 0xff) << 16)
		| ((p[3] & 0xff) << 24));
}
