
/*
 * Copyright (C) 2005 Joel Crisp <jcrisp@s-r-s.co.uk>
 * Licensed under the MIT license:
 *    http://www.opensource.org/licenses/mit-license.html
 * I.e., do what you like, but keep copyright and there's NO WARRANTY.
 */

import java.awt.*;
import java.awt.event.*;
import java.io.*;
import javax.swing.*;
import java.util.List;
import javax.swing.tree.*;
import javax.swing.event.*;

import org.apache.batik.swing.JSVGCanvas;
import org.apache.batik.swing.svg.JSVGComponent;
import org.apache.batik.swing.JSVGScrollPane;
import org.apache.batik.swing.gvt.GVTTreeRendererAdapter;
import org.apache.batik.swing.gvt.GVTTreeRendererEvent;
import org.apache.batik.swing.svg.SVGDocumentLoaderAdapter;
import org.apache.batik.swing.svg.SVGDocumentLoaderEvent;
import org.apache.batik.swing.svg.GVTTreeBuilderAdapter;
import org.apache.batik.swing.svg.GVTTreeBuilderEvent;
import org.apache.batik.dom.svg.SAXSVGDocumentFactory;
import org.apache.batik.util.XMLResourceDescriptor;
import org.w3c.dom.svg.SVGDocument;

/**
 * Somewhat misnamed class to provide a simple GUI against a monotone database
 * NOTE: This class requires the Batick SVG library from the Apache project http://xml.apache.org
 *
 * @author Joel Crisp, loosely based on the example from the Batik library documentation
 */
public class GXLViewer {

    public static void main(String[] args) {
        JFrame f = new JFrame("GXL Viewer");
        GXLViewer app = new GXLViewer(f);
        f.getContentPane().add(app.createComponents());

        f.addWindowListener(new WindowAdapter() {
		public void windowClosing(WindowEvent e) {
		    System.exit(0);
		}
	    });
        f.setSize(400, 400);
        f.setVisible(true);
    }
    
    JFrame frame;
    JButton button = new JButton("Choose Database...");
    JLabel label = new JLabel();
    JSVGCanvas svgCanvas = new JSVGCanvas();
    JTree tree=new JTree();
    Monotone database=null;
    JDialog progress;
    
    public void finishJob(String message) {
	label.setText(message);
	progress.setVisible(false);
    }

    public GXLViewer(JFrame f) {
        frame = f;
    }

    public JTree getBranchTree() {
	return tree;
    }

    public Monotone getDatabase() { 
	return database;
    }

    private void setProgressWindow() {
	progress=new JDialog(frame,"Processing...",true);
	JProgressBar bar=new JProgressBar();
	bar.setIndeterminate(true);
	progress.add("Center",bar);
	progress.pack();
	progress.setLocation(frame.getX()+(frame.getWidth()-progress.getWidth())/2,frame.getY()+(frame.getHeight()-progress.getHeight())/2);
	progress.setVisible(true);
    }

