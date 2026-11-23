/******************************************************************************
 * @file            Random.java
 *****************************************************************************/
package     java.util;

public class Random {

    private final static long multiplier = 0x5DEECE66DL;
    private final static long addend = 0xBL;
    private final static long mask = (1L << 48) - 1;
    
    private boolean haveNextNextGaussian = false;
    
    private double nextNextGaussian;
    private long seed;
    
    public Random () {
        this (System.currentTimeMillis ());
    }
    
    public Random (long seed) {
    
        setSeed (seed);
        haveNextNextGaussian = false;
    
    }
    
    synchronized private int next (int bits) {
    
        long nextseed = (seed * multiplier + addend) & mask;
        seed = nextseed;
        
        return (int) (nextseed >>> (48 - bits));
    
    }
    
    public double nextDouble () {
    
        long l = ((long) (next (27)) << 27) + next (27);
        return l / (double) (1L << 54);
    
    }
    
    public float nextFloat () {
    
        int i = next (30);
        return i / ((float) (1 << 30));
    
    }
    
    public int nextInt () {
        return next (32);
    }
    
    public long nextLong () {
    
        // it's okay that the bottom word remains signed.
        return (next (32) << 32L) + next (32);
    
    }
    
    synchronized public void setSeed (long seed) {
        this.seed = (seed ^ multiplier) & mask;
    }
    
    synchronized public double nextGaussian () {
    
        // See Knuth, ACP, Section 3.4.1 Algorithm C.
        if (haveNextNextGaussian) {
        
            haveNextNextGaussian = false;
            return nextNextGaussian;
        
        } else {
        
            double v1, v2, s;
            
            do { 
            
                v1 = 2 * nextDouble () - 1;                 // between -1 and 1
                v2 = 2 * nextDouble () - 1;                 // between -1 and 1 
                
                s = v1 * v1 + v2 * v2;
            
            } while (s >= 1);
            
            double multiplier = Math.sqrt (-2 * Math.log (s) / s);
            
            nextNextGaussian = v2 * multiplier;
            haveNextNextGaussian = true;
            
            return v1 * multiplier;
        
        }
    
    }

}
