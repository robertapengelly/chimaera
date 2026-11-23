/******************************************************************************
 * @file            Number.java
 *****************************************************************************/
package     java.lang;

public abstract class Number {

    /**
     * Returns the value of the number as a double.  This may involve
     * rounding if the number is not already a double.
     */
    public abstract double doubleValue ();
    
    /**
     * Returns the value of the number as a float.  This may involve
     * rounding if the number is not already a float.
    public abstract float floatValue ();
    
    /**
     * Returns the value of the number as an int.  This may involve
     * rounding if the number is not already an integer.
     */
    public abstract int intValue ();
    
    /**
     * Returns the value of the number as a long.  This may involve
     * rounding if the number is not already a long.
     */
    public abstract long longValue ();

}
