#
# 
#  Written for System V Release 4	(ESIX 4.0.4)	
#
#  Gerard van Dorth	(gdorth@nl.oracle.com)
#  Paul Bauwens	(paul@pphbau.atr.bso.nl)
#
#  May 1993
#
# This software is provided "as is".
#
# The author supplies this software to be publicly
# redistributed on the understanding that the author
# is not responsible for the correct functioning of
# this software in any circumstances and is not liable
# for any damages caused by this software.
#

# "@(#)Makefile	1.5 93/10/17 "

CONF	= /etc/conf
INC	    = /usr/include
INCSYS	= $(INC)/sys
INCFS	= $(INCSYS)/fs

FSCMDS	= /etc/fs/iso9660

CAT1DIR = /usr/share/man/cat1

# CONFIGURATION OPTIONS
#

# If you are on SYSV release 4.2, uncomment the following line.
#SYSVR42=-DSYSVR42

# Normal compilation
CFLAGS = -O -D_KERNEL $(SYSVR42) -I. -I$(INC) 

# For debugging purposes
# CFLAGS = -D_KERNEL -DDEBUG -DISO9660_DEBUG $(SYSVR42) -I. -I$(INC)

FRC =

MOUNT  = mount
UMOUNT  = umount
DRIVER = Driver.o

SRCS = \
	iso9660_lookup.c \
	iso9660_node.c \
	iso9660_util.c \
	iso9660_vfsops.c \
	iso9660_vnops.c \
	$(FRC)

OBJS = \
	iso9660_lookup.o \
	iso9660_node.o \
	iso9660_util.o \
	iso9660_vfsops.o \
	iso9660_vnops.o \
	$(FRC)

all:	$(DRIVER) $(MOUNT) $(UMOUNT)

$(DRIVER):	$(OBJS)
	$(LD) -r -o $(DRIVER) $(OBJS)


$(MOUNT):	mount.c iso9660.h
		$(CC) -s -o $(MOUNT) -O mount.c

$(UMOUNT):	umount.c iso9660.h
		$(CC) -s -o $(UMOUNT) -O umount.c

# kernel build - must be done as root

build:	install
	/etc/conf/bin/idbuild

# filesystem driver install - must be done as root

install:	all 
	@[ -w $(CONF) ] || { echo 'You must be root to install!';exit 1; }
	[ -d $(FSCMDS) ] || mkdir -p $(FSCMDS) 
	/usr/sbin/install -f $(FSCMDS) $(MOUNT)
	/usr/sbin/install -f $(FSCMDS) $(UMOUNT)
	/usr/sbin/install -f $(INCFS) iso9660.h
	/etc/conf/bin/idinstall -d iso9660
	/etc/conf/bin/idinstall -k -a -opcl iso9660

install.man:
	@[ -w $(CONF) ] || { echo 'You must be root to use install.man!';exit 1; }
	nroff -man -Tlp mount.1 | compress > mount_iso9660.1a.Z
	@[ -d $(CAT1DIR) ] || mkdir -p $(CAT1DIR)
	install -f $(CAT1DIR) mount_iso9660.1a.Z
	rm -f mount_iso9660.1a.Z

lint:
	lint -n -m -u -v -x $(CFLAGS) $(SRCS) > lint.out

clean:
	-rm -f *.o
	-rm -f lint.out
	-rm -f cscope.out tags

clobber:	clean
	-rm -f $(DRIVER) $(MOUNT) $(UMOUNT)


#
# Header dependencies
#
# DO NOT DELETE THIS LINE -- make depend depends on it.

