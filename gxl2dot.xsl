<!-- 
  Experimental XSLT file for transforming GXL to Dot 
  $Id: gxl2dot.xsl,v 1.1.1.1 2002/03/07 19:44:31 michal Exp $
  
  Author: Michal Young, michal@cs.uoregon.edu
  First version November 2000

  Copyright (c) 2000  University of Oregon and Michal Young.  

  This software was developed for educational purposes 
  and is provided AS IS in the hope that others may 
  find it useful but without any warrantee whatsoever,
  express or implied.  

  Permission is granted to use, copy, and modify this program 
  provided this entire notice is preserved and notice of any modifications
  is also given, and provided you agree that this is a demonstration 
  only, with no explicit or implied warrantee of any kind. 

  Change log is at end of file.     
-->

<xsl:stylesheet version="1.0" 
                xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
>

<xsl:output method="text"/>
<xsl:strip-space elements="*"/>


<!-- ********************************************************** 
 * Parameters: 
 *  nodelabel: (default "type")
 *             gxl node attribute to translate to 
 *             dot "label" attribute on node
 *  edgelabel: (default "rel")
 *             gxl edge attribute to translate to
 *             dot "label" attribute on edge
 *
 *  debug:     (default 0, false)
 *             pass debug=true for verbose progress messages
 *
 * Example usage (for XT):
 *  java com.jclark.xsl.sax.Driver foo.xml foo.xsl edgelabel=Line
 *    
 **************************************************************** -->

<xsl:param name="nodelabel" select="'type'"/>
<xsl:param name="edgelabel" select="'rel'"/>
<xsl:param name="debug" select="0"/>

<!-- ********************************************************** -->
<!-- *              Top-level template for document           * -->
<!-- ********************************************************** -->

<xsl:template match="/">
  <xsl:text>
    /* This is a machine-generated file.              */
    /* It was created from a gxl source file by       */
    /* gxl2dot, an xslt style sheet.                  */
    /* ($Id: gxl2dot.xsl,v 1.1.1.1 2002/03/07 19:44:31 michal Exp $) */
    /*                                                */
    /* Editing this file by hand is a really bad idea.*/
    /* Edit the source document or the transformation */
    /* script instead.                                */
  </xsl:text>
  <xsl:text>&#xA;</xsl:text>
  <xsl:apply-templates/>
</xsl:template>

