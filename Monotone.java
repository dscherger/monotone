
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
    }
    
    /**
     * Construct the basic monotone command specifying the database
     *
     * @return the base monotone command
     */
    public String getBaseCommand() {
	return "monotone \"--db="+database+"\" ";
    }

    /**
     * List all the branches in the current monotone database
     *
     * @return a list of strings which enumerates the branches in the current monotone database
     */
    public List<String> listBranches() throws IOException {
	List<String> result=runMonotone("list branches");
	return result;
    }

    /**
     * List all the heads of a specific branch in the current monotone database
     *
     * @param branch the name of the branch
     * @return a list of strings which enumerates the heads of the specified branch in the current monotone database
     */
    public List<String> listHeads(String branch) throws IOException {
	List<String>result=runMonotone("heads --branch \""+branch+"\"");
	return result;
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
     * Run monotone and get an SVG stream from a log 
     *
     * @param id the identifier for which the log should be generated
     * @return a stream from which an SVG format graph may be read
     */
    public InputStream getSVGLog(String id) throws IOException {
	String command="log "+id;
	
	// Start the inferior processes
	Process monotone=Runtime.getRuntime().exec(getBaseCommand()+command);
	new ErrorReader("monotone",monotone.getErrorStream());
	Process gxl2dot=Runtime.getRuntime().exec("gxl2dot -d");
	new ErrorReader("gxl2dot",gxl2dot.getErrorStream());
	Process dot2svg=Runtime.getRuntime().exec("dot -Tsvg");
	new ErrorReader("dot2svg",dot2svg.getErrorStream());

	// Chain the log output to the GXL generator and into the dot converter
	String[] args=new String[] { "--authorfile","authors.map" };
	if(!(new File("authors.map")).exists()) args=new String[0];
	new Log2Gxl().start(args,monotone.getInputStream(),new BufferedOutputStream(gxl2dot.getOutputStream()));

	// Chain the dot graph to the svg generator
	new StreamCopier("gxl2dot -> dot2svg",new BufferedInputStream(gxl2dot.getInputStream()),new BufferedOutputStream(dot2svg.getOutputStream()),true);
	return new BufferedInputStream(dot2svg.getInputStream());
      }

    /**
     * Run monotone returning its output as a string list. This method should only be used for 
     * monotone commands which produce a small amount of output
     * 
     * @param command the monotone sub-command to execute, e.g. "list branches"
     * @return a string list containing the output lines from monotone's stdout
     */
    public List<String> runMonotone(String command) throws IOException {
	List<String> results=new ArrayList<String>();
	LineNumberReader source=null;
	try {
	    Process monotone=Runtime.getRuntime().exec(getBaseCommand()+command);
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