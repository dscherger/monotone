#!/usr/bin/env python2.3
#
# this is a GUI for monotone. it is very incomplete.
#
# copyright (C) 2003, 2004 graydon hoare <graydon@pobox.com>
# released under the GNU GPL v2+
#

import wx
from wx import ogl
import posix, re
import sys

ogl.OGLInitialize()

# our program is more or less a tree of dynamic 'Model' values. each
# Model is lazily computed from its prerequisites. when a Model
# changes it invalidates its observers. an Update is an event object
# which ensures it is only delivered to a given recipient once

class Update:

    def __init__(self):
        self.recipients = []

    def maybeDrive(self, other):
        i = id(other)
        if i not in self.recipients:
            other.drive()
            self.recipients.append(i)

class Model:

    def __init__(self):
        self.observers = []
        self.valid = 0
        self.value = 0
        
    def addObserver(self, other):
        self.observers.append(other)

    def delObserver(self, other):
        self.observers.remove(other)

    def observe(self, other):
        other.addObserver(self)

    def forget(self, other):
        other.delObserver(self)

    def invalidate(self, update):
        self.valid = 0
        self.value = 0
        for observer in self.observers:
            observer.invalidate(update)

    def changed(self, update):
        update.maybeDrive(self)
        for observer in self.observers:
            observer.changed(update)

    def set(self, v):
        self.invalidate(Update())
        self.value = v
        self.valid = 1
        self.changed(Update())

    def get(self):
        if not self.valid:
            self.value = self.build()
            self.valid = 1
        return self.value

    # these two you override:
    # build() is called when it's time for you to make a new value
    # drive() is called when a value you depend on has changed

    def build(self):
        # you need to override this if you are a dynamic value
        # (otherwise you can be set directly). if it gets called and
        # you haven't overridden it, you are in error.        
        assert(1 == 0)

    def drive(self):
        pass


# a Command encapsulates a single "ask monotone some question"
# cycle. it returns some derived value from its "run" method, which
# you can customize by overriding the beginResults, resultLine, and
# endResults methods.

class Command:

    def __init__(self):
        self.options={}
        self.multiOptions={}
        self.args=[]

    def addMultiOption(self, key, val):
        if not self.multiOptions.has_key(key):
            self.multiOptions[key] = []
        self.multiOptions[key].append(val)

    def addOption(self, key, val):
        self.options[key] = val

    def addArg(self, arg):
        self.args.append(arg)

    def buildCmdString(self):
        argv = ["monotone"]
        space = " "
        for k,v in self.options.items():
            argv.append("--%s=%s" % (k,v))

        for k,l in self.multiOptions.items():
            for v in l: 
                argv.append("--%s=%s" % (k,v))

        for a in self.args:
            argv.append(a)
                
        return space.join(argv)

    def run(self):
        cmdString = self.buildCmdString()
        print "running %s" % cmdString
        p = posix.popen(cmdString, "r")
        self.beginResults()
        for line in p.xreadlines():
            self.resultLine(line)
        return self.endResults()

    # specific methods to insulate against cursed strings

    def setBranch(self, branch):
        self.addOption("branch", branch)

    def setDatabase(self, db):
        self.addOption("db", db)

    def setKey(self, key):
        self.addOption("key", key)

    def addRcFile(self, rcfile):
        self.addMultiOption("rcfile", rcfile)

    # override these in subclasses

    def beginResults(self):
        self.lines = []

    def resultLine(self, line):
        self.lines.append(line)

    def endResults(self):
        return self.lines


class Cert:

    def __init__(self):
        self.sig = 0
        self.key = 0
        self.name = 0
        self.value = 0


class LsManifestCerts (Command):

    def __init__(self, database, manifest):
        Command.__init__(self)
        self.setDatabase(database)
        self.addArg("ls")
        self.addArg("certs")
        self.addArg("manifest")
        self.addArg(manifest)

        pat = "^(Key  |Sig  |Name |Value|     ) : (.*)$"
        self.pat = re.compile(pat)

    def beginCert(self):
        if (self.cert != 0):
            self.endCert()
        self.cert = Cert()
        self.key = 0
        self.val = 0
            
    def readItem(self):
        (k,v) = (self.key, self.value)
        if (k == "Key"):
            self.cert.key = v
        elif (k == "Sig"):
            self.cert.sig = v
        elif (k == "Name"):
            self.cert.name = v
        elif (k == "Value"):
            self.cert.value = v
        self.key = 0
        self.val = 0

    def endCert(self):
        if (self.key != 0):
            self.readItem()
        if (self.cert != 0):
            self.certs.append(self.cert)
        self.cert = 0

    def beginResults(self):
        self.certs = []
        self.cert = 0
        self.key = 0
        self.value = 0

    def resultLine(self, line):
        m = self.pat.match(line)
        if m:
            if (self.cert == 0):
                self.beginCert()
                
            (k, v) = m.groups()
            k = k.strip()

            if (k == ""):
                self.value += v
            else:
                if (self.key != 0):
                    self.readItem()
                self.key = k
                self.value = v
        else:
            self.endCert()

    def endResults(self):
        self.endCert()
        return self.certs



