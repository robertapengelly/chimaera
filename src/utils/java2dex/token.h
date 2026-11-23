/******************************************************************************
 * @file            token.h
 *****************************************************************************/
#ifndef     _TOKEN_H
#define     _TOKEN_H

enum token_kind {

    TOK_EOF = 0,
    
    /* Single character punctionation. */
    TOK_XMARK = 33,
    TOK_HASH = 35,
    TOK_DOLLAR,
    TOK_MOD,
    TOK_AMPER,
    TOK_LPAREN = 40,
    TOK_RPAREN,
    TOK_STAR,
    TOK_PLUS,
    TOK_COMMA,
    TOK_MINUS,
    TOK_DOT,
    TOK_BSLASH,
    TOK_COLON = 58,
    TOK_SEMI,
    TOK_LESS,
    TOK_ASSIGN,
    TOK_GREATER,
    TOK_QMARK,
    TOK_AT = 64,
    TOK_LBRACK = 91,
    TOK_FSLASH,
    TOK_RBRACK,
    TOK_CARET,
    TOK_UNDERSCORE,
    TOK_BACKTICK,
    TOK_LBRACE = 123,
    TOK_PIPE,
    TOK_RBRACE,
    TOK_TILDE,
    
    /* Multi-character punctuation. */
    TOK_EQEQ = 128,
    TOK_NOTEQ,
    TOK_LTEQ,
    TOK_GTEQ,
    
    TOK_INCR,
    TOK_DECR,
    
    TOK_LOGAND,
    TOK_LOGOR,
    TOK_LSH,
    TOK_RSH,
    TOK_URSH,
    TOK_PLUSEQ,
    TOK_MINUSEQ,
    TOK_STAREQ,
    TOK_SLASHEQ,
    TOK_MODEQ,
    TOK_LSHEQ,
    TOK_RSHEQ,
    TOK_URSHEQ,
    TOK_ANDEQ,
    TOK_XOREQ,
    TOK_OREQ,
    TOK_ELLIPSIS,
    TOK_ARROW,
    
    /* Start of keywords. */
    TOK_ABSTRACT = 256,
    TOK_BOOLEAN,
    TOK_BREAK,
    TOK_BYTE,
    TOK_CASE,
    TOK_CATCH,
    TOK_CHAR,
    TOK_CLASS,
    TOK_CONST,
    TOK_CONTINUE,
    TOK_DEFAULT,
    TOK_DO,
    TOK_DOUBLE,
    TOK_ELSE,
    TOK_EXTENDS,
    TOK_FALSE,
    TOK_FINAL,
    TOK_FINALLY,
    TOK_FLOAT,
    TOK_FOR,
    TOK_GOTO,
    TOK_IF,
    TOK_IMPLEMENTS,
    TOK_IMPORT,
    TOK_INSTANCEOF,
    TOK_INT,
    TOK_INTERFACE,
    TOK_LONG,
    TOK_NATIVE,
    TOK_NEW,
    TOK_NULL,
    TOK_PACKAGE,
    TOK_PRIVATE,
    TOK_PROTECTED,
    TOK_PUBLIC,
    TOK_RETURN,
    TOK_SHORT,
    TOK_STRICTFP,
    TOK_SUPER,
    TOK_STATIC,
    TOK_SYNCHRONIZED,
    TOK_SWITCH,
    TOK_THIS,
    TOK_THROW,
    TOK_THROWS,
    TOK_TRANSIENT,
    TOK_TRUE,
    TOK_TRY,
    TOK_VOID,
    TOK_VOLATILE,
    TOK_WHILE,
    
    TOK_PPSTR,
    TOK_PPNUM,
    
    TOK_CCHAR,
    TOK_CINT,
    TOK_CLONG,
    
    TOK_CFLOAT,
    TOK_CDOUBLE,
    TOK_CLDOUBLE,
    
    TOK_IDENT
    
};

#include    "int64.h"

struct token {

    const char *start;
    const char *caret;
    
    char *ident;
    unsigned long len;
    
    enum token_kind kind;
    
    union {
    
        double ld;
        
        double d;
        float f;
        
        int64_s i;
    
    } val;
    
    int is_unget;

};

extern struct token tok;
extern int tok_ident;

void get_token (void);
int get_token_brace_depth (void);

enum token_kind peek_token_kind (void);
void unget_token (struct token *saved_tok);

struct punct {

    char *name;
    enum token_kind kind;

};

struct punct *find_punct (char *p);
int init_tokenizer (const char *ifile);

#endif      /* _TOKEN_H */
