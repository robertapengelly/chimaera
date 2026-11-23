/******************************************************************************
 * @file            spin.h
 *****************************************************************************/
#ifndef     _SPIN_H
#define     _SPIN_H

typedef volatile struct {

    volatile int latch[1];
    int owner;
    
    const char * func;

} spin_lock_t;

void spin_lock (spin_lock_t *lock);
void spin_unlock (spin_lock_t *lock);

#endif      /* _SPIN_H */
