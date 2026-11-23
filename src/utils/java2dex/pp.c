/******************************************************************************
 * @file            pp.c
 *****************************************************************************/
#include    <ctype.h>
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>
#include    <time.h>

#include    "j2d.h"
#include    "lex.h"
#include    "lib.h"
#include    "ll.h"
#include    "pp.h"
#include    "report.h"
#include    "token.h"
#include    "vector.h"

static struct vector vector_tokens = { 0 };

struct vector *preprocess_line (char **line) {

    char *caret = *line, *start;
    char *sname, ch;
    
    struct token *tok;
    
    while (!is_end_of_line[(int) *caret]) {
    
        start = caret;
        
        if (isspace ((int) *caret)) {
        
            while (isspace ((int) *caret)) {
                caret++;
            }
            
            continue;
        
        }
        
        if (*caret == '"' || *caret == '\'') {
        
            ch = *caret++;
            
            while (!is_end_of_line[(int) *caret]) {
            
                if (*caret == '\\') {
                
                    caret++;
                    
                    if (!is_end_of_line[(int) *caret]) {
                        caret++;
                    }
                    
                    continue;
                
                }
                
                if (*caret == ch) { break; }
                caret++;
            
            }
            
            if (*caret != ch) {
            
                char *temp = xmalloc ((caret - start) + 2);
                sprintf (temp, "%.*s%c", (int) (caret - start), start, ch);
                
                report_line_at (get_filename (), get_line_number (), REPORT_WARNING, *line, start, "missing terminating %c character", ch);
                
                tok = xmalloc (sizeof (*tok));
                tok->kind = (ch == '"' ? TOK_PPSTR : TOK_CCHAR);
                
                tok->start = *line;
                tok->caret = start;
                
                tok->ident = temp;
                tok->len = (caret - start);
                
                vec_push (&vector_tokens, tok);
                continue;
            
            }
            
            caret++;
            
            tok = xmalloc (sizeof (*tok));
            tok->kind = (ch == '"' ? TOK_PPSTR : TOK_CCHAR);
            
            tok->start = *line;
            tok->caret = start;
            
            tok->ident = xstrndup (start, (int) (caret - start));
            tok->len = (caret - start);
            
            vec_push (&vector_tokens, tok);
            continue;
        
        }
        
        if (is_name_beginner ((int) *caret)) {
        
            sname = symname (&caret);
            
            tok = xmalloc (sizeof (*tok));
            tok->kind = TOK_IDENT;
            
            tok->start = *line;
            tok->caret = start;
            
            tok->ident = sname;
            tok->len = strlen (sname);
            
            vec_push (&vector_tokens, tok);
            continue;
        
        }
        
        if (caret[0] == '0' && tolower ((int) caret[1]) == 'x') {
        
            caret += 2;
            
            while (isxdigit ((int) *caret)) {
                caret++;
            }
            
            if (toupper ((int) *caret) == 'L') {
                caret++;
            }
            
            tok = xmalloc (sizeof (*tok));
            tok->kind = TOK_PPNUM;
            
            tok->start = *line;
            tok->caret = start;
            
            tok->ident = xstrndup (start, (int) (caret - start));
            tok->len = (caret - start);
            
            vec_push (&vector_tokens, tok);
            
            if (is_name_part ((int) *caret)) {
            
                report_line_at (get_filename (), get_line_number (), REPORT_ERROR, *line, caret, "unexpected '%c' character", *caret);
                
                while (is_name_part ((int) *caret)) {
                    caret++;
                }
            
            }
            
            continue;
        
        }
        
        if (isdigit ((int) *caret) || (caret[0] == '.' && isdigit ((int) caret[1]))) {
        
            int saw_dot = 0, saw_exp = 0;
            
            while (!is_end_of_line[(int) *caret]) {
            
                if (isdigit ((int) *caret)) {
                
                    caret++;
                    continue;
                
                }
                
                if (*caret == '.' && !saw_dot && !saw_exp) {
                
                    saw_dot = 1;
                    caret++;
                    
                    continue;
                
                }
                
                if ((*caret == 'e' || *caret == 'E' || *caret == 'p' || *caret == 'P') && !saw_exp) {
                
                    saw_exp = 1;
                    caret++;
                    
                    if (*caret == '+' || *caret == '-') {
                        caret++;
                    }
                    
                    continue;
                
                }
                
                break;
            
            }
            
            if (saw_dot || saw_exp) {
            
                if (tolower ((int) *caret) == 'f') {
                    caret++;
                } else if (toupper ((int) *caret) == 'L') {
                    caret++;
                }
            
            } else {
            
                if (toupper ((int) *caret) == 'U') {
                
                    caret++;
                    
                    if (toupper ((int) *caret) == 'L') {
                        caret++;
                    }
                    
                    if (toupper ((int) *caret) == 'L') {
                        caret++;
                    }
                
                } else if (toupper ((int) *caret) == 'L') {
                
                    caret++;
                    
                    if (toupper ((int) *caret) == 'L') {
                        caret++;
                    }
                    
                    if (toupper ((int) *caret) == 'U') {
                        caret++;
                    }
                
                }
            
            }
            
            tok = xmalloc (sizeof (*tok));
            tok->kind = TOK_PPNUM;
            
            tok->start = *line;
            tok->caret = start;
            
            tok->ident = xstrndup (start, (int) (caret - start));
            tok->len = (caret - start);
            
            vec_push (&vector_tokens, tok);
            
            if (is_name_part ((int) *caret)) {
            
                report_line_at (get_filename (), get_line_number (), REPORT_ERROR, *line, caret, "unexpected '%c' character", *caret);
                
                while (is_name_part ((int) *caret)) {
                    caret++;
                }
            
            }
            
            continue;
        
        }
        
        if (ispunct ((int) *caret)) {
        
            struct punct *punct;
            tok = xmalloc (sizeof (*tok));
            
            if ((punct = find_punct (caret))) {
            
                tok->kind = punct->kind;
                
                tok->ident = xstrdup (punct->name);
                tok->len = strlen (punct->name);
                
                caret += tok->len;
            
            } else {
            
                tok->kind = *caret;
                
                tok->ident = xstrndup (caret, 1);;
                tok->len = 1;
                
                caret++;
            
            }
            
            tok->start = *line;
            tok->caret = start;
            
            vec_push (&vector_tokens, tok);
            continue;
        
        }
        
        report_line_at (get_filename (), get_line_number (), REPORT_INTERNAL_ERROR, *line, caret, "Do we see this???");
        caret++;
    
    }
    
    *line = caret;
    return &vector_tokens;

}
