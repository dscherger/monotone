#! /usr/bin/python

import re
import string
import sys
import time
import rcsparse
import gtk
from types import StringType, TupleType
from gtk import FALSE, TRUE

# Utility.
def overlapping_rectangles(x0, y0, w0, h0, x1, y1, w1, h1):
    neww = min(x0 + w0, x1 + w1) - max(x0, x1)
    newh = min(y0 + h0, y1 + h1) - max(y0, y1)
    return (neww >= 0 and newh >= 0)

def pt_in_rect(x, y, w, h, px, py):
    return 0 <= px - x <= w and 0 <= py - y <= h

def split_and_trim(str):
    """Given STR, a string containing many lines of text, return an
    array containing all the lines with all trailing whitespace stripped."""
    if type(str) != StringType: raise TypeError, str
    result = map(string.rstrip, string.split(str, '\n'))
    if result[-1] == '':
        del result[-1]
    return result

class Revision:
    """This is a data-object class which describes a node in the
    revisions graph."""

    # Precompiled regular expressions.
    rcs_command   = re.compile(r'^([da])(\d+)\s(\d+)$')

    def __init__(self, revision):
        self.revstr   = revision  # RCS rev number, as a string
        self.parent   = None      # true parent (ignoring merge arrows)
        self.children = []        # array of child revisions
        self.tags     = {}        # dict of tags (later changed to array)
        self.strings  = None      # array of strings for body text
        self.text     = None      # extent vector describing body text

        # Derived properties:
        self.revno    = map(int, string.split(self.revstr, '.'))
        self.on_trunk = (len(self.revno) == 2)

    def set_position(self, x, y, pad):
        # upper left corner of box
        self.position  = (x, y)
        # position to draw the revision string
        self.labelpos  = (x + pad, y + self.labelsize[1] - pad)
        # start connections to children here
        self.arrowfrom = (x + self.labelsize[0], y + self.labelsize[1]/2)
        # end connections to parents here
        self.arrowto   = (x, y + self.labelsize[1]/2)

    def draw_on(self, drawable, style):
        font = style.font
        default_gc = style.fg_gc[gtk.STATE_NORMAL]
        current_gc = style.fg_gc[gtk.STATE_SELECTED]

        if self.current:
            gtk.draw_rectangle(drawable, current_gc, TRUE,
                               *(self.position + self.labelsize))

        if self.selected: default_gc.line_width = 2
        gtk.draw_rectangle(drawable, default_gc, FALSE,
                           *(self.position + self.labelsize))
        if self.selected: default_gc.line_width = 1

        gtk.draw_string(drawable, font, default_gc,
                        self.labelpos[0], self.labelpos[1],
                        self.revstr)

        for c in self.children:
            if self.selected and c.selected: default_gc.line_width = 2
            gtk.draw_line(drawable, default_gc, *(self.arrowfrom + c.arrowto))
            if self.selected and c.selected: default_gc.line_width = 1

    def make_visible_on(self, port):
        hadj = port.get_hadjustment()
        vadj = port.get_vadjustment()

        mybounds = self.bounds()
        portbounds = (hadj.value, vadj.value,
                      hadj.value + hadj.page_size, vadj.value + vadj.page_size)

        if mybounds[0] < portbounds[0]:
            hadj.set_value(mybounds[0] - 5)
        elif mybounds[2] > portbounds[2]:
            hadj.set_value(hadj.value + (mybounds[2] - portbounds[2]) + 5)

        if mybounds[1] < portbounds[1]:
            hadj.set_value(mybounds[0] - 5)
        elif mybounds[3] > portbounds[3]:
            hadj.set_value(hadj.value + (mybounds[3] - portbounds[3]) + 5)

    def branch_head(self):
        """Return the head (initial revision) of the branch containing SELF."""
        l = len(self.revno)
        while self.parent and len(self.parent.revno) == l:
            self = self.parent
        return self

    def overlaps(self, other):
        return overlapping_rectangles(*(self.position + self.labelsize
                                        + other.position + other.labelsize))

    def bounds(self):
        return self.position + (self.position[0] + self.labelsize[0],
                                self.position[1] + self.labelsize[1])

    def clicked_in(self, x, y):
        return pt_in_rect (*(self.position + self.labelsize + (x, y)))

    def label(self):
        return ("Revision %s  by %s on %s"
                % (self.revstr, self.author,
                   time.strftime('%Y-%m-%d %H:%M:%S',
                                 time.gmtime(self.date))))

    # Compute the text of this revision relative to PRIOR.
    def calculate_text(self, prior):
        cmds = split_and_trim(self.strings)
        debug = 0 #(self.revstr == '1.18')
        import sys

        # Initially, the operation list is a copy of PRIOR's.
        text = prior.text[:]
        if debug:
            print >> sys.stderr, text

        strings = []
        here = 0
        adjust = 0
        add_lines_remaining = 0
        for s in cmds:
            match = self.rcs_command.match(s)
            if match:
                start = int(match.group(2)) + adjust
                count = int(match.group(3))
                pos = 0
                if match.group(1) == 'd': start -= 1

                for i in xrange(len(text)):
                    if pos <= start < pos + text[i][2]:
                        break
                    pos += text[i][2]

                offset = start - pos
                if match.group(1) == 'a':
                    new_node = (self, here, count)
                    adjust += count
                    add_lines_remaining = count

                    if debug:
                        print >> sys.stderr, "insert", new_node, "at", text[i]

                    if offset == 0:
                        # Simply inject this entry here.
                        text[i:i] = [new_node]
                    else:
                        # Split this entry, but do not lose any lines.
                        before = (text[i][0],
                                  text[i][1],
                                  offset)
                        after = (text[i][0],
                                 text[i][1] + offset,
                                 text[i][2] - offset)
                        text[i:i+1] = [before, new_node, after]
                else:
                    adjust -= count

                    # Deletion, contrariwise, may span several nodes.
                    if debug:
                        print >> sys.stderr, "delete", count, \
                              "at", offset, "from", text[i]

                    remain = count + offset
                    j = i
                    while remain > 0:
                        remain -= text[j][2]
                        j += 1

                    new_nodes = []
                    remain = -remain

                    if debug:
                        print >> sys.stderr, "      ", gapsize, \
                              "at", offset, "from", text[i:j]

                    if offset > 0:
                        new_nodes.append((text[i][0], text[i][1], offset))
                    if remain > 0:
                        trail_offset = text[j-1][1] + text[j-1][2] - remain
                        new_nodes.append((text[j-1][0], trail_offset, remain))
                    text[i:j] = new_nodes

                if debug:
                    print >> sys.stderr, text
            elif add_lines_remaining > 0:
                strings.append(s)
                here += 1
                add_lines_remaining -= 1
            else:
                raise RuntimeError, "Too many additions [%s]" % s

        self.text = text
        self.strings = strings

    def calculate_diff_column(self, nlines):
        """Compute a column of indicators of where this version changed
        with respect to its parent.  Assumes gaps have been calculated."""

        if self.parent is None:
            self.diff_column = [' '] * nlines
            self.diff_distance = [0] * nlines
            return

        t = self.text
        p = self.parent.text
        d = []
        for i in xrange(len(t)):
            if p[i][0] is t[i][0] and p[i][1] == t[i][1]:
                char = ' '
            elif p[i][0] is None:
                char = '+'
            elif t[i][0] is None:
                char = '-'
            else:
                char = 'C'
            d.extend([char] * p[i][2])
        self.diff_column = d
        if len(d) != nlines:
            raise RuntimeError, "len(d)[%d] != nlines[%d]" % (len(d), nlines)

        # Now compute a column of distances: how long ago was each line
        # last changed?
        d = [None]*nlines
        for i in xrange(nlines):
            p = self
            n = 0
            while p:
                if p.diff_column[i] != ' ':
                    break
                p = p.parent
                n += 1
            d[i] = min(n, 9)
        self.diff_distance = d

    def collapse_your_column(self, ui):
        t = ui.revtable
        col = self.col
        diff_column = self.diff_column

        t.set_column_title(col, ' ')
        t.set_column_width(col, ui.colwidth_collapsed)
        for i in xrange(ui.model.nlines):
            t.set_text(i, col, diff_column[i])
    
    def expand_your_column(self, ui):
        t = ui.revtable
        col = self.col
        row = 0
        colors = ui.colors
        dist = self.diff_distance
        dcol = self.diff_column
        
        t.set_column_title(col, self.label())
        t.set_column_width(col, ui.colwidth)

        for op in self.text:
            if op[0] is None:
               for i in xrange(op[2]):
                   t.set_text(row, col, dcol[row])
                   t.set_background(row, colors[dist[row]])
                   row += 1
            else:
                s = op[0].strings
                base = op[1]
                if base + op[2] > len(s):
                    raise(IndexError,
  ("short string pool (length %d) for %s accessing (%d,%d) from rev %s" %
   (len(s), op[0], op[1], op[2], self)))
                for i in xrange(op[2]):
                    t.set_text(row, col, dcol[row] +' '+ s[base + i])
                    t.set_background(row, colors[dist[row]])
                    row += 1

    # Comparison primitives.
    # A revision number with fewer elements sorts before one with
    # more elements (so the trunk sorts before all branches);
    # numbers with the same number of elements are compared
    # element-by-element.  Equality can be optimized down to
    # string comparison.
    def __eq__(self, other): return self.revstr == other.revstr
    def __ne__(self, other): return self.revstr != other.revstr

    def __lt__(self, other):
        if len(self.revno) < len(other.revno): return 1
        if len(self.revno) > len(other.revno): return 0
        for i in xrange(len(self.revno)):
            if self.revno[i] < other.revno[i]: return 1
        return 0
    def __gt__(self, other):
        if len(self.revno) > len(other.revno): return 1
        if len(self.revno) < len(other.revno): return 0
        for i in xrange(len(self.revno)):
            if self.revno[i] > other.revno[i]: return 1
        return 0

    def __le__(self, other): return __eq__(self, other) or __lt__(self, other)
    def __ge__(self, other): return __eq__(self, other) or __gt__(self, other)

    def __hash__(self): return hash(self.revstr)

    # Stringifiers.
    def __repr__(self): return self.revstr
