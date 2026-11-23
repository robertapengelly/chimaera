/******************************************************************************
 * @file            FilterOutputStream.java
 *****************************************************************************/
package     java.io;

public class FilterOutputStream extends OutputStream {

    /* The actual output stream. */
    protected OutputStream out;
    
    /* Creates an output stream filter. */
    public FilterOutputStream (OutputStream out) {
        this.out = out;
    }
    
    /**
     * Closes the stream. This method must be called to release any
     * resources associated with the stream.
     */
    @Override
    public void close () throws IOException {
    
        flush ();
        out.close ();
    
    }
    
    /* Flushes the stream. This will write any buffered output bytes. */
    @Override
    public void flush () throws IOException {
        out.flush ();
    }
    
    /** Writes a byte. Will block until the byte is actually written. */
    @Override
    public void write (int b) throws IOException {
        out.write (b);
    }
    
    /**
     * Writes an array of bytes. Will block until the bytes
     * are actually written.
     */
    @Override
    public void write (byte b[]) throws IOException {
        write (b, 0, b.length);
    }
    
    /**
     * Writes a subarray of bytes. To be efficient it should
     * be overridden in a subclass. 
     */
    @Override
    public void write (byte b[], int off, int len) throws IOException {
    
        for (int i = 0; i < len; i++) {
            out.write (b[off + i]);
        }
    
    }

}
