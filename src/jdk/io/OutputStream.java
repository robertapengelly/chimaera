/******************************************************************************
 * @file            OutputStream.java
 *****************************************************************************/
package     java.io;

public abstract class OutputStream {

    /**
     * Closes the stream. This method must be called to release any
     * resources associated with the stream.
     */
    public void close () throws IOException {}
    
    /* Flushes the stream. This will write any buffered output bytes. */
    public void flush () throws IOException {}
    
    /**
     * Writes a byte. This method will block until the byte is
     * actually written.
     */
    public abstract void write (int b) throws IOException;
    
    /**
     * Writes an array of bytes. This method will block until the bytes
     * are actually written.
     */
    public void write (byte b[]) throws IOException {
        write (b, 0, b.length);
    }
    
    /* Writes a sub array of bytes. */
    public void write (byte b[], int off, int len) throws IOException {
    
        for (int i = 0; i < len; i++) {
            write (b[off + i]);
        }
    
    }

}
