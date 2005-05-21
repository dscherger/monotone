
/*
 * Copyright (C) 2005 Joel Crisp <jcrisp@s-r-s.co.uk>
 * Licensed under the MIT license:
 *    http://www.opensource.org/licenses/mit-license.html
 * I.e., do what you like, but keep copyright and there's NO WARRANTY.
 */

import java.io.InputStream;
import java.io.File;
import java.io.InputStreamReader;
import java.io.LineNumberReader;
import java.io.IOException;
import java.util.List;
import java.util.Collections;
import java.util.ArrayList;
import java.io.PipedInputStream;
import java.io.PipedOutputStream;
import java.io.OutputStream;
import java.io.BufferedOutputStream;
import java.io.BufferedInputStream;
import java.util.logging.Logger;
import java.util.logging.Level;
import javax.xml.transform.TransformerFactory;
import javax.xml.transform.Transformer;
import javax.xml.transform.Source;
import javax.xml.transform.URIResolver;
import javax.xml.transform.stream.StreamSource;
import javax.xml.transform.stream.StreamResult;
import java.util.Arrays;

/**
 * Interface class to control an inferior Monotone process and return information from it
 *
 * @author Joel Crisp
 */
public class Monotone {

    /**
     * Log sink
     */
    private static Logger logger=Logger.getLogger("Monotone");

    /**
     * Pointer to the database file for monotone (the .db file)
     */
    private File database;

    /**
     * Create a new interface to a monotone database
     *
     * @param database the location of the monotone database to use
     */
    public Monotone(File database) {
	this.database=database;
	logger.setLevel(Level.FINEST);
    }
    
    /**
     * Construct the basic monotone command specifying the database
     *
     * @return the base monotone command
     */
    public String[] getBaseCommand() {
	return new String[] { "monotone","--db="+database };
    }

    /**
     * List all the branches in the current monotone database
     *
     * @return a list of strings which enumerates the branches in the current monotone database
     */
    public List<String> listBranches() throws IOException {
	List<String> result=runMonotone(new String[] { "list", "branches"});
	return result;
    }

    /**
     * List all the heads of a specific branch in the current monotone database
     *
     * @param branch the name of the branch
     * @return a list of strings which enumerates the heads of the specified branch in the current monotone database
     */
    public List<String> listHeads(String branch) throws IOException {
	List<String>result=runMonotone(new String[] { "heads", "--branch",branch});
	return result;
    }

    /**
     * Get the location of this database
     */
    File getDatabaseFile() {
	return database;
    }

    /**
     * Get the name of the monotone database (without the full path)
     * TODO: Should this strip the .db suffix?
     *
     * @return the name of the database
     */
    public String getName() { 
	return database.getName();
    } 

    /**
     * HACK - remember the last Log2Gxl we ran so GXLViewer can get to it
     */
    public Log2Gxl log2gxl;

    public enum HighlightTypes {
        NONE,
	AUTHORS,
	BRANCHES    
        };

    /**
     * Return the full command by composing the base command and the sub-command
     * @param subCommand an array of strings representing the sub-command
     * @return an array of strings containing the full command
     */
    private String[] getCommand(String[] subCommand) {
	String[] base=getBaseCommand();
	ArrayList<String> fullCommand=new ArrayList<String>();
        fullCommand.addAll(Arrays.asList(base));
        fullCommand.addAll(Arrays.asList(subCommand));
        return fullCommand.toArray(new String[0]);
    }

    /** 
     * Run monotone and get an SVG stream from a log 
     *
     * @param id the identifier (revision or file) for which the log should be generated (not null)
     * @param highlight an enum specifing the node background highlight type requested
     * @return a stream from which an SVG format graph may be read
     */
    public InputStream getSVGLog(final String id,final HighlightTypes highlight) throws IOException {
	final String[] command=new String[] { "log","--revision",id };
	
	// Start the inferior processes
	final Process monotone=Runtime.getRuntime().exec(getCommand(command));
	new ErrorReader("monotone",monotone.getErrorStream());
	final Process dot2svg=Runtime.getRuntime().exec(new String[] { "dot","-Tsvg" });
	new ErrorReader("dot2svg",dot2svg.getErrorStream());

	final PipedOutputStream gxl2dotSourceOutputStream=new PipedOutputStream();
	final PipedInputStream gxl2dotSourceInputStream=new PipedInputStream(gxl2dotSourceOutputStream);

	// Chain the log output to the GXL generator and into the dot converter
        final String[] args;
	if(!(new File("colors.map")).exists()) args=new String[0];
	else args=new String[] { "--colorfile","colors.map" };

	log2gxl=new Log2Gxl();
	log2gxl.start(args,monotone.getInputStream(),gxl2dotSourceOutputStream);

	final PipedOutputStream gxl2dotSinkOutputStream=new PipedOutputStream();
	final PipedInputStream gxl2dotSinkInputStream=new PipedInputStream(gxl2dotSinkOutputStream);

	// Create a thread to transform the GXL semantic graph into an DOT visual graph
	final Thread transformerThread=new Thread(new Runnable() { public void run() {
	    try {
		TransformerFactory factory=TransformerFactory.newInstance();
		factory.setURIResolver(new InternalURIResolver());
		Transformer transformer=factory.newTransformer(new StreamSource(ClassLoader.getSystemResourceAsStream("gxl2dot.xsl")));
		transformer.setParameter("HIGHLIGHT",highlight.toString());
		transformer.transform(new StreamSource(gxl2dotSourceInputStream),new StreamResult(gxl2dotSinkOutputStream));
	    }
	    catch(Exception e) {
		e.printStackTrace();
	    }
	}});
	transformerThread.setDaemon(true);
	transformerThread.start();
    
	// Chain the dot graph to the svg generator
	new StreamCopier("gxl2dot -> dot2svg",gxl2dotSinkInputStream,new BufferedOutputStream(dot2svg.getOutputStream()),true);
	return new BufferedInputStream(dot2svg.getInputStream());
      }

