#!/usr/bin/env python
"""gladebase.py
This module provides base classes for Glade-derived UIs and their
controllers."""

import os, gtk
from libglade import GladeXML

class UI(GladeXML):
    """Base class for UIs loaded from glade."""

    def __init__(self, filename, rootname):
        """Initialize a new instance.
        `filename' is the name of the .glade file containing the UI hierarchy.
        `rootname' is the name of the topmost widget to be loaded."""
        self._filename = filename

        GladeXML.__init__(self, filename, rootname)
        self.root = self.get_widget(rootname)

    def __getattr__(self, name):
        """Look up an as-yet undefined attribute, assuming it's a widget."""
        result = self.get_widget(name)
        if result is None:
            raise AttributeError("Can't find widget %s in %s.\n" %
                                 (`name`, `self._filename`))

        # Cache the widget to speed up future lookups.  If multiple
        # widgets in a hierarchy have the same name, the lookup
        # behavior is non-deterministic just as for libglade.
        setattr(self, name, result)
        return result

class Controller:
    """Base class for all controllers of glade-derived UIs."""
    def __init__(self, ui):
        """Initialize a new instance.
        `ui' is the user interface to be controlled."""
        self.ui = ui
        self.ui.signal_autoconnect(self._getAllMethods())

    def _getAllMethods(self):
        """Get a dictionary of all methods in self's class hierarchy."""
        result = {}

        # Find all callable instance/class attributes.  This will miss
        # attributes which are "interpreted" via __getattr__.  By
        # convention such attributes should be listed in
        # self.__methods__.
        allAttrNames = self.__dict__.keys() + self._getAllClassAttributes()
        for name in allAttrNames:
            value = getattr(self, name)
            if callable(value):
                result[name] = value
        return result

    def _getAllClassAttributes(self):
        """Get a list of all attribute names in self's class hierarchy."""
        nameSet = {}
        for currClass in self._getAllClasses():
            nameSet.update(currClass.__dict__)
        result = nameSet.keys()
        return result

    def _getAllClasses(self):
        """Get all classes in self's heritage."""
        result = [self.__class__]
        i = 0
        while i < len(result):
            currClass = result[i]
            result.extend(list(currClass.__bases__))
            i = i + 1
        return result
