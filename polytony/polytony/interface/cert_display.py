import wx

class CertPopupWindow(wx.PopupTransientWindow):

    def __init__(self, parent, id, monotone, version):

        wx.PopupTransientWindow.__init__(self, parent, id)

        panel = wx.Panel(self, -1)
        panel.SetBackgroundColour("CORNSILK")

        margin = 10
        text = wx.StaticText(panel, -1,
                             monotone.get_cert_text(version).strip(),
                             pos=(margin, margin))
        size = text.GetBestSize()
        panel.SetSize((size.width + margin * 2, size.height + margin * 2))
        self.SetSize(panel.GetSize())


    def _do_dismiss(self, *dontcare):

        self.Dismiss()

    ProcessLeftDown = _do_dismiss
    ProcessRightDown = _do_dismiss
