all: Manual.valid Manual.html Structs.png Prune.png

%.png: %.dia
	dia --nosplash -e $@ $<

%.valid: %.xml
	nsgmls -s -wxml /usr/lib/sgml/declaration/xml.decl $<

%.html: %.xml to_html.xsl
	xsltproc -o $@ to_html.xsl $<