    /**
     * Run monotone returning its output as a string list. This method should only be used for 
     * monotone commands which produce a small amount of output
     * 
     * @param command the monotone sub-command to execute, e.g. "list branches"
     * @return a string list containing the output lines from monotone's stdout
     */
    public List<String> runMonotone(String[] command) throws IOException {
	List<String> results=new ArrayList<String>();
	LineNumberReader source=null;
	try {
	    Process monotone=Runtime.getRuntime().exec(getCommand(command));
	    new ErrorReader("monotone",monotone.getErrorStream());
	    source=new LineNumberReader(new InputStreamReader(monotone.getInputStream()));
	    
	    String line;
	    while((line=source.readLine())!=null) {
		results.add(line);
	    }
	    try {
		// Wait for the monotone process to exit
		monotone.waitFor();
	    }
	    catch(InterruptedException ie) {
		logger.throwing(this.getClass().getName(),"run",ie);
	    }
	}
	finally {
	    if(source!=null) source.close();
	}
	return Collections.unmodifiableList(results);
    }
	
    /**
     * Thread which reads from a stream and stores the output in a list, one line per entry
     */
    private class ErrorReader extends Thread {
	private LineNumberReader source;
	private List<String> errors=new ArrayList<String>();

	/**
	 * Return any error messages collected by this thread
	 * @return a list of strings, one line per entry
	 */
	public List<String> getErrors() { 
	    return Collections.unmodifiableList(errors);
	}

	/**
	 * Create and start a new error reader thread
	 *
	 * @param stream the stream to read from
	 */
	public ErrorReader(String name,InputStream stream) {
	    super(name);
	    source=new LineNumberReader(new InputStreamReader(stream));
	    setDaemon(true);
	    start();
	}
	
	/**
	 * Thread body. Copy from the input stream into the error list one line at a time
	 */
	public void run() {
	    try {
		String line;
		while((line=source.readLine())!=null) {
		    errors.add(line);
		    logger.warning(line);
		}
	    }
	    catch(IOException ioe) {
		    logger.throwing(this.getClass().getName(),"run",ioe);
	    }
	    finally {
		try {
		    if(source!=null) source.close();
		}
		catch(IOException ioe2) {
		    logger.throwing(this.getClass().getName(),"run",ioe2);
		}
	    }
	}
    }
    
    /**
     * Thread which copies one stream to another
     */
    private class StreamCopier extends Thread {

	/**
	 * Source stream
	 */
	private InputStream source;

	/**
	 * Destination stream
	 */
	private OutputStream sink;

	/**
	 * If true, close the sink when the source runs dry
	 */
	private boolean closeSink;

	/**
	 * Start a new thread to copy from the source stream to the sink
	 *
	 * @param name the name of the thread
	 * @param source the input source
	 * @param sink the output destination
	 * @param closeSink if true, close the sink when the input runs dry
	 */
	public StreamCopier(String name,InputStream source,OutputStream sink,boolean closeSink) {
	    super(name);
	    this.source=source;
	    this.sink=sink;
	    this.closeSink=closeSink;
	    setDaemon(true);
	    start();
	}

	/**
	 * Thread body. Copy single bytes from the source stream to the sink until
	 * the end of the source stream is reached
	 */
	public void run() {
	    try {
		int data;
		while((data=source.read())!=-1) {
		    sink.write(data);
		}
	    }
	    catch(IOException ioe) {
		logger.throwing(this.getClass().getName(),"run",ioe);
	    }
	    finally {
		try {
		    sink.flush();
		    if(closeSink) sink.close(); 
		}
		catch(IOException ioe) {
		    logger.throwing(this.getClass().getName(),"run",ioe);
		}
	    }
	}
    }
}

class InternalURIResolver implements URIResolver {
 
    /**
     * Log sink
     */
    private static Logger logger=Logger.getLogger("Monotone");
   
    public Source resolve(String href,String base) {
	logger.info("URI is "+href);
	if(href.equals("http://www.gupro.de/GXL/gxl-1.0.dtd")) {
	    return new StreamSource(ClassLoader.getSystemResourceAsStream("gxl-1.0.dtd"));
	}
	return null;
    }
}