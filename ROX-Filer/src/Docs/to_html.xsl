<?xml version="1.0"?>

<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                version='1.0'>

  <xsl:import href="/usr/share/sgml/docbook/xsl-stylesheets-1.29/html/docbook.xsl"/>

  <xsl:param name="html.stylesheet">reference.css</xsl:param>

  <xsl:template name="head.content">
    <STYLE type="text/css"><![CDATA[

html {
  background-color: #dddddd;
}

body {
  background-color: white;
  margin: 1em;
  padding: 0em 2em 1em 2em;
  max-width: 40em;
  border-colour: red;
  border-width: 2px;
  border-style: solid;
}

table.simplelist {
  border: none;
}

table.simplelist tr td {
  border: none;
}

table {
  border-collapse: collapse;
  border-top: 1px solid #ccc;
  border-right: none;
  border-bottom: none;
  border-left: 1px solid #ccc;
  border-spacing: 0;
}

th {
  background-color: #eee;
  border-top: none;
  border-right: 1px solid #ccc;
  border-bottom: 1px solid #ccc;
  border-left: none;
}

td {
  padding: 2px;
  border-top: none;
  border-right: 1px solid #ccc;
  border-bottom: 1px solid #ccc;
  border-left: none;
}

pre.programlisting {
  padding: 1em;
  background-color: #eee;
}

pre.screen {
  padding: 1em;
  background-color: #eee;
}

span.guimenuitem {
  border: 1px solid #888;
  padding: 1px;
}

div.chapter {
  padding-top: 3em;
}

tt.filename {
  color: #c00;
}

span.keycap {
  background-color: #ddd;
  border: 1px solid #888;
  padding: 1px;
}

    ]]></STYLE>
  </xsl:template>

  <xsl:template match="guimenuitem">
    <span class="guimenuitem">
      <xsl:call-template name="inline.charseq"/>
    </span>
  </xsl:template>

  <xsl:template match="filename">
    '<tt class="filename"><xsl:apply-templates/></tt>'
  </xsl:template>

  <xsl:template match="keycap">
    <span class="keycap"><xsl:apply-templates/></span>
  </xsl:template>
  
</xsl:stylesheet>
