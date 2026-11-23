/******************************************************************************
 * @file            mem.c
 *****************************************************************************/
#include    <stddef.h>
#include    <stdint.h>

typedef struct _memmgrn {

    struct _memmgrn *next;
    struct _memmgrn *prev;
    
    struct _memmgrn *nextf;
    struct _memmgrn *prevf;
    
    int fixed;
    size_t size;
    
    int allocated;
    int id;
    
    size_t filter;

} memmgrn_t;

struct _memmgr {

    memmgrn_t *start;
    memmgrn_t *startf;

};

#define     MEMMGR_ALIGN                8

#define     MEMMGRN_SZ                  \
    ((sizeof (memmgrn_t) % MEMMGR_ALIGN == 0) ? \
        sizeof (memmgrn_t) : \
            ((sizeof (memmgrn_t) / MEMMGR_ALIGN + 1) * MEMMGR_ALIGN))

#define     MEMMGR_MINFRTOT             MEMMGRN_SZ

static struct _memmgr memmgr = { 0 };
uintptr_t mem_start = 0, mem_end = 0, mem_used = 0;

void *alloc_mem (size_t bytes) {

    size_t oldbytes = bytes, *q;
    memmgrn_t *p = 0, *n = 0;
    
    bytes += MEMMGRN_SZ;
    
    if ((bytes % MEMMGR_MINFRTOT) != 0) {
        bytes = ((bytes / MEMMGR_MINFRTOT) + 1) * MEMMGR_MINFRTOT;
    }
    
    if (bytes < oldbytes) {
        return 0;
    }
    
    p = memmgr.startf;
    
    while (p) {
    
        if (p->size >= bytes) {
        
            if (p->allocated) {
                return 0;
            }
            
            if ((p->size - bytes) >= MEMMGR_MINFRTOT) {
            
                n = (memmgrn_t *) ((char *) p + bytes);
                n->next = p->next;
                
                if (n->next) {
                    n->next->prev = n;
                }
                
                n->prev = p;
                p->next = n;
                
                n->fixed = 0;
                n->size = p->size - bytes;
                
                n->allocated = 0;
                p->size = bytes;
                
                n->nextf = p->nextf;
                n->prevf = p->prevf;
                
                if (n->nextf) {
                    n->nextf->prevf = n;
                }
                
                if (n->prevf) {
                    n->prevf->nextf = n;
                } else if (memmgr.startf != p) {
                    return 0;
                } else {
                    memmgr.startf = n;
                }
            
            } else {
            
                if (p->nextf) {
                    p->nextf->prevf = p->prevf;
                }
                
                if (p->prevf) {
                    p->prevf->nextf = p->nextf;
                } else if (memmgr.startf != p) {
                    return 0;
                } else {
                    memmgr.startf = p->nextf;
                }
            
            }
            
            p->nextf = 0;
            p->prevf = 0;
            
            p->allocated = 0x5A5A;
            /*p->id = id;*/
            
            break;
        
        }
        
        p = p->nextf;
    
    }
    
    if (!p) {
        return p;
    }
    
    q = (size_t *) ((char *) p + MEMMGRN_SZ);
    *(q - 1) = oldbytes;
    
    return ((char *) p + MEMMGRN_SZ);

}

int free_mem (void *ptr) {

    memmgrn_t *p, *n, *l;
    
    int combprev = 0;
    int combnext = 0;
    
    p = (memmgrn_t *) ((char *) ptr - MEMMGRN_SZ);
    
    if (p->allocated != 0x5A5A) {
        return 1;
    }
    
    p->allocated = 0;
    
    l = p->prev;
    n = p->next;
    
    if (!p->fixed && l && !l->allocated) {
    
        l->size += p->size;
        l->next = p->next;
        
        if (l->next) {
            l->next->prev = l;
        }
        
        combprev = 1;
    
    }
    
    if (n && !n->allocated && !n->fixed) {
        combnext = 1;
    }
    
    if (combnext && !combprev) {
    
        p->size += n->size;
        p->next = n->next;
        
        if (p->next) {
            p->next->prev = p;
        }
        
        p->nextf = n->nextf;
        
        if (p->nextf) {
            p->nextf->prevf = p;
        }
        
        p->prevf = n->prevf;
        
        if (p->prevf) {
            p->prevf->nextf = p;
        } else if (memmgr.startf != n) {
            return 1;
        } else {
            memmgr.startf = p;
        }
    
    } else if (combnext && combprev) {
    
        if (n->nextf) {
            n->nextf->prevf = n->prevf;
        }
        
        if (n->prevf) {
            n->prevf->nextf = n->nextf;
        } else if (memmgr.startf != n) {
            return 1;
        } else {
        
            memmgr.startf = n->nextf;
            n->nextf->prevf = 0;
        
        }
        
        l->size += n->size;
        l->next = n->next;
        
        if (l->next) {
            l->next->prev = l;
        }
    
    }
    
    if (combnext) {
    
        n->nextf = 0;
        n->prevf = 0;
    
    }
    
    if (combprev) {
    
        p->nextf = 0;
        p->prevf = 0;
    
    }
    
    if (!combprev && !combnext) {
    
        p->nextf = memmgr.startf;
        memmgr.startf = p;
        
        p->prevf = 0;
        
        if (p->nextf) {
            p->nextf->prevf = p;
        }
    
    }
    
    return 0;

}

