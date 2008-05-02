import os.path
import pythoncom
import struct
import tempfile
import tortoise.contextmenu
import unittest
import win32con
import win32ui

class _MockDataObject:
    """
    Class to pretend to be a IDataObject
    """
    def __init__(self, storage):
        self._storage = storage

    def GetData(self, format):
        return self._storage

def _packDROPFILES(filenames):
    text = "\0".join(filenames)
    offset_to_text = 14
    return struct.pack("LLLbb%is" % (len(text) + 1), offset_to_text, 0, 0, 0, 0, text)

class TestContextMenuExtension(unittest.TestCase):
    """
    Test the context menu shell extension.
    """

    def _get_menu_entries(self, filenames):
        """
        Populate a menu given selected filenames
        """
        # Create a storage medium with a drop target files information
        storage = pythoncom.STGMEDIUM()
        storage.set(pythoncom.TYMED_HGLOBAL, _packDROPFILES(filenames))
        data = _MockDataObject(storage)

        contextmenu = tortoise.contextmenu.ContextMenuExtension()
        contextmenu.Initialize(None, data, None)

        # Populate the menu
        menu = win32ui.CreateMenu()
        initial_id = 10
        final_id = initial_id + contextmenu.QueryContextMenu(menu.GetHandle(), 0, initial_id, 1000, 0)
        result = []
        for i in range(menu.GetMenuItemCount()):
            text = menu.GetMenuString(i, win32con.MF_BYPOSITION)
            if text:
                self.assertTrue(menu.GetMenuItemID(i) in xrange(initial_id, final_id))
            result.append(text)

        return result

    def test_checkout_menu_item_when_not_in_tree(self):
        entries = self._get_menu_entries([tempfile.gettempdir()])
        self.assertEqual(["", "Bzr Checkout", ""], entries)

    def test_folder_context_menu_items(self):
        entries = self._get_menu_entries([os.path.dirname(__file__)])
        self.assertEqual(["", "Commit", "Diff", ""], entries)
