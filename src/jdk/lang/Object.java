/******************************************************************************
 * @file            Object.java
 *****************************************************************************/
package     java.lang;

public class Object {

    /**
     * Returns the Class of this Object. Java has a runtime
     * representation for classes- a descriptor of type Class- 
     * which the method getClass() returns for any Object.
     */
    public final native Class getClass ();
    
    /**
     * Returns a hashcode for this Object. Each Object in the Java
     * system has a hashcode. The hashcode is a number that is
     * usually different for different Objects. It is used when
     * storing Objects in hashtables.
     * 
     * Note: hashcodes can be negative as well as positive.
     */
    public native int hashCode ();
    
    /**
     * Compares two Objects for equality.  Returns a boolean that
     * indicates whether this Object is equivalent  to the
     * specified Object. This method is used when an Object
     * is stored in a hashtable.
     */
    public boolean equals (Object obj) {
        return this == obj;
    }
    
    /**
     * Creates a clone of the object. A new instance is allocated and a 
     * bitwise clone of the current object is place in the new object.
     */
    protected native Object clone () throws CloneNotSupportedException;
    
    /**
     * Returns a String that represents the value of this Object.  It
     * is recommended that all subclasses override this method.
     */
    public String toString () {
        return getClass ().getName () + "@" + Integer.toString (hashCode () << 1 >>> 1, 16);
    }
    
    /**
     * Notifies a single waiting thread on a change in condition of
     * another thread.  The thread effecting the change notifies the
     * waiting thread using notify(). Threads that want to wait for a
     * condition to change before proceeding can call wait().
     */
    public final native void notify ();
    
    /**
     * Notifies all of the threads waiting for a condition to change.
     * Threads that are waiting are generally waiting for another
     * thread to change some condition. Thus, the thread effecting
     * a change that more than one thread is waiting for notifies
     * all the waiting threads using the method notifyAll(). Threads
     * that want to wait for a condition to  change before proceeding
     * can call wait().
     */
    public final native void notifyAll ();
    
    /**
     * Causes a thread to wait until it is notified or the specified
     * timeout expires.
     */
    public final native void wait (long timeout) throws InterruptedException;
    
    /* More accurate wait. */
    public final void wait (long timeout, int nanos) throws InterruptedException {
    
        if (nanos >= 500000 || (nanos != 0 && timeout == 0)) {
            timeout--;
        }
        
        wait (timeout);
    
    }
    
    /* Causes a thread to wait forever until it is notified.*/
    public final void wait () throws InterruptedException {
        wait (0);
    }
    
    /**
     * Code to perform when this object is garbage collected.  
     * The default is that nothing needs to be performed.
     */
    protected void finalize () throws Throwable {}

}
