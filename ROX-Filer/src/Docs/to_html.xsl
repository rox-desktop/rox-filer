<?xml version="1.0"?>

<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                version='1.0'>

  <xsl:import href="/usr/share/sgml/docbook/xsl-stylesheets-1.29/html/docbook.xsl"/>

  <xsl:param name="html.stylesheet">reference.css</xsl:param>

  <xsl:template name="head.content">
    <STYLE type="text/css"><![CDATA[

body {
  background-color: #dddddd;
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
  background: #ccc;
  white-space: nowrap;
}

span.guibutton {
  background: #ddd;
  padding: 1px;
  border-top: 1px solid #eee;
  border-left: 1px solid #eee;
  border-right: 1px solid #444;
  border-bottom: 1px solid #444;
  white-space: nowrap;
}

span.guilabel {
  color: #55e;
}

div.chapter {
  margin-top: 3em;
  background-color: white;
  margin: 1em;
  padding: 0em 2em 1em 2em;
  max-width: 40em;
  border-colour: red;
  border-width: 2px;
  border-style: solid;
}

tt.filename {
  color: #c00;
}

span.keycap {
  background-color: #ddd;
  border: 1px solid #888;
  white-space: nowrap;
}

    ]]></STYLE>
  </xsl:template>

  <xsl:template match="guimenuitem">
    <span class="guimenuitem">
      <xsl:call-template name="inline.charseq"/>
    </span>
  </xsl:template>

  <xsl:template match="guibutton">
    <span class="guibutton">
      <xsl:call-template name="inline.charseq"/>
    </span>
  </xsl:template>

  <xsl:template match="guilabel">
    `<span class="guilabel"><xsl:call-template name="inline.charseq"/></span>'
  </xsl:template>

  <xsl:template match="filename">
    `<tt class="filename"><xsl:apply-templates/></tt>'
  </xsl:template>

  <xsl:template match="keycap">
    <span class="keycap"><xsl:apply-templates/></span>
  </xsl:template>

  <xsl:template match="book">
    <xsl:variable name="id">
      <xsl:call-template name="object.id"/>
    </xsl:variable>
    <div class="{name(.)}" id="{$id}">
      <div class="chapter">
        <xsl:call-template name="book.titlepage"/>
        <xsl:apply-templates select="dedication" mode="dedication"/>
        <xsl:if test="$generate.book.toc != '0'">
          <xsl:call-template name="division.toc"/>
        </xsl:if>
      </div>
      <xsl:apply-templates/>
    </div>
  </xsl:template>
  
</xsl:stylesheet>
