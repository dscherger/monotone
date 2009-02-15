<?xml version="1.0" encoding="UTF-8"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="2.0">
	<xsl:output method="text"/>
	<!-- Global variables for customization -->
	<xsl:variable name="mdwn_path_prefix" select="'wikifiles'" />
	<xsl:variable name="mdwn_path_suffix" select="'.mdwn'" />
	<xsl:variable name="mdwn_path_separator" select="'/'" />
	<xsl:variable name="mdwn_index_filename">
		<xsl:call-template name="filter-filename">
			<xsl:with-param name="file_name" select="/texinfo/titlepage/booktitle/text()" />
		</xsl:call-template>
	</xsl:variable>
	<!-- Root Template -->
	<xsl:template match="/texinfo">
		<!-- Select each "node" element -->
		<xsl:for-each select="./node">
			<!-- Construct target .mdwn filename -->
			<xsl:variable name="current_filename">
				<xsl:call-template name="create-node-path">
					<xsl:with-param name="node_name" select="nodename" />
				</xsl:call-template>
			</xsl:variable>
			<!-- Status message -->
			<xsl:message terminate="no">
				<xsl:text>Processing ... </xsl:text>
				<xsl:value-of select="$current_filename" />
			</xsl:message>
			<!-- Create target file -->
			<xsl:result-document href="{$current_filename}">
				<html><body>
					<xsl:value-of select="./section"/>
				</body></html>
			</xsl:result-document>
		</xsl:for-each>
	</xsl:template>  <!-- End of Root Template -->

	<!--
		Template: create-node-path

		Takes a node name and creates the complete corresponding filename.
	-->
	<xsl:template name="create-node-path">
		<!-- template parameters -->
		<xsl:param name="node_name" />
		<!-- parent node name -->
		<xsl:variable name="parent_node_name">
			<xsl:call-template name="node-parent-name">
				<xsl:with-param name="node_name" select="$node_name" />
			</xsl:call-template>
		</xsl:variable>
		<xsl:choose>
			<!-- if called from toplevel node, output index document filename -->
			<xsl:when test="$parent_node_name='(dir)'">
				<xsl:value-of select="$mdwn_path_prefix" />
				<xsl:value-of select="$mdwn_path_separator" />
				<xsl:value-of select="$mdwn_index_filename" />
			</xsl:when>
			<!-- not at top level, do recursive lookup of complete path -->
			<xsl:otherwise>
				<xsl:call-template name="recursive-create-node-path">
					<xsl:with-param name="node_name" select="$node_name" />
				</xsl:call-template>
			</xsl:otherwise>
		</xsl:choose>
		<!-- Finally output filename suffix -->
		<xsl:value-of select="$mdwn_path_suffix" />
	</xsl:template>  <!-- End of create-node-path -->

	<!--
		Template: recursive-create-node-path

		Helper template for create-node-path, builds up the file path by
		recursively looking up the node's parent names.
	-->
	<xsl:template name="recursive-create-node-path">
		<!-- template parameters -->
		<xsl:param name="node_name" />
		<!-- parent node name -->
		<xsl:variable name="parent_node_name">
			<xsl:call-template name="node-parent-name">
				<xsl:with-param name="node_name" select="$node_name" />
			</xsl:call-template>
		</xsl:variable>
		<!-- test if we need to go one level up first -->
		<xsl:choose>
			<xsl:when test="$parent_node_name='(dir)'">  <!-- reached top level, return wiki path prefix -->
				<xsl:value-of select="$mdwn_path_prefix" />
			</xsl:when>
			<xsl:otherwise>  <!-- not at top level yet, recursively go up -->
				<xsl:call-template name="recursive-create-node-path">
					<xsl:with-param name="node_name" select="$parent_node_name" />
				</xsl:call-template>
				<!-- after returning: output path separator and current node name -->
				<xsl:value-of select="$mdwn_path_separator" />
				<xsl:call-template name="filter-filename">
					<xsl:with-param name="file_name" select="$node_name" />
				</xsl:call-template>
			</xsl:otherwise>
		</xsl:choose>
	</xsl:template>  <!-- End of recursive-create-node-path -->

	<!--
		Template: filter-filename

		Takes a string and converts it to a file system compliant name by
		replacing unallowed characters and removing whitespace.
	-->
	<xsl:template name="filter-filename">
		<xsl:param name="file_name" />
		<xsl:value-of select="replace(replace($file_name,'\s',''), '/', '_')" /> <!-- TODO: please add extra characters to the second regexp as they appear -->
	</xsl:template>  <!-- End of filter-filename -->

	<!--
		Template: node-parent-name

		Determines a node's parent's name.
	-->
	<xsl:template name="node-parent-name">
		<xsl:param name="node_name" />
		<xsl:value-of select="/texinfo/node/nodename[text()=$node_name]/../nodeup" />
	</xsl:template>  <!-- End of node-parent-name -->
</xsl:stylesheet>
