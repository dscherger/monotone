
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
import java.awt.SystemColor;
import javax.swing.border.LineBorder;
import javax.swing.filechooser.FileFilter;
import java.util.logging.Logger;
import java.util.logging.Level;

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
import org.apache.batik.swing.svg.LinkActivationListener;
import org.apache.batik.swing.svg.SVGUserAgentGUIAdapter;
import org.apache.batik.swing.svg.LinkActivationEvent;
import org.apache.batik.util.ParsedURL;
import org.apache.batik.bridge.UpdateManager;
import org.apache.batik.bridge.ScriptingEnvironment;
import org.apache.batik.script.Interpreter;
import org.w3c.dom.svg.SVGDocument;
import org.w3c.dom.Element;
import org.w3c.dom.NodeList;
import org.w3c.dom.Text;
import org.w3c.dom.Node;
import org.w3c.dom.events.EventTarget;
import org.w3c.dom.events.EventListener;
import org.w3c.dom.events.Event;
import org.w3c.dom.events.MouseEvent;

import net.sourceforge.gxl.GXLDocument;
import net.sourceforge.gxl.GXLGraph;
import net.sourceforge.gxl.GXLNode;
import net.sourceforge.gxl.GXLEdge;
import net.sourceforge.gxl.GXLString;
import net.sourceforge.gxl.GXLSet;
import net.sourceforge.gxl.GXLAttr;
import net.sourceforge.gxl.GXLInt;
import net.sourceforge.gxl.GXLTup;
import net.sourceforge.gxl.GXL;
import net.sourceforge.gxl.GXLValue;

/**
 * Somewhat misnamed class to provide a simple GUI against a monotone database
 * NOTE: This class requires the Batick SVG library from the Apache project http://xml.apache.org
 *
 * @author Joel Crisp, loosely based on the example from the Batik library documentation
 */
public class GXLViewer {

    /**
     * Log sink
     */
    private final static Logger logger=Logger.getLogger("GXLViewer");

    /**
     * GUI Frame to hold the main window
     */
    private JFrame frame;

    /**
     * Button to allow a user to select a database
     * TODO: This should be an action dammit Jim!
     */
    private JButton button = new JButton("Choose Database...");

    /**
     * Label which is used to display the status of some operations and some hints
     */
    private JLabel label = new JLabel();

    /**
     * Canvas which displays the actual SVG version of the ancestor tree
     */
    private JSVGCanvas svgCanvas;

    /**
     * Tree used to display the branches and heads
     */
    private JTree tree=new JTree();

    /**
     * The interface to the current monotone database
     */
    private Monotone database=null;

    /**
     * A dialog used to display a progress bar
     */
    private JDialog progress;

    /**
     * Panel which displays the properties of the currently selected node
     */
    private JPanel properties;

    public static void main(String[] args) throws IOException {
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
	String defaultDb=findDefaultDB(new File(".").getAbsoluteFile());
	if(defaultDb!=null) { 
	    logger.info("Found default database ["+defaultDb+"]");
	    app.setDatabase(new File(defaultDb));
	}
    }

    /**
     * Recurse up the directory tree from the directory specified and check to see if we
     * can find an MT directory. If so, read the options file and use the database specified
     * as the default database.
     *
     * @param candidate the directory to begin the search at
     * @return the name of the default database or null if one can't be found
     * @throws IOException if an IO exception occurs
     */
    private static String findDefaultDB(File candidate) throws IOException {
	logger.finer("Searching for default database in "+candidate); 
	if(!candidate.isDirectory()) throw new IOException("Current candidate "+candidate+" is not a directory.");
	File mt=new File(candidate,"MT");
	if(!mt.exists() || !mt.isDirectory()) {
	    // Recurse up and try the parent directory
	    if(candidate.getParentFile()==null) return null;
	    return findDefaultDB(candidate.getParentFile());
	}
	return readDatabaseFromOptionsFile(mt);
    }

    /**
     * Read the options file and return the database name from it
     *
     * @param mt a file pointing to the MT directory containing the options file
     * @return the database name from the options file.
     * @throws IOException if an IO exception occurs
     */
    private static String readDatabaseFromOptionsFile(File mt) throws IOException{
	File options=new File(mt,"options");
	FileInputStream rawSource=null;
	try {
	    rawSource=new FileInputStream(options);
	    LineNumberReader source=new LineNumberReader(new InputStreamReader(rawSource));
	    String line=null;
	    while((line=source.readLine())!=null) {
		if(line.startsWith("database")) {
		    return line.substring("database".length()+2,line.length()-1);
		}
	    }
	    return null;
	}
	finally {
	    if(rawSource!=null) rawSource.close();
	}
    }

