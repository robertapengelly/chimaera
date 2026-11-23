/******************************************************************************
 * @file            token.c
 *****************************************************************************/
#include    <ctype.h>
#include    <errno.h>
#include    <math.h>
#include    <stdlib.h>
#include    <string.h>

#include    "cstr.h"
#include    "int64.h"
#include    "j2d.h"
#include    "lex.h"
#include    "lib.h"
#include    "ll.h"
#include    "pp.h"
#include    "report.h"
#include    "token.h"
#include    "vector.h"

static unsigned long new_line_number = 1, newlines = 0;
static char *start = 0, *line = 0;

static char *line_end = 0;
static void *load_line_internal_data = 0;

static unsigned long real_new_line_number = 1;

static struct vector *vec_tokens = 0;

static int get_line (void) {

    if (!load_line_internal_data || !state->ifp) {
        return 0;
    }
    
    while (!load_line (&line, &line_end, 0, 0, &newlines, state->ifp, &load_line_internal_data)) {
    
        set_real_line_number (real_new_line_number);
        set_line_number (new_line_number);
        
        real_new_line_number += newlines + 1;
        new_line_number += newlines + 1;
        
        start = line;
        
        if (skip_whitespace (line) < line_end) {
            return 1;
        }
    
    }
    
    load_line_destroy_internal_data (load_line_internal_data);
    load_line_internal_data = 0;
    
    return 0;

}

static int chrpos (char *s, int ch) {

    char *p = strchr (s, ch);
    return p ? p - s : -1;

}

#include    <stdint.h>

static int hex_char (uint64_t *out, struct token *tok, int idx) {

    const char *s = tok->ident + idx + 1;
    
    int f = 0, i = 0, h;
    uint64_t n = 0;
    
    while (*s && isxdigit ((int) *s)) {
    
        h = chrpos ("0123456789abcdef", tolower ((int) *s));
        
        f = 1;
        i++;
        
        n = n * 16 + h;
        s++;
    
    }
    
    if (!f) {
    
        report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok->start, tok->caret + idx - 1, "\\x used with no following hex digits");
        return i;
    
    }
    
    if (n > ((1U << 8) - 1)) {
    
        uint64_t val = n % ((1U << 8) - 1);
        
        report_line_at (get_filename (), get_line_number (), REPORT_WARNING, tok->start, tok->caret + idx - 1, "hex escape sequence out of range");
        *out = *out * (1U << 8) + val;
    
    } else {
        *out = *out * (1U << 8) + n;
    }
    
    return i;

}

static void scan_ch (struct token *tok) {

    const char *s = tok->ident;
    int has_warned = 0, i, is_unicode;
    
    int64_s i64;
    uint64_t n = 0;
    
    s++;
    
    while (*s) {
    
        is_unicode = 0;
        
        if (*s == '\\') {
        
            switch (*(++s)) {
            
                case 'a':
                
                    n = '\a';
                    break;
                
                case 'b':
                
                    n = '\b';
                    break;
                
                case 'f':
                
                    n = '\f';
                    break;
                
                case 'n':
                
                    n = '\n';
                    break;
                
                case 'r':
                
                    n = '\r';
                    break;
                
                case 't':
                
                    n = '\t';
                    break;
                
                case 'v':
                
                    n = '\v';
                    break;
                
                case '\\':
                
                    n = '\\';
                    break;
                
                case '"':
                
                    n = '"' | (1U << 8);
                    break;
                
                case '\'':
                
                    n = '\'';
                    break;
                
                case '0':   case '1':
                case '2':   case '3':
                case '4':   case '5':
                case '6':   case '7':
                
                    for (i = 0; isdigit ((int) *s) && *s < '8'; s++) {
                    
                        if (++i > 3) {
                            break;
                        }
                        
                        n = n * 8 + (*s - '0');
                    
                    }
                    
                    s--;
                    break;
                
                case 'u':
                
                    is_unicode = 1;
                    n = 0;
                    
                    while (*s == 'u') {
                        s++;
                    }
                    
                    for (i = 0; i < 4; i++, s++) {
                    
                        int h;
                        
                        if (!isxdigit ((unsigned char) *s)) {
                        
                            report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok->start, tok->caret + (s - tok->ident), "invalid Unicode escape sequence");
                            break;
                        
                        }
                        
                        h = chrpos ("0123456789abcdef", tolower ((unsigned char) *s));
                        n = (n << 4) | h;
                    
                    }
                    
                    s--;
                    break;
                
                case 'x':
                
                    s += hex_char (&n, tok, s - tok->ident);
                    break;
                
                default:
                
                    report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok->start, tok->caret + (s - tok->ident), "unknown escape sequence");
                    
                    n = ' ';
                    break;
            
            }
        
        } else {
            n = *s;
        }
        
        zext64 (&i64, 1U << 8);
        mul64 (&tok->val.i, i64);
        
        zext64 (&i64, is_unicode ? n : (n & ((1U << 8) - 1)));
        add64 (&tok->val.i, i64);
        
        if (*(++s) == '\'') {
            break;
        }
        
        if (!has_warned) {
        
            report_line_at (get_filename (), get_line_number (), REPORT_WARNING, tok->start, tok->caret, "multi-character character constant");
            has_warned = 1;
        
        }
    
    }

}

