<pre>Newsgroups: alt.sources
Path: funic!news.funet.fi!sunic!mcsun!sun4nl!orcenl!gdorth
From: gdorth@nl.oracle.com (Gerard van Dorth)
Subject: Announcement fsk-0.93
Message-ID: &lt;CF1roG.1sB@nl.oracle.com&gt;
Sender: gdorth@nl.oracle.com
Nntp-Posting-Host: nlsun1
Organization: Oracle Europe
X-Newsreader: TIN [version 1.2 PL1]
Date: Sun, 17 Oct 1993 15:14:40 GMT
Lines: 131


A new version of our SVR4 Filesystem Survival Kit has been posted
to &apos;alt.sources&apos; on October 16, 1993

Latest version: fsk-0.93


1. What is the &apos;Filesystem Survival Kit&apos;
----------------------------------------

The Filesystem Survival Kit is meant to provide the filesystem drivers
that AT&amp;T forgot to include with their SVR4 UNIX system, but are more and
more necessary in a modern installation. Although platform independent
in principle the drivers in this package are primarily meant for the
SVR4 versions available on the Intel platform.

If you don&apos;t have SVR4 (or possibly Solaris 2.x) this package is 
probably not for you. You could use it as a basis for your own
developments though.

The &apos;Filesystem Survival Kit&apos; provides the following 
filesystem drivers:

-  An ISO9660 filesystem driver that enables you to mount CD-ROM disks
   under the UNIX System V Release 4 operating system for the Intel
   platforms.

   The ISO9660 filesystem supports:

   - plain iso9660 CDROM format

   - iso9660 CDROM format with RockRidge extensions

   - High Sierra CDROM format

   You now use NFS to share mounted CDROMs on the network and
   map the cdrom in memory using the mmap() system call.


-  A PCFS filesystem driver to enable you to mount MS-DOS(tm)
   filesystems on either floppy or hard disk under the
   UNIX System V Release 4 operating system for the Intel platforms.

   The PCFS filesystem supports:

   - standard DOS format diskettes (360K / 720K / 1.2M and 1.44M)
         (also 2.88M if your floppy driver allows this...)

   - all types of DOS partitions on your hard disk, provided that the
     hard disk driver is able to access them as block device.
     This is possible on any Intel-based SVR4 implementation.

   You can use NFS to share mounted PCFS partitions on the network.


2. New features since version fsk-0.92
-------------------------------------


The Filesystem Survival Kit version fsk-0.93 provides the following
new features:

        There are NO new features added in this release. This release
        is only a bugfix/speed-up release for the PCFS part:
                The PCFS filesystem is expected to be write safe now.
                                (see also pcfs/README)
        There are NO (relevant) changes made in the ISO9660 part since 0.92.


3   *** DISCLAIMER ***
----------------------

The software in the &apos;fsk-0.93&apos; Filesystem Survival Kit
is provided as is.

The author(s) supply this software to be publicly redistributed
on the understanding that the author(s) are not responsible for the
correct functioning of this software in any circumstances and are
not liable for any damages caused by this software.

Starting with &apos;fsk-0.91&apos; we have put in a lot of testing and we feel
that the drivers are very stable now.

Except for some minor missing features and possible performance
enhancements they should be good enough for general use. 

Since there is always one last (minor) bug it is good practice to
make a backup of things first. It is recommended to make backups of
DOS partitions first before using &apos;pcfs&apos; for the first time.

To put in a lighter note:
------------------------

We have been able to fully backup and restore DOS partitions on different
``cluster-sized&apos;&apos; hard disks using the standard UNIX &apos;tar&apos; command.

The iso9660 filesystem has been very stable right from the first version,
not so surprising since it is read-only.


The drivers in this kit have been developed and tested on:

         ESIX SVR4  4.0.4 on a 486 PC.

A lot of people have reported success with &apos;fsk-0.9x&apos; on the
following systems:

        ESIX SVR4 4.0.4
    DELL SVR4 2.2
        Consensys SVR4
        Sorix (Siemens) SVR4
    USL SVR 4.0.4
        Interactive SVR4.0.3.0

Since these drivers are fairly hardware independent we expect them
to work on most 386/486 ports of System V Release 4.

We have no reports of users using a SVR4.2 version yet.

Possibly because they already have it or because the technical
guys still use SVR4.0. Who needs a graphical desktop anyway :-) :-)

We would like to hear about your experiences on DELL, Consensys,
UHC and others. 

Send bug reports, comments etc. to:

        Gerard van Dorth        gdorth@nl.oracle.com

        Paul Bauwens            paul@pphbau.atr.bso.nl</pre>