    /**
     * Finish a background job, remove the progress meter, display the message
     * 
     * @param message the message to display in the status bar
     */
    public void finishJob(String message) {
	label.setText(message);
	if(progress==null) return;
	progress.setVisible(false);
	progress.dispose();
	progress=null;
    }

    public GXLViewer(JFrame f) {
        frame = f;
    }

    private JTree getBranchTree() {
	return tree;
    }

    public Monotone getDatabase() { 
	return database;
    }

    public void dumpEvent(Object e) {
	logger.info(e.getClass().getName()+" "+e);
    }

    /**
     * Open the modal progress window with an indeterminate progress bar
     *
     * @param message the message to display in the label and title of the dialog
     */
    private void setProgressWindow(String message) {
	progress=new JDialog(frame,message,true);
	JProgressBar bar=new JProgressBar();
	bar.setIndeterminate(true);
	JPanel box=new JPanel();
	progress.add("Center",box);
	box.setBorder(new LineBorder(SystemColor.windowBorder,3,true));
	box.add("North",new JLabel(message));
	box.add("Center",bar);
	progress.setDefaultCloseOperation(WindowConstants.DO_NOTHING_ON_CLOSE);
        progress.setUndecorated(true);
	progress.pack();
	progress.setLocation(frame.getX()+(frame.getWidth()-progress.getWidth())/2,frame.getY()+(frame.getHeight()-progress.getHeight())/2);
	progress.setVisible(true);
    }

    /**
     * Change the database file to use
     * This must be called on the GUI thread once the GUI has been initialised
     * 
     * @param file a pointer to the database file to use
     */
    private void setDatabase(File file) {
	database = new Monotone(file);
	new ReadBranches(GXLViewer.this);
	setProgressWindow("Reading branches..");
    }

    private class GXLUserAgent extends SVGUserAgentGUIAdapter {
	public GXLUserAgent(Component parent) {
	    super(parent);
	}
	
	public void openLink(String uri,boolean newTarget) {
	    logger.info("Link activated : "+uri);
	}
    }	


    /**
     * Make the GUI
     */
    private JComponent createComponents() {
	svgCanvas = new JSVGCanvas(new GXLUserAgent(frame),true,true);
        final JPanel panel = new JPanel(new BorderLayout());

        JPanel p = new JPanel(new FlowLayout(FlowLayout.LEFT));
        p.add(button);
        p.add(label);

	properties=new JPanel();
	tree.setModel(new DefaultTreeModel(new DefaultMutableTreeNode()));
	JSplitPane splitter=new JSplitPane(JSplitPane.VERTICAL_SPLIT,new JScrollPane(tree),new JScrollPane(properties));
	splitter.setDividerLocation(200);
	splitter=new JSplitPane(JSplitPane.HORIZONTAL_SPLIT,splitter,new JSVGScrollPane(svgCanvas));
        panel.add("North", p);
        panel.add("Center",splitter); 
	splitter.setDividerLocation(200);
	JLabel help=new JLabel();
	panel.add("South",help);
	help.setText("C-LMB = drag zoom, S-RMB = move zoom, S-LMB = pan, C-S-RMB = reset");
	label.setText("Select database");

	button.setEnabled(false);
        // Set the button action.
        button.addActionListener(new ActionListener() {
		public void actionPerformed(ActionEvent ae) {
		    JFileChooser fc = new JFileChooser(".");
		    fc.setFileFilter(new MonotoneFileFilter());
		    int choice = fc.showOpenDialog(panel);
		    if (choice == JFileChooser.APPROVE_OPTION) {
			setDatabase(fc.getSelectedFile());
		    }
		}
	    });
       	

	tree.addTreeSelectionListener(new TreeSelectionListener() {
		/**
		 * Listen for selection changes on the tree and if a head revision is selected
		 * draw the ancestor graph for it
		 *
		 * @param e the tree selection event from the branch tree
		 */
		public void valueChanged(TreeSelectionEvent e) {
		    if(progress!=null) return; // In another job - not safe to continue
		    Object node=e.getPath().getLastPathComponent();
		    // Check that our selection is a leaf node (all heads are leaf nodes) and ignore if not
		    if(!((DefaultMutableTreeNode)node).isLeaf()) return;
		    // Extract the id from the leaf node (should really use a proper user object for this!)
		    final String id=node.toString().substring(0,node.toString().indexOf(' ')-1);
		    label.setText("Reading log...");
		    new DisplayLog(GXLViewer.this,id);
		    setProgressWindow("Reading log for revision");
		}
	    });

        // Set the JSVGCanvas listeners.
        svgCanvas.addSVGDocumentLoaderListener(new SVGDocumentLoaderAdapter() {
		public void documentLoadingStarted(SVGDocumentLoaderEvent e) {
		    label.setText("Document Loading...");
	// Setup the interface between the SVG scripting environment and the viewer
	UpdateManager manager=svgCanvas.getUpdateManager();
	ScriptingEnvironment scripting=manager.getScriptingEnvironment();
	Interpreter interpreter=scripting.getInterpreter();
	interpreter.bindObject("host",this);
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
		    // Fixup graph to notify onClick events
		    SVGDocument graph=svgCanvas.getSVGDocument();
		    EventTarget rootNode=(EventTarget)graph.getDocumentElement();
		    rootNode.addEventListener("click",new OnClickAction(),false);
		}
	    });

        svgCanvas.addGVTTreeRendererListener(new GVTTreeRendererAdapter() {
		public void gvtRenderingPrepare(GVTTreeRendererEvent e) {
		    label.setText("Rendering Started...");
		}
		public void gvtRenderingCompleted(GVTTreeRendererEvent e) {
		    finishJob("");
		}
	    });
	
	//	svgCanvas.setEnableZoomInteractor(true);
	svgCanvas.addLinkActivationListener(new LinkActivationListener() { 
		public void linkActivated(LinkActivationEvent e) {
		    logger.info("Link activated : "+e.getReferencedURI());
		}});
	
	InputMap keys=svgCanvas.getInputMap();
	keys.put(KeyStroke.getKeyStroke("="),JSVGCanvas.ZOOM_IN_ACTION);
	keys.put(KeyStroke.getKeyStroke("-"),JSVGCanvas.ZOOM_OUT_ACTION);
	svgCanvas.setInputMap(JComponent.WHEN_FOCUSED,keys);
	
	button.setEnabled(true);
        return panel;
    }

