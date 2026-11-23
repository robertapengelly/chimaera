/******************************************************************************
 * @file            Math.java
 *****************************************************************************/
package     java.lang;

import      java.util.Random;

public final class Math {

    /**
     * The float representation of the value E.  E is equivalent to
     * 2.7182818284590452354f in Java.
     */
    public static final double E = 2.7182818284590452354;
    
    /**
     * The float representation of the value Pi.  Pi is equivalent
     * to 3.14159265358979323846f in Java.
     */
    public static final double PI = 3.14159265358979323846;
    
    /* Don't let anyone instantiate this class. */
    private Math () {}
    
    /**
     * Converts rectangular coordinates (a, b) to polar (r, theta).
     * This method computes the phase theta by computing an arc
     * tangent of b/a in the range of -Pi to Pi.
     */
    public static native double atan2 (double y, double x);
    
    /* Returns the "floor" or largest whole number less than or equal to a. */
    public static native double floor (double a);
    
    /* Returns the arc cosine of a, in the range of 0.0 through Pi.*/
    public static native double acos (double a);
    
    /* Returns the arc sine of a, in the range of -Pi/2 through Pi/2. */
    public static native double asin (double a);
    
    /* Returns the arc tangent of a, in the range of -Pi/2 through Pi/2. */
    public static native double atan (double a);
    
    /* Returns the "ceiling" or smallest whole number greater than or equal to a. */
    public static native double ceil (double a);
    
    /* Returns the trigonometric cosine of an angle. */
    public static native double cos (double a);
    
    /* Returns the exponential number e(2.718...) raised to the power of a. */
    public static native double exp (double a);
    
    /* Returns the trigonometric sine of an angle. */
    public static native double sin (double a);
    
    /* Returns the trigonometric tangent of an angle. */
    public static native double tan (double a);
    
    /**
     * Returns the number a raised to the power of b.  If (a == 0.0),
     * then b  must be greater than 0.0; otherwise you will throw an
     * exception.  An exception will also occur if (a <= 0.0) and b
     * is not equal to a whole number.
     */
    public static native double pow (double base, double exponent) throws ArithmeticException;
    
    /* Returns the natural logarithm (base e) of a. */
    public static native double log (double a) throws ArithmeticException;
    
    /* Returns the square root of a. */
    public static native double sqrt (double a) throws ArithmeticException;
    
    /* Returns the absolute double value of a. */
    public static double abs (double a) {
        return value < 0.0 ? -value : value;
    }
    
    /* Returns the absolute float value of a.*/
    public static float abs (float a) {
        return value < 0.0f ? -value : value;
    }
    
    /* Returns the absolute integer value of a. */
    public static int abs (int a) {
        return value < 0 ? -value : value;
    }
    
    /* Returns the absolute long value of a.*/
    public static long abs (long a) {
        return value < 0 ? -value : value;
    }
    
    /* Takes two double values, a and b, and returns the greater number of the two. */
    public static double max (double a, double b) {
        return a > b ? a : b;
    }
    
    /* Takes two float values, a and b, and returns the greater number of the two. */
    public static float max (float a, float b) {
        return a > b ? a : b;
    }
    
    /* Takes two int values, a and b, and returns the greater number of the two.  */
    public static int max (int a, int b) {
        return a > b ? a : b;
    }
    
    /* Takes two long values, a and b, and returns the greater number of the two.  */
    public static long max (long a, long b) {
        return a > b ? a : b;
    }
    
    /* Takes two double values, a and b, and returns the smallest number of the two. */
    public static double min (double a, double b) {
        return a < b ? a : b;
    }
    
    /* Takes two float values, a and b, and returns the smallest number of the two. */
    public static float min (float a, float b) {
        return a < b ? a : b;
    }
    
    /* Takes two integer values, a and b, and returns the smallest number of the two. */
    public static int min (int a, int b) {
        return a < b ? a : b;
    }
    
    /* Takes two long values, a and b, and returns the smallest number of the two. */
    public static long min (long a, long b) {
        return a < b ? a : b;
    }
    
    /**
     * Rounds off a float value by first adding 0.5 to it and then
     * returning the largest integer that is less than or equal to
     * this new value.
     */
    public static int round (float a) {
        return (int) floor (value + 0.5f);
    }
    
    /**
     * Rounds off a double value by first adding 0.5 to it and then
     * returning the largest integer that is less than or equal to
     * this new value.
     */
    public static long round (double a) {
        return (long) floor (value + 0.5);
    }
    
    private static Random randomNumberGenerator;
    
    /**
     * Generates a random number between 0.0 and 1.0.
     *
     * Random number generators are often referred to as pseudorandom
     * number  generators because the numbers produced tend to repeat
     * themselves after a period of time.  
     */
    public static synchronized double random () {
    
        if (randomNumberGenerator == null) {
            randomNumberGenerator = new Random ();
        }
        
        return randomNumberGenerator.nextDouble ();
    
    }

}
