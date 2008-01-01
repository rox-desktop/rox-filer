# Note: this Makefile is NOT built from Makefile.in. The generated Makefile
# goes in the build directory. We just call that from here.
# Use AppRun --compile to create the build directory in the first place.

BUILDDIR = ../build

all:
	0launch --source --main=/ROX-Filer/build --wrapper='make -C' ../ROX-Filer-src.xml
	#cd ${BUILDDIR} && ${MAKE}

clean:
	cd ${BUILDDIR} && ${MAKE} clean