#    def __repr__(self):
#        if self.parent: pstr = self.parent.revstr
#        else: pstr = "None"
#        return "Revision(%s, %s, %s)" % \
#               (self.revstr, pstr,
#                `[c.revstr for c in self.children]`)
#    def __str__(self): return self.revstr


class Revtree:
    """Another data-object class, which holds data common to the
    complete version tree of this file."""

    def __init__(self, filename, description, revisions, head):
        self.revisions = revisions
        self.revisions.sort()
        self.root = self.revisions[0]
        self.head = head

        self.filename = filename
        self.description = description

        col = 0
        for r in self.revisions:
            r.tags = r.tags.keys()
            r.tags.sort()
            r.col = col
            col += 1

        self.calculate_texts()
        self.calculate_diff_columns()

    def clicked_revision(self, x, y):
        """The user clicked on (x, y); return which revision, if any,
        is displayed there."""

        for r in self.revisions:
            if r.clicked_in(x, y):
                return r
        return None

    def select_branch_of(self, rev):
        """Make REV the current revision, and its branch the selected
        branch."""

        # First deselect everything.
        for r in self.revisions: r.selected = r.current = 0

        # Select REV and all its parents.
        rev.current = 1
        p = rev
        while p:
            p.selected = 1
            p = p.parent
        # Select the primary child of this node, and its primary child,
        # etc. etc.
        c = rev
        while len(c.children) > 0:
            c = c.children[0]
            c.selected = 1

    def draw_tree(self, drawable, style):
        gtk.draw_rectangle(drawable, style.bg_gc[gtk.STATE_NORMAL],
                           TRUE, 0, 0, drawable.width, drawable.height)
        for r in self.revisions:
            r.draw_on(drawable, style)

    def place_tree(self, pane):
        """Calculate where the tree contained in SELF should be
        positioned on PANE, by depth-first tree walk, and set the
        size of the pane appropriately."""

        hmax = 0
        vmax = 0
        hspacing  = 20
        vspacing  = 10
        labelpad  = 5
        font      = pane.get_style().font

        revisions = self.revisions
        root = self.root

        if root.parent: raise RuntimeError, ('First rev, '
                                             + `root`
                                             + ' has parent')
        # Initial placement.

        root.labelsize = (font.width(root.revstr) + labelpad*2,
                          font.height(root.revstr) + labelpad*2)
        root.set_position(hspacing, vspacing, labelpad)

        queue = [root]
        while queue:
            p = queue.pop(0)
            p.selected = p.on_trunk  # part of the selected branch?
            p.current  = 0           # currently chosen revision

            b = p.bounds()
            if hmax < b[2]: hmax = b[2]
            if vmax < b[3]: vmax = b[3]

            hpos = b[2] + hspacing
            vpos = b[1]
            for k in p.children:
                k.labelsize = (font.width(k.revstr) + labelpad*2,
                               font.height(k.revstr) + labelpad*2)
                k.set_position(hpos, vpos, labelpad)

                vpos = vpos + k.labelsize[1] + vspacing
                queue.append(k)

        self.head.current = 1

        # At this point, branches may overlap in the tree.  If so, we
        # have to move them until they don't.  There cannot be
        # conflicts with the trunk, so skip all of those (which should
        # keep the size of N in this O(N^2) algorithm down to something
        # acceptable).
        max_rev = len(revisions)
        for first_branch_rev in xrange(max_rev):
            if not revisions[first_branch_rev].on_trunk:
                break

        conflicts = 1
        while conflicts:
            conflicts = 0
            for i in xrange(first_branch_rev, max_rev):
                for j in xrange(i+1, max_rev):
                    a = revisions[i]
                    b = revisions[j]
                    if a.overlaps(b):
                        conflicts = 1
                        # We want to move the older of a and b, since
                        # this reduces the probability of branch lines
                        # crossing.
                        ap = a.branch_head()
                        bp = b.branch_head()
                        if ap.parent > bp.parent: (ap, bp) = (bp, ap)
                        queue = [ap]
                        while queue:
                            q = queue.pop(0)
                            x = q.position[0]
                            y = q.position[1] + q.labelsize[1] + vspacing
                            q.set_position(x, y, labelpad)

                            v = q.position[1] + q.labelsize[1]
                            if vmax < v: vmax = v

                            queue.extend(q.children)

        pane.size(hmax + 10, vmax + 10)

    def calculate_texts(self):
        """For each revision, compute its full text, with the appropriate
        gaps needed to display all the revisions side by side."""

        # Each version is converted into a string pool and a sequence
        # of (REV, BASE, COUNT) triples.  REV may be None, in which
        # case BASE is always zero and COUNT is a number of blank lines
        # to insert.

        # The head revision is known to be cleartext.
        # Just convert its text into an array of lines.
        cur = self.head
        cur.strings = split_and_trim(cur.strings)
        cur.text = [(cur, 0, len(cur.strings))]

        queue = [cur]
        while queue:
            q = queue.pop(0)

            if q.on_trunk and q.parent:
                q.parent.calculate_text(q)
                queue.append(q.parent)
            for c in q.children:
                if c.text is None:
                    c.calculate_text(q)
                    queue.append(c)

