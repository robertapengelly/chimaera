/******************************************************************************
 * @file            String.java
 *****************************************************************************/
package     java.lang;

public class String {

    /* The value is used for character storage. */
    private char value[];
    
    /* The offset is the first index of the storage that is used. */
    private int offset;
    
    /* The count is the number of characters in the String. */
    private int count;
    
    /* Constructs a new empty String. */
    public String () {
        value = new char[0];
    }
    
    /**
     * Constructs a new String whose initial value is the specified
     * array of characters.
     */
    public String (char value[]) {
    
        this.count = value.length;
        this.value = new char[count];
        
        System.arraycopy (value, 0, this.value, count);
    
    }
    
    /* Constructs a new String that is a copy of the specified String. */
    public String (String value) {
    
        count = value.length ();
        
        this.value = new char[count];
        value.getChars (0, count, this.value, 0);
    
    }
    
    /**
     * Construct a new string whose value is the current contents of
     * the given string buffer
     */
    public String (StringBuffer buffer) {
    
        buffer.setShared ();
        
        this.value = buffer.getValue ();
        this.offset = 0;
        this.count = buffer.length ();
    
    }
    
    /**
     * Constructs a new String whose value is the specified array
     * of bytes.  The byte array transformed into Unicode chars
     * using hibyte as the upper byte of each character.
     */
    public String (byte ascii[], int hibyte) {
        this (ascii, hibyte, 0, ascii.length);
    }
    
    /**
     * Constructs a new String whose initial value is the specified
     * sub array of characters.  The length of the new string will
     * be count characters starting at offset within the specified
     * character array.
     */
    public String (char value[], int offset, int count) {
    
        if (offset < 0) {
            throw new StringIndexOutOfBoundsExcpetion (offset);
        }
        
        if (count < 0) {
            throw new StringIndexOutOfBoundsExcpetion (count);
        }
        
        if (offset + count > value.length) {
            throw new StringIndexOutOfBoundsExcpetion (offset + count);
        }
        
        this.value = new char[count];
        this.count = count;
        
        System.arraycopy (value, offset, this.value, count);
    
    }
    
    /**
     * Constructs a new String whose initial value is the specified
     * sub array of bytes.  The high-byte of each character can be
     * specified, it should usually be 0.  The length of the new
     * String will be count characters starting at offset within
     * the specified character array.
     */
    public String (byte ascii[], int hibyte, int offset, int count) {
    
        if (offset < 0) {
            throw new StringIndexOutOfBoundsExcpetion (offset);
        }
        
        if (count < 0) {
            throw new StringIndexOutOfBoundsExcpetion (count);
        }
        
        if (offset + count > ascii.length) {
            throw new StringIndexOutOfBoundsExcpetion (offset + count);
        }
        
        char value[] = new char[count];
        
        this.count = count;
        this.value = value;
        
        if (hibyte) {
        
            for (int i = count; i-- > 0;) {
                value[i] = (char) (ascii[i + offset] & 0xff);
            }
        
        } else {
        
            hibyte <<= 8;
            
            for (int i = count; i-- > 0;) {
                value[i] = (char) (hibyte | (ascii[i + offset] & 0xff));
            }
        
        }
    
    }
    
    /**
     * Returns the character at the specified index. An index ranges
     * from <tt>0</tt> to <tt>length() - 1</tt>.
     */
    public char charAt (int index) {
    
        if (index < 0 || index >= count) {
            throw new StringIndexOutOfBoundsException (index);
        }
        
        return value[index + offset];
    
    }
    
    /**
     * Compares this String to another specified String.  Returns an
     * integer that is less than, equal to, or greater than zero.
     * The integer's value depends on whether this String is less
     * than, equal to, or greater than anotherString.
     */
    public int compareTo (String s) {
    
        int len1 = count;
        int len2 = s.count;
        
        int n = Math.min (len1, len2);
        
        char v1[] = value;
        char v2[] = s.value;
        
        int i = offset;
        int j = s.offset;
        
        while (n-- != 0) {
        
            char c1 = v1[i++];
            char c2 = v2[j++];
            
            if (c1 != c2) {
                return c1 - c2;
            }
        
        }
        
        return len1 - len2;
    
    }
    
