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

PNG_FIGURES=$(PDF_FIGURES:.pdf=.png)
EPS_FIGURES=$(PDF_FIGURES:.pdf=.eps)

# See the rules further down that does the work.
PDF_FIGURES_copied = $(addprefix $(abs_builddir)/,$(PDF_FIGURES))
PNG_FIGURES_copied = $(addprefix $(abs_builddir)/,$(PNG_FIGURES))
PNG_IMAGES_copied = $(addprefix $(abs_builddir)/,$(PNG_IMAGES))
EPS_FIGURES_generated = $(addprefix $(abs_builddir)/,$(EPS_FIGURES))
HTML_EXTRA_copied = $(abs_builddir)/texinfo.css

TEXI_FRAGMENTS = monotonevars.texi pcrepattern.texi pcresyntax.texi

man1_MANS = mtn.1
info_TEXINFOS = monotone.texi

monotone_TEXINFOS = ../src/std_hooks.lua $(TEXI_FRAGMENTS) \
	$(EPS_FIGURES_generated) \
	$(PDF_FIGURES_copied) \
	$(PNG_FIGURES_copied) $(PNG_IMAGES_copied) \
	$(HTML_EXTRA_copied)

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
htmldir: monotone.texi $(srcdir)/version.texi $(doc_monotone_TEXINFOS) html/$(am__dirstamp)
	$(MKDIR_P) html.htp
	cp -r $(srcdir)/figures html.htp
	cp -r $(srcdir)/images html.htp
	cp $(srcdir)/texinfo.css html.htp
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
	@: > $(am__dirstamp)
# Then let's have the html target produce the multifile HTML as well.
html-local: htmldir

# automake doesn't seem to generate any specific PostScript target.
# It does generate the general rule to produce .ps from .dvi, though,
# so all we need to do is to set up the dependencies.
#dvi-local: $(EPS_FIGURES)
#monotone.ps: $(srcdir)/version.texi $(doc_monotone_TEXINFOS) $(EPS_FIGURES) $(am__dirstamp)

