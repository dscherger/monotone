import sys
import os.path

from wxPython.wx import *
from wxPython.ogl import *

wxOGLInitialize()

import graphs
import interface.cert_display


class TreeVersionShape(wxRectangleShape):

    def __init__(self, width, height, monotone, version):

        wxRectangleShape.__init__(self, width, height)

        self.SetDraggable(1, 1)

        self.__width = width
        self.__height = height
        self.__monotone = monotone
        self.__version = version

        self.AddText(version.short_name())

        self.__menu = None


    def maybe_setup_popup_menu(self):
        # Can't set up menu in __init__, because we don't have a canvas yet.
        # So we do it lazily.

        if self.__menu is not None:
            return
        
        self.__menu = wxMenu()

        copy_hex_id = wxNewId()
        self.__menu.Append(copy_hex_id,
                           "Copy ID (" + self.__version.hex_version()[:4]
                           + "...)")
        EVT_MENU(self.GetCanvas(), copy_hex_id, self.OnCopyHex)

        copy_bibble_id = wxNewId()
        self.__menu.Append(copy_bibble_id,
                           "Copy ID (" + self.__version.short_name() + "-...)")
        EVT_MENU(self.GetCanvas(), copy_bibble_id, self.OnCopyBibble)

        diff_id = wxNewId()
        self.__menu.Append(diff_id, "Diff")
        EVT_MENU(self.GetCanvas(), diff_id, self.OnDiff)

        checkout_id = wxNewId()
        self.__menu.Append(checkout_id, "Checkout")
        EVT_MENU(self.GetCanvas(), checkout_id, self.OnCheckout)
        

    def OnLeftClick(self, x, y, keys, attachment):

        if self.GetCanvas().maybe_select_this_node(self):
            return
        
        window = CertPopupWindow(self.GetCanvas(), -1,
                                 self.__monotone, self.__version)

        click_position = self.GetCanvas().ClientToScreen(wxPoint(x, y))
        window.Position(click_position, (0, 0))
        window.Popup()


    def OnEndDragLeft(self, *args):

        # Make scrollbars automagically fit the required area.
        self.base_OnEndDragLeft(*args)
        self.GetCanvas().fit_virtual_size()


    def OnRightClick(self, x, y, keys, attachment):

        self.maybe_setup_popup_menu()
        self.GetCanvas().PopupMenu(self.__menu, wxPoint(x, y))


    OnBeginDragRight = OnRightClick


    def copy_to_clipboard(self, text):

        if wxTheClipboard.Open():
            wxTheClipboard.UsePrimarySelection(1)
            dataobj = wxTextDataObject()
            dataobj.SetText(text)
            wxTheClipboard.SetData(dataobj)
            wxTheClipboard.Close()


    def OnCopyHex(self, event):

        self.copy_to_clipboard(self.__version.hex_version())


    def OnCopyBibble(self, event):

        self.copy_to_clipboard(self.__version.version())


    def closest_containing_frame(self):

        window = self.GetCanvas()
        while window and not isinstance(window, wxFrame):
            window = window.GetParent()
        return window


    def OnCheckout(self, event):

        dialog = wxDirDialog(self.closest_containing_frame(),
                             "Choose a destination directory",
                             os.getcwd(),
                             style=wxDD_NEW_DIR_BUTTON)
        if dialog.ShowModal() != wxID_OK:
            return
        destdir = dialog.GetPath()

        try:
            self.__monotone.checkout(self.__version, destdir)
        except Exception, e:
            error_dialog = wxMessageDialog(self.closest_containing_frame(),
                                           "Checkout failed!\n" + str(e),
                                           "Checkout status",
                                           wxOK | wxICON_ERROR)
            error_dialog.ShowModal()
            raise

        message_dialog = wxMessageDialog(self.closest_containing_frame(),
                                         "Checkout succeeded",
                                         "Checkout status",
                                         wxOK | wxICON_INFORMATION)
        message_dialog.ShowModal()


    def OnDiff(self, event):

        print "Select a node to diff against"
        self.GetCanvas().select_node(self.do_diff)


    def do_diff(self, other):

        pass


    def __str__(self):

        return "Proxy for %s (id: %i)" % (self.__version.version(), id(self))