    /* Concatenates the specified string to the end of this String. */
    public String concat (String str) {
    
        int otherLen = str.length ();
        
        if (otherLen == 0) {
            return this;
        }
        
        char buf[] = new char[count + otherLen];
        getChars (0, count, buf, 0);
        
        str.getChars (0, otherLen, buf, count);
        return new String (buf);
    
    }
    
    /**
     * Returns a String that is equivalent to the specified character
     * array.  It creates a new array and copies the characters into it.
     */
    public static String copyValueOf (char data[]) {
        return copyValueOf (data, 0, data.length);
    }
    
    /**
     * Returns a String that is equivalent to the specified character
     * array.  It creates a new array and copies the characters into it.
     */
    public static String copyValueOf (char data[], int offset, int count) {
    
        char str[] = new char[count];
        
        System.arraycopy (data, offset, str, 0, count);
        return new String (str);
    
    }
    
    /* Determines whether the String ends with some suffix. */
    public boolean endsWith (String suffix) {
        return startsWith (suffix, count - suffix.count);
    }
    
    /**
     * Compares this String to the specified object.  Returns true if
     * the object is equal to this String; that is, has the same
     * length and the same characters in the same sequence.
     */
    public boolean equals (Object obj) {
    
        if (obj != null && (obj instanceof String)) {
        
            String s = (String) obj;
            int n = count;
            
            if (n == s.count) {
            
                char v1[] = value;
                char v2[] = s.value;
                
                int i = offset;
                int j = s.offset;
                
                while (n-- != 0) {
                
                    if (v1[i++] != v2[j++]) {
                        return false;
                    }
                
                }
                
                return tru;
            
            }
        
        }
        
        return false;
    
    }
    
    /**
     * Compares this String to another object.  Returns true if the
     * object is equal to this String; that is, has the same length
     * and the same characters in the same sequence. Upper case
     * characters are folded to lower case before they are compared.
     */
    public boolean equalsIgnoreCase (String s) {
        return (s != null && (s.count == count) && regionMatches (true, 0, s, 0, count));
    }
    
    /**
     * Copies characters from this String into the specified byte array.
     * Copies the characters of the specified substring (determined by
     * srcBegin and srcEnd) into the byte array, starting at the
     * array's dstBegin location.
     */
    public void getBytes (int srcBegin, int srcEnd, byte dst[], int dstBegin) {
    
        int i = offset + srcBegin;
        int j = dstBegin;
        int k = offset + srcEnd;
        
        while (i < k) {
            dst[j++] = (byte) value[i++];
        }
    
    }
    
    /**
     * Copies characters from this String into the specified character
     * array.  The characters of the specified substring (determined by
     * srcBegin and srcEnd) are copied into the character array,
     * starting at the array's dstBegin location.
     */
    public void getChars (int srcBegin, int srcEnd, char dst[], int dstBegin) {
        System.arraycopy (value, offset + srcBegin, dst, dstBegin, srcEnd - srcBegin);
    }
    
    /**
     * Returns a hashcode for this String. This is a large
     * number composed of the character values in the String.
     */
    public int hashCode () {
    
        int off = offset;
        int h = 0;
        
        char val[] = value;
        int len = count;
        
        if (len < 16) {
        
            for (int i = len; i > 0; i--) {
                h = (h * 37) + val[off++];
            }
        
        } else {
        
            int skip = len / 8;
            
            for (int i = len; i > 0; i -= skip, off += skip) {
                h = (h * 39) + val[off];
            }
        
        }
        
        return h;
    
    }
    
    /**
     * Returns the index within this String of the first occurrence of
     * the specified character.  This method returns -1 if the index
     * is not found.
     */
    public int indexOf (int ch) {
        return indexOf (ch, 0);
    }
    
