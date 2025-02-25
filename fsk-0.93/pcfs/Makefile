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
INC		= /usr/include
INCSYS	= $(INC)/sys
INCFS	= $(INCSYS)/fs

FSCMDS	= /etc/fs/pcfs

CAT1DIR = /usr/share/man/cat1

# CONFIGURATION OPTIONS

#
# If you are on a SYSV release 4.2, uncomment the following line;
#SYSVR42=-DSYSVR42

#
# Store the NFS file generation number in the DOS dirctory
# reserved area.
# GENOFF specifies the offset in the DOS directory reserved area
# where 'pcfs' stores the NFS file generation number.
# 0 <= GENOFF <= 8

CFLAGS = -O -D_KERNEL -DGENOFF=0 $(SYSVR42) -I. -I$(INC) 

# Do not store the NFS file generation number in the DOS
# directory reserved area (do not conform fully to NFS specs!)
#
# Always returns same 'generation' number and thus prevents 
# you from detecting 'stale' NFS handles.
#
# CFLAGS = -O -D_KERNEL -DNO_GENOFF $(SYSVR42) -I. -I$(INC) 

# For debugging purposes
# CFLAGS = -D_KERNEL -DGENOFF=0 -DDEBUG -DPCFS_DEBUG $(SYSVR42) -I. -I$(INC)

FRC =

MOUNT  = mount
DRIVER = Driver.o

SRCS = \
	pcfs_vfsops.c \
	pcfs_conv.c \
	pcfs_fat.c \
	pcfs_vnops.c \
	pcfs_lookup.c \
	pcfs_denode.c \
	pcfs_lbuf.c \
	$(FRC)

OBJS = \
	pcfs_vfsops.o \
	pcfs_conv.o \
	pcfs_fat.o \
	pcfs_vnops.o \
	pcfs_lookup.o \
	pcfs_denode.o \
	pcfs_lbuf.o \
	$(FRC)

all:	$(DRIVER) $(MOUNT)

$(DRIVER):	$(OBJS)
	$(LD) -r -o $(DRIVER) $(OBJS)


$(MOUNT):	mount.c pcfs_filsys.h
		$(CC) -s -o $(MOUNT) -O mount.c

# kernel build - must be done as root

build: install
	/etc/conf/bin/idbuild

# filesystem driver install - must be done as root

install:	all 
	@[ -w $(CONF) ] || { echo 'You must be root to install!';exit 1; }
	[ -d $(FSCMDS) ] || mkdir -p $(FSCMDS) 
	/usr/sbin/install -f $(FSCMDS) mount
	/usr/sbin/install -f $(INCFS) pcfs_filsys.h
	/etc/conf/bin/idinstall -d pcfs
	/etc/conf/bin/idinstall -k -a -opcl pcfs

install.man:
	@[ -w $(CONF) ] || { echo 'You must be root to use install.man!';exit 1; }
	nroff -man -Tlp mount.1 | compress > mount_pcfs.1a.Z
	@[ -d $(CAT1DIR) ] || mkdir -p $(CAT1DIR)
	install -f $(CAT1DIR) mount_pcfs.1a.Z
	rm -f mount_pcfs.1a.Z

lint:
	lint -n -m -u -v -x $(CFLAGS) $(SRCS) > lint.out

clean:
	-rm -f *.o
	-rm -f lint.out
	-rm -f cscope.out tags

clobber:	clean
	-rm -f $(DRIVER) $(MOUNT)

#
# Header dependencies
#
# DO NOT DELETE THIS LINE -- make depend depends on it.

