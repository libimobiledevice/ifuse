AUTOMAKE_OPTIONS = foreign
SUBDIRS = src

DISTCHECK_CONFIGURE_FLAGS = --disable-root-sbin

indent:
	indent -kr -ut -ts4 -l120 src/*.c

release:
	git archive --format=tar --prefix=ifuse-${RELEASE}/ tags/${RELEASE} | bzip2 > ifuse-${RELEASE}.tar.bz2