class LsBranches (Command):

    def __init__(self, database):
        Command.__init__(self)
        self.setDatabase(database)
        self.addArg("ls")
        self.addArg("branches")

    def resultLine(self, line):
        branch = line.strip()
        self.lines.append(branch)


class GetHeads (Command):

    def __init__(self, database, branchname):
        Command.__init__(self)
        self.setDatabase(database)
        self.setBranch(branchname)
        self.addArg("heads")
        self.pat = re.compile("([a-zA-Z0-9]{40})")

    def resultLine(self, line):
        m = self.pat.match(line)
        if m:
            id = m.group(1)
            self.lines.append(id)


class GetManifest (Command):

    def __init__(self, database, manifest):
        Command.__init__(self)
        self.setDatabase(database)
        self.addArg("cat")
        self.addArg("manifest")
        self.addArg(manifest)
        self.pat = re.compile("^([a-zA-Z0-9]{40})  (.*)$")

    def beginResults(self):
        self.entries = {}

    def resultLine(self, line):
        m = self.pat.match(line)
        if m:
            (id, path) = m.group(1,2)
            self.entries[path] = id

    def endResults(self):
        return self.entries


class BranchList (Model):

    def __init__(self, database):
        Model.__init__(self)
        self.database = database
        self.observe(database)

    def build(self):
        return LsBranches(self.database.get()).run()


class Branch (Model):

    def __init__(self, branches):
        Model.__init__(self)
        self.branches = branches
        self.observe(branches)

    def build(self):
        x = self.branches.get()
        if len(x) > 0:
            return x[-1]
        else:
            return 0

class Heads (Model):

    def __init__(self, database, branch):
        Model.__init__(self)
        self.branch = branch
        self.database = database
        self.observe(branch)
        self.observe(database)

    def build(self):
        return GetHeads(self.database.get(),
                        self.branch.get()).run()


class ManifestID (Model):

    def __init__(self, heads):
        Model.__init__(self)
        self.heads = heads
        self.observe(heads)

    def build(self):
        heads = self.heads.get()
        if len(heads) > 0:
            return heads[0]
        else:
            return 0


class Manifest (Model):

    def __init__(self, database, manifestID):
        Model.__init__(self)
        self.database = database
        self.manifestID = manifestID
        self.observe(database)
        self.observe(manifestID)

    def build(self):
        id = self.manifestID.get()
        if id == 0:
            return {}
        return GetManifest(self.database.get(), id).run()


class CertList (Model):

    def __init__(self, database, manifestID):
        Model.__init__(self)
        self.database = database
        self.manifestID = manifestID
        self.observe(database)
        self.observe(manifestID)

    def build(self):
        return LsManifestCerts(self.database.get(),
                               self.manifestID.get()).run()


class DataStore:

    def __init__(self):
        
        self.database = Model()

        self.branches = BranchList(self.database)
        self.branch = Branch(self.branches)
        self.heads = Heads(self.database, self.branch)

        self.manifestID = ManifestID(self.heads)
        self.manifest = Manifest(self.database, self.manifestID)
        self.manifestCerts = CertList(self.database, self.manifestID)


class ManifestTree(wx.TreeCtrl, Model):

    def __init__(self, store, parent, id):
        Model.__init__(self)
        wx.TreeCtrl.__init__(self, parent, id)
        self.store = store
        self.observe(store.manifest)
        self.SetIndent(10)

    def drive(self):
        self.rootID = self.AddRoot(self.store.branch.get())
        for (path, id) in self.store.manifest.get().items():
            self.AppendItem(self.rootID, path)


class AncestryNode(ogl.RectangleShape):

    def __init__(self, parent, text):
        ogl.RectangleShape.__init__(self, 70.0, 20.0)
        self.AddText(text)

class AncestryGraph(ogl.ShapeCanvas):

    def __init__(self, store, parent, ID):
        ogl.ShapeCanvas.__init__(self, parent, ID)
        self.store = store
        self.diagram = ogl.Diagram()
        self.SetDiagram(self.diagram)
        self.diagram.SetCanvas(self)
        self.nodes = {}
        self.new_node("hello")
        self.new_node("this")
        self.new_node("is")
        self.new_node("a")
        self.new_node("graph")        
        self.diagram.ShowAll(1)
        

    def new_node(self, text):
        shape = AncestryNode(self, text)
        shape.SetX(25.0)
        shape.SetY(25.0)
        self.AddShape(shape)
        self.nodes[text] = shape

class DatabasePage(wx.Panel, Model):

    def __init__(self, store, parent, ID):
        Model.__init__(self)
        wx.Panel.__init__(self, parent, ID)
        self.store = store
        self.branchList=wx.ListCtrl(self, -1, size=(100,200), style=wx.LC_REPORT|wx.SUNKEN_BORDER)
        self.branchList.InsertColumn(0,"branch")

        self.statsPanel = wx.Panel(self, -1)
        box = wx.BoxSizer(wx.VERTICAL|wx.HORIZONTAL)
        box.Add(self.branchList, 3, wx.EXPAND)
        box.Add(self.statsPanel, 1, wx.EXPAND)
        
        self.SetAutoLayout(True)
        self.SetSizer(box)
        
        self.Layout()
        self.branchList.Show(True)
        self.observe(store.branches)
        self.index = 0

    def drive(self):
        branches = self.store.branches.get()
        for branch in branches:
            self.branchList.InsertStringItem(self.index, branch)
            self.branchList.SetColumnWidth(0, wx.LIST_AUTOSIZE)
            self.index = self.index + 1