/*static void scan_int (struct token *tok) {

    const char *s = tok->ident;
    int radix = 10, k;
    
    if (*s == '0') {
    
        radix = 8;
        s++;
        
        if (*s == 'x') {
        
            radix = 16;
            s++;
        
        }
    
    }
    
    while (*s != '\0' && (k = chrpos ("0123456789abcdef", tolower ((int) *s))) >= 0) {
    
        if (k >= radix) {
        
            int idx = s - tok->ident;
            report_line_at (get_filename (), get_line_number (), REPORT_ERROR, tok->start, tok->caret + idx, "invalid digit in integer literal");
        
        }
        
        tok->val.d = tok->val.d * radix + k;
        s++;
    
    }

}*/


int tok_ident = TOK_IDENT;

struct keyword {

    char *name;
    enum token_kind kind;

};

struct punct *find_punct (char *p) {

    static struct punct kws[] = {
    
        {   ">>>=", TOK_URSHEQ      },
        {   "<<=",  TOK_LSHEQ       },
        {   ">>=",  TOK_RSHEQ       },
        
        {   ">>>",  TOK_URSH        },
        {   "!=",   TOK_NOTEQ       },
        {   "%=",   TOK_MODEQ       },
        {   "&&",   TOK_LOGAND      },
        {   "&=",   TOK_ANDEQ       },
        {   "*=",   TOK_STAREQ      },
        {   "++",   TOK_INCR        },
        {   "+=",   TOK_PLUSEQ      },
        {   "--",   TOK_DECR        },
        {   "-=",   TOK_MINUSEQ     },
        {   "->",   TOK_ARROW       },
        {   "/=",   TOK_SLASHEQ     },
        {   "<<",   TOK_LSH         },
        {   "<=",   TOK_LTEQ        },
        {   "==",   TOK_EQEQ        },
        {   ">>",   TOK_RSH         },
        {   ">=",   TOK_GTEQ        },
        {   "^=",   TOK_XOREQ       },
        {   "||",   TOK_LOGOR       },
        {   "|=",   TOK_OREQ        },
        
        {   "!",    TOK_XMARK       },
        {   "%",    TOK_MOD         },
        {   "&",    TOK_AMPER       },
        {   "(",    TOK_LPAREN      },
        {   ")",    TOK_RPAREN      },
        {   "*",    TOK_STAR        },
        {   "+",    TOK_PLUS        },
        {   ",",    TOK_COMMA       },
        {   "-",    TOK_MINUS       },
        {   ".",    TOK_DOT         },
        {   "/",    TOK_BSLASH      },
        {   ":",    TOK_COLON       },
        {   ";",    TOK_SEMI        },
        {   "<",    TOK_LESS        },
        {   "=",    TOK_ASSIGN      },
        {   ">",    TOK_GREATER     },
        {   "?",    TOK_QMARK       },
        {   "@",    TOK_AT          },
        {   "[",    TOK_LBRACK      },
        {   "]",    TOK_RBRACK      },
        {   "^",    TOK_CARET       },
        {   "{",    TOK_LBRACE      },
        {   "|",    TOK_PIPE        },
        {   "}",    TOK_RBRACE      },
        {   "~",    TOK_TILDE       }
    
    };
    
