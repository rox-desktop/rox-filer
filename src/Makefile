
CFLAGS=-Wall -Wmissing-prototypes  -Wno-sign-compare -g -DXDG_PREFIX=xdg_test

all: test-mime

test-mime: xdgmime.o xdgmimeglob.o xdgmimeint.o xdgmimemagic.o xdgmimealias.o xdgmimeparent.o xdgmimecache.o

clean:
	rm -f *~ *.o test-mime

