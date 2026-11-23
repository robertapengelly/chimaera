/******************************************************************************
 * @file            Class.java
 *****************************************************************************/
package     java.lang;

public final class Class {

    /* Make sure nobody instantiates this class. */
    private Class () {}
    
    /**
     * Returns the runtime Class descriptor for the specified Class.
     * For example, the following code fragment returns the runtime
     * Class descriptor for the Class named java.lang.Thread:
     * <pre>
     *      Class t = Class.forName("java.lang.Thread")
     * </pre>
     */
    public static native Class forName (String className) throws ClassNotFoundException;
    
    /* Creates a new instance of this Class. */
    public native Object newInstance () throws InstantiationException, IllegalAccessException;
    
    /* Returns the superclass of this Class. */
    public native Class getSuperclass ();
    
    /**
     * Returns the interfaces of this Class. An array of length 0 is
     * returned if this Class implements no interfaces.
     */
    public native Class getInterfaces ();
    
    /**
     * Returns the Class loader of this Class.  Returns null if this
     * Class does not have a Class loader.
     */
    public native ClassLoader getClassLoader ();
    
    /* Returns the name of this Class. */
    public native String getName ();
    
    /* Returns a boolean indicating whether or not this Class is an interface. */
    public native boolean isInterface ();
    
    /**
     * Returns the name of this class or interface. The word "class"
     * is prepended if it is a Class; the word "interface" is
     * prepended if it is an interface.
     */
    public String toString () {
        return (isInterface () ? "interface " : "class ") + getName ();
    }

}