    struct punct *kw;
    unsigned long i;
    
    for (i = 0; i < (sizeof (kws) / sizeof (*kws)); i++) {
    
        kw = &kws[i];
        
        if (strncmp (p, kw->name, strlen (kw->name)) == 0) {
            return kw;
        }
    
    }
    
    return 0;

}

static int find_kind (const char *p) {

    static struct keyword kws[] = {
    
        {   "abstract",                 TOK_ABSTRACT            },
        {   "boolean",                  TOK_BOOLEAN             },
        {   "break",                    TOK_BREAK               },
        {   "byte",                     TOK_BYTE                },
        {   "case",                     TOK_CASE                },
        {   "catch",                    TOK_CATCH               },
        {   "char",                     TOK_CHAR                },
        {   "class",                    TOK_CLASS               },
        {   "const",                    TOK_CONST               },
        {   "continue",                 TOK_CONTINUE            },
        {   "default",                  TOK_DEFAULT             },
        {   "do",                       TOK_DO                  },
        {   "double",                   TOK_DOUBLE              },
        {   "else",                     TOK_ELSE                },
        {   "extends",                  TOK_EXTENDS             },
        {   "false",                    TOK_FALSE               },
        {   "final",                    TOK_FINAL               },
        {   "finally",                  TOK_FINALLY             },
        {   "float",                    TOK_FLOAT               },
        {   "for",                      TOK_FOR                 },
        {   "goto",                     TOK_GOTO                },
        {   "if",                       TOK_IF                  },
        {   "implements",               TOK_IMPLEMENTS          },
        {   "import",                   TOK_IMPORT              },
        {   "instanceof",               TOK_INSTANCEOF          },
        {   "int",                      TOK_INT                 },
        {   "interface",                TOK_INTERFACE           },
        {   "long",                     TOK_LONG                },
        {   "native",                   TOK_NATIVE              },
        {   "new",                      TOK_NEW                 },
        {   "null",                     TOK_NULL                },
        {   "package",                  TOK_PACKAGE             },
        {   "private",                  TOK_PRIVATE             },
        {   "protected",                TOK_PROTECTED           },
        {   "public",                   TOK_PUBLIC              },
        {   "return",                   TOK_RETURN              },
        {   "short",                    TOK_SHORT               },
        {   "static",                   TOK_STATIC              },
        {   "strictfp",                 TOK_STRICTFP            },
        {   "super",                    TOK_SUPER               },
        {   "synchronized",             TOK_SYNCHRONIZED        },
        {   "switch",                   TOK_SWITCH              },
        {   "this",                     TOK_THIS                },
        {   "throw",                    TOK_THROW               },
        {   "throws",                   TOK_THROWS              },
        {   "transient",                TOK_TRANSIENT           },
        {   "true",                     TOK_TRUE                },
        {   "try",                      TOK_TRY                 },
        {   "void",                     TOK_VOID                },
        {   "volatile",                 TOK_VOLATILE            },
        {   "while",                    TOK_WHILE               },
    
    };
    
    struct keyword *kw;
    unsigned long i;
    
    for (i = 0; i < (sizeof (kws) / sizeof (*kws)); i++) {
    
        kw = &kws[i];
        
        if (strcmp (p, kw->name) == 0) {
            return kw->kind;
        }
    
    }
    
    return TOK_IDENT;

}

#define     BN_SIZE                     4


