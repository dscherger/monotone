

all: monotree.jar

monotree.jar: Log2Gxl.class Monotone.class GXLViewer.class META-INF/MANIFEST.MF
	jar cvmf META-INF/MANIFEST.MF monotree.jar Log2Gxl.class Monotone.class GXLViewer.class GXLViewer\$$1.class GXLViewer\$$2.class GXLViewer\$$3.class GXLViewer\$$4.class GXLViewer\$$5.class GXLViewer\$$6.class Monotone\$$ErrorReader.class Monotone\$$StreamCopier.class GXLViewer\$$3\$$1.class GXLViewer\$$ReadBranches.class GXLViewer\$$MonotoneFileFilter.class GXLViewer\$$ReadBranches\$$1.class

Log2Gxl.class: Log2Gxl.java
	javac -g -classpath "gxl/gxl-0.92/gxl.jar;." Log2Gxl.java

Monotone.class Monotone$$ErrorReader.class Monotone$$StreamCopier.class: Monotone.java Log2Gxl.class
	javac -g Monotone.java

GXLViewer$$ReadBranches.class GXLViewer$$2$$1.class GXLViewer$$3$$1.class GXLViewer$$1.class GXLViewer$$2.class GXLViewer$$3.class GXLViewer$$4.class GXLViewer$$5 GXLViewer$$6 GXLViewer.class GXLViewer$$MonotoneFileFilter.class GXLViewer$$ReadBranches$$1.class : GXLViewer.java Monotone.class
	javac -g -classpath "batik/batik-1.5.1/batik.jar;." GXLViewer.java

clean:
	rm -f *.class *~

spotless: clean
	rm -f monotree.jar *.dot *.gxl *.svg