    public JComponent createComponents() {
        final JPanel panel = new JPanel(new BorderLayout());

        JPanel p = new JPanel(new FlowLayout(FlowLayout.LEFT));
        p.add(button);
        p.add(label);

	tree.setModel(new DefaultTreeModel(new DefaultMutableTreeNode()));
	JSplitPane splitter=new JSplitPane(JSplitPane.HORIZONTAL_SPLIT,new JScrollPane(tree),new JSVGScrollPane(svgCanvas));
        panel.add("North", p);
        panel.add("Center",splitter); 
	JLabel help=new JLabel();
	panel.add("South",help);
	help.setText("C-LMB = drag zoom, S-RMB = move zoom, S-LMB = pan, C-S-RMB = reset");
	label.setText("Select database");

        // Set the button action.
        button.addActionListener(new ActionListener() {
		public void actionPerformed(ActionEvent ae) {
		    JFileChooser fc = new JFileChooser(".");
		    int choice = fc.showOpenDialog(panel);
		    if (choice == JFileChooser.APPROVE_OPTION) {
			database = new Monotone(fc.getSelectedFile());
			new ReadBranches(GXLViewer.this);
			setProgressWindow();
		    }
		}
	    });
       	

	tree.addTreeSelectionListener(new TreeSelectionListener() {
		public void valueChanged(TreeSelectionEvent e) {
		    Object node=e.getPath().getLastPathComponent();
		    if(!((DefaultMutableTreeNode)node).isLeaf()) return;
		    final String id=node.toString().substring(0,node.toString().indexOf(' ')-1);
		    
		    new Thread(new Runnable() { public void run() {
		    try {
			//			if(e.getPaths().length==1) return; // Root node.
			// System.err.println("Getting log for "+id);
			label.setText("Reading log...");
			final InputStream svgStream=new BufferedInputStream(database.getSVGLog(id));
			SAXSVGDocumentFactory factory=new SAXSVGDocumentFactory(XMLResourceDescriptor.getXMLParserClassName());
			SVGDocument doc=factory.createSVGDocument("http://local",svgStream);
			svgCanvas.setSVGDocument(doc);
			svgCanvas.setDocumentState(JSVGComponent.ALWAYS_DYNAMIC);
			svgCanvas.setEnableZoomInteractor(true);
			InputMap keys=svgCanvas.getInputMap();
			keys.put(KeyStroke.getKeyStroke("="),JSVGCanvas.ZOOM_IN_ACTION);
			keys.put(KeyStroke.getKeyStroke("-"),JSVGCanvas.ZOOM_OUT_ACTION);
			svgCanvas.setInputMap(JComponent.WHEN_FOCUSED,keys);
		    } catch (IOException ex) {
			ex.printStackTrace();
		    }
		    }}).start();
		    setProgressWindow();
		}
	    });

        // Set the JSVGCanvas listeners.
        svgCanvas.addSVGDocumentLoaderListener(new SVGDocumentLoaderAdapter() {
		public void documentLoadingStarted(SVGDocumentLoaderEvent e) {
		    label.setText("Document Loading...");
		}
		public void documentLoadingCompleted(SVGDocumentLoaderEvent e) {
		    label.setText("Document Loaded.");
		}
	    });

        svgCanvas.addGVTTreeBuilderListener(new GVTTreeBuilderAdapter() {
		public void gvtBuildStarted(GVTTreeBuilderEvent e) {
		    label.setText("Build Started...");
		}
		public void gvtBuildCompleted(GVTTreeBuilderEvent e) {
		    label.setText("Build Done.");
		    //                frame.pack();
		}
	    });

        svgCanvas.addGVTTreeRendererListener(new GVTTreeRendererAdapter() {
		public void gvtRenderingPrepare(GVTTreeRendererEvent e) {
		    label.setText("Rendering Started...");
		}
		public void gvtRenderingCompleted(GVTTreeRendererEvent e) {
		    label.setText("");
		    svgCanvas.requestFocusInWindow();
		    progress.setVisible(false);
		    progress.dispose();
		    progress=null;
		}
	    });

        return panel;
    }

    class ReadBranches extends Thread {
	GXLViewer parent;

	public ReadBranches(GXLViewer parent) {
	    this.parent=parent;
	    start();
	}

	public void run() {
	    Monotone database=parent.getDatabase();
	    DefaultMutableTreeNode root=new DefaultMutableTreeNode("Monotone "+database.getName());
	    try { 
		//			    System.err.println("Reading branches...");
		List<String> branches=database.listBranches();
		for(String branch: branches) {
		    // System.err.println(branch);
		    DefaultMutableTreeNode node=new DefaultMutableTreeNode(branch);
		    root.add(node);
		    try {
			List<String>heads=database.listHeads(branch);
			for(String head: heads) {
			    DefaultMutableTreeNode leaf=new DefaultMutableTreeNode(head);
			    node.add(leaf);
			}
			
		    }
		    catch(IOException ioe) {
			ioe.printStackTrace();
			return;
		    }
		}
		
	    }
	    catch(IOException ioe) {
		ioe.printStackTrace();
		return;
	    }

	    DefaultTreeModel model=new DefaultTreeModel(root);
	    parent.getBranchTree().setModel(model);
	    parent.finishJob("Select identifier from tree");
	}
    }
}