static float j2d_strtof (const char *nptr, char **endptr) {

    const char *s = nptr;
    
    float res = 0;
    float frac = 0;
    float div = 1;
    float pow10;
    
    int sign = 1;
    int exp = 0;
    int exp_sign = 1;
    int has_digits = 0;
    int i;
    
    while (isspace ((int) *s)) {
        s++;
    }
    
    if (*s == '-') {
    
        sign = -1;
        s++;
    
    } else if (*s == '+') {
        s++;
    }
    
    /* Integer part */
    while (isdigit ((int) *s)) {
    
        res = (res * 10) + (*s - '0');
        s++;
        
        has_digits = 1;
    
    }
    
    /* Fractional part */
    if (*s == '.') {
    
        s++;
        
        while (isdigit ((int) *s)) {
        
            frac = (frac * 10) + (*s - '0');
            
            div *= 10;
            s++;
            
            has_digits = 1;
        
        }
    
    }
    
    if (!has_digits) {
    
        if (endptr) {
            *endptr = (char *) nptr;
        }
        
        return 0;
    
    }
    
    res += frac / div;
    
    /* Exponent part */
    if (*s == 'e' || *s == 'E') {
    
        const char *exp_start = s;
        int exp_val = 0;
        
        s++;
        
        if (*s == '-') {
        
            exp_sign = -1;
            s++;
        
        } else if (*s == '+') {
            s++;
        }
        
        if (isdigit ((int) *s)) {
        
            while (isdigit ((int) *s)) {
            
                exp_val = (exp_val * 10) + (*s - '0');
                s++;
            
            }
            
            exp = exp_val * exp_sign;
        
        } else {
            s = exp_start;
        }
    
    }
    
    pow10 = 1;
    
    if (exp > 0) {
    
        for (i = 0; i < exp; i++) {
            pow10 *= 10;
        }
        
        res *= pow10;
    
    } else if (exp < 0) {
    
        for (i = 0; i < -exp; i++) {
            pow10 *= 10;
        }
        
        res /= pow10;
    
    }
    
    if (sign < 0) {
        res = -res;
    }
    
    if (endptr) {
        *endptr = (char *) s;
    }
    
    return res;

}

static double j2d_strtod (const char *nptr, char **endptr) {

    const char *s = nptr;
    
    double x = 0;
    double frac = 0;
    double div = 1;
    double pow10;

    int neg = 0;
    int exp_neg = 0;
    int exp_val = 0;
    int saw_digit = 0;
    int i;
    
    while (isspace ((int) *s)) {
        s++;
    }
    
    if (*s == '-') {
    
        neg = 1;
        s++;
    
    } else if (*s == '+') {
        s++;
    }
    
    while (isdigit ((int) *s)) {
    
        x = (x * 10) + (*s - '0');
        s++;
        
        saw_digit = 1;
    
    }
    
    if (*s == '.') {
    
        s++;
        
        while (isdigit ((int) *s)) {
        
            frac = (frac * 10) + (*s - '0');
            
            div *= 10;
            s++;
            
            saw_digit = 1;
        
        }

    }

    if (!saw_digit) {
    
        if (endptr) {
            *endptr = (char *) nptr;
        }
        
        return 0;
    
    }
    
    x += frac / div;
    
    if (*s == 'e' || *s == 'E') {
    
        const char *exp_start = s++;
        
        if (*s == '-') {
        
            exp_neg = 1;
            s++;
        
        } else if (*s == '+') {
            s++;
        }
        
        if (isdigit ((int) *s)) {
        
            while (isdigit ((int) *s)) {
            
                if (exp_val < 308) {
                    exp_val = (exp_val * 10) + (*s - '0');
                }
                
                s++;
            
            }
        
        } else {
            s = exp_start;
        }
    
    }
    
    pow10 = 1;
    
    for (i = 0; i < exp_val; i++) {
        pow10 *= 10;
    }
    
    if (exp_val != 0) {
    
        if (exp_neg) {
            x /= pow10;
        } else {
            x *= pow10;
        }
    
    }
    
    if (neg) {
        x = -x;
    }
    
    if (endptr) {
        *endptr = (char *) s;
    }
    
    return x;

}