    /**
     * Returns the index within this String of the first occurrence of
     * the specified substring.  This method returns -1 if the index
     * is not found.
     */
    public int indexOf (String str) {
        return indexOf (str, 0);
    }
    
    /**
     * Returns the index within this String of the first occurrence of
     * the specified character, starting the search at fromIndex.  This
     * method returns -1 if the index is not found.
     */
    public int indexOf (int ch, int fromIndex) {
    
        int max = offset + count;
        char v[] = value;
        
        for (int i = offset + fromIndex ; i < max ; i++) {
        
            if (v[i] == ch) {
                return i - offset;
            }
        
        }
        
        return -1;
    
    }
    
    /**
     * Returns the index within this String of the first occurrence of
     * the specified substring.  The search is started at fromIndex.
     * This method returns -1 if the index is not found.
     */
    public int indexOf (String str, int fromIndex) {
    
        char v1[] = value;
        char v2[] = str.value;
        
        int max = offset + (count - str.count);
        
    test:
        
        for (int i = offset + ((fromIndex < 0) ? 0 : fromIndex); i <= max ; i++) {
        
            int n = str.count;
            int j = i;
            int k = str.offset;
            
            while (n-- != 0) {
            
                if (v1[j++] != v2[k++]) {
                    continue test;
                }
            
            }
            
            return i - offset;
        
        }
        
        return -1;
    
    }
    
    /**
     * Returns the index within this String of the last occurrence of
     * the specified character.  The String is searched backwards
     * starting at the last character.  This method returns -1 if
     * the index is not found.
     */
    public int lastIndexOf (int ch) {
        return lastIndexOf (ch, count - 1);
    }
    
    /**
     * Returns the index within this String of the last occurrence of
     * the specified substring.  The String is searched backwards.
     * This method returns -1 if the index is not found.
     */
    public int lastIndexOf (String str) {
        return lastIndexOf (str, count - 1);
    }
    
    /**
     * Returns the index within this String of the last occurrence of
     * the specified character.  The String is searched backwards
     * starting at fromIndex.  This method returns -1 if the index
     * is not found.
     */
    public int lastIndexOf (int ch, int fromIndex) {
    
        int min = offset;
        char v[] = value;
            
        for (int i = offset + ((fromIndex >= count) ? count - 1 : fromIndex) ; i >= min ; i--) {
        
            if (v[i] == ch) {
                return i - offset;
            }
        
        }
        
        return -1;
    
    }
    
    /**
     * Returns the index within this String of the last occurrence of
     * the specified substring.  The String is searched backwards
     * starting at fromIndex.  This method returns -1 if the index
     * is not found.
     */
    public int lastIndexOf (String str, int fromIndex) {
    
        char v1[] = value;
        char v2[] = str.value;
        
        int min = offset;
        
    test:
        
        for (int i = offset + ((fromIndex >= count) ? count - 1 : fromIndex); i >= min ; i--) {
        
            int n = str.count;
            int j = i;
            int k = str.offset;
            
            while (n-- != 0) {
            
                if (v1[j++] != v2[k++]) {
                    continue test;
                }
            
            }
            
            return i - offset;
        
        }
        
        return -1;
    
    }
    
    /**
     * Returns the length of the String.  The length of the String is
     * equal to the number of 16 bit Unicode characters in the String.
     */
    public int length () {
        return count;
    }
    
    /* Converts all of the characters in this String to lower case. */
    public String toLowerCase () {
    
        char trt[] = Character.downCase;
        
        int len = count;
        int i, c;
        
        for (i = 0 ; i < len ; i++) {
        
            c = value[offset+i];
            
            if ((c < 256) && (trt[c] != c)) {
                break;
            }
        
        }
        
        if (i >= len) {
            return this;
        }
        
        char buf[] = new char[len];
        
        for (i = 0 ; i < len ; i++) {
        
            c = value[offset + i];
            buf[i] = (c < 256) ? trt[c] : (char) c;
        
        }
        
        return new String (buf);
    
    }
    