mount.o: /usr/include/stdio.h /usr/include/string.h /usr/include/stdlib.h
mount.o: /usr/include/unistd.h /usr/include/sys/unistd.h
mount.o: /usr/include/limits.h /usr/include/errno.h /usr/include/sys/errno.h
mount.o: /usr/include/fcntl.h /usr/include/sys/types.h
mount.o: /usr/include/sys/fcntl.h /usr/include/pwd.h /usr/include/grp.h
mount.o: /usr/include/time.h /usr/include/sys/mnttab.h
mount.o: /usr/include/sys/mount.h /usr/include/sys/signal.h pcfs_filsys.h
pcfs_conv.o: pcfs.h /usr/include/sys/types.h /usr/include/sys/param.h
pcfs_conv.o: /usr/include/sys/fs/s5param.h /usr/include/sys/fcntl.h
pcfs_conv.o: /usr/include/sys/proc.h /usr/include/sys/immu.h
pcfs_conv.o: /usr/include/sys/user.h /usr/include/sys/signal.h
pcfs_conv.o: /usr/include/sys/siginfo.h /usr/include/sys/tss.h
pcfs_conv.o: /usr/include/sys/seg.h /usr/include/sys/resource.h
pcfs_conv.o: /usr/include/sys/exec.h /usr/include/sys/vnode.h
pcfs_conv.o: /usr/include/sys/time.h /usr/include/sys/cred.h
pcfs_conv.o: /usr/include/vm/faultcatch.h /usr/include/sys/stat.h
pcfs_conv.o: /usr/include/sys/statvfs.h /usr/include/sys/dirent.h
pcfs_conv.o: /usr/include/sys/sysmacros.h /usr/include/sys/vfs.h
pcfs_conv.o: /usr/include/sys/buf.h /usr/include/sys/cmn_err.h
pcfs_conv.o: /usr/include/sys/conf.h /usr/include/sys/ddi.h
pcfs_conv.o: /usr/include/sys/uio.h /usr/include/sys/debug.h
pcfs_conv.o: /usr/include/sys/disp.h /usr/include/sys/priocntl.h
pcfs_conv.o: /usr/include/sys/dnlc.h /usr/include/sys/errno.h
pcfs_conv.o: /usr/include/sys/fbuf.h /usr/include/sys/file.h
pcfs_conv.o: /usr/include/sys/flock.h /usr/include/sys/inode.h
pcfs_conv.o: /usr/include/sys/kmem.h /usr/include/sys/mkdev.h
pcfs_conv.o: /usr/include/sys/mman.h /usr/include/sys/mount.h
pcfs_conv.o: /usr/include/sys/open.h /usr/include/sys/pathname.h
pcfs_conv.o: /usr/include/sys/swap.h /usr/include/sys/sysinfo.h
pcfs_conv.o: /usr/include/sys/systm.h /usr/include/fs/fs_subr.h bootsect.h
pcfs_conv.o: bpb.h direntry.h denode.h fat.h pcfs_filsys.h pcfs_lbuf.h
pcfs_denode.o: pcfs.h /usr/include/sys/types.h /usr/include/sys/param.h
pcfs_denode.o: /usr/include/sys/fs/s5param.h /usr/include/sys/fcntl.h
pcfs_denode.o: /usr/include/sys/proc.h /usr/include/sys/immu.h
pcfs_denode.o: /usr/include/sys/user.h /usr/include/sys/signal.h
pcfs_denode.o: /usr/include/sys/siginfo.h /usr/include/sys/tss.h
pcfs_denode.o: /usr/include/sys/seg.h /usr/include/sys/resource.h
pcfs_denode.o: /usr/include/sys/exec.h /usr/include/sys/vnode.h
pcfs_denode.o: /usr/include/sys/time.h /usr/include/sys/cred.h
pcfs_denode.o: /usr/include/vm/faultcatch.h /usr/include/sys/stat.h
pcfs_denode.o: /usr/include/sys/statvfs.h /usr/include/sys/dirent.h
pcfs_denode.o: /usr/include/sys/sysmacros.h /usr/include/sys/vfs.h
pcfs_denode.o: /usr/include/sys/buf.h /usr/include/sys/cmn_err.h
pcfs_denode.o: /usr/include/sys/conf.h /usr/include/sys/ddi.h
pcfs_denode.o: /usr/include/sys/uio.h /usr/include/sys/debug.h
pcfs_denode.o: /usr/include/sys/disp.h /usr/include/sys/priocntl.h
pcfs_denode.o: /usr/include/sys/dnlc.h /usr/include/sys/errno.h
pcfs_denode.o: /usr/include/sys/fbuf.h /usr/include/sys/file.h
pcfs_denode.o: /usr/include/sys/flock.h /usr/include/sys/inode.h
pcfs_denode.o: /usr/include/sys/kmem.h /usr/include/sys/mkdev.h
pcfs_denode.o: /usr/include/sys/mman.h /usr/include/sys/mount.h
pcfs_denode.o: /usr/include/sys/open.h /usr/include/sys/pathname.h
pcfs_denode.o: /usr/include/sys/swap.h /usr/include/sys/sysinfo.h
pcfs_denode.o: /usr/include/sys/systm.h /usr/include/fs/fs_subr.h bootsect.h
pcfs_denode.o: bpb.h direntry.h denode.h fat.h pcfs_filsys.h pcfs_lbuf.h
pcfs_fat.o: pcfs.h /usr/include/sys/types.h /usr/include/sys/param.h
pcfs_fat.o: /usr/include/sys/fs/s5param.h /usr/include/sys/fcntl.h
pcfs_fat.o: /usr/include/sys/proc.h /usr/include/sys/immu.h
pcfs_fat.o: /usr/include/sys/user.h /usr/include/sys/signal.h
pcfs_fat.o: /usr/include/sys/siginfo.h /usr/include/sys/tss.h
pcfs_fat.o: /usr/include/sys/seg.h /usr/include/sys/resource.h
pcfs_fat.o: /usr/include/sys/exec.h /usr/include/sys/vnode.h
pcfs_fat.o: /usr/include/sys/time.h /usr/include/sys/cred.h
pcfs_fat.o: /usr/include/vm/faultcatch.h /usr/include/sys/stat.h
pcfs_fat.o: /usr/include/sys/statvfs.h /usr/include/sys/dirent.h
pcfs_fat.o: /usr/include/sys/sysmacros.h /usr/include/sys/vfs.h
pcfs_fat.o: /usr/include/sys/buf.h /usr/include/sys/cmn_err.h
pcfs_fat.o: /usr/include/sys/conf.h /usr/include/sys/ddi.h
pcfs_fat.o: /usr/include/sys/uio.h /usr/include/sys/debug.h
pcfs_fat.o: /usr/include/sys/disp.h /usr/include/sys/priocntl.h
pcfs_fat.o: /usr/include/sys/dnlc.h /usr/include/sys/errno.h
pcfs_fat.o: /usr/include/sys/fbuf.h /usr/include/sys/file.h
pcfs_fat.o: /usr/include/sys/flock.h /usr/include/sys/inode.h
pcfs_fat.o: /usr/include/sys/kmem.h /usr/include/sys/mkdev.h
pcfs_fat.o: /usr/include/sys/mman.h /usr/include/sys/mount.h
pcfs_fat.o: /usr/include/sys/open.h /usr/include/sys/pathname.h
pcfs_fat.o: /usr/include/sys/swap.h /usr/include/sys/sysinfo.h
pcfs_fat.o: /usr/include/sys/systm.h /usr/include/fs/fs_subr.h bootsect.h
pcfs_fat.o: bpb.h direntry.h denode.h fat.h pcfs_filsys.h pcfs_lbuf.h
pcfs_lbuf.o: pcfs.h /usr/include/sys/types.h /usr/include/sys/param.h
pcfs_lbuf.o: /usr/include/sys/fs/s5param.h /usr/include/sys/fcntl.h
pcfs_lbuf.o: /usr/include/sys/proc.h /usr/include/sys/immu.h
pcfs_lbuf.o: /usr/include/sys/user.h /usr/include/sys/signal.h
pcfs_lbuf.o: /usr/include/sys/siginfo.h /usr/include/sys/tss.h
pcfs_lbuf.o: /usr/include/sys/seg.h /usr/include/sys/resource.h
pcfs_lbuf.o: /usr/include/sys/exec.h /usr/include/sys/vnode.h
pcfs_lbuf.o: /usr/include/sys/time.h /usr/include/sys/cred.h
pcfs_lbuf.o: /usr/include/vm/faultcatch.h /usr/include/sys/stat.h
pcfs_lbuf.o: /usr/include/sys/statvfs.h /usr/include/sys/dirent.h
pcfs_lbuf.o: /usr/include/sys/sysmacros.h /usr/include/sys/vfs.h
pcfs_lbuf.o: /usr/include/sys/buf.h /usr/include/sys/cmn_err.h
pcfs_lbuf.o: /usr/include/sys/conf.h /usr/include/sys/ddi.h
pcfs_lbuf.o: /usr/include/sys/uio.h /usr/include/sys/debug.h
pcfs_lbuf.o: /usr/include/sys/disp.h /usr/include/sys/priocntl.h
pcfs_lbuf.o: /usr/include/sys/dnlc.h /usr/include/sys/errno.h
pcfs_lbuf.o: /usr/include/sys/fbuf.h /usr/include/sys/file.h
pcfs_lbuf.o: /usr/include/sys/flock.h /usr/include/sys/inode.h
pcfs_lbuf.o: /usr/include/sys/kmem.h /usr/include/sys/mkdev.h
pcfs_lbuf.o: /usr/include/sys/mman.h /usr/include/sys/mount.h
pcfs_lbuf.o: /usr/include/sys/open.h /usr/include/sys/pathname.h
pcfs_lbuf.o: /usr/include/sys/swap.h /usr/include/sys/sysinfo.h
pcfs_lbuf.o: /usr/include/sys/systm.h /usr/include/fs/fs_subr.h bootsect.h
pcfs_lbuf.o: bpb.h direntry.h denode.h fat.h pcfs_filsys.h pcfs_lbuf.h
pcfs_lookup.o: pcfs.h /usr/include/sys/types.h /usr/include/sys/param.h
pcfs_lookup.o: /usr/include/sys/fs/s5param.h /usr/include/sys/fcntl.h
pcfs_lookup.o: /usr/include/sys/proc.h /usr/include/sys/immu.h
pcfs_lookup.o: /usr/include/sys/user.h /usr/include/sys/signal.h
pcfs_lookup.o: /usr/include/sys/siginfo.h /usr/include/sys/tss.h
pcfs_lookup.o: /usr/include/sys/seg.h /usr/include/sys/resource.h
pcfs_lookup.o: /usr/include/sys/exec.h /usr/include/sys/vnode.h
pcfs_lookup.o: /usr/include/sys/time.h /usr/include/sys/cred.h
pcfs_lookup.o: /usr/include/vm/faultcatch.h /usr/include/sys/stat.h
pcfs_lookup.o: /usr/include/sys/statvfs.h /usr/include/sys/dirent.h
pcfs_lookup.o: /usr/include/sys/sysmacros.h /usr/include/sys/vfs.h
pcfs_lookup.o: /usr/include/sys/buf.h /usr/include/sys/cmn_err.h
pcfs_lookup.o: /usr/include/sys/conf.h /usr/include/sys/ddi.h
pcfs_lookup.o: /usr/include/sys/uio.h /usr/include/sys/debug.h
pcfs_lookup.o: /usr/include/sys/disp.h /usr/include/sys/priocntl.h
pcfs_lookup.o: /usr/include/sys/dnlc.h /usr/include/sys/errno.h
pcfs_lookup.o: /usr/include/sys/fbuf.h /usr/include/sys/file.h
pcfs_lookup.o: /usr/include/sys/flock.h /usr/include/sys/inode.h
pcfs_lookup.o: /usr/include/sys/kmem.h /usr/include/sys/mkdev.h
pcfs_lookup.o: /usr/include/sys/mman.h /usr/include/sys/mount.h
pcfs_lookup.o: /usr/include/sys/open.h /usr/include/sys/pathname.h
pcfs_lookup.o: /usr/include/sys/swap.h /usr/include/sys/sysinfo.h
pcfs_lookup.o: /usr/include/sys/systm.h /usr/include/fs/fs_subr.h bootsect.h
pcfs_lookup.o: bpb.h direntry.h denode.h fat.h pcfs_filsys.h pcfs_lbuf.h
pcfs_vfsops.o: pcfs.h /usr/include/sys/types.h /usr/include/sys/param.h
pcfs_vfsops.o: /usr/include/sys/fs/s5param.h /usr/include/sys/fcntl.h
pcfs_vfsops.o: /usr/include/sys/proc.h /usr/include/sys/immu.h
pcfs_vfsops.o: /usr/include/sys/user.h /usr/include/sys/signal.h
pcfs_vfsops.o: /usr/include/sys/siginfo.h /usr/include/sys/tss.h
pcfs_vfsops.o: /usr/include/sys/seg.h /usr/include/sys/resource.h
pcfs_vfsops.o: /usr/include/sys/exec.h /usr/include/sys/vnode.h
pcfs_vfsops.o: /usr/include/sys/time.h /usr/include/sys/cred.h
pcfs_vfsops.o: /usr/include/vm/faultcatch.h /usr/include/sys/stat.h
pcfs_vfsops.o: /usr/include/sys/statvfs.h /usr/include/sys/dirent.h
pcfs_vfsops.o: /usr/include/sys/sysmacros.h /usr/include/sys/vfs.h
pcfs_vfsops.o: /usr/include/sys/buf.h /usr/include/sys/cmn_err.h
pcfs_vfsops.o: /usr/include/sys/conf.h /usr/include/sys/ddi.h
pcfs_vfsops.o: /usr/include/sys/uio.h /usr/include/sys/debug.h
pcfs_vfsops.o: /usr/include/sys/disp.h /usr/include/sys/priocntl.h
pcfs_vfsops.o: /usr/include/sys/dnlc.h /usr/include/sys/errno.h
pcfs_vfsops.o: /usr/include/sys/fbuf.h /usr/include/sys/file.h
pcfs_vfsops.o: /usr/include/sys/flock.h /usr/include/sys/inode.h
pcfs_vfsops.o: /usr/include/sys/kmem.h /usr/include/sys/mkdev.h
pcfs_vfsops.o: /usr/include/sys/mman.h /usr/include/sys/mount.h
pcfs_vfsops.o: /usr/include/sys/open.h /usr/include/sys/pathname.h
pcfs_vfsops.o: /usr/include/sys/swap.h /usr/include/sys/sysinfo.h
pcfs_vfsops.o: /usr/include/sys/systm.h /usr/include/fs/fs_subr.h bootsect.h
pcfs_vfsops.o: bpb.h direntry.h denode.h fat.h pcfs_filsys.h pcfs_lbuf.h
pcfs_vnops.o: pcfs.h /usr/include/sys/types.h /usr/include/sys/param.h
pcfs_vnops.o: /usr/include/sys/fs/s5param.h /usr/include/sys/fcntl.h
pcfs_vnops.o: /usr/include/sys/proc.h /usr/include/sys/immu.h
pcfs_vnops.o: /usr/include/sys/user.h /usr/include/sys/signal.h
pcfs_vnops.o: /usr/include/sys/siginfo.h /usr/include/sys/tss.h
pcfs_vnops.o: /usr/include/sys/seg.h /usr/include/sys/resource.h
pcfs_vnops.o: /usr/include/sys/exec.h /usr/include/sys/vnode.h
pcfs_vnops.o: /usr/include/sys/time.h /usr/include/sys/cred.h
pcfs_vnops.o: /usr/include/vm/faultcatch.h /usr/include/sys/stat.h
pcfs_vnops.o: /usr/include/sys/statvfs.h /usr/include/sys/dirent.h
pcfs_vnops.o: /usr/include/sys/sysmacros.h /usr/include/sys/vfs.h
pcfs_vnops.o: /usr/include/sys/buf.h /usr/include/sys/cmn_err.h
pcfs_vnops.o: /usr/include/sys/conf.h /usr/include/sys/ddi.h
pcfs_vnops.o: /usr/include/sys/uio.h /usr/include/sys/debug.h
pcfs_vnops.o: /usr/include/sys/disp.h /usr/include/sys/priocntl.h
pcfs_vnops.o: /usr/include/sys/dnlc.h /usr/include/sys/errno.h
pcfs_vnops.o: /usr/include/sys/fbuf.h /usr/include/sys/file.h
pcfs_vnops.o: /usr/include/sys/flock.h /usr/include/sys/inode.h
pcfs_vnops.o: /usr/include/sys/kmem.h /usr/include/sys/mkdev.h
pcfs_vnops.o: /usr/include/sys/mman.h /usr/include/sys/mount.h
pcfs_vnops.o: /usr/include/sys/open.h /usr/include/sys/pathname.h
pcfs_vnops.o: /usr/include/sys/swap.h /usr/include/sys/sysinfo.h
pcfs_vnops.o: /usr/include/sys/systm.h /usr/include/fs/fs_subr.h bootsect.h
pcfs_vnops.o: bpb.h direntry.h denode.h fat.h pcfs_filsys.h pcfs_lbuf.h
