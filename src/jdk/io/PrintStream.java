/******************************************************************************
 * @file            PrintStream.java
 *****************************************************************************/
package     java.io;

public class PrintStream extends FilterOutputStream {

    private boolean autoflush;
    private boolean trouble;
    
    /* Creates a new PrintStream. */
    public PrintStream (OutputStream out) {
        this (out, false);
        
        trouble = false;
    
    }
    
    /* Creates a new PrintStream, with auto flushing. */
    public PrintStream (OutputStream out, boolean autoflush) {
        super (out);
        
        this.autoflush = autoflush;
        trouble = false;
    
    }
    
    /**
     * Flushes the print stream and returns whether or not there was
     * an error on the output stream.  Errors are cumulative; once the
     * print stream encounters an error this routine will continue to
     * return true on all successive calls.
     */
    public boolean checkError () {
    
        flush ();
        return trouble;
    
    }
    
    /* Closes the stream. */
    public void close () {
    
        try {
            super.close ();
        } catch (IOException ex) {
            trouble = true;
        }
    
    }
    
    /* Flushes the stream. This will write any buffered output bytes. */
    public void flush () {
    
        try {
            super.flush ();
        } catch (IOException ex) {
            trouble = true;
        }
    
    }
    
    /* Prints a boolean. */
    public void print (boolean b) {
        print (b ? "true" : "false");
    }
    
    /* Prints an character. */
    public void print (char c) {
        print (String.valueOf (c));
    }
    
    /* Prints an array of characters. */
    synchronized public void print (char s[]) {
    
        for (int i = 0; i < s.length; i++) {
            write (s[i]);
        }
    
    }
    
    /* Prints a double. */
    public void print (double d) {
        print (String.valueOf (d));
    }
    
    /* Prints a float. */
    public void print (float f) {
        print (String.valueOf (f));
    }
    
    /* Prints an integer. */
    public void print (int i) {
        print (String.valueOf (i));
    }
    
    /* Prints a long. */
    public void print (long l) {
        print (String.valueOf (l));
    }
    
    /* Prints an object. */
    public void print (Object obj) {
        print (String.valueOf (obj));
    }
    
    /* Prints a String. */
    synchronized public void print (String s) {
    
        if (s == null) {
            s = "null";
        }
        
        int len = s.length ();
        
        for (int i = 0; i < len; i++) {
            write (s.charAt (i));
        }
    
    }
    
    /* Prints a newline. */
    public void println () {
        write ('\n');
    }
    
    /* Prints a boolean followed by a newline. */
    synchronized public void println (boolean b) {
    
        print (b);
        write ('\n');
    
    }
    
    /* Prints a character followed by a newline. */
    synchronized public void println (char c) {
    
        print (c);
        write ('\n');
    
    }
    
    /* Prints an array of characters followed by a newline. */
    synchronized public void println (char s[]) {
    
        print (s);
        write ('\n');
    
    }
    
    /* Prints a double followed by a newline. */
    synchronized public void println (double d) {
    
        print (d);
        write ('\n');
    
    }
    
    /* Prints a float followed by a newline. */
    synchronized public void println (float f) {
    
        print (f);
        write ('\n');
    
    }
    
    /* Prints an integer followed by a newline. */
    synchronized public void println (int i) {
    
        print (i);
        write ('\n');
    
    }
    
    /* Prints a long followed by a newline. */
    synchronized public void println (long l) {
    
        print (l);
        write ('\n');
    
    }
    
    /* Prints an object followed by a newline. */
    synchronized public void println (Object obj) {
    
        print (obj);
        write ('\n');
    
    }
    
    /* Prints a string followed by a newline. */
    synchronized public void println (String s) {
    
        print (s);
        write ('\n');
    
    }
    
    /**
     * Writes a byte. This method will block until the byte is
     * actually written.
     */
    public void write (int b) {
    
        try {
        
            out.write (b);
            
            if (autoflush && (b == '\n')) {
                out.flush ();
            }
        
        } catch (InterruptedIOException ex) {
            Thread.currentThread ().interrupt ();
        } catch (IOException ex) {
            trouble = true;
        }
    
    }
    
    /* Writes a sub array of bytes. */
    public void write (byte b[], int off, int len) {
    
        try {
        
            out.write (b, off, len);
            
            if (autoflush && (b == '\n')) {
                out.flush ();
            }
        
        } catch (InterruptedIOException ex) {
            Thread.currentThread ().interrupt ();
        } catch (IOException ex) {
            trouble = true;
        }
    
    }

}