    /**
     * Background thread to read the log for a revision and display the ancestry graph in SVG
     */
    class DisplayLog extends Thread {
	/**
	 * The revision id for which the ancestor graph should be drawn
	 */
	private final String id;

	/**
	 * The parent viewer for this thread
	 */
	private final GXLViewer parent;

	/**
	 * Create and start a new background thread to read the log for a revision 
	 * and display the ancestry graph in SVG
	 *
	 * @param parent the parent viewer to populate 
	 * @param id the revision identifier for which the ancestor graph should be drawn
	 */
	public DisplayLog(final GXLViewer parent,final String id) {
	    this.parent=parent;
	    this.id=id;
	    start();
	}
	
	/** 
	 * Thread to read the log for a revision and display the ancestry graph in SVG
	 */
	public void run() {
	    try {
		logger.fine("Getting log for "+id);
		final InputStream svgStream=database.getSVGLog(id);
		SAXSVGDocumentFactory factory=new SAXSVGDocumentFactory(XMLResourceDescriptor.getXMLParserClassName());
		final SVGDocument doc=factory.createSVGDocument("http://internal/graph",svgStream);
		SwingUtilities.invokeLater(new Runnable() { public void run() {
		    svgCanvas.setSVGDocument(doc);
		    svgCanvas.setDocumentState(JSVGComponent.ALWAYS_DYNAMIC);
		}});
	    } catch (IOException ex) {
		ex.printStackTrace();
	    }
	}
    }

    /**
     * Background thread to read the branch list from a monotone datababase
     */
    class ReadBranches extends Thread {
	/**
	 * The parent viewer for this thread
	 */
	private final GXLViewer parent;

	/**
	 * Create and start a new thread to read the branches from the specified viewer's database in the background
	 * and populate the viewer's branch tree
	 *
	 * @param parent the parent viewer to populate 
	 */
	public ReadBranches(final GXLViewer parent) {
	    this.parent=parent;
	    start();
	}

