/******************************************************************************
 * @file            arena.c
 *****************************************************************************/
#include    <string.h>

#include    "regf.h"
#include    "arena.h"
#include    "lib.h"

static void arena_reserve (arena_t *a, uint32_t need) {

    if (need <= a->cap) {
        return;
    }
    
    while (a->cap < need) {
        a->cap *= 2;
    }
    
    a->buf = xrealloc (a->buf, a->cap);
    memset (a->buf + a->used, 0, a->cap - a->used);

}

static void arena_add_cell_info (arena_t *a, uint32_t off, uint32_t size, int type) {

    if (a->cell_count == a->cell_cap) {
    
        uint32_t new_cap = ARRAY_GROW_CAP (a->cell_cap);
        
        a->cells = xrealloc (a->cells, new_cap * sizeof (cell_info_t));
        a->cell_cap = new_cap;
    
    }
    
    a->cells[a->cell_count].off = off;
    a->cells[a->cell_count].size = size;
    a->cells[a->cell_count].type = type;
    
    a->cell_count++;

}

cell_info_t *find_cell (arena_t *a, uint32_t off) {

    uint32_t i;
    
    for (i = 0; i < a->cell_count; i++) {
    
        if (a->cells[i].off == off) {
            return &a->cells[i];
        }
    
    }
    
    return 0;

}

uint32_t arena_alloc_cell (arena_t *a, uint32_t payload_size, int type) {

    uint32_t cell_size = align8 (4 + payload_size);
    uint32_t off = HBIN_HEADER_SIZE + a->used;
    
    arena_reserve (a, a->used + cell_size + HBIN_HEADER_SIZE + 4096);
    memset (a->buf + a->used, 0, cell_size);
    
    wr32 (a->buf + a->used, (uint32_t) (0U - cell_size));
    a->used += cell_size;
    
    arena_add_cell_info (a, off, cell_size, type);
    return off;

}

unsigned char *arena_ptr (arena_t *a, uint32_t rel_off) {
    return a->buf + (rel_off - HBIN_HEADER_SIZE);
}

void init_arena (arena_t *a, uint32_t initial_cap) {

    a->buf = xmalloc (initial_cap);
    
    a->cap = initial_cap;
    a->sk_off = INVALID_OFFSET;

}
