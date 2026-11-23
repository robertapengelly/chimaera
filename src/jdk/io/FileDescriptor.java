/******************************************************************************
 * @file            FileDescriptor.java
 *****************************************************************************/
package     java.io;

public final class FileDescriptor {

    /* This routine initializes in, out and err in a sytem dependent way. */
    private static native FileDescriptor initSystemFD (FileDescriptor fdObj, int desc);
    
    /* Handle to standard input. */
    public static final FileDescriptor in = initSystemFD (new FileDescriptor (), 0);
    
    /* Handle to standard output. */
    public static final FileDescriptor out = initSystemFD (new FileDescriptor (), 1);
    
    /* Handle to standard error. */
    public static final FileDescriptor err = initSystemFD (new FileDescriptor (), 2);

}
