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

public class Monotone {
    private File database;
    
    public Monotone(File database) {
	this.database=database;
    }
    
    public String getBaseCommand() {
	return "monotone \"--db="+database+"\" ";
    }

    public List<String> listBranches() throws IOException {
	List<String> result=runMonotone("list branches");
	return result;
    }

    public List<String> listHeads(String branch) throws IOException {
	List<String>result=runMonotone("heads --branch \""+branch+"\"");
	return result;
    }

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
	Process monotone=Runtime.getRuntime().exec(getBaseCommand()+command);
	new ErrorReader(monotone.getErrorStream()).start();
	Process gxl2dot=Runtime.getRuntime().exec("gxl2dot -d");
	new ErrorReader(gxl2dot.getErrorStream()).start();
	Process dot2svg=Runtime.getRuntime().exec("dot -Tsvg");
	new ErrorReader(dot2svg.getErrorStream()).start();

	new Log2Gxl().start(new String[] { "--authorfile","authors.map" },monotone.getInputStream(),gxl2dot.getOutputStream());

	new StreamCopier(new BufferedInputStream(gxl2dot.getInputStream()),new BufferedOutputStream(dot2svg.getOutputStream()));
	

        return dot2svg.getInputStream();
      }

    /**
     * Run monotone returning its output as a string array. This method should only be used for 
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
	    new ErrorReader(monotone.getErrorStream()).start();
	    source=new LineNumberReader(new InputStreamReader(monotone.getInputStream()));
	    
	    String line;
	    while((line=source.readLine())!=null) {
		results.add(line);
	    }
	    try {
		monotone.waitFor();
	    }
	    catch(InterruptedException ie) {
		// No action required
	    }
	}
	finally {
	    if(source!=null) source.close();
	}
	return Collections.unmodifiableList(results);
    }
	

    private class ErrorReader extends Thread {
	private LineNumberReader source;
	private List<String> errors=new ArrayList<String>();
	
	public List<String> getErrors() { 
	    return Collections.unmodifiableList(errors);
	}

	public ErrorReader(InputStream stream) {
	    source=new LineNumberReader(new InputStreamReader(stream));
	}
	
	public void run() {
	    try {
		String line;
		while((line=source.readLine())!=null) {
		    errors.add(line);
		    System.err.println(line);
		}
	    }
	    catch(IOException ioe) {
		// No action required
	    }
	    finally {
		try {
		    if(source!=null) source.close();
		}
		catch(IOException ioe2) {
		// No action required
		}
	    }
	}
    }
    
    private class StreamCopier extends Thread {
	private InputStream source;
	private OutputStream sink;

	public StreamCopier(InputStream source,OutputStream sink) {
	    this.source=source;
	    this.sink=sink;
	    start();
	}

	public void run() {
	    try {
		int data;
		while((data=source.read())!=-1) sink.write(data);
	    }
	    catch(IOException ioe) {
		// Nothing to be done
	    }
	    finally {
		try {
		    sink.flush();
		}
		catch(IOException ioe) {
		    // Ignore
		}
	    }
	}
    }
}