/******************************************************************************
 * @file            System.java
 *****************************************************************************/
package     java.lang;

import      java.io.PrintStream;

public final class System {

    public static PrintStream in;
    public static PrintStream out;
    public static PrintStream err;
    
    private System () {}
    
    public static native void arraycopy (Object src, int src_position, Object dst, int dst_position, int length);
    public static native long currentTimeMillis ();
    
    public static String getProperty (String key) {}

}
