/******************************************************************************
 * @file            ll.c
 *****************************************************************************/
#include    <kaishi/vfs/node.h>
#include    <kaishi/ll.h>
#include    <kaishi/stdio.h>
#include    <kaishi/stdlib.h>
#include    <stddef.h>
#include    <stdint.h>
#include    <string.h>

struct load_line_data {

    char *line, *real_line;
    
    size_t end_of_prev_real_line, read_size, capacity;
    int remaining;
    
    uint64_t *new_line_number_p;

};

#define     CAPACITY_INCREMENT          256

int load_line (char **line_p, char **line_end_p, uint64_t *newlines_p, struct fs_node *node, void **load_line_internal_data_p) {

    struct load_line_data *ll_data = *load_line_internal_data_p;
    
    size_t pos_in_line = 0, pos_in_real_line = 0;
    uint64_t newlines = 0;
    
    int in_escape = 0, in_double_quote = 0, in_single_quote = 0;
    int in_line_comment = 0, skipping_spaces = 0;
    
    if (ll_data->end_of_prev_real_line) {
    
        memmove (ll_data->real_line, ll_data->real_line + ll_data->end_of_prev_real_line, ll_data->read_size - ll_data->end_of_prev_real_line);
        ll_data->read_size -= ll_data->end_of_prev_real_line;
    
    }
    
    while (1) {
    
        if (pos_in_line >= ll_data->capacity || pos_in_real_line >= ll_data->capacity) {
        
            ll_data->capacity += CAPACITY_INCREMENT;
            
            ll_data->line = krealloc (ll_data->line, ll_data->capacity + 2);
            ll_data->real_line = krealloc (ll_data->real_line, ll_data->capacity + 1);
        
        }
        
        if (pos_in_real_line >= ll_data->read_size) {
        
            ll_data->read_size = kread (node, ll_data->real_line, ll_data->capacity);
            
            if (ll_data->read_size < 0) {
                return 4;
            }
            
            if (!ll_data->read_size) {
                return 1;
            }
            
            ll_data->remaining = ll_data->read_size;
            ll_data->real_line[ll_data->read_size] = '\0';
            
            pos_in_real_line = 0;
        
        }
        
    copying:
        
        if (in_line_comment) {
        
            while (pos_in_real_line < ll_data->read_size) {
            
                ll_data->remaining--;
                
                if (ll_data->real_line[pos_in_real_line] == '\n') {
                
                    in_line_comment = 0;
                    break;
                
                }
                
                pos_in_real_line++;
            
            }
        
        }
        
        if (skipping_spaces) {
        
            while (pos_in_real_line < ll_data->read_size) {
            
                ll_data->remaining--;
                
                if (ll_data->real_line[pos_in_real_line] != ' ' && ll_data->real_line[pos_in_real_line] != '\t') {
                
                    skipping_spaces = 0;
                    break;
                
                }
                
                pos_in_real_line++;
            
            }
        
        }
        
        while (pos_in_real_line < ll_data->read_size && pos_in_line < ll_data->capacity) {
        
            ll_data->line[pos_in_line] = ll_data->real_line[pos_in_real_line++];
            ll_data->remaining--;
            
            if (in_double_quote || in_single_quote) {
            
                if (in_escape) {
                    in_escape = 0;
                } else if (in_double_quote && ll_data->line[pos_in_line] == '"') {
                    in_double_quote = 0;
                } else if (in_single_quote && ll_data->line[pos_in_line] == '\'') {
                    in_single_quote = 0;
                } else if (ll_data->line[pos_in_line] == '\\') {
                    in_escape = 1;
                }
                
                if (ll_data->line[pos_in_line] == '\n') {
                
                    int pos = pos_in_line;
                    
                    if (pos > 0 && ll_data->line[pos - 1] == '\r') {
                        ll_data->line[--pos] = '\n';
                    }
                    
                    if (pos > 0) {
                    
                        if (ll_data->line[pos - 1] != '\\') {
                    
                            ll_data->line[pos + 1] = '\0';
                            ll_data->end_of_prev_real_line = pos_in_real_line;
                            
                            *line_p = ll_data->line;
                            *line_end_p = ll_data->line + pos;
                            
                            *newlines_p = newlines;
                            return 0;
                        
                        } else {
                        
                            pos_in_line = pos - 1;
                            
                            newlines++;
                            goto copying;
                        
                        }
                    
                    }
                
                }
            
            } else {
            
                if (ll_data->line[pos_in_line] == ' ' || ll_data->line[pos_in_line] == '\t') {
                
                    if (ll_data->line[pos_in_line] == '\t') {
                    
                        int cnt = 4 - (pos_in_line % 4);
                        int i = 0;
                        
                        ll_data->capacity += CAPACITY_INCREMENT;
                        
                        ll_data->line = krealloc (ll_data->line, ll_data->capacity + 2);
                        ll_data->real_line = krealloc (ll_data->real_line, ll_data->capacity + 1);
                        
                        for (; i < cnt; i++) {
                            ll_data->line[pos_in_line++] = ' ';
                        }
                        
                        continue;
                    
                    }
                
                } else if (ll_data->line[pos_in_line] == '\n') {
                
                    if (pos_in_line > 0 && ll_data->line[pos_in_line - 1] == '\r') {
                        ll_data->line[--pos_in_line] = '\n';
                    }
                    
                    ll_data->line[pos_in_line + 1] = '\0';
                    ll_data->end_of_prev_real_line = pos_in_real_line;
                    
                    *line_p = ll_data->line;
                    *line_end_p = ll_data->line + pos_in_line;
                    
                    *newlines_p = newlines;
                    return 0;
                
                } else if (ll_data->line[pos_in_line] == '\\') {
                
                    pos_in_line--;
                    
                    while (pos_in_real_line < ll_data->read_size) {
                    
                        if (ll_data->real_line[pos_in_real_line] == '\r' || ll_data->real_line[pos_in_real_line] == '\n') {
                        
                            if (ll_data->real_line[pos_in_real_line] == '\r') {
                                pos_in_real_line++;
                            }
                            
                            if (ll_data->real_line[pos_in_real_line] == '\n') {
                                pos_in_real_line++;
                            }
                            
                            break;
                        
                        }
                        
                        pos_in_real_line++;
                    
                    }
                    
                    newlines++;
                    continue;
                
                } else if (ll_data->line[pos_in_line] == '"') {
                    in_double_quote = 1;
                } else if (ll_data->line[pos_in_line] == '\'') {
                    in_single_quote = 1;
                } else if (ll_data->line[pos_in_line] == '#') {
                
                    in_line_comment = 1;
                    goto copying;
                
                }
            
            }
            
            pos_in_line++;
        
        }
        
        if (!ll_data->remaining) {

            ll_data->read_size = 0;
            
            pos_in_real_line = 0;
            continue;
        
        }
        
        ll_data->line[pos_in_line] = '\n';
        ll_data->line[pos_in_line + 1] = '\0';
        
        ll_data->end_of_prev_real_line = 0;
        ll_data->read_size = 0;
        
        *line_p = ll_data->line;
        *line_end_p = ll_data->line + pos_in_line;
        
        *newlines_p = newlines;
        return 0;
    
    }

}

void load_line_destroy_internal_data (void *load_line_internal_data) {

    struct load_line_data *ll_data;
    
    if (load_line_internal_data) {
    
        ll_data = load_line_internal_data;
        
        kfree (ll_data->line);
        kfree (ll_data->real_line);
        kfree (ll_data);
    
    }

}

void *load_line_create_internal_data (uint64_t *new_line_number_p) {

    struct load_line_data *ll_data = kmalloc (sizeof (*ll_data));;
    
    ll_data->capacity = 0;
    ll_data->line = NULL;
    ll_data->real_line = NULL;
    
    ll_data->read_size = 0;
    ll_data->end_of_prev_real_line = 0;
    
    ll_data->new_line_number_p = new_line_number_p;
    return ll_data;

}
