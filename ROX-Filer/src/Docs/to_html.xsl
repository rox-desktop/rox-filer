<?xml version="1.0"?>

<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                version='1.0'>

  <xsl:import href="/usr/share/sgml/docbook/xsl-stylesheets-1.29/html/docbook.xsl"/>

  <xsl:param name="html.stylesheet">../style.css</xsl:param>

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

  <xsl:template match="emphasis">
    <xsl:choose>
      <xsl:when test="@role='underline'">
        <u><xsl:apply-templates/></u>
      </xsl:when>
      <xsl:otherwise>
        <xsl:apply-imports/>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>
  
</xsl:stylesheet>
