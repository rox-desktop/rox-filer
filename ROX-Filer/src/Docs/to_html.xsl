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
  border-top: 1px solid #888;
  border-right: none;
  border-bottom: none;
  border-left: 1px solid #888;
  border-spacing: 0;
}

td {
  border-top: none;
  border-right: 1px solid #888;
  border-bottom: 1px solid #888;
  border-left: none;
}

    ]]></STYLE>
  </xsl:template>

</xsl:stylesheet>
