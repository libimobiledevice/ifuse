INCLUDES = -I$(top_srcdir)/include

AM_CFLAGS = $(libglib2_CFLAGS) $(libfuse_CFLAGS) -g
AM_LDFLAGS =  $(libglib2_LIBS) $(libfuse_LIBS) -liphone

bin_PROGRAMS = mount.fuse.ifuse 

mount_fuse_ifuse_SOURCES = ifuse.c
mount_fuse_ifuse_LDADD = libiphone.la
