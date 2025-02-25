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

#ident "@(#)Space.c	1.3 93/07/17 "

#include "config.h"   /* for tunable parameters */

/* 
 * Default number of pcfs 'inodes' if not specified
 * in /etc/conf/cf.d/mtune PCFSNINODE parameter
 */

#ifndef PCFSNINODE
#define PCFSNINODE	100
#endif

long ndenode = PCFSNINODE;