int resize_mem (void *ptr, size_t new_size) {

    size_t old_bytes = new_size;
    memmgrn_t *p, *n, *z;
    
    if (!ptr) {
        return -1;
    }
    
    new_size += MEMMGRN_SZ;
    
    if ((new_size % MEMMGR_MINFRTOT) != 0) {
        new_size = ((new_size / MEMMGR_MINFRTOT) + 1) * MEMMGR_MINFRTOT;
    }
    
    if (new_size < old_bytes) {
        return -1;
    }
    
    p = (memmgrn_t *) ((char *) ptr - MEMMGRN_SZ);
    
    if (p->allocated != 0x5A5A) {
        return -1;
    }
    
    if (p->size < new_size) {
    
        n = p->next;
        
        if (n && !n->allocated && !n->fixed && ((n->size + p->size) >= new_size)) {
        
            if ((p->size + n->size) < (new_size + MEMMGR_MINFRTOT)) {
            
                if (n->nextf) {
                    n->nextf->prevf = n->prevf;
                }
                
                if (n->prevf) {
                    n->prevf->nextf = n->nextf;
                } else if (memmgr.startf != n) {
                    return -1;
                } else {
                    memmgr.startf = n->nextf;
                }
                
                p->size += n->size;
                p->next = n->next;
                
                if (p->next) {
                    p->next->prev = p;
                }
            
            } else {
            
                z = (memmgrn_t *) ((char *) p + new_size);
                
                z->allocated = 0;
                z->fixed = 0;
                
                z->size = p->size + n->size - new_size;
                z->prev = p;
                
                p->next = z;
                
                if ((z->next = n->next)) {
                    z->next->prev = z;
                }
                
                z->nextf = n->nextf;
                
                if (z->nextf) {
                    z->nextf->prevf = z;
                }
                
                z->prevf = n->prevf;
                
                if (z->prevf) {
                    z->prevf->nextf = z;
                } else if (memmgr.startf != n) {
                    return -1;
                } else {
                    memmgr.startf = z;
                }
                
                p->size = new_size;
            
            }
        
        } else {
            return -1;
        }
    
    } else if ((new_size + MEMMGR_MINFRTOT) <= p->size) {
    
        n = (memmgrn_t *) ((char *) p + new_size);
        
        if ((n->next = p->next)) {
            n->next->prev = n;
        }
        
        n->prev = p;
        p->next = n;
        
        n->size = p->size - new_size;
        
        n->allocated = 0;
        n->fixed = 0;
        
        p->size = new_size;
        
        if ((z = n->next) && !z->allocated && !z->fixed) {
        
            n->size += z->size;
            n->next = z->next;
            
            if (n->next) {
                n->next->prev = n;
            }
            
            n->nextf = z->nextf;
            
            if (n->nextf) {
                n->nextf->prevf = n;
            }
            
            n->prevf = z->prevf;
            
            if (n->prevf) {
                n->prevf->nextf = n;
            } else if (memmgr.startf != z) {
                return -1;
            } else {
                memmgr.startf = n;
            }
        
        } else {
        
            n->nextf = memmgr.startf;
            
            if (n->nextf) {
                n->nextf->prevf = n;
            }
            
            n->prevf = 0;
            memmgr.startf = n;
        
        }
    
    }
    
    {
    
        size_t *q = (size_t *) ((char *) p + MEMMGRN_SZ);
        *(q - 1) = old_bytes;
    
    }
    
    return 0;

}

void mem_init (void) {

    uintptr_t buffer_addr = mem_start + mem_used;
    uintptr_t end_addr = mem_end;
    
    memmgrn_t *p = 0, *l = 0, *b = 0;
    size_t szbuf;
    
    if (end_addr <= buffer_addr) {
        return;
    }
    
    if (((size_t) buffer_addr % MEMMGR_ALIGN) != 0) {
    
        uintptr_t adjust = (MEMMGR_ALIGN - (buffer_addr % MEMMGR_ALIGN));
        buffer_addr += adjust;
    
    }
    
    if (end_addr <= buffer_addr) {
        return;
    }
    
    szbuf = end_addr - buffer_addr;
    szbuf -= szbuf % MEMMGR_ALIGN;

    if (szbuf < MEMMGRN_SZ) {
        return;
    }
    
    p = memmgr.start;
    
    while (p && ((memmgrn_t *) buffer_addr >= p)) {
    
        l = p;
        p = p->next;
    
    }
    
    b = (memmgrn_t *) buffer_addr;
    
    b->prev = l;
    b->next = p;
    
    if (l) {
        l->next = b;
    } else {
        memmgr.start = b;
    }
    
    if (p) {
        p->prev = b;
    }
    
    b->fixed = 1;
    b->size = szbuf;
    b->allocated = 0;
    
    b->nextf = memmgr.startf;
    
    if (b->nextf) {
        b->nextf->prevf = b;
    }
    
    b->prevf = 0;
    memmgr.startf = b;

}
