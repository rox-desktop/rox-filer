HELP=../../Help
RES=../..
MAN=../../..

all: Manual.valid ${HELP}/Manual.html ${MAN}/rox.1 \
     Manual-fr.valid ${HELP}/Manual-fr.html

%.valid: %.xml
	nsgmls -s -wxml /usr/lib/sgml/declaration/xml.decl $<

${HELP}/%.html: %.xml to_html.xsl
	xsltproc -o $@ to_html.xsl $<

${MAN}/rox.1: Manual.xml to_man.xsl
	xsltproc -o $@ to_man.xsl $<
