all: Manual.valid Manual.html

%.valid: %.xml
	nsgmls -s -wxml /usr/lib/sgml/declaration/xml.decl $<

%.html: %.xml to_html.xsl
	xsltproc -o $@ to_html.xsl $<
