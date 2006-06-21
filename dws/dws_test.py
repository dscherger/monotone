#
# This file is part of 'mtnplain'
#
# Copyright (C) Zbigniew Zagorski <zzbigg@o2.pl>
# licensed to the public under the terms of the GNU GPL (>= 2)
# see the file COPYING for details
# I.e., do what you like, but keep copyright and there's NO WARRANTY.
#

from dws import *
import unittest

class TestSequenceFunctions(unittest.TestCase):    
    def setUp(self):
        self.c = Connection("http://localhost/mtdumb/dws/impl/php/mtdumb.php","path/")
        self.c.clear()
        
    def testList(self):
        self.c.list()
            
    def __doTestContent(self,name,content):
        self.c.put(name, content)
        self.assert_(int(self.c.stat(name)['size']) == len(content)) 
        list = self.c.list()
        self.assert_(name in self.c.list())
        readed = self.c.get(name)

        if readed != content:
            self.assert_( name and False )

        self.c.append(name, content)
        self.assert_(int(self.c.stat(name)['size']) == len(content)*2)
        
        readed2 = self.c.get(name)

        content2 = content + content
        
        self.assert_( readed2 == content2 )

        if len(content2) > 1:
            lx = len(content)
            self.assert_(self.c.getParts(name, [ ( 0, lx*2) ] ) == content2 )
            self.assert_(self.c.getParts(name, [(0, lx)] ) == content )
            self.assert_(self.c.getParts(name, [(lx, lx)] ) == content )
            self.assert_(self.c.getParts(name, [(0,1), (1, lx-1)] ) == content )
            self.assert_(self.c.getParts(name, [(0,lx), (lx, lx)] ) == content2 )
            self.assert_(self.c.getParts(name, [(0,lx-1), (lx-1, 1)] ) == content )
        if len(content) > 3 and len(content) % 2 == 0:
            ly = lx/2
            self.assert_(self.c.getParts(name, [(0,ly), (ly, lx), (ly+lx,ly)] ) == content2 )
            self.assert_(self.c.getParts(name, [(0,ly), (ly+lx,ly)] ) == content )
    def testPutEmpty(self):
        self.__doTestContent("f1", "")
    def testPutZero(self):
        self.__doTestContent("f2", "\0")
    def testPutGeneric(self):
        self.__doTestContent("f3", "jioia")
    def testPutWeidCharacters(self):
        self.__doTestContent("f4", "ZBiu\0\0xf0")
    def testBig1(self):
        z = ""
        for x in xrange(0,10000):
            z += "          "
        self.__doTestContent("f5", z)
        
           
    def testGet(self):
        pass
    
if __name__ == '__main__':            
    unittest.main()