static void parse_number (struct token *tok) {

    const char *p = tok->ident, *q;
    
    CString cstr;
    int b, t, ch;
    
    cstr_new (&cstr);
    
    ch = *p++;
    cstr_ccat (&cstr, ch);
    
    b = 10;
    
    if (ch == '0') {
    
        ch = *p++;
        
        if (ch == 'x' || ch == 'X') {
        
            b = 16;
            
            cstr.size--;                /* remove leading 0 */
            ch = *p++;
        
        } else if (ch == '.' || ch == 'e' || ch == 'E') {
            b = 10;
        } else {
            b = 8;
        }
    
    } else {
        ch = *p++;
    }
    
    for (;;) {
    
        if (ch >= 'a' && ch <= 'f') {
            t = ch - 'a' + 10;
        } else if (ch >= 'A' && ch <= 'F') {
            t = ch - 'A' + 10;
        } else if (isdigit ((int) ch)) {
            t = ch - '0';
        } else {
            break;
        }
        
        if (t >= b) {
        
            if (b == 8 && t < 10) {
                report_at (get_filename (), get_line_number (), REPORT_ERROR, "invalid digit in octal integer literal");
            }
            
            break;
        
        }
        
        cstr_ccat (&cstr, ch);
        ch = *p++;
    
    }
    
    /*
     * Original Java has decimal floating-point literals only.
     * Hexadecimal floating-point literals were added much later.
     */
    if (ch == '.' || ch == 'e' || ch == 'E') {
    
        if (b != 10) {
            report_at (get_filename (), get_line_number (), REPORT_ERROR, "hexadecimal/octal floating literals are not valid in original Java");
        }
        
        if (ch == '.') {
        
            cstr_ccat (&cstr, ch);
            ch = *p++;
            
            while (isdigit ((int) ch)) {
            
                cstr_ccat (&cstr, ch);
                ch = *p++;
            
            }
        
        }
        
        if (ch == 'e' || ch == 'E') {
        
            cstr_ccat (&cstr, ch);
            ch = *p++;
            
            if (ch == '-' || ch == '+') {
            
                cstr_ccat (&cstr, ch);
                ch = *p++;
            
            }
            
            if (!isdigit ((int) ch)) {
                report_at (get_filename (), get_line_number (), REPORT_ERROR, "expected exponent digits");
            }
            
            while (isdigit ((int) ch)) {
            
                cstr_ccat (&cstr, ch);
                ch = *p++;
            
            }
        
        }
        
        cstr_ccat (&cstr, '\0');
        
        if (ch == 'f' || ch == 'F') {
        
            ch = *p++;
            
            tok->kind = TOK_CFLOAT;
            tok->val.f = j2d_strtof (cstr.data, 0);
        
        } else {
        
            if (ch == 'd' || ch == 'D') {
                ch = *p++;
            }
            
            tok->kind = TOK_CDOUBLE;
            tok->val.d = j2d_strtod (cstr.data, 0);
        
        }
    
    } else {
    
        int64_s n = { 0 }, n1 = { 0 }, n2, n3, n4, n5;
        
        int ov = 0;
        int is_long = 0;
        
        cstr_ccat (&cstr, '\0');
        q = cstr.data;
        
        for (;;) {
        
            t = *q++;
            
            if (t == '\0') {
                break;
            }
            
            if (t >= 'a') {
                t = t - 'a' + 10;
            } else if (t >= 'A') {
                t = t - 'A' + 10;
            } else {
                t = t - '0';
            }
            
            if (t >= b) {
                report_at (get_filename (), get_line_number (), REPORT_ERROR, "invalid digit in integer literal");
            }
            
            zext64 (&n2, b);
            zext64 (&n3, t);
            
            n1 = n;
            
            mul64 (&n, n2);
            add64 (&n, n3);
            
            parse_string_to_i64 (&n4, "0x1000000000000000");
            n5 = n;
            
            if (gte64_unsigned (n1, n4)) {
            
                div64 (&n5, n2);
                
                if (neq64 (n5, n1)) {
                    ov = 1;
                }
            
            }
        
        }
        
        if (ch == 'l' || ch == 'L') {
        
            is_long = 1;
            ch = *p++;
        
        }
        
        if (ch == 'l' || ch == 'L') {
            report_at (get_filename (), get_line_number (), REPORT_ERROR, "Java does not support LL integer suffixes");
        }
        
        if (ch == 'u' || ch == 'U') {
            report_at (get_filename (), get_line_number (), REPORT_ERROR, "Java does not support unsigned integer suffixes");
        }
        
        if (is_long) {
        
            tok->kind = TOK_CLONG;
            
            /*
             * Java long is always signed 64-bit.
             *
             * Decimal long literals must fit signed long, except that
             * 9223372036854775808L is only valid as the operand of unary '-'.
             *
             * Hex/octal long literals may use all 64 bits.
             */
            if (b == 10) {
            
                parse_string_to_i64 (&n4, "0x8000000000000000");
                
                if (gte64_unsigned (n, n4)) {
                    ov = 1;
                }
            
            }
        
        } else {
        
            tok->kind = TOK_CINT;
            
            /*
             * For now, keep this front end permissive: if an unsuffixed
             * integer literal does not fit in a Java int but does fit in 64
             * bits, classify it as a long literal.  That lets field parsing
             * decide whether assigning it to the destination type is valid.
             */
            if (b == 10) {
            
                parse_string_to_i64 (&n4, "0x80000000");
                
                if (gte64_unsigned (n, n4)) {
                    tok->kind = TOK_CLONG;
                }
            
            } else {
            
                parse_string_to_i64 (&n4, "0x100000000");
                
                if (gte64_unsigned (n, n4)) {
                    tok->kind = TOK_CLONG;
                }
            
            }
        
        }
        
        if (ov) {
            report_at (get_filename (), get_line_number (), REPORT_ERROR, "integer literal out of range");
        }
        
        tok->val.i = n;
    
    }
    
    if (ch != '\0') {
        report_at (get_filename (), get_line_number (), REPORT_ERROR, "invalid character in numeric literal");
    }

}

