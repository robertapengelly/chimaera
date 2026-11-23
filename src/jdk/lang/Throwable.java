/******************************************************************************
 * @file            Throwable.java
 *****************************************************************************/
package     java.lang;

import      java.io.PrintStream;

public class Throwable {

    private Object backtrace;
    private String detailMessage;
    
    public Throwable () {
        fillInStackTrace ();
    }
    
    public Throwable (String message) {
    
        fillInStackTrace ();
        detailMessage = message;
    
    }
    
    public String getMessage () {
        return detailMessage;
    }
    
    public String toString () {
    
        String s = getClass ().getName ();
        String msg = getMessage ();
        
        return (msg != null) ? (s + ": " + msg) : s;
    
    }
    
    public void printStackTrace () {
    
        System.err.println (this);
        printStackTrace (System.err);
    
    }
    
    public void printStackTrace (PrintStream s) {
    
        s.println (this);
        printStackTrace (s);
    
    }
    
    private native void printStackTrace (PrintStream s);
    public native Throwable fileInStackTrace ();

}
