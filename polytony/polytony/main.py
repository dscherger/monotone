import sys
import wx

import polytony.monotone
from polytony.interface.main import MainFrame


def main(args):
    if len(args) != 2:
        sys.stderr.write("Usage: $0 <command to run monotone>"
                         " <path to monotone db>")
        sys.exit(1)
    ex, db = args
    M = polytony.monotone.Monotone(executable=ex, db=db)

    graph = M.get_full_ancestry()

    frame = MainFrame(None, -1, "Polytony: " + db, M, graph)
    frame.Show(1)

    app = wx.PySimpleApp()
    app.MainLoop()
