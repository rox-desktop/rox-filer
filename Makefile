SUBDIRS=src

all-recursive clean-recursive:
	target=`echo $@ | sed s/-recursive//`;		\
	for subdir in $(SUBDIRS); do 			\
		(cd $$subdir && make "$$target")	\
	done;

all: all-recursive

clean: clean-recursive
	rm -f *~
