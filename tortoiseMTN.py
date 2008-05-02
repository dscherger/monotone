#
# Simple TortoiseSVN-like Mercurial plugin for the Windows Shell
# Published under the GNU GPL, v2 or later.
# Copyright (C) 2007 Jelmer Vernooij <jelmer@samba.org>
# Copyright (C) 2007 Henry Ludemann <misc@hl.id.au>
# Copyright (C) 2007 TK Soh <teekaysoh@gmail.com>
#

import os
import sys
import _winreg

if hasattr(sys, "frozen") and sys.frozen == 'dll':
    import win32traceutil

# specify version string, otherwise 'mtn identify' will be used:
version = ''

import tortoise.version
tortoise.version.remember_version(version)

# shell extension classes
from tortoise.contextmenu import ContextMenuExtension
from tortoise.iconoverlay import ChangedOverlay, AddedOverlay, UnchangedOverlay

bin_path = os.path.dirname(os.path.join(os.getcwd(), sys.argv[0]))
print "bin path = ", bin_path

# TortoiseHg registry setup
def register_tortoise_path(unregister=False):
    key = r"Software\TortoiseHg"
    cat = _winreg.HKEY_LOCAL_MACHINE
    if (unregister):
        _winreg.DeleteKey(cat, key)
        print "TortoiseHg unregistered"
    else:
        _winreg.SetValue(cat, key, _winreg.REG_SZ, bin_path)
        print "TortoiseHg registered"

# for COM registration via py2exe
def DllRegisterServer():
    RegisterServer(ContextMenuExtension)
    RegisterServer(ChangedOverlay)
    RegisterServer(AddedOverlay)
    RegisterServer(UnchangedOverlay)
    register_tortoise_path()

# for COM registration via py2exe
def DllUnregisterServer():
    UnregisterServer(ContextMenuExtension)
    UnregisterServer(ChangedOverlay)
    UnregisterServer(AddedOverlay)
    UnregisterServer(UnchangedOverlay)
    register_tortoise_path(unregister=True)

def RegisterServer(cls):
    # Add monotone to the library path
    try:
        import monotone
    except ImportError:
        from win32com.server import register
        register.UnregisterClasses(cls)
        raise "Error: Failed to find monotone module!"

    mtn_path = os.path.dirname(os.path.dirname(monotone.__file__))
    try:
        key = "CLSID\\%s\\PythonCOMPath" % cls._reg_clsid_
        path = _winreg.QueryValue(_winreg.HKEY_CLASSES_ROOT, key)
        _winreg.SetValue(_winreg.HKEY_CLASSES_ROOT, key, _winreg.REG_SZ, "%s;%s" % (path, mtn_path))
    except:
        pass
        
    # Add the appropriate shell extension registry keys
    for category, keyname in cls.registry_keys: 
        _winreg.SetValue(category, keyname, _winreg.REG_SZ, cls._reg_clsid_)

    print cls._reg_desc_, "registration complete."

def UnregisterServer(cls):
    for category, keyname in cls.registry_keys:
        try:
            _winreg.DeleteKey(category, keyname)
        except WindowsError, details:
            import errno
            if details.errno != errno.ENOENT:
                raise
    print cls._reg_desc_, "unregistration complete."

if __name__=='__main__':
    from win32com.server import register
    register.UseCommandLine(ContextMenuExtension,
            finalize_register = lambda: RegisterServer(ContextMenuExtension),
            finalize_unregister = lambda: UnregisterServer(ContextMenuExtension))
    
    for cls in (ChangedOverlay, AddedOverlay, UnchangedOverlay):
        register.UseCommandLine(cls,
                finalize_register = lambda: RegisterServer(cls),
                finalize_unregister = lambda: UnregisterServer(cls))

    if "--unregister" in sys.argv[1:]:
        register_tortoise_path(unregister=True)
    else:
        register_tortoise_path()

    