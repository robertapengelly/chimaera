/******************************************************************************
 * @file            tree.c
 *****************************************************************************/
#include    <limits.h>
#include    <inttypes.h>
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>

#include    "pkg.h"
#include    "lib.h"

#include    "report.h"

extern int pkg_chc_compress (const unsigned char *in, uint64_t in_size, unsigned char **out, uint64_t *out_size);

static uint32_t crc_table[256];
static int crc_table_ready;

static void make_crc_table (void) {

    uint32_t c;
    int n, k;
    
    for (n = 0; n < 256; n++) {
    
        c = (uint32_t) n;
        
        for (k = 0; k < 8; k++) {
        
            if (c & 1) {
                c = 0xEDB88320UL ^ (c >> 1);
            } else {
                c = c >> 1;
            }
        
        }
        
        crc_table[n] = c;
    
    }
    
    crc_table_ready = 1;

}

static uint32_t pkg_crc32_file (const char *path) {

    unsigned char buf[4096];
    FILE *fp;
    
    uint32_t crc = 0xFFFFFFFFUL;
    uint64_t n, i;
    
    if (!crc_table_ready) {
        make_crc_table ();
    }
    
    if (!(fp = fopen (path, "rb"))) {
        return 0;
    }
    
    while ((n = fread (buf, 1, sizeof (buf), fp)) != 0) {
    
        for (i = 0; i < n; i++) {
            crc = crc_table[(crc ^ buf[i]) & 0xff] ^ (crc >> 8);
        }
    
    }
    
    fclose (fp);
    return crc ^ 0xFFFFFFFFUL;

}


static long pkg_file_size (const char *path) {

    FILE *fp = fopen (path, "rb");
    long size;
    
    if (!fp) {
        return -1;
    }
    
    if (fseek (fp, 0, SEEK_END) != 0) {
    
        fclose (fp);
        return -1;
    
    }
    
    size = ftell (fp);
    
    fclose (fp);
    return size;

}


static void appendf (char **buf, uint64_t *len, uint64_t *cap, const char *text) {

    uint64_t add = strlen (text);
    uint64_t new_cap;
    
    if (*len + add + 1 > *cap) {
    
        new_cap = *cap ? *cap * 2 : 1024;
        
        while (*len + add + 1 > new_cap) {
            new_cap *= 2;
        }
        
        *buf = xrealloc (*buf, new_cap);
        *cap = new_cap;
    
    }
    
    memcpy (*buf + *len, text, add);
    *len += add;
    
    (*buf)[*len] = '\0';

}

static void build_metadata (struct pkg_manifest *m, char **out, uint64_t *out_len) {

    char *buf = 0, *line;
    uint64_t len = 0, cap = 0;
    
    line = xmalloc (5 + strlen (m->name) + 2);
    sprintf (line, "name=%s\n", m->name);
    
    appendf (&buf, &len, &cap, line);
    free (line);
    
    line = xmalloc (8 + strlen (m->version) + 2);
    sprintf (line, "version=%s\n", m->version);
    
    appendf (&buf, &len, &cap, line);
    free (line);
    
    line = xmalloc (5 + strlen (m->arch) + 2);
    sprintf (line, "arch=%s\n", m->arch);
    
    appendf (&buf, &len, &cap, line);
    free (line);
    
    if (m->desc) {
    
        line = xmalloc (12 + strlen (m->desc) + 2);
        sprintf (line, "description=%s\n", m->desc);
        
        appendf (&buf, &len, &cap, line);
        free (line);
    
    }
    
    *out_len = len;
    *out = buf;

}

#define     INT_STR_SIZE(type)          ((sizeof (type) * 8 * 3) / 10 + 1)

static void build_file_table (struct pkg_manifest *m, char **out, uint64_t *out_len) {

    char *buf = 0, *line;
    uint64_t len = 0, cap = 0, i;
    
    struct pkg_file *f;
    
    for (i = 0; i < m->file_count; i++) {
    
        f = &m->files[i];
        
        line = xmalloc (strlen (f->dest) + 1 + INT_STR_SIZE (f->offset) + 1 + INT_STR_SIZE (f->size) + 1 + INT_STR_SIZE (f->crc32) + 2);
        sprintf (line, "%s\t%" PRIu64 "\t%" PRIu64 "\t%04X\n", f->dest, f->offset, f->size, f->crc32);
        
        appendf (&buf, &len, &cap, line);
        free (line);
    
    }
    
    *out_len = len;
    *out = buf;

}

static int write_integer (FILE *fp, uint64_t value, size_t size) {

    unsigned char b[8];
    size_t i;
    
    for (i = 0; i < size; i++) {
        b[i] = (value >> (CHAR_BIT * i)) & UCHAR_MAX;
    }
    
    return fwrite (b, 1, size, fp) == size ? 0 : -1;

}

