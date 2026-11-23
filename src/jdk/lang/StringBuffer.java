/******************************************************************************
 * @file            StringBuffer.java
 *****************************************************************************/
package     java.lang;

public final class StringBuffer {

    /* The value is used for character storage. */
    private char value[];
    
    /* A flag indicating whether the buffer is shared */
    private boolean shared;
    
    /* The count is the number of characters in the buffer. */
    private int count;
    
    /* Constructs an empty String buffer. */
    public StringBuffer () {
        this (16);
    }
    
    /**
     * Constructs an empty String buffer with the specified
     * initial length.
     */
    public StringBuffer (int length) {
    
        value = new char[length];
        shared = false;
    
    }
    
    /* Constructs a String buffer with the specified initial value. */
    public StringBuffer (String s) {
        this (s.length () + 16);
        
        append (s);
    
    }
    
    /* Appends a boolean to the end of this buffer. */
    public StringBuffer append (boolean b) {
        return append (String.valueOf (b));
    }
    
    /* Appends a character to the end of this buffer. */
    public synchronized StringBuffer append (char c) {
    
        ensureCapacity (count + 1);
        copyWhenShared ();
        
        value[count++] = c;
        return this;
    
    }
    
    /* Appends an array of characters to the end of this buffer. */
    public synchronized StringBuffer append (char s[]) {
    
        int len = s.length;
        
        ensureCapacity (count + len);
        copyWhenShared ();
        
        System.arraycopy (s, 0, value, count, len);
        count += len;
        
        return this;
    
    }
    
    /**
     * Appends a part of an array of characters to the end of
     * this buffer.
     */
    public synchronized StringBuffer append (char s[], int offset, int len) {
    
        ensureCapacity (count + len);
        copyWhenShared ();
        
        System.arraycopy (s, offset, value, count, len);
        count += len;
        
        return this;
    
    }
    
    /* Appends a double to the end of this buffer. */
    public StringBuffer append (double d) {
        return append (String.valueOf (d));
    }
    
    /* Appends a float to the end of this buffer. */
    public StringBuffer append (float f) {
        return append (String.valueOf (f));
    }
    
    /* Appends an integer to the end of this buffer. */
    public StringBuffer append (int i) {
        return append (String.valueOf (i));
    }
    
    /* Appends a long to the end of this buffer. */
    public StringBuffer append (long l) {
        return append (String.valueOf (l));
    }
    
    /* Appends an object to the end of this buffer. */
    public synchronized StringBuffer append (Object obj) {
        return append (String.valueOf (obj));
    }
    
    /* Appends a String to the end of this buffer. */
    public synchronized StringBuffer append (String s) {
    
        if (s == null) {
            s = String.valueOf (s);
        }
        
        int len = s.length ();
        
        ensureCapacity (count + len);
        copyWhenShared ();
        
        s.getChars (0, len, value, count);
        count += len;
        
        return this;
    
    }
    
    /**
     * Returns the character at the specified index. An index ranges
     * from 0..length()-1.
     */
    public synchronized char charAt (int index) {
    
        if ((index < 0) || (index >= count)) {
            throw new StringIndexOutOfBoundsException (index);
        }
        
        return value[index];
    
    }
    
    /**
     * Returns the current capacity of the String buffer. The capacity
     * is the amount of storage available for newly inserted
     * characters; beyond which an allocation will occur.
     */
    public int capacity () {
        return value.length;
    }
    
    /* Copies the buffer value if it is shared. */
    private void copyWhenShared () {
    
        if (shared) {
        
            char newValue[] = new char[value.length];
            System.arraycopy (value, 0, newValue, 0, count);
            
            value = newValue;
            shared = false;
        
        }
    
    }
    
    /**
     * Ensures that the capacity of the buffer is at least equal to the
     * specified minimum.
     */
    public synchronized void ensureCapacity (int minimumCapacity) {
    
        int maxCapacity = value.length;
        
        if (minimumCapacity > maxCapacity) {
        
            int newCapacity = (maxCapacity + 1) * 2;
            
            if (minimumCapacity > newCapacity) {
                newCapacity = minimumCapacity;
            }
            
            char newValue[] = new char[newCapacity];
            System.arraycopy (value, 0, newValue, 0, count);
            
            value = newValue;
            shared = false;
        
        }
    
    }
    
