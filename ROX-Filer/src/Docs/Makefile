all: Manual/index.html

%.eps: %.dia
	dia -e $@ $<

Manual/index.html: Manual.tex Prune.eps Structs.eps
	latex Manual.tex
	latex2html -split +1 -antialias -local_icon -show_section_numbers \
	-up_url='http://rox.sourceforge.net' Manual.tex -up_title='ROX website'

%.tex: %.lyx
	echo Manual.tex not up-to-date...
	echo Use lyx to create $@ and try again.
	exit 1

clean:
	rm -f *.eps
	rm -f Manual.tex
	rm -rf Manual