static int write_header (FILE *fp, struct pkg_header *h) {

    if (fwrite (h->magic, 1, 4, fp) != 4) {
        return -1;
    }
    
    if (write_integer (fp, h->version, 4) != 0) {
        return -1;
    }
    
    if (write_integer (fp, h->metadata_size, 8) != 0) {
        return -1;
    }
    
    if (write_integer (fp, h->file_table_size, 8) != 0) {
        return -1;
    }
    
    if (write_integer (fp, h->payload_size, 8) != 0) {
        return -1;
    }
    
    if (write_integer (fp, h->file_count, 8) != 0) {
        return -1;
    }
    
    if (write_integer (fp, h->control_compression, 4) != 0) {
        return -1;
    }
    
    if (write_integer (fp, h->control_size, 8) != 0) {
        return -1;
    }
    
    if (write_integer (fp, h->payload_compression, 4) != 0) {
        return -1;
    }
    
    if (write_integer (fp, h->unpacked_payload_size, 8) != 0) {
        return -1;
    }
    
    return 0;

}

static int read_whole_file (const char *path, unsigned char *dst, uint64_t size) {

    FILE *fp = fopen (path, "rb");
    
    if (!fp) {
        return -1;
    }
    
    if (size && fread (dst, 1, size, fp) != size) {
    
        fclose (fp);
        return -1;
    
    }
    
    if (fclose (fp) != 0) {
        return -1;
    }
    
    return 0;

}

int pkg_write_package (struct pkg_manifest *m) {

    struct pkg_header h;
    uint64_t i;
    
    char *metadata = NULL;
    char *file_table = NULL;
    
    uint64_t metadata_len = 0;
    uint64_t file_table_len = 0;
    
    unsigned char *payload = 0;
    uint64_t unpacked_payload_size = 0;
    
    unsigned char *stored_payload = 0;
    uint64_t stored_payload_size = 0;
    
    unsigned char *control = 0;
    uint64_t control_size = 0;
    
    unsigned char *stored_control = 0;
    uint64_t stored_control_size = 0;
    
    uint32_t control_compression = PKG_COMPRESS_CHC;
    uint32_t payload_compression = PKG_COMPRESS_CHC;
    
    for (i = 0; i < m->file_count; i++) {
    
        int64_t size = pkg_file_size (m->files[i].source);
        
        if (size < 0) {
            return -1;
        }
        
        m->files[i].size = (uint64_t) size;
        
        m->files[i].offset = unpacked_payload_size;
        m->files[i].crc32 = pkg_crc32_file (m->files[i].source);
        
        unpacked_payload_size += (uint64_t) size;
    
    }
    
    build_metadata (m, &metadata, &metadata_len);
    build_file_table (m, &file_table, &file_table_len);
    
    control_size = (metadata_len + file_table_len);
    control = xmalloc (control_size ? control_size : 1);
    
    if (metadata_len) {
        memcpy (control, metadata, metadata_len);
    }
    
    if (file_table_len) {
        memcpy (control + metadata_len, file_table, file_table_len);
    }
    
    if (pkg_chc_compress (control, control_size, &stored_control, &stored_control_size) != 0) {
        return -1;
    }
    
    if (stored_control_size >= control_size && control_size != 0) {
    
        free (stored_control);
        
        stored_control_size = control_size;
        stored_control = control;
        
        control_compression = PKG_COMPRESS_NONE;
        control = 0;
    
    }
    
    payload = xmalloc (unpacked_payload_size ? unpacked_payload_size : 1);
    
    for (i = 0; i < m->file_count; i++) {
    
        if (read_whole_file (m->files[i].source, payload + m->files[i].offset, m->files[i].size) != 0) {
        
            free (payload);
            return -1;
        
        }
    
    }
    
    if (pkg_chc_compress (payload, unpacked_payload_size, &stored_payload, &stored_payload_size) != 0) {
    
        free (payload);
        return -1;
    
    }
    
    /* If compression makes it larger, store raw. */
    if (stored_payload_size >= unpacked_payload_size && unpacked_payload_size != 0) {
    
        free (stored_payload);
        
        stored_payload_size = unpacked_payload_size;
        stored_payload = payload;
        
        payload_compression = PKG_COMPRESS_NONE;
        payload = 0;
    
    }
    
    memset (&h, 0, sizeof (h));
    memcpy (h.magic, PKG_MAGIC, 4);
    
    h.version = PKG_VERSION;
    
    h.metadata_size = (uint64_t) metadata_len;
    h.file_table_size = (uint64_t) file_table_len;
    
    h.payload_size = stored_payload_size;
    h.file_count = (uint64_t) m->file_count;
    
    h.control_compression = control_compression;
    h.control_size = stored_control_size;
    
    h.payload_compression = payload_compression;
    h.unpacked_payload_size = unpacked_payload_size;
    
    if (!(state->ofp = fopen (state->ofile, "wb"))) {
        return -1;
    }
    
    if (write_header (state->ofp, &h) != 0) {
        return -1;
    }
    
    if (stored_control_size && fwrite (stored_control, 1, stored_control_size, state->ofp) != stored_control_size) {
        return -1;
    }
    
    if (stored_payload_size && fwrite (stored_payload, 1, stored_payload_size, state->ofp) != stored_payload_size) {
        return -1;
    }
    
    return 0;

}
