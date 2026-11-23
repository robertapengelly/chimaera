/******************************************************************************
 * @file            Integer.java
 *****************************************************************************/
package     java.lang;

public final class Integer extends Number {

    /**
     * The minimum value an Integer can have.  The lowest minimum value an
     * Integer can have is 0x80000000.
     */
    public static final int MIN_VALUE = 0x80000000;
    
    /**
     * The maximum value an Integer can have.  The greatest maximum value an
     * Integer can have is 0x7fffffff.
     */
    public static final int MAX_VALUE = 0x7fffffff;
    
    /* The value of the Integer. */
    private int value;
    
    /* Constructs an Integer object initialized to the specified int value. */
    public Integer (int value) {
        this.value = value;
    }
    
    /**
     * Constructs an Integer object initialized to the value specified by
     * the String parameter.  The radix is assumed to be 10.
     */
    public Integer (String s) throws NumberFormatException {
        this.value = parseInt (s, 10);
    }
    
    /* Returns the value of this Integer as a double. */
    public double doubleValue () {
        return (double) value;
    }
    
    /* Returns the value of this Integer as a float. */
    public float floatValue () {
        return (float) value;
    }
    
    /* Returns the value of this Integer as an int. */
    public int intValue () {
        return value;
    }
    
    /* Returns the value of this Integer as a long. */
    public long longValue () {
        return (long) value;
    }
    
    /**
     * Assuming the specified String represents an integer, returns a new
     * Integer object initialized to that value. Throws an exception if
     * the String cannot be parsed as an int.
     */
    public static int parseInt (String s) throws NumberFormatException {
        return parseInt (s, 10);
    }
    
    /**
     * Assuming the specified String represents an integer, returns that
     * integer's value. Throws an exception if the String cannot be
     * parsed as an int.
     */
    public static int parseInt (String s, int radix) throws NumberFormatException {
    
        if (s == null) {
            throw new NumberFormatException ("null");
        }
        
        int result = 0;
        
        boolean negative = false;
        int i = 0, max = s.length ();
        
        if (max > 0) {
        
            if (s.charAt (0) == '-') {
            
                negative = true;
                i++;
            
            }
            
            while (i < max) {
            
                int digit = Character.digit (s.charAt (i++),radix);
                
                if (digit < 0) {
                    throw new NumberFormatException (s);
                }
                
                result = result * radix + digit;
            
            }
        
        } else {
            throw new NumberFormatException (s);
        }
        
        if (negative) {
            return -result;
        } else {
            return result;
        }
    
    }
    
    /* Compares this object to the specified object. */
    public boolean equals (Object obj) {
    
        if ((obj != null) && (obj instanceof Integer)) {
            return value == ((Integer) obj).intValue ();
        }
        
        return false;
    
    }
    
    /**
     * Gets an Integer property. If the property does not
     * exist, it will return 0.
     */
    public static Integer getInteger (String nm) {
        return getInteger (nm, null);
    }
    
    /**
     * Gets an Integer property. If the property does not exist, it will
     * return val. Deals with Hexadecimal and octal numbers.
     */
    public static Integer getInteger (String nm, int val) {
    
        Integer result = getInteger (nm, null);
        return (result == null) ? new Integer (val) : result;
    
    }
    
    /**
     * Gets an Integer property. If the property does not exist, it will
     * return val. Deals with Hexadecimal and octal numbers.
     */
    public static Integer getInteger (String nm, Integer val) {
    
        String v = System.getProperty (nm);
        
        if (v != null) {
        
            try {
            
                if (v.startsWith ("0x")) {
                    return Integer.valueOf (v.substring (2), 16);
                }
                
                if (v.startsWith ("#")) {
                    return Integer.valueOf (v.substring (1), 16);
                }
                
                if (v.startsWith ("0")) {
                    return Integer.valueOf (v.substring (1), 8);
                }
                
                return Integer.valueOf (v);
            
            } catch (NumberFormatException e) {}
        
        }
        
        return val;
    
    }
    
    /* Returns a hashcode for this Integer. */
    public int hashCode () {
        return value;
    }
    
    /* Returns a String object representing this Integer's value. */
    public String toString () {
        return String.valueOf (value);
    }
    
    /**
     * Returns a new String object representing the specified integer.
     * The radix is assumed to be 10.
     */
    public static String toString (int i) {
        return toString (i, 10);
    }
    
    /**
     * Returns a new String object representing the specified integer in
     * the specified radix.
     */
    public static String toString (int i, int radix) {
    
        StringBuffer buf;
        int digitCount = 0;
        
        boolean minval = false;
        boolean negative = false;
        
        if (i == 0x80000000) {
        
            switch (radix) {
            
                case 2:
                
                    return "-10000000000000000000000000000000";
                
                case 4:
                
                    return "-2000000000000000";
                
                case 8:
                
                    return "-20000000000";
                
                case 16:
                
                    return "-80000000";
                
                default: 
                
                    minval = negative = true;
                    i = MAX_VALUE;
            
            }
        
        } else if (i < 0) {
        
            i = -i;
            negative = true;
        
        }
        
        buf = new StringBuffer (32);
        
        while (i > 0) {
        
            if (i < radix) {
            
                buf.append (Character.forDigit (i,radix));
                i = 0;
            
            } else {
            
                int j = i % radix;
                i = i / radix;
                
                buf.append (Character.forDigit (j,radix));
            
            }
            
            digitCount++;
        
        }
        
        if (digitCount > 0) {
        
            int j = buf.length ();
            int k = 0;
            
            char tmp[];
            
            if (negative) {
            
                tmp = new char[j + 1];
                
                tmp[0] = '-';
                k = 1;
            
            } else {
                tmp = new char[j];
            }
            
            i = 0;
            
            while (j-- > 0) {
                tmp[j + k] = buf.charAt (i++);
            }
            
            if (minval) {
                tmp[tmp.length-1]++;
            }
            
            return String.valueOf (tmp);
        
        } else {
            return "0";
        }
    
    }
    
    /**
     * Assuming the specified String represents an integer, returns a new
     * Integer object initialized to that value. Throws an exception if
     * the String cannot be parsed as an int. The radix is assumed to
     * be 10.
     */
    public static Integer valueOf (String s) throws NumberFormatException {
        return new Integer (parseInt (s, 10));
    }
    
    public static Integer valueOf (String s, int radix) throws NumberFormatException {
        return new Integer (parseInt (s, radix));
    }

}