static int token_brace_depth = 0;

int get_token_brace_depth (void) {
    return token_brace_depth;
}

static int tok_index = 0;
struct token tok = { 0 };

void unget_token (struct token *saved_tok) {

    if (tok_index > 0) {
        tok_index--;
    }
    
    tok.val = saved_tok->val;
    tok.kind = saved_tok->kind;
    
    tok.start = saved_tok->start;
    tok.caret = saved_tok->caret;
    
    tok.ident = saved_tok->ident;
    tok.len = saved_tok->len;
    
    tok.is_unget = 1;
    free (saved_tok);

}

enum token_kind peek_token_kind (void) {

    if (vec_tokens && tok_index < vec_tokens->length) {
    
        struct token *next = vec_tokens->data[tok_index];
        return next->kind;
    
    }
    
    return TOK_EOF;

}

void get_token (void) {

    if (tok.is_unget) {
    
        if (tok.start) {
            free ((char *) tok.start);
        }
    
    }
    
    if (tok.ident) { free (tok.ident); }
    memset (&tok, 0, sizeof (tok));
    
    for (;;) {
        
        if (!line || skip_whitespace (line) >= line_end) {
        
            if (vec_tokens) {
            
                struct token *tok;
                
                if (tok_index < vec_tokens->length) {
                    break;
                }
                
                while ((tok = vec_pop (vec_tokens))) {
                    free (tok);
                }
            
            }
            
            if (!get_line ()) {
            
                free ((char *) get_filename ());
                return;
            
            }
            
            continue;
        
        }
        
        vec_tokens = preprocess_line (&line);
        
        tok_index = 0;
        break;
    
    }
    
    if (tok_index < vec_tokens->length) {
    
        memcpy (&tok, vec_tokens->data[tok_index++], sizeof (tok));
        
        if (tok.kind == TOK_LBRACE) {
            token_brace_depth++;
        } else if (tok.kind == TOK_RBRACE) {
            token_brace_depth--;
        }
        
        if (tok.kind == TOK_CCHAR) {
        
            scan_ch (&tok);
            return;
        
        }
        
        if (tok.kind == TOK_PPNUM) {
        
            parse_number (&tok);;
            return;
        
        }
        
        if (tok.kind == TOK_IDENT) {
        
            tok.kind = find_kind (tok.ident);;
            return;
        
        }
    
    }

}

int init_tokenizer (const char *ifile) {

    const char *tmp;
    
    if (!ifile || strcmp (ifile, "-") == 0) {
    
        tmp = xstrdup ("<stdin>");
        
        set_real_filename (tmp);
        set_filename (tmp);
        
        state->ifp = stdin;
    
    } else {
    
        tmp = xstrdup (ifile);
        
        set_real_filename (tmp);
        set_filename (tmp);
        
        if (!(state->ifp = fopen (ifile, "r"))) {
        
            report_at (program_name, 0, REPORT_FATAL_ERROR, "failed to open '%s' for reading", ifile);
            return 1;
        
        }
    
    }
    
    set_real_line_number (0);
    set_line_number (0);
    
    new_line_number = 1, newlines = 0;
    start = 0, line = 0;
    
    line_end = 0;
    real_new_line_number = 1;
    
    token_brace_depth = 0;
    load_line_internal_data = load_line_create_internal_data (&new_line_number);
    
    return 0;

}
