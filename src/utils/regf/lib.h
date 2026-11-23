/******************************************************************************
 * @file            lib.h
 *****************************************************************************/
#ifndef     _LIB_H
#define     _LIB_H

unsigned char *ascii_to_utf16le (char **pp, uint32_t *out_len);

void ignore_rest_of_line (char **pp);
void parse_args (int argc, char **argv, int optind);

char *skip_whitespace (char *__p);
char *get_token (char **pp);

char *xstrdup (const char *__p);
char *xstrndup (const char *__p, unsigned long __len);

int xstrcasecmp (const char *__s1, const char *__s2);
int xstrncasecmp (const char *__s1, const char *__s2, unsigned long __len);

void *xmalloc (unsigned long __size);
void *xrealloc (void *__ptr, unsigned long __size);


const char *get_filename (void);
unsigned long get_line_number (void);

void get_filename_and_line_number (const char **__filename_p, unsigned long *__line_number_p);
void set_line_number (unsigned long __line_number);
void set_filename_and_line_number (const char *__filename, unsigned long __line_number);


uint32_t align8 (uint32_t x);

uint16_t rd16 (uint8_t *p);
uint32_t rd32 (uint8_t *p);

void wr16 (uint8_t *p, uint16_t v);
void wr32 (uint8_t *p, uint32_t v);
void wr64 (uint8_t *p, uint64_t v);


int ascii_casecmp (const char *a, const char *b);
uint32_t utf16le_len_bytes_from_ascii(const char *s);

#include    <time.h>
uint64_t unix_time_to_filetime (time_t t);

#endif      /* _LIB_H */