class ManifestPage(wx.Panel, Model):

    def __init__(self, store, parent, ID):
        Model.__init__(self)
        wx.Panel.__init__(self, parent, ID)
        self.store = store
        self.certList=wx.ListCtrl(self, -1, size=(800,600), style=wx.LC_REPORT|wx.SUNKEN_BORDER)
        self.certList.InsertColumn(0,"key")
        self.certList.InsertColumn(1,"sig")
        self.certList.InsertColumn(2,"name")
        self.certList.InsertColumn(3,"value")

        box = wx.BoxSizer(wx.VERTICAL|wx.HORIZONTAL)
        box.Add(self.certList, 2, wx.EXPAND)
        
        self.SetAutoLayout(True)
        self.SetSizer(box)
        
        self.Layout()
        self.certList.Show(True)
        self.observe(self.store.manifestCerts)
        self.index = 0

    def drive(self):
        certs = self.store.manifestCerts.get()
        for cert in certs:
            self.certList.InsertStringItem(self.index, cert.key)
            self.certList.SetStringItem(self.index, 1, cert.sig)
            self.certList.SetStringItem(self.index, 2, cert.name)
            self.certList.SetStringItem(self.index, 3, cert.value)
            self.certList.SetColumnWidth(0, wx.LIST_AUTOSIZE)
            self.certList.SetColumnWidth(1, wx.LIST_AUTOSIZE)
            self.certList.SetColumnWidth(2, wx.LIST_AUTOSIZE)
            self.certList.SetColumnWidth(3, wx.LIST_AUTOSIZE)
            self.index = self.index + 1


class ManifestNotebook(wx.Notebook):

    def __init__(self, store, parent, ID):
        wx.Notebook.__init__(self, parent, ID)
        self.store = store
        self.databasePanel = DatabasePage(store, self, -1)
        self.manifestPanel = ManifestPage(store, self, -1)
        self.filePanel = wx.Panel(self, -1)
        self.AddPage(self.databasePanel, "database")
        self.AddPage(self.manifestPanel, "manifest")
        self.AddPage(self.filePanel, "file")


class VertSplitter(wx.SplitterWindow):

    def __init__(self, store, parent, ID):        
        wx.SplitterWindow.__init__(self, parent, ID,style=wx.NO_3D|wx.SP_3D)
        self.store = store
        self.left = ManifestTree(store, self, -1)
        self.right = ManifestNotebook(store, self, -1)
        self.SplitVertically(self.left, self.right, 200)


class HorizSplitter(wx.SplitterWindow):
    
    def __init__(self, store, parent, ID):
        wx.SplitterWindow.__init__(self, parent, ID,style=wx.NO_3D|wx.SP_3D)
        self.store = store
        self.top = AncestryGraph(store, self, -1)
        self.bot = VertSplitter(store, self,  -1)
        self.SplitHorizontally(self.top, self.bot, 200)
  
class Frame(wx.Frame):

    def __init__(self, store, parent, ID, title):
        wx.Frame.__init__(self, parent, -1, title, size = (800, 600),
                          style=wx.DEFAULT_FRAME_STYLE|wx.NO_FULL_REPAINT_ON_RESIZE)

        self.store = store
        self.statusbar = self.CreateStatusBar()

        menu = wx.Menu()
        menu.Append(1, "E&xit", "Terminate the program")
        menu.Append(2, "&Open", "Open a new database")
        menuBar = wx.MenuBar()
        menuBar.Append(menu, "&File");
        self.SetMenuBar(menuBar)
        wx.EVT_MENU(self, 1, self.DoExit)
        wx.EVT_MENU(self, 2, self.DoFileOpen)

        self.control = HorizSplitter(store, self, -1)
        self.Show(True)


    def DoExit(self, event):
        self.Close(True)

    def DoFileOpen(self, event):
        f = wx.FileDialog(self, style=wx.OPEN)
        if f.ShowModal() == wx.ID_OK:
            self.store.database.set(f.GetPath())



class App(wx.App):

    def __init__(self, parent, store):
        self.store = store
        wx.App.__init__(self, parent)

    def OnInit(self):
        self.frame = Frame(self.store, None,-1,"monotone")
        return True


class UpdateManager(Model):

    def __init__(self, store):
        Model.__init__(self)
        self.manifest = store.manifest
#        self.observe(self.manifest)

    def drive(self):
        for (k,v) in self.manifest.get().items():
            print "UpdateManager: manifest entry: '%s' -> '%s'" % (k, v)

    
store = DataStore()
upd = UpdateManager(store)
app = App(0, store)

for i in sys.argv[1:]:
    store.database.set(i)

app.MainLoop()


        
