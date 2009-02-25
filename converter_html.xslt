<?xml version="1.0" encoding="UTF-8"?>
<!--
	Monotone distributed version control system
	TechML to Wiki XSLT converter

	Converter for HTML output format
-->
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="2.0">
	<xsl:include href="mtn_texml_to_wiki.xslt"/>

	<xsl:output method="html"/>
	<xsl:variable name="conv_path_suffix">.html</xsl:variable>

	<xsl:template name="document-root">
		<html>
			<head>
				<title><xsl:value-of select="nodename"/></title>
			</head>
			<body>
				<xsl:apply-templates/>
			</body>
		</html>
	</xsl:template>

	<xsl:template match="section">
		<xsl:apply-templates/>
	</xsl:template>

	<xsl:template match="title">
		<h1><xsl:value-of select="."/></h1>
	</xsl:template>

	<xsl:template match="para">
		<p><xsl:value-of select="."/></p>
	</xsl:template>

</xsl:stylesheet>