    /**
     * Copies the characters of the specified substring (determined by
     * srcBegin and srcEnd) into the character array, starting at the
     * array's dstBegin location. Both srcBegin and srcEnd must be legal
     * indexes into the buffer.
     */
    public synchronized void getChars (int srcBegin, int srcEnd, char dst[], int dstBegin) {
    
        if ((srcBegin < 0) || (srcBegin >= count)) {
            throw new StringIndexOutOfBoundsException (srcBegin);
        }
        
        if ((srcEnd < 0) || (srcEnd > count)) {
            throw new StringIndexOutOfBoundsException (srcEnd);
        }
        
        if (srcBegin < srcEnd) {
            System.arraycopy (value, srcBegin, dst, dstBegin, srcEnd - srcBegin);
        }
    
    }
    
    /* Inserts a boolean into the String buffer. */
    public StringBuffer insert (int offset, boolean b) {
        return insert (offset, String.valueOf (b));
    }
    
    /* Inserts a character into the String buffer. */
    public synchronized StringBuffer insert (int offset, char c) {
    
        ensureCapacity (count + 1);
        copyWhenShared ();
        
        System.arraycopy (value, offset, value, offset + 1, count - offset);
        value[offset] = c;
        
        count += 1;
        return this;
    
    }
    
    /* Inserts an array of characters into the String buffer. */
    public synchronized StringBuffer insert (int offset, char s[]) {
    
        if ((offset < 0) || (offset > count)) {
            throw new StringIndexOutOfBoundsException ();
        }
        
        int len = s.length;
        
        ensureCapacity (count + len);
        copyWhenShared ();
        
        System.arraycopy (value, offset, value, offset + len, count - offset);
        System.arraycopy (s, 0, value, offset, len);
        
        count += len;
        return this;
    
    }
    
    /* Inserts a double into the String buffer. */
    public StringBuffer insert (int offset, double d) {
        return insert (offset, String.valueOf (d));
    }
    
    /* Inserts a float into the String buffer. */
    public StringBuffer insert (int offset, float f) {
        return insert (offset, String.valueOf (f));
    }
    
    /* Inserts an integer into the String buffer. */
    public StringBuffer insert (int offset, int i) {
        return insert (offset, String.valueOf (i));
    }
    
    /* Inserts a long into the String buffer. */
    public StringBuffer insert (int offset, long l) {
        return insert (offset, String.valueOf (l));
    }
    
    /* Inserts an object into the String buffer. */
    public synchronized StringBuffer insert (int offset, Object obj) {
        return insert (offset, String.valueOf (obj));
    }
    
    /* Inserts a String into the String buffer. */
    public synchronized StringBuffer insert (int offset, String s) {
    
        if ((offset < 0) || (offset > count)) {
            throw new StringIndexOutOfBoundsException ();
        }
        
        int len = s.length ();
        
        ensureCapacity (count + len);
        copyWhenShared ();
        
        System.arraycopy (value, offset, value, offset + len, count - offset);
        s.getChars (0, len, value, offset);
        
        count += len;
        return this;
    
    }
    
    /* Returns the length (character count) of the buffer. */
    public int length () {
        return count;
    }
    
    /* Changes the character at the specified index to be ch. */
    public synchronized void setCharAt (int index, char ch) {
    
        if ((index < 0) || (index >= count)) {
            throw new StringIndexOutOfBoundsException (index);
        }
        
        copyWhenShared ();
        value[index] = ch;
    
    }
    
    /**
     * Sets the length of the String. If the length is reduced,
     * characters are lost. If the length is extended, the
     * values of the new characters are set to 0.
     */
    public synchronized void setLength (int newLength) {
    
        if (newLength < 0) {
            throw new StringIndexOutOfBoundsException (newLength);
        }
        
        ensureCapacity (newLength);
        
        if (count < newLength) {
        
            copyWhenShared ();
            
            for (; count < newLength; count++) {
                value[count] = '\0';
            }
        
        }
        
        count = newLength;
    
    }
    
    /* Converts to a String representing the data in the buffer. */
    public String toString () {
        return new String (this);
    }
    
    /**
     * The following two methods are needed by String to efficiently
     * convert a StringBuffer into a String.  They are not public.
     * They shouldn't be called by anyone but String.
     */
    char[] getValue () {
        return value;
    }
    
    void setShared () {
        shared = true;
    } 

}
