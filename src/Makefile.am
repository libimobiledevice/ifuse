AM_CFLAGS = $(libfuse_CFLAGS) $(libimobiledevice_CFLAGS) $(libplist_CFLAGS) -g
AM_LDFLAGS =  $(libfuse_LIBS) $(libimobiledevice_LIBS) $(libplist_LIBS)

bin_PROGRAMS = ifuse

ifuse_SOURCES = ifuse.c

ifuse_LDADD = $(AM_LDFLAGS)
