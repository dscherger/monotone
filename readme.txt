Welcome!

This is a small java GUI to display monotone ancestry graphs.
It also contains a parser for the monotone log output format which converts the log output to a GXL graph.

Whilst not as complete or functional as monotone-viz, it does run on windows.

In addition to these files and java 1.5 you also need:

GXL Java API - http://www.gupro.de/GXL/index.html as: gxl/gxl-0.92/gxl.jar to monotree.jar
Batick - SVG library from the Apache project http://xml.apache.org as: batik/batick-1.5.1/batik.jar & batick/batik-1.5.1/lib relative to monotree.jar

Run using:

d:\> java -Xmx512m -classpath monotree.jar GXLViewer

The GXL converter can also be run stand-alone:

d:\> monotone log <id> | java -classpath monotree.jar Log2Gxl | gxl2dot -d | dot -Tps2

This will produce a postscript file suitable for converting into PDF 

The code is copyright as follows:

/*
 * Copyright (C) 2005 Joel Crisp <jcrisp@s-r-s.co.uk>
 * Licensed under the MIT license:
 *    http://www.opensource.org/licenses/mit-license.html
 * I.e., do what you like, but keep copyright and there's NO WARRANTY.
 */

I hope this is useful.

Joel Crisp