#        for rev in self.revisions:
#            print rev.text
#        sys.exit(0)

        # At this point each rev.text is a column vector of extents.
        # We want to line these all up so that each extent gets a unique
        # vertical position.  It is okay for [(A x 5)] to occupy the
        # same space as [(A x 3) (B y 2)], but if that were (B y 3)
        # then [(A x 5)] would have to become [(A x 5) (/ / 1)].
        # Algorithm devised by Seth Schoen.
        level = 0
        vers = self.revisions
        below = _below
        minrun = _minrun
        sys.stderr.write("gaps: ")
        while level < max(map(lambda r: len(r.text), vers)):
            to_push = []

            for v in vers:
                if level>=len(v.text):
                    l = len(v.text)
                    v.text[l:level+1] = [None] * (level-l+1)
                symbol = v.text[level]
                if symbol is None: continue
                if below(vers, level, symbol) and v not in to_push:
                    to_push.append(v)

            for v in to_push:
                v.text[level:level] = [None]

            mr = reduce(minrun, [v.text[level] for v in vers])
            for v in vers:
                old = v.text[level]
                if old is None:
                    v.text[level] = (None, 0, mr)
                    continue
                if old[2] == mr: continue
                a = (old[0], old[1], mr)
                b = (old[0], old[1] + mr, old[2] - mr)
                v.text[level:level+1] = [a, b]
		
            level = level + 1
            sys.stderr.write('.')

        sys.stderr.write('\n')
        
        nlines = 0
        for op in vers[0].text: nlines += op[2]
        self.nlines = nlines


    def calculate_diff_columns(self):
        """For each revision, compute a vertical column of +, -, C
        indicators to show where changes occurred with respect to the
        parent."""
        sys.stderr.write('cols: ')
        for v in self.revisions:
            v.calculate_diff_column(self.nlines)
            sys.stderr.write('.')
        sys.stderr.write('\n')

