HELP=../../Help
RES=../..

all: Manual.valid ${HELP}/Manual.html ${RES}/Structs.png ${RES}/Prune.png

${RES}/%.png: %.dia
	dia --nosplash -e $@ $<

%.valid: %.xml
	nsgmls -s -wxml /usr/lib/sgml/declaration/xml.decl $<

${HELP}/%.html: %.xml to_html.xsl
	xsltproc -o $@ to_html.xsl $<
