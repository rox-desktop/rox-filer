all: Manual.valid Manual.html

style=/usr/share/sgml/docbook/xsl-stylesheets-1.29/html/docbook.xsl

%.valid: %.xml
	nsgmls -s -wxml /usr/lib/sgml/declaration/xml.decl $<

%.html: %.xml
	xsltproc -o $@ ${style} $<
