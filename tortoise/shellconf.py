# Published under the GNU GPL, v2 or later.
# Copyright (C) 2007 Henry Ludemann <misc@hl.id.au>
# Copyright (C) 2007 TK Soh <teekaysoh@gmail.com>

from mercurial import ui

# overlay icons display setting
show_overlay_icons = False

def read():
    global show_overlay_icons
    overlayicons = ui.ui().config('tortoisehg', 'overlayicons', '')
    print "tortoisehg.overlayicons = ", overlayicons
    show_overlay_icons = overlayicons != 'disabled'
