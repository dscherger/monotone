<?xml version="1.0" encoding="UTF-8"?>
<!--
	Monotone distributed version control system
	TechML to Wiki XSLT converter

	Converter for Ikiwiki MDWN output format
-->
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="2.0">
	<xsl:include href="mtn_texml_to_wiki.xslt"/>

	<xsl:output method="text" media-type="text/plain"/>
	<xsl:variable name="conv_path_suffix">.mdwn</xsl:variable>

	<xsl:template name="document-root">
		<xsl:text>[[tag original-documentation]]&#10;&#10;</xsl:text>
		<xsl:apply-templates/>
	</xsl:template>

	<xsl:template match="section">
		<xsl:apply-templates/>
	</xsl:template>

	<xsl:template match="title">
		<xsl:text># </xsl:text>
		<xsl:value-of select="."/>
		<xsl:text>&#10;&#10;</xsl:text>
	</xsl:template>

	<xsl:template match="para">
		<xsl:apply-templates/>
		<xsl:text>&#10;&#10;</xsl:text>
	</xsl:template>

	<xsl:template match="file">[[[<xsl:value-of select="."/>]]]</xsl:template>  <!-- no whitespace! -->

</xsl:stylesheet>