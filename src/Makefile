
CFLAGS=-Wall -Wmissing-prototypes  -Wno-sign-compare -g -DXDG_PREFIX=xdg_test

all: test-mime

test-mime: xdgmime.o xdgmimeglob.o xdgmimeint.o xdgmimemagic.o

clean:
	rm -f *~ *.o test-mime