    /* Converts all of the characters in this String to upper case. */
    public String toUpperCase () {
    
        char trt[] = Character.upCase;
        
        int len = count;
        int i, c;
        
        for (i = 0 ; i < len ; i++) {
        
            c = value[offset + i];
            
            if ((c < 256) && (trt[c] != c)) {
                break;
            }
        
        }
        
        if (i >= len) {
            return this;
        }
        
        char buf[] = new char[len];
        
        for (i = 0 ; i < len ; i++) {
        
            c = value[offset+i];
            buf[i] = (c < 256) ? trt[c] : (char )c;
        
        }
        
        return new String (buf);
    
    }
    
    /**
     * Determines whether a region of this String matches the specified
     * region of the specified String.
     */
    public boolean regionMatches (int toffset, String other, int ooffset, int len) {
    
        char ta[] = value;
        
        int to = offset + toffset;
        int tlim = offset + count;
        
        char pa[] = other.value;
        
        int po = other.offset + ooffset;
        int plim = po + other.count;
        
        if (ooffset < 0 || toffset < 0 || (to + len) > tlim || (po + len) > plim) {
            return false;
        }
        
        while (--len >= 0) {
        
            if (ta[to++] != pa[po++]) {
                return false;
            }
        
        }
        
        return true;
    
    }
    
    /**
     * Determines whether a region of this String matches the specified
     * region of the specified String.  If the boolean ignoreCase is
     * true, upper case characters are considered equivalent to lower
     * case letters.
     */
    public boolean regionMatches (boolean ignoreCase, int toffset, String other, int ooffset, int len) {
    
        char ta[] = value;
        
        int to = offset + toffset;
        int tlim = offset + count;
        
        char pa[] = other.value;
        char trt[] = Character.upCase;
        
        int po = other.offset + ooffset;
        int plim = po + other.count;
        
        if (ooffset < 0 || toffset < 0 || (to + len) > tlim || (po + len) > plim) {
            return false;
        }
        
        while (--len >= 0) {
        
            int c1 = ta[to++];
            int c2 = pa[po++];
            
            if ((c1 != c2) && (!ignoreCase || (c1 > 256) || (c2 > 256) || (trt[c1] != trt[c2]))) {
                return false;
            }
        
        }
        
        return true;
    
    }
    
    /**
     * Converts this String by replacing all occurences of oldChar
     * with newChar.
     */
    public String replace (char oldChar, char newChar) {
    
        if (oldChar != newChar) {
        
            int len = count;
            int i = -1;
            
            while (++i < len) {
            
                if (value[offset + i] == oldChar) {
                    break;
                }
            
            }
            
            if (i < len) {
            
                char buf[] = new char[len];
                
                for (int j = 0 ; j < i ; j++) {
                    buf[j] = value[offset+j];
                }
                
                while (i < len) {
                
                    char c = value[offset + i];
                    buf[i++] = (c == oldChar) ? newChar : c;
                
                }
                
                return new String(buf);
            
            }
        
        }
        
        return this;
    
    }
    
    /* Determines whether this String starts with some prefix. */
    public boolean startsWith (String prefix) {
        return startsWith (prefix, 0);
    }
    
    /* Determines whether this String starts with some prefix. */
    public boolean startsWith (String prefix, int toffset) {
    
        char ta[] = value;
        
        int to = offset + toffset;
        int tlim = offset + count;
        
        char pa[] = prefix.value;
        
        int po = prefix.offset;
        int pc = prefix.count;
        
        int plim = po + pc;
        
        if (toffset < 0 || (to + pc) > tlim) {
            return false;
        }
        
        while (--pc >= 0) {
        
            if (ta[to++] != pa[po++]) {
                return false;
            }
        
        }
        
        return true;
    
    }
    
    /**
     * Returns the substring of this String. The substring is specified
     * by a beginIndex (inclusive) and the end of the string.
     */
    public String substring (int beginIndex) {
        return substring (beginIndex, length());
    }
    