<xsl:template match="gxl">
  <xsl:text>digraph g {&#xA;</xsl:text>
  <xsl:apply-templates/>
  <xsl:text>&#xA;} /* End of generated dot file */&#xA;</xsl:text>
</xsl:template>

<!-- ********************************************************** -->
<!-- Graph Nodes -->
<!-- ********************************************************** -->

<xsl:template match="node">
  <xsl:if test="$debug">
    <xsl:message>Node</xsl:message> 
  </xsl:if>
  <xsl:text>  "</xsl:text>
  <xsl:value-of select="@id"/>
  <xsl:text>" </xsl:text>
  <xsl:text>[</xsl:text>
     <xsl:apply-templates select="@*"/> <!-- Attributes of this node -->
     <xsl:apply-templates/>             <!-- "attr" nodes -->
  <xsl:text>];&#xA;</xsl:text>
</xsl:template>

<!-- ********************************************************** -->
<!-- EDGES (directed edges only, no hyper-edges or undirected)  -->
<!-- ********************************************************** -->

<xsl:template match="edge[@from]">
  <xsl:if test="$debug">
     <xsl:message>Processing edge[@from]</xsl:message>
  </xsl:if>
  <xsl:text>  "</xsl:text>
  <xsl:value-of select="@from"/>
  <xsl:text>"</xsl:text>
  <xsl:text> -&gt; </xsl:text>
  <xsl:text>"</xsl:text>
  <xsl:value-of select="@to"/>
  <xsl:text>"</xsl:text>
  <xsl:text>[</xsl:text>
    <xsl:apply-templates select="@*"/>
    <xsl:apply-templates/>
  <xsl:text>];&#xA;</xsl:text>
</xsl:template>

<xsl:template match="edge[@begin]">
  <xsl:if test="$debug">
     <xsl:message>Old-fashioned edge with "begin="</xsl:message>
  </xsl:if>
  <xsl:text>   "</xsl:text>
  <xsl:value-of select="@begin"/>
  <xsl:text>"</xsl:text>
  <xsl:text> -&gt; </xsl:text>
  <xsl:text>"</xsl:text>
  <xsl:value-of select="@end"/>
  <xsl:text>"</xsl:text>
  <xsl:text>[</xsl:text>
    <xsl:apply-templates select="@*"/>
    <xsl:apply-templates/>
  <xsl:text>];&#xA;</xsl:text>
</xsl:template>

<!-- ********************************************************** -->
<!-- ATTR gxl attributes, old and new styles -->
<!-- ********************************************************** -->

<!-- Special cases for names that match nodelabel and edgelabel -->

<xsl:template match="node/attr[@name=$nodelabel]"> 
  <xsl:if test="$debug">
     <xsl:message>Parameterized node label</xsl:message> 
  </xsl:if>
  <xsl:text>label = "</xsl:text>
  <xsl:choose> <!-- Old or new style attribute? -->
     <xsl:when test="@value"> <!-- Old style -->
        <xsl:value-of select="@value"/>
     </xsl:when>
     <xsl:otherwise>          <!-- New style -->
        <xsl:value-of select="*"/>
     </xsl:otherwise>
  </xsl:choose>
  <xsl:text>"</xsl:text>
</xsl:template>

<xsl:template match="edge/attr[@name=$edgelabel]"> 
  <xsl:if test="$debug">
     <xsl:message>Parameterized edge label</xsl:message> 
  </xsl:if>
  <xsl:text>label = "</xsl:text>
  <xsl:choose> <!-- Old or new style attribute? -->
     <xsl:when test="@value"> <!-- Old style -->
        <xsl:value-of select="@value"/>
     </xsl:when>
     <xsl:otherwise>          <!-- New style -->
        <xsl:value-of select="*"/>
     </xsl:otherwise>
  </xsl:choose>
  <xsl:text>"</xsl:text>
</xsl:template>

<!-- Then general case if name is used "as is"  
     and not translated into a label 
  -->

<xsl:template match="attr[@value]"> 
  <xsl:if test="$debug">
     <xsl:message>Old style attribute with "value="</xsl:message>
  </xsl:if>
  <xsl:text> </xsl:text>
  <xsl:value-of select="@name"/>
  <xsl:text>="</xsl:text>
  <xsl:value-of select="@value"/>
  <xsl:text>"</xsl:text>
</xsl:template>

<xsl:template match="attr"> 
  <xsl:if test="$debug">
      <xsl:message>New style attribute with content</xsl:message>
  </xsl:if>
  <xsl:text> </xsl:text>
  <xsl:value-of select="@name"/>
  <xsl:text>="</xsl:text>
  <xsl:value-of select="*"/>
  <xsl:text>"</xsl:text>
</xsl:template>

<!-- ********************************************************** -->
<!--  ATTRIBUTES (in XML sense, not attr elements)              -->
<!-- ********************************************************** -->

<!-- 
  The following is invoked only when we explicitly force matching
  of attributes using a "select" clause in an "xsl:apply-templates"
  verb. A default "apply-templates" verb only looks
  at elements and not at attributes. 
-->

<xsl:template match="@*">
  <xsl:if test="$debug">
      <xsl:message>Attribute (@*)</xsl:message>
  </xsl:if>
  <xsl:value-of select="name()"/>
  <xsl:text>="</xsl:text>
  <xsl:value-of select="."/> 
  <xsl:text>" </xsl:text>
</xsl:template>

<!-- ***   Edge attributes: 
     ***   Replace attribute by "label" for the attribute
     ***   selected in edgelabel parameter.
     ***
 -->

<xsl:template match="edge/@*"> 
  <xsl:if test="$debug">
      <xsl:message>Edge attribute (edge/@*)</xsl:message>
  </xsl:if>
  <xsl:choose>
  <xsl:when  test="name()=$edgelabel">
      <xsl:text>label</xsl:text>
  </xsl:when>
  <xsl:otherwise>
     <xsl:value-of select="name()"/>
  </xsl:otherwise>
  </xsl:choose>
  <xsl:text>="</xsl:text>
  <xsl:value-of select="."/> 
  <xsl:text>" </xsl:text>
</xsl:template>

<!-- ***   Node attributes: 
     ***   Replace attribute by "label" for the attribute
     ***   selected in nodelabel parameter.
     ***
 -->

<xsl:template match="node/@*"> 
  <xsl:if test="$debug">
     <xsl:message>Node attribute (node/@*)</xsl:message>
  </xsl:if>
  <xsl:choose>
  <xsl:when  test="name()=$nodelabel">
      <xsl:text>label</xsl:text>
  </xsl:when>
  <xsl:otherwise>
     <xsl:value-of select="name()"/>
  </xsl:otherwise>
  </xsl:choose>
  <xsl:text>="</xsl:text>
  <xsl:value-of select="."/> 
  <xsl:text>" </xsl:text>
</xsl:template>


</xsl:stylesheet>

                  