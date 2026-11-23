/******************************************************************************
 * @file            Character.java
 *****************************************************************************/
package     java.lang;

public final class Character extends Object {

    /**
     * The minimum radix available for conversion to and from Strings.  
     * The lowest minimum value that a radix can be is 2.
     */
    public static final int MIN_RADIX = 2;
    
    /**
     * The maximum radix available for conversion to and from Strings.  The
     * largest maximum value that a radix can have is 36.
     */
    public static final int MAX_RADIX = 36;
    
    /* Case folding translation table upper => lower. */
    static char downCase[];
    
    /* Case folding translation table lower => upper. */
    static char upCase[];
    
    static {
    
        char down[] = new char[256];
        char up[] = new char[256];
        
        for (int i = 0 ; i < 256 ; i++) {
            down[i] = up[i] = (char) i;
        }
        
        for (int lower = 'a' ; lower <= 'z' ; lower++) {
        
            int upper = (lower + ('A' - 'a'));
            
            up[lower] = (char) upper;
            down[upper] = (char) lower;
        
        }

        for (int lower = 0xE0; lower <= 0xFE; lower++) {
        
            if (lower != 0xF7) {        // multiply and divide
            
                int upper = (lower + ('A' - 'a'));
                
                up[lower] = (char) upper;
                down[upper] = (char) lower;
            
            }
        
        }
        
        downCase = down;
        upCase = up;
    
    }
    
    /**
     * Returns the numeric value of the character digit using the
     * specified radix. If the character is not a valid digit, it
     * returns -1.
     */
    public static int digit (char ch, int radix) {
    
        if (radix >= MIN_RADIX && radix <= MAX_RADIX) {
        
            if (radix <= 10) {
            
                char max = (char) ('0' + radix - 1);
                
                if ((ch >= '0') && (ch <= max)) {
                    return (ch - '0');
                }
            
            } else {
            
                ch = toLowerCase (ch);
                
                if ((ch >= '0') && (ch <= '9')) {
                    return (ch - '0');
                } 
                
                char max = (char) ('a' + radix - 11);
                
                if (ch >= 'a' && ch <= max) {
                    return (10 + ch - 'a');
                }
            
            }
        
        }
        
        return -1;
    
    }
    
    /**
     * Returns the character value for the specified digit in the
     * specified radix. If the digit is not valid in the radix,
     * the 0 character is returned.
     */
    public static char forDigit (int digit, int radix) {
    
        if ((digit >= radix) || (digit < 0)) {
            return '\0';
        }
        
        if ((radix < MIN_RADIX) || (radix > MAX_RADIX)) {
            return '\0';
        }
        
        if (digit < 10) {
            return (char) ('0' + digit);
        }
        
        return (char) ('a' + digit - 10);
    
    }
    
    /* Determines if the specified character is a ISO-LATIN-1 digit. */
    public static boolean isDigit (char ch) {
        return (ch >= '0') && (ch <= '9');
    }
    
    /* Determines if the specified character is ISO-LATIN-1 lower case. */
    public static boolean isLowerCase (char ch) {
        return ((ch <= '\u00FF') && ((upCase[ch] != ch) || (ch == '\u00df') || (ch == '\u00ff')));
    }
    
    /**
     * Determines if the specified character is ISO-LATIN-1 white space
     * according to Java.
     */
    public static boolean isSpace (char ch) {
    
        switch (ch) {
        
            case ' ':   case '\t':  case '\f':  case '\n':  case '\r':
            
                return true;
        
        }
        
        return false;
    
    }
    
    /* Determines if the specified character is ISO-LATIN-1 upper case. */
    public static boolean isUpperCase (char ch) {
        return (ch <= '\u00FF' && downCase[ch] != ch);
    }
    
    /**
     * Returns the lower case character value of the specified
     * ISO-LATIN-1 character. Characters that are not upper case
     * letters are returned unmodified.
     */
    public static char toLowerCase (char ch) {
        return (ch <= '\u00FF') ? downCase[ch] : ch;
    }
    
    /**
     * Returns the upper case character value of the specified
     * ISO-LATIN-1 character.  Characters that are not lower case
     * letters are returned unmodified.
     */
    public static char toUpperCase (char ch) {
        return (ch <= '\u00FF') ? upCase[ch] : ch;
    }
    
    /* The value of the Character. */
    private char value;
    
    /* Constructs a Character object with the specified value. */
    public Character (char value) {
        this.value = value;
    }
    
    /* Returns the value of this Character object. */
    public char charValue () {
        return value;
    }
    
    /* Compares this object against the specified object. */
    public boolean equals (Object obj) {
    
        if (obj != null && (obj instanceof Character)) {
            return value == ((Character) obj).charValue ();
        }
        
        return false;
    
    }
    
    /* Returns a hashcode for this Character. */
    public int hashCode () {
        return (int) value;
    }
    
    /* Returns a String object representing this character's value. */
    public String toString () {
    
        char buf[] = { value };
        return String.valueOf (buf);
    
    }

}