class AncestryWindow(wxShapeCanvas):

    def __init__(self, parent, id, monotone, graph):
        
        wxShapeCanvas.__init__(self, parent, id)

        self.__monotone = monotone
        self.__graph = graphs.Graph()
        self.__shapes = {}
        self.__diagram = wxDiagram()
        self.SetDiagram(self.__diagram)
        self.__diagram.SetCanvas(self)

        self.__node_selection_callback = None

        for node in graph:
            self.add_node(node)
        for edge in graph.iteredges():
            self.add_edge(*edge)

        self.SetBackgroundColour("LIGHT BLUE")
        self.SetScrollRate(100, 100)
        self.fit_virtual_size()

        # Set up background popup menu.
        self.__menu = wxMenu()

        exit_id = wxNewId()
        self.__menu.Append(exit_id, "Exit")
        EVT_MENU(self, exit_id, self.OnExitProgram)


    def OnRightClick(self, x, y, keys):

        self.PopupMenu(self.__menu, wxPoint(x, y))


    OnBeginDragRight = OnRightClick


    def get_node_width_height(self):

        # Calculates the width and height to be used for revision nodes
        dc = wxClientDC(self)
        self.PrepareDC(dc)
        width, junk = dc.GetTextExtent("wawaw-memom")
        junk, height = dc.GetTextExtent("htdhklp")
        
        return (width + 5, height + 5)


    def add_node(self, node):

        if node in self.__graph:
            return

        self.__graph.add_node(node)
        width, height = self.get_node_width_height()
        shape = TreeVersionShape(width, height, self.__monotone, node)
        self.__shapes[node] = shape
        shape.SetCanvas(self)
        self.AddShape(shape)
        shape.SetX(100)
        shape.SetY(100)
        shape.Show(1)


    def add_edge(self, source, target):

        self.__graph.add_edge(source, target)

        source_shape = self.__shapes[source]
        target_shape = self.__shapes[target]

        line = wxLineShape()
        line.SetCanvas(self)
        line.SetPen(wxBLACK_PEN)
        line.SetBrush(wxBLACK_BRUSH)
        line.AddArrow(ARROW_ARROW)
        line.MakeLineControlPoints(2)
        source_shape.AddLine(line, target_shape)
        self.AddShape(line)
        line.Show(1)

        # wxOGL demo says:
        # for some reason, the shapes have to be moved for the line to show up...
        dc = wxClientDC(self)
        self.PrepareDC(dc)
        source_shape.Move(dc, source_shape.GetX(), source_shape.GetY())


    def fit_virtual_size(self):

        shapes = self.GetShapeList()
        max_x, max_y = 0, 0
        for shape in shapes:
            center_x, center_y = shape.GetX(), shape.GetY()
            extent_x, extent_y = shape.GetBoundingBoxMax()
            max_x = max(max_x, center_x + extent_x / 2)
            max_y = max(max_y, center_y + extent_y / 2)
        self.SetVirtualSize(wxSize(int(max_x) + 150, int(max_y) + 150))


    def OnExitProgram(self, event):

        sys.exit(0)


    def select_node(self, callback):

        if self.__node_selection_callback:
            print "Selection interrupted selection, canceling previous"
            callback = self.__node_selection_callback
            self.__node_selection_callback = None
            callback(None)
        self.__node_selection_callback = callback


    def maybe_select_this_node(self, node):
        """Returns true if a call to this function terminated a selection.

        In general, nodes that want to be selectable should call this function
        on left click, and if this function returns true than they should
        consider the event consumed."""

        if self.__node_selection_callback:
            callback = self.__node_selection_callback
            self.__node_selection_callback = None
            callback(node)
            return 1
        else:
            return 0


    def OnLeftClick(self, x, y, keys):

        if self.__node_selection_callback:
            callback = self.__node_selection_callback
            self.__node_selection_callback = None
            print "Node selection canceled."


class MainFrame(wxFrame):

    def __init__(self, parent, id, title, monotone, graph):

        wxFrame.__init__(self, parent, id, title, size=(500, 500))
        self.sizer = wxBoxSizer(wxVERTICAL)
        self.ancestry = AncestryWindow(self, -1, monotone, graph)
        self.sizer.Add(self.ancestry, 1, wxGROW)
        self.SetSizer(self.sizer)
        self.SetAutoLayout(1)
        self.Show(1)


if __name__ == "__main__":
    import monotone
    M = monotone.Monotone(executable=sys.argv[1], db=sys.argv[2])
    graph = M.get_full_ancestry()
    
    frame = MainFrame(None, -1, "Polytony: " + sys.argv[2], M, graph)
    frame.Show(1)

    app = wxPySimpleApp()
    app.MainLoop()