iso9660_lookup.o: /usr/include/sys/param.h /usr/include/sys/types.h
iso9660_lookup.o: /usr/include/sys/fs/s5param.h /usr/include/sys/errno.h
iso9660_lookup.o: /usr/include/sys/buf.h /usr/include/sys/vnode.h
iso9660_lookup.o: /usr/include/sys/time.h /usr/include/sys/vfs.h
iso9660_lookup.o: /usr/include/sys/mount.h /usr/include/sys/pathname.h
iso9660_lookup.o: /usr/include/sys/cred.h /usr/include/sys/dnlc.h
iso9660_lookup.o: /usr/include/sys/cmn_err.h /usr/include/sys/kmem.h
iso9660_lookup.o: /usr/include/sys/utsname.h /usr/include/sys/statvfs.h
iso9660_lookup.o: /usr/include/sys/uio.h /usr/include/sys/debug.h
iso9660_lookup.o: /usr/include/sys/fcntl.h /usr/include/sys/file.h
iso9660_lookup.o: /usr/include/sys/flock.h iso9660.h iso9660_node.h
iso9660_node.o: /usr/include/sys/param.h /usr/include/sys/types.h
iso9660_node.o: /usr/include/sys/fs/s5param.h /usr/include/sys/systm.h
iso9660_node.o: /usr/include/sys/mount.h /usr/include/sys/proc.h
iso9660_node.o: /usr/include/sys/immu.h /usr/include/sys/disp.h
iso9660_node.o: /usr/include/sys/priocntl.h /usr/include/sys/buf.h
iso9660_node.o: /usr/include/sys/time.h /usr/include/sys/errno.h
iso9660_node.o: /usr/include/sys/vfs.h /usr/include/sys/vnode.h
iso9660_node.o: /usr/include/sys/cmn_err.h /usr/include/sys/statvfs.h
iso9660_node.o: /usr/include/sys/kmem.h /usr/include/sys/swap.h
iso9660_node.o: /usr/include/sys/cred.h /usr/include/sys/debug.h
iso9660_node.o: /usr/include/sys/pathname.h /usr/include/sys/fcntl.h
iso9660_node.o: /usr/include/sys/file.h /usr/include/sys/flock.h iso9660.h
iso9660_node.o: iso9660_node.h
iso9660_util.o: /usr/include/sys/param.h /usr/include/sys/types.h
iso9660_util.o: /usr/include/sys/fs/s5param.h
iso9660_vfsops.o: /usr/include/sys/types.h /usr/include/sys/param.h
iso9660_vfsops.o: /usr/include/sys/fs/s5param.h /usr/include/sys/systm.h
iso9660_vfsops.o: /usr/include/sys/proc.h /usr/include/sys/immu.h
iso9660_vfsops.o: /usr/include/sys/disp.h /usr/include/sys/priocntl.h
iso9660_vfsops.o: /usr/include/sys/sysmacros.h /usr/include/sys/kmem.h
iso9660_vfsops.o: /usr/include/sys/vnode.h /usr/include/sys/time.h
iso9660_vfsops.o: /usr/include/sys/pathname.h /usr/include/sys/vfs.h
iso9660_vfsops.o: /usr/include/sys/mount.h /usr/include/sys/buf.h
iso9660_vfsops.o: /usr/include/sys/file.h /usr/include/sys/cred.h
iso9660_vfsops.o: /usr/include/sys/cmn_err.h /usr/include/sys/errno.h
iso9660_vfsops.o: /usr/include/sys/debug.h /usr/include/sys/statvfs.h
iso9660_vfsops.o: /usr/include/sys/uio.h /usr/include/sys/conf.h
iso9660_vfsops.o: /usr/include/sys/swap.h /usr/include/fs/fs_subr.h
iso9660_vfsops.o: /usr/include/sys/fcntl.h /usr/include/sys/flock.h iso9660.h
iso9660_vfsops.o: iso9660_node.h
iso9660_vnops.o: /usr/include/sys/types.h /usr/include/sys/buf.h
iso9660_vnops.o: /usr/include/sys/cmn_err.h /usr/include/sys/conf.h
iso9660_vnops.o: /usr/include/sys/cred.h /usr/include/sys/user.h
iso9660_vnops.o: /usr/include/sys/param.h /usr/include/sys/fs/s5param.h
iso9660_vnops.o: /usr/include/sys/proc.h /usr/include/sys/immu.h
iso9660_vnops.o: /usr/include/sys/signal.h /usr/include/sys/siginfo.h
iso9660_vnops.o: /usr/include/sys/tss.h /usr/include/sys/seg.h
iso9660_vnops.o: /usr/include/sys/resource.h /usr/include/sys/exec.h
iso9660_vnops.o: /usr/include/sys/vnode.h /usr/include/sys/time.h
iso9660_vnops.o: /usr/include/vm/faultcatch.h /usr/include/sys/debug.h
iso9660_vnops.o: /usr/include/sys/dirent.h /usr/include/sys/errno.h
iso9660_vnops.o: /usr/include/sys/fbuf.h /usr/include/sys/fcntl.h
iso9660_vnops.o: /usr/include/sys/file.h /usr/include/sys/flock.h
iso9660_vnops.o: /usr/include/sys/kmem.h /usr/include/sys/inline.h
iso9660_vnops.o: /usr/include/sys/mman.h /usr/include/sys/open.h
iso9660_vnops.o: /usr/include/sys/pathname.h /usr/include/sys/sysinfo.h
iso9660_vnops.o: /usr/include/sys/uio.h /usr/include/sys/vfs.h
iso9660_vnops.o: /usr/include/sys/dnlc.h /usr/include/sys/stat.h
iso9660_vnops.o: /usr/include/sys/swap.h /usr/include/sys/sysmacros.h
iso9660_vnops.o: /usr/include/sys/statvfs.h /usr/include/sys/ioccom.h
iso9660_vnops.o: /usr/include/fs/fs_subr.h iso9660.h iso9660_node.h
mount.o: /usr/include/stdio.h /usr/include/string.h /usr/include/sys/signal.h
mount.o: /usr/include/unistd.h /usr/include/sys/unistd.h /usr/include/errno.h
mount.o: /usr/include/sys/errno.h /usr/include/fcntl.h
mount.o: /usr/include/sys/types.h /usr/include/sys/fcntl.h /usr/include/pwd.h
mount.o: /usr/include/grp.h /usr/include/limits.h /usr/include/sys/mnttab.h
mount.o: /usr/include/sys/vfstab.h /usr/include/sys/mount.h
mount.o: /usr/include/sys/vnode.h /usr/include/sys/time.h iso9660.h
umount.o: /usr/include/stdio.h /usr/include/string.h
umount.o: /usr/include/sys/signal.h /usr/include/unistd.h
umount.o: /usr/include/sys/unistd.h /usr/include/errno.h
umount.o: /usr/include/sys/errno.h /usr/include/fcntl.h
umount.o: /usr/include/sys/types.h /usr/include/sys/fcntl.h
umount.o: /usr/include/pwd.h /usr/include/grp.h /usr/include/limits.h
umount.o: /usr/include/sys/mnttab.h /usr/include/sys/vfstab.h
umount.o: /usr/include/sys/mount.h /usr/include/sys/vnode.h
umount.o: /usr/include/sys/time.h iso9660.h
