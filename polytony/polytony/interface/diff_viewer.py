import wx

class DiffDisplayText(wx.TextCtrl):

    def __init__(self, parent, id, diff_text):

        wx.TextCtrl.__init__(self, parent, id, "",
                             size=(800,600),
                             style=wx.TE_MULTILINE
                             # It's not actually possible to prevent GTK+ from
                             # wrapping, apparently...
                                   | wx.TE_DONTWRAP
                                   | wx.TE_RICH2
                                   | wx.TE_READONLY)
        
        text_attr = self.GetDefaultStyle()
        point_size = text_attr.GetFont().GetPointSize()
        fixed_font = wx.Font(point_size, wx.TELETYPE, wx.NORMAL, wx.NORMAL)
        text_attr.SetFont(fixed_font)
        self.SetDefaultStyle(text_attr)
        self.AppendText(diff_text)


class DiffDisplayFrame(wx.Frame):

    def __init__(self, monotone, version1, version2):

        desc_string = version1.short_name() + " -> " + version2.short_name()

        wx.Frame.__init__(self, None, -1, "Polytony: diff " + desc_string)

        sizer = wx.BoxSizer(wx.VERTICAL)

        header = wx.StaticText(self, -1, desc_string)
        sizer.Add(header, 0, wx.ALIGN_CENTER)

        diff_text = monotone.diff(version1, version2)
        text_display = DiffDisplayText(self, -1, diff_text)
        text_display.SetFocus()  # FIXME: why doesn't this work?
        sizer.Add(text_display, 1, wx.EXPAND | wx.ALL)

        close_id = wx.NewId()
        close_button = wx.Button(self, close_id, "Close", wx.Point(0, 0))
        sizer.Add(close_button, 0, wx.ALIGN_CENTER)
        wx.EVT_BUTTON(self, close_id, self.OnCloseRequest)

        self.SetSizer(sizer)
        sizer.SetSizeHints(self)


    def OnCloseRequest(self, event):

        self.Show(0)
        self.Destroy()