def _below(vers, level, symbol):
    """ The element symbol appears at a greater depth than level. """
    for v in vers:
        if level+1 >= len(v.text): continue
        for s in v.text[level+1:]:
            if symbol[0] is s[0] and symbol[1] == s[1]:
                return 1
    return 0

def _minrun(x, y):
    if type(x) is TupleType: x = x[2]
    if type(y) is TupleType: y = y[2]
    if x is None: return y
    if y is None: return x
    return min(x,y)

class RevtreeBuilderRCS (rcsparse.Sink):
    """Factory class which constructs a Revtree structure from an RCS file."""

    def __init__(self):
        self.parser = rcsparse.Parser()

    def _numtorev(self, num):
        if not num: return None
        if not self.revisions.has_key(num):
            self.revisions[num] = Revision(num)
        return self.revisions[num]

    def read(self, file):
        """Read FILE and return its revision tree."""

        # Use a dictionary so that each revision structure gets created
        # only once.
        self.revisions = {}
        self.filename = file[:-2]
        self.description = ''
        self.head = None
        
        self.parser.parse(open(file), self)

        # For some weird reason RCS files with a lot of history tend to
        # have lots of tags pointing to nonexistent revisions (this may
        # have something to do with the kluge that is RCS branching).
        # These will have created bogus revision entries in the dict;
        # weed them out.
        for k in self.revisions.keys():
            if (self.revisions[k].parent is None and
                len(self.revisions[k].children) == 0):
                del self.revisions[k]

        t = Revtree(self.filename, self.description,
                    self.revisions.values(), self.head)

        self.revisions = None
        self.filename = None
        self.description = None
        self.head = None
        return t

    # Sink callbacks.
    def set_filename(self, name):
        self.filename = name

    def set_description(self, description):
        self.description = description

    def set_head_revision(self, num):
        self.head = self._numtorev(num)

    def define_tag(self, name, rev):
        rev = self._numtorev(rev)
        rev.tags[name] = 1

    def define_revision(self, rev, timestamp, author, state, branches, next):
        rev  = self._numtorev(rev)
        next = self._numtorev(next)

        rev.author = author
        rev.date   = timestamp
        rev.state  = state

        # the 'next' field is the parent if we're on the trunk, the
        # child if we're on the branch.
        if next:
            if rev.on_trunk:
                next.children.append(rev)
                rev.parent = next
            else:
                rev.children.append(next)
                next.parent = rev

        # branches are always children of rev.
        for b in branches:
            child = self._numtorev(b)
            rev.children.append(child)
            child.parent = rev

    def set_revision_info(self, rev, log, text):
        rev = self._numtorev(rev)
        rev.log = log
        rev.strings = text
