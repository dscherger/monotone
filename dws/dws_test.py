#
# This file is part of 'mtndumb'
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
        self.c = Connection("http://zbigg.internet.v.pl/zbigg-dump/","dws_python_test2/")        
        self.c.verbosity = 1
        
    def testList(self):
        self.c.list()
            
    def assertEquals(self, actual, expected, name = ""):
        if actual != expected:
            raise AssertionError("%sactual(%s) differs from expected(%s)" % (name,repr(actual), repr(expected)))

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
    def __testMany(self, files):
        from itertools import izip,count
        names = [ name for name, content in files]
        contents = [content for name, content in files ]
        self.c.putMany(files)
        c1 = self.c.getMany(names)
        for i,act,exp in izip(count(),c1,contents):
            self.assertEquals(act, exp, name="C%i: " %i)
            xact = self.c.get(names[i])
            self.assertEquals(xact, exp, name="C(single)%i" % i )
    
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
        
    def testMany_zeros(self):
        files = [ ("m1%i" % i, "" ) for i in range(10) ]
        self.__testMany(files)

    def testMany_onebytes(self):
        files = [ ("m2%i" % i, ("%c" % i)*1 ) for i in range(10) ]
        self.__testMany(files)

    def testMany_onearith(self):
        files = [ ("m3%i" % i, ("%c" % i)*i ) for i in range(100) ]
        self.__testMany(files)
        
    def testGet(self):
        pass
        
    def testSplit(self):
        om = self.c.MAX_POST
        try:
            self.c.MAX_POST = 127
            
            self.c.MAX_POST = 1
            self.__doTestContent("s1","abcdefg")
            
            self.c.MAX_POST = 3
            self.__doTestContent("s1","abcdefg" * 50)
            
            self.c.MAX_POST = 512
            self.__doTestContent("s1","abcdefghij" * 52)
            
            self.c.MAX_POST = 520
            self.__doTestContent("s1","abcdefghij" * 52)
            
        finally:
            self.c.MAX_POST = om
    
if __name__ == '__main__':            
    unittest.main()