	/**
	 * Recurse down from the root of the tree building nodes for each fragment in the branch name
	 * unless the node already exists. Fragments are separated by '.'
	 *
	 * @param root the root of the tree
	 * @param branch the name of the branch
	 * @return the terminal node representing the full branch name
	 */
	private DefaultMutableTreeNode buildTree(DefaultMutableTreeNode root,String branch) {
	    String[] path=branch.split("\\.");
	    DefaultMutableTreeNode currentNode=root;
	    for(String fragment: path) {
		int children=currentNode.getChildCount();
		DefaultMutableTreeNode next=null;
		for(int I=0;I<children;I++) {
		    DefaultMutableTreeNode candidate=(DefaultMutableTreeNode)currentNode.getChildAt(I);
		    if(fragment.equals(candidate.getUserObject())) {
			next=candidate;
                        break;
		    }
		}
		if(next==null) {
		    next=new DefaultMutableTreeNode(fragment);
		    currentNode.add(next);
		}
		currentNode=next;
	    }
	    return currentNode;
	}

	/** 
	 * Thread to build a tree from the branches in the database
	 */
	public void run() {
	    Monotone database=parent.getDatabase();
	    DefaultMutableTreeNode root=new DefaultMutableTreeNode("Monotone: "+database.getName());
	    try { 
		logger.fine("Reading branches...");
		List<String> branches=database.listBranches();
		for(String branch: branches) {
		    logger.finest(branch);
		    
		    DefaultMutableTreeNode node=buildTree(root,branch);
		    // Now create leaf nodes for each head in the monotone branch
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

	    final DefaultTreeModel model=new DefaultTreeModel(root);
	    SwingUtilities.invokeLater(new Runnable() { public void run() { 
		parent.getBranchTree().setModel(model); 
		parent.finishJob("Select identifier from tree");
	    }});
	}
    }

    /**
     * File filter which only displays monotone .db files
     * Note: It actually displays all files which end in .db 
     */
    private class MonotoneFileFilter extends FileFilter {

	/**
	 * Return true for directories and monotone database files
	 *
	 * @param file the file to test
	 * @return true if the file is a directory or ends in .db
	 */
	public boolean accept(File file) {
	    if(file.isDirectory()) return true;
	    return file.getName().endsWith(".db");
	}

	/**
	 * Return the description of this filter
	 *
	 * @param return a non-internationalised description of this filter
	 */
	public String getDescription() {
	    return "Monotone Database Files";
	}
    }

    private class OnClickAction implements EventListener {
	
	private void addInfo(JPanel info,GXLNode gxlNode,String infoSet) {
	    GXLAttr setValue=gxlNode.getAttr(infoSet);
	    if(setValue==null) return;
	    GXLSet set=(GXLSet)setValue.getValue();
	    GridBagConstraints c=new GridBagConstraints();	    
	    c.anchor=GridBagConstraints.WEST;
	    JLabel key=new JLabel(infoSet+" ");
	    info.add(key,c);

	    for(int I=0;I<set.getValueCount();I++) {
		c=new GridBagConstraints();
		c.anchor=GridBagConstraints.WEST;
		c.gridx=1;
		c.gridwidth=GridBagConstraints.REMAINDER;
		JLabel value=new JLabel(((GXLString)set.getValueAt(I)).getValue());
		info.add(value,c);
	    }
	}

	public void handleEvent(Event evt) {
	    // System.err.println(evt);
	    MouseEvent mouseEvent=(MouseEvent)evt;
	    EventTarget where=mouseEvent.getTarget();
	    Element element=(Element)where; // This seems intuitative, but doesn't appear to be documented as legal
	    while(element.getTagName()!="g") {
		Node parent=element.getParentNode();
		if(parent==null) return;
		if(!(parent instanceof Element)) return;
		element=(Element)parent;
	    }
	    NodeList titles=element.getElementsByTagName("title");
	    if(titles.getLength()==0) return;
	    Element title=(Element)titles.item(0);
	    String id=((Text)title.getFirstChild()).getData(); // Fragile - should check node type
	    // System.err.println("["+id+"]");
	    JPanel info=new JPanel();
	    info.setLayout(new GridBagLayout());
	    GridBagConstraints c=new GridBagConstraints();
	    c.gridwidth=GridBagConstraints.REMAINDER;
	    c.anchor=GridBagConstraints.CENTER;
	    JLabel property=new JLabel(id);
	    info.add(property,c);
	    GXLNode gxlNode=(GXLNode)database.log2gxl.gxlDocument.getElement(id);
	    addInfo(info,gxlNode,"Authors");
	    addInfo(info,gxlNode,"Branches");
	    addInfo(info,gxlNode,"Tags");
	    addInfo(info,gxlNode,"ChangeLog");
	    properties.removeAll();
	    properties.add(BorderLayout.CENTER,info);
	    properties.revalidate();
	}
    }
}
