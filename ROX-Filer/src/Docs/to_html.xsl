<?xml version="1.0"?>

<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                version='1.0'>

		<xsl:import href="/usr/share/sgml/docbook/stylesheet/xsl/nwalsh/html/docbook.xsl"/>

  <xsl:param name="generate.component.toc">0</xsl:param>

  <!-- Try to stop Netscape mucking things up using media.
  <xsl:param name="html.stylesheet">../style.css</xsl:param>
  -->
  <xsl:template name="head.content">
    <link rel="stylesheet" href="../style.css" type="text/css" media="all"/>
    <title><xsl:value-of select='/book/bookinfo/title'/></title>
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

  <xsl:template match="function">
    <span class="function"><xsl:apply-templates/></span>
  </xsl:template>

  <xsl:template match="parameter">
    <span class="parameter"><xsl:apply-templates/></span>
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

  <xsl:template match="othercredit" mode="titlepage.mode">
    <h3 class="{name(.)}"><xsl:call-template name="person.name"/></h3>
    <xsl:apply-templates mode="titlepage.mode" select="./contrib"/>
    <xsl:apply-templates mode="titlepage.mode" select="./affiliation"/>
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

  <xsl:template match="citation">
    <xsl:text>[</xsl:text>
    <xsl:variable name="cited"><xsl:value-of select="."/></xsl:variable>
    <a href="#{generate-id(/book/bibliography/bibliomixed[string(abbrev/.) = $cited])}">
      <xsl:call-template name="inline.charseq"/>
    </a>
    <xsl:text>]</xsl:text>
  </xsl:template>

</xsl:stylesheet>
