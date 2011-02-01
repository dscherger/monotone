# Copyright (C) 2011 Richard Levitte <richard@levitte.org>
#
# This program is made available under the GNU GPL version 2.0 or
# greater. See the accompanying file COPYING for details.
#
# This program is distributed WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
# PURPOSE.

# Makefile for monotone

AUTOMAKE_OPTIONS = subdir-objects -Wno-portability
ACLOCAL_AMFLAGS  = -I m4

PDF_FIGURES= figures/branch-heads.pdf figures/cert.pdf			\
	figures/difference-between-versions.pdf				\
	figures/file-id-manifest-id.pdf figures/fork.pdf		\
	figures/general-workflow.pdf figures/linear-history.pdf		\
	figures/local-workflow.pdf figures/manifest.pdf			\
	figures/merge.pdf figures/network-workflow.pdf			\
	figures/parent-child-hashes.pdf					\
	figures/parent-child-names-hashes.pdf figures/parent-child.pdf	\
	figures/revision-chaining.pdf figures/revision.pdf		\
	figures/statement.pdf figures/three-versions.pdf		\
	figures/two-branches.pdf

PNG_IMAGES= images/bg.png images/go-next.png			\
	images/go-previous.png images/go-up.png			\
	images/toc.png

HTML_EXTRA = texinfo.css

PNG_FIGURES=$(PDF_FIGURES:.pdf=.png)
EPS_FIGURES=$(PDF_FIGURES:.pdf=.eps)

# See the rules further down that does the work.
PDF_FIGURES_copied = $(addprefix $(abs_builddir)/,$(PDF_FIGURES))
PNG_FIGURES_copied = $(addprefix $(abs_builddir)/,$(PNG_FIGURES))
PNG_IMAGES_copied = $(addprefix $(abs_builddir)/,$(PNG_IMAGES))
HTML_EXTRA_copied = $(addprefix $(abs_builddir)/,$(HTML_EXTRA))

TEXI_VARS = monotonevars.texi
TEXI_FRAGMENTS = pcrepattern.texi pcresyntax.texi

CLEANFILES = $(EPS_FIGURES) $(PDF_FIGURES_copied) $(PNG_FIGURES_copied) \
	$(PNG_IMAGES_copied) $(HTML_EXTRA_copied) $(TEXI_VARS)

info_TEXINFOS = monotone.texi

monotone_TEXINFOS = ../src/std_hooks.lua $(TEXI_VARS) $(TEXI_FRAGMENTS)

EXTRA_DIST = \
	$(PDF_FIGURES) $(PNG_FIGURES) $(TEXI_FRAGMENTS)			\
	figures/monotone-logo.svg figures/oo-figures.sxd	\
	figures/pdfcrop.pl figures/README

AM_MAKEINFOFLAGS= -I $(srcdir)

AM_MAKEINFOHTMLFLAGS= -I $(srcdir) \
	--number-sections --no-split --no-headers --css-ref=texinfo.css

monotonevars.texi: Makefile
	rm -f monotonevars.texi
	echo "@set top_srcdir $(top_srcdir)" >> monotonevars.texi
	echo "@set top_builddir $(top_builddir)" >> monotonevars.texi

## # Html and pdf dependencies are simply copied.
## # All other are generated with the rules further down.
## copy-html-deps: texinfo.css $(PNG_FIGURES) $(PNG_IMAGES)
## 	for f in texinfo.css $(PNG_FIGURES) $(PNG_IMAGES); do \
## 	    d=`dirname $$f`; \
## 	    [ -d $$d ] || $(MKDIR_P) $$d && \
## 		cmp -s $(srcdir)/$$f $(builddir)/$$f || \
## 		    cp -v $(srcdir)/$$f $(builddir)/$$f; \
## 	done

#%.eps: %.epsi
#	mv -f $< $@
#
#%.epsi: %.ps
#	ps2epsi $< $@

figures/%.eps: figures/%.ps
	$(MKDIR_P) figures
	ps2eps <$< >$@

figures/%.ps: figures/%.pdf
#	acroread -toPostScript -pairs $< $@
	$(MKDIR_P) figures
	pdftops $< $@

$(abs_builddir)/figures/%.pdf: figures/%.pdf
	$(MKDIR_P) figures
	cmp -s $< $@ || cp $< $@

$(abs_builddir)/figures/%.png: figures/%.png
	$(MKDIR_P) figures
	cmp -s $< $@ || cp $< $@

$(abs_builddir)/images/%.png: images/%.png
	$(MKDIR_P) images
	cmp -s $< $@ || cp $< $@

$(abs_builddir)/texinfo.css: texinfo.css
	cmp -s $< $@ || cp $< $@

# Stealing code from automake and adapting, we want a HTML file tree as well
AM_MAKEINFODOCHTMLFLAGS= -I $(srcdir) \
	--number-sections --css-ref=texinfo.css

# Because automake already generates a html target that produces a single file
# HTML, we have to use a different target to make a multifile HTML.
htmldir: monotone.texi $(srcdir)/version.texi $(doc_monotone_TEXINFOS) $(PNG_FIGURES) $(PNG_IMAGES) $(HTML_EXTRA) html/$(am__dirstamp)
	$(MKDIR_P) html.htp && \
	  cp $(addprefix $(srcdir)/,$(HTML_EXTRA)) html.htp
	$(MKDIR_P) html.htp/figures && \
	  cp $(addprefix $(srcdir)/,$(PNG_FIGURES)) html.htp/figures
	$(MKDIR_P) html.htp/images && \
	  cp $(addprefix $(srcdir)/,$(PNG_IMAGES)) html.htp/images
	if $(MAKEINFOHTML) $(AM_MAKEINFODOCHTMLFLAGS) $(MAKEINFOFLAGS) -I doc -I $(srcdir)/doc -o html.htp `test -f 'monotone.texi' || echo '$(srcdir)/'`monotone.texi; \
	then \
	  rm -rf html; \
	  mv html.htp html; \
	else \
	  rm -Rf html.htp; \
	  exit 1; \
	fi
html/$(am__dirstamp):
	@$(MKDIR_P) html
	@touch $@
# Then let's have the html target produce the multifile HTML as well.
html-local: htmldir

# Because we don't want to have the different document targets depend on
# irrelevant files, we have these dependencies lists.
# NOTE: This wouldn't work if this Makefile had been part of the top Makefile,
# because automake produces very different results when the main .texi is in
# a subdirectory.
# Also note that we need to specify everything that automake would specify,
# since what we write overrides what automake would otherwise produce.
monotone.dvi: monotone.texi $(srcdir)/version.texi $(monotone_TEXINFOS) \
	$(EPS_FIGURES)
monotone.pdf: monotone.texi $(srcdir)/version.texi $(monotone_TEXINFOS) \
	$(PDF_FIGURES_copied)
monotone.html: monotone.texi $(srcdir)/version.texi $(monotone_TEXINFOS) \
	$(PNG_FIGURES_copied) $(PNG_IMAGES_copied) $(HTML_EXTRA_copied)
# Note that we don't need anything for monotone.ps, as it's produced using
# the generic rule .dvi.ps, and the monotone.dvi already has all the
# dependencies.
# Also, we don't need anything extra for htmldir, as it does things a bit
# differently.
