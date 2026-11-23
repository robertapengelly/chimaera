/******************************************************************************
 * @file            arena.h
 *****************************************************************************/
#ifndef     _ARENA_H
#define     _ARENA_H

#include    "regf.h"

cell_info_t *find_cell (arena_t *a, uint32_t off);
uint32_t arena_alloc_cell (arena_t *a, uint32_t payload_size, int type);

unsigned char *arena_ptr (arena_t *a, uint32_t rel_off);
void init_arena (arena_t *a, uint32_t initial_cap);

#endif      /* _ARENA_H */