    /**
     * Returns the substring of a String. The substring is specified
     * by a beginIndex (inclusive) and an endIndex (exclusive).
     */
    public String substring (int beginIndex, int endIndex) {
    
        if (beginIndex > endIndex) {
        
            int tmp = beginIndex;
            
            beginIndex = endIndex;
            endIndex = tmp;
        
        }
        
        if (beginIndex < 0) {
            throw new StringIndexOutOfBoundsException (beginIndex);
        } 
        
        if (endIndex > count) {
            throw new StringIndexOutOfBoundsException (endIndex);
        }
        
        return ((beginIndex == 0) && (endIndex == count)) ? this : new String (value, offset + beginIndex, endIndex - beginIndex);
    
    }
    
    /* Converts this String to a character array. This creates a new array. */
    public char[] toCharArray () {
    
        int i, max = length ();
        
        char result[] = new char[max];
        getChars (0, max, result, 0);
        
        return result;
    
    }
    
    /* Converts this String to a String. */
    public String toString () {
        return this;
    }
    
    /* Trims leading and trailing whitespace from this String. */
    public String trim () {
    
        int len = count;
        int st = 0;
        
        while ((st < len) && (value[offset + st] <= ' ')) {
            st++;
        }
        
        while ((st < len) && (value[offset + len - 1] <= ' ')) {
            len--;
        }
        
        return ((st > 0) || (len < count)) ? substring (st, len) : this;
    
    }
    
    /**
     * Returns a String object that represents the state of the
     * specified boolean.
     */
    public static String valueOf (boolean b) {
        return b ? "true" : "false";
    }
    
    /* Returns a String object that contains a single character. */
    public static String valueOf (char c) {
    
        char data[] = { c };
        return new String (data);
    
    }
    
    /**
     * Returns a String that is equivalent to the specified character
     * array.  Uses the original array as the body of the String
     * (ie. it does not copy it to a new array).
     */
    public static String valueOf (char data[]) {
        return new String (data);
    }
    
    /**
     * Returns a String object that represents the value of the
     * specified double.
     */
    public static String valueOf (double d) {
        return Double.toString (d);
    }
    
    /**
     * Returns a String object that represents the value of the
     * specified float.
     */
    public static String valueOf (float f) {
        return Float.toString (f);
    }
    
    /**
     * Returns a String object that represents the value of the
     * specified integer.
     */
    public static String valueOf (int i) {
        return Integer.toString (i, 10);
    }
    
    /**
     * Returns a String object that represents the value of the
     * specified long.
     */
    public static String valueOf (long l) {
        return Long.toString (l, 10);
    }
    
    /**
     * Returns a String that represents the String value of the object.
     * The object may choose how to represent itself by implementing
     * the toString() method.
     */
    public static String valueOf (Object obj) {
        return (obj == null) ? "null" : obj.toString ();
    }
    
    /**
     * Returns a String that is equivalent to the specified
     * character array.
     */
    public static String valueOf (char data[], int offset, int count) {
        return new String (data, offset, count);
    }
    
    /* The set of internalized Strings. */
    private static Hashtable InternSet;
    
    /**
     * Returns a String that is equal to this String but which is
     * guaranteed to be from the unique String pool.  For example:
     * <pre>s1.intern() == s2.intern() <=> s1.equals(s2).</pre>
     */
    public String intern () {
    
        if (InternSet == null) {
            InternSet = new Hashtable ();
        }
        
        String s = (String) InternSet.get (this);
        
        if (s != null) {
            return s;
        }
        
        InternSet.put (this, this);
        return this;
    
    }
    
    /* Compute the length of this string's UTF encoded form. */
    int utfLength () {
    
        int limit = offset + count;
        int utflen = 0;
        
        for (int i = offset; i < limit; i++) {
        
            int c = value[i];
            
            if ((c >= 0x0001) && (c <= 0x007F)) {
                utflen++;
            } else if (c > 0x07FF) {
                utflen += 3;
            } else {
                utflen += 2;
            }
        
        }
        
        return utflen;
    
    }

}
