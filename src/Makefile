
CFLAGS=-Wall -Wmissing-prototypes  -Wno-sign-compare -g -DXDG_PREFIX=xdg_test -DHAVE_MMAP

all: test-mime test-mime-data

test-mime: xdgmime.o xdgmimeglob.o xdgmimeint.o xdgmimemagic.o xdgmimealias.o xdgmimeparent.o xdgmimecache.o xdgmimeicon.o

test-mime-data: xdgmime.o xdgmimeglob.o xdgmimeint.o xdgmimemagic.o xdgmimealias.o xdgmimeparent.o xdgmimecache.o xdgmimeicon.o

clean:
	rm -f *~ *.o test-mime test-mime-data

