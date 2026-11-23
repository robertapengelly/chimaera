/******************************************************************************
 * @file            unzip.c
 *****************************************************************************/
#include    <sys/stat.h>

#include    <assert.h>
#include    <errno.h>
#include    <limits.h>
#include    <stdint.h>
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>
#include    <time.h>

#include    "lib.h"
#include    "report.h"
#include    "tables.h"
#include    "unzip.h"
#include    "vector.h"

struct unzip_state *state = 0;
const char *program_name = 0;

static int make_directory (const char *path) {

    char *p = (char *) path;
    
    while (p && *p != '\0') {
    
        while (*p && *p == '/') {
            p++;
        }
        
        if (*p == '\0') { break; }
        
        if ((p = strchr (p, '/'))) {
            *p = '\0';
        }
        
        if (mkdir (path, 0755) < 0) {
        
            if (errno != EEXIST) {
                return 1;
            }
        
        }
        
        if (p) { *p = '/'; }
    
    }
    
    return 0;

}

static FILE *fp = 0;

struct eocdr {

    uint16_t disk_nbr;                  /* Number of this disk. */
    
    uint16_t cd_start_disk;             /* Nbr. of disk with start of the CD. */
    uint16_t disk_cd_entries;           /* Nbr. of CD entries on this disk. */
    uint16_t cd_entries;                /* Nbr. of Centeral Directory Entiries. */
    
    uint32_t cd_size;                   /* Centeral Directory size in bytes. */
    uint32_t cd_offset;                 /* Centeral Directory file offset. */
    
    uint16_t comment_len;               /* Archive comment length. */
    unsigned char *comment;             /* Archive comment. */

};

#define     EOCDR_SIGNATURE             0x504B0506
#define     EOCDR_BASE_SIZE             22

#define     MAX_BACK_OFFSET             (1024 + 100)

static int find_eocdr (struct eocdr *r) {

    unsigned long back_offset, length, signature;
    
    unsigned char *buf, *p;
    int ret = 0;
    
    fseek (fp, 0, SEEK_END);
    length = ftell (fp);
    
    for (back_offset = 0; back_offset <= MAX_BACK_OFFSET; back_offset++) {
    
        if (length < EOCDR_BASE_SIZE + back_offset) {
            break;
        }
        
        fseek (fp, length - EOCDR_BASE_SIZE - back_offset, SEEK_SET);
        
        if (!(buf = malloc (EOCDR_BASE_SIZE))) {
        
            ret = -1;
            break;
        
        }
        
        p = buf;
        
        if ((fread (buf, 1, EOCDR_BASE_SIZE, fp)) != EOCDR_BASE_SIZE) {
        
            free ((void *) buf);
            
            ret = -1;
            break;
        
        }
        
        signature = array_to_integer (p, 4, 1), p += 4;
        
        if (signature == EOCDR_SIGNATURE) {
        
            r->disk_nbr = array_to_integer (p, 2, 0), p += 2;
            r->cd_start_disk = array_to_integer (p, 2, 0), p += 2;
            r->disk_cd_entries = array_to_integer (p, 2, 0), p += 2;
            r->cd_entries = array_to_integer (p, 2, 0), p += 2;
            
            r->cd_size = array_to_integer (p, 4, 0), p += 4;
            r->cd_offset = array_to_integer (p, 4, 0), p += 4;
            
            if ((r->comment_len = array_to_integer (p, 2, 0)) > back_offset) {
            
                free ((void *) buf);
                
                ret = -1;
                break;
            
            }
            
            free ((void *) buf);
            
            if (r->comment_len > 0) {
                
                if ((r->comment = malloc (r->comment_len + 1))) {
                
                    memset (r->comment, 0, r->comment_len + 1);
                    
                    if (fread (r->comment, 1, r->comment_len, fp) != r->comment_len) {
                    
                        ret = -1;
                        break;
                    
                    }
                
                }
                
                break;
                
            }
            
            break;
        
        }
        
        free ((void *) buf);
    
    }
    
    rewind (fp);
    return ret;

}

struct cfh {

    uint16_t made_by_ver;               /* Version made by. */
    uint16_t extract_ver;               /* Version needed to extract. */
    uint16_t gp_flag;                   /* General-purpose bit flag. */
    uint16_t method;                    /* Compression method. */
    uint16_t mod_time;                  /* Modification time. */
    uint16_t mod_date;                  /* Modification date. */
    
    uint32_t crc32;                     /* CRC-32 checkusm. */
    uint32_t comp_size;                 /* Compressed size. */
    uint32_t uncomp_size;               /* Uncompressed size. */
    
    uint16_t name_len;                  /* Filename length. */
    uint16_t extra_len;                 /* Extra data length. */
    uint16_t comment_len;               /* Comment length. */
    uint16_t disk_nbr_start;            /* Disk nbr. where file begins. */
    
    uint16_t int_attrs;                 /* Internal file attributes. */
    uint32_t ext_attrs;                 /* External file attributes. */
    
    uint32_t lfh_offset;                /* Local File HEader offset. */
    
    char *name;                         /* Filename. */
    char *extra;                        /* Extra data. */
    char *comment;                      /* File comment. */

};

#define     CFH_SIGNATURE               0x504B0102
#define     CFH_BASE_SIZE               46

static int read_cfh (struct cfh *cfh) {

    unsigned char buf[CFH_BASE_SIZE], *p = buf;
    uint32_t signature;
    
    if (fread (buf, 1, CFH_BASE_SIZE, fp) != CFH_BASE_SIZE) {
        return -1;
    }
    
    signature = array_to_integer (buf, 4, 1), p += 4;
    
    if (signature != CFH_SIGNATURE) {
        return -1;
    }
    
    cfh->made_by_ver = array_to_integer (p, 2, 0), p += 2;
    cfh->extract_ver = array_to_integer (p, 2, 0), p += 2;
    cfh->gp_flag = array_to_integer (p, 2, 0), p += 2;
    cfh->method = array_to_integer (p, 2, 0), p += 2;
    cfh->mod_time = array_to_integer (p, 2, 0), p += 2;
    cfh->mod_date = array_to_integer (p, 2, 0), p += 2;
    cfh->crc32 = array_to_integer (p, 4, 0), p += 4;
    cfh->comp_size = array_to_integer (p, 4, 0), p += 4;
    cfh->uncomp_size = array_to_integer (p, 4, 0), p += 4;
    cfh->name_len = array_to_integer (p, 2, 0), p += 2;
    cfh->extra_len = array_to_integer (p, 2, 0), p += 2;
    cfh->comment_len = array_to_integer (p, 2, 0), p += 2;
    cfh->disk_nbr_start = array_to_integer (p, 2, 0), p += 2;
    cfh->int_attrs = array_to_integer (p, 2, 0), p += 2;
    cfh->ext_attrs = array_to_integer (p, 4, 0), p += 4;
    cfh->lfh_offset = array_to_integer (p, 4, 0), p += 4;
    
    if ((unsigned char *) (p - CFH_BASE_SIZE) != buf) {
    
        printf ("FUCKKKK\n");
        return -1;
    
    }
    
    if ((cfh->name = malloc (cfh->name_len + 1))) {
    
        memset (cfh->name, 0, cfh->name_len + 1);
        
        if (fread (cfh->name, 1, cfh->name_len, fp) != cfh->name_len) {
        
            free (cfh->name);
            return -1;
        
        }
    
    }
    
    if ((cfh->extra = malloc (cfh->extra_len))) {
    
        memset (cfh->extra, 0, cfh->extra_len);
        
        if (fread (cfh->extra, 1, cfh->extra_len, fp) != cfh->extra_len) {
        
            free (cfh->extra);
            free (cfh->name);
            
            return -1;
        
        }
    
    }
    
    if ((cfh->comment = malloc (cfh->comment_len + 1))) {
    
        memset (cfh->comment, 0, cfh->comment_len + 1);
        
        if (fread (cfh->comment, 1, cfh->comment_len, fp) != cfh->comment_len) {
        
            free (cfh->comment);
            free (cfh->extra);
            free (cfh->name);
            
            return -1;
        
        }
    
    }
    
    return 0;

}

static time_t dos2ctime (uint16_t dos_date, uint16_t dos_time) {

    struct tm tm = { 0 };
    
    tm.tm_sec = (dos_time & 0x1f) * 2;                      /* Bits 0--4; Secs divided by 2. */
    tm.tm_min = (dos_time >> 5) & 0x3f;                     /* Bits 5--10; Minute. */
    tm.tm_hour = (dos_time >> 11);                          /* Bits 11--15; Hour (0--23). */
    
    tm.tm_mday = (dos_date & 0x1f);                         /* Bits 0--4; Day (1--31). */
    tm.tm_mon = ((dos_date >> 5) & 0x0f) - 1;               /* Bits 5--8; Month (1--12). */
    tm.tm_year = (dos_date >> 9) + 80;                      /* Bits 9--15; Year - 1980. */
    
    tm.tm_isdst = -1;
    return mktime (&tm);

}

static void display_comment (const char *path) {

    struct eocdr eocdr = { 0 };
    
    if (find_eocdr (&eocdr)) {
    
        report_at (program_name, 0, REPORT_ERROR, "%s is not a valid ZIP file", path);
        return;
    
    }
    
    printf ("Achive: %s\n", path);
    
    if (eocdr.comment) {
    
        printf ("%s\n", eocdr.comment);
        free (eocdr.comment);
    
    }

}

static void list_zip (const char *path) {

    struct eocdr eocdr = { 0 };
    struct cfh cfh = { 0 };
    
    char date[11], time[6];
    time_t ctime;
    
    unsigned long final_size = 0, total_files = 0, i;
    
    if (find_eocdr (&eocdr)) {
    
        report_at (program_name, 0, REPORT_ERROR, "%s is not a valid ZIP file", path);
        return;
    
    }
    
    if (eocdr.disk_nbr != 0 || eocdr.cd_start_disk != 0 || eocdr.disk_cd_entries != eocdr.cd_entries) {
    
        report_at (program_name, 0, REPORT_INTERNAL_ERROR, "currently multi-volume archives aren't supported");
        return;
    
    }
    
    printf ("Achive: %s\n", path);
    if (eocdr.comment) { printf ("%s\n", eocdr.comment); }
    
    fseek (fp, eocdr.cd_offset, SEEK_SET);
    
    printf ("   Length        Date        Time   Name      \n");
    printf ("----------  ----------  ----------  ----------\n");
    
    for (i = 0; i < eocdr.cd_entries; i++) {
    
        if (read_cfh (&cfh)) {
        
            report_at (program_name, 0, REPORT_ERROR, "failed to process Ceneral File Header");
            break;
        
        }
        
        if (!cfh.name) {
        
            report_at (program_name, 0, REPORT_INTERNAL_ERROR, "bad filename");
            break;
        
        }
        
        ctime = dos2ctime (cfh.mod_date, cfh.mod_time);
        
        strftime (date, 11, "%Y-%m-%d", localtime (&ctime));
        strftime (time, 6, "%H:%M", localtime (&ctime));
        
        printf ("%10d  %10s  %10s  %s\n", cfh.uncomp_size, date, time, cfh.name);
        
        final_size += cfh.uncomp_size;
        total_files++;
        
        free (cfh.name);
        
        if (cfh.comment) { free (cfh.comment); }
        if (cfh.extra) { free (cfh.extra); }
        
        memset (&cfh, 0, sizeof (cfh));
    
    }
    
    printf ("----------  ----------  ----------  ----------\n");
    printf ("%10ld  %10s  %10s  %ld files\n", final_size, "", "", total_files);

}

static int is_relative (const char *name, uint64_t name_len) {

    uint64_t i = 0;
    
    if (name_len < 1) {
        return 0;
    }
    
    if (name[0] == '/' || name[0] == '\\' || name[0] == '~') {
        return 0;
    }
    
    for (i = 0; i < name_len; i++) {
    
        switch (name[i]) {
        
            case '<':
            case '>':
            case ':':
            case '"':
            case '|':
            case '?':
            case '*':
            
                return 0;
            
            case '.':
            
                if (i + 1 < name_len && name[i + 1] == '.') {
                    return 0;
                }
                
                break;
            
            default:
            
                if (name[i] < ' ') {
                    return 0;
                }
                
                break;
        
        }
    
    }
    
    if (name[i - 1] == ' ' || name[i - 1] == '.') {
        return 0;
    }
    
    return 1;

}

struct lfh {

    uint16_t extract_ver;               /* Version needed to extract. */
    uint16_t gp_flag;                   /* General-purpose bit flag. */
    uint16_t method;                    /* Compression method. */
    uint16_t mod_time;                  /* Modification time. */
    uint16_t mod_date;                  /* Modification date. */
    
    uint32_t crc32;                     /* CRC-32 checkusm. */
    uint32_t comp_size;                 /* Compressed size. */
    uint32_t uncomp_size;               /* Uncompressed size. */
    
    uint16_t name_len;                  /* Filename length. */
    uint16_t extra_len;                 /* Extra data length. */
    
    char *name;                         /* Filename. */
    unsigned char *extra;               /* Extra data. */

};

#define     LFH_SIGNATURE               0x504B0304
#define     LFH_BASE_SIZE               30

static int read_lfh (struct lfh *lfh) {

    unsigned char buf[CFH_BASE_SIZE], *p = buf;
    uint32_t signature;
    
    if (fread (buf, 1, LFH_BASE_SIZE, fp) != LFH_BASE_SIZE) {
        return -1;
    }
    
    signature = array_to_integer (buf, 4, 1), p += 4;
    
    if (signature != LFH_SIGNATURE) {
        return -1;
    }
    
    lfh->extract_ver = array_to_integer (p, 2, 0), p += 2;
    lfh->gp_flag = array_to_integer (p, 2, 0), p += 2;
    lfh->method = array_to_integer (p, 2, 0), p += 2;
    lfh->mod_time = array_to_integer (p, 2, 0), p += 2;
    lfh->mod_date = array_to_integer (p, 2, 0), p += 2;
    lfh->crc32 = array_to_integer (p, 4, 0), p += 4;
    lfh->comp_size = array_to_integer (p, 4, 0), p += 4;
    lfh->uncomp_size = array_to_integer (p, 4, 0), p += 4;
    lfh->name_len = array_to_integer (p, 2, 0), p += 2;
    lfh->extra_len = array_to_integer (p, 2, 0), p += 2;
    
    if ((unsigned char *) (p - LFH_BASE_SIZE) != buf) {
    
        printf ("FUCKKKK\n");
        return -1;
    
    }
    
    if ((lfh->name = malloc (lfh->name_len + 1))) {
    
        memset (lfh->name, 0, lfh->name_len + 1);
        
        if (fread (lfh->name, 1, lfh->name_len, fp) != lfh->name_len) {
        
            free (lfh->name);
            return -1;
        
        }
    
    }
    
    if ((lfh->extra = malloc (lfh->extra_len))) {
    
        memset (lfh->extra, 0, lfh->extra_len);
        
        if (fread (lfh->extra, 1, lfh->extra_len, fp) != lfh->extra_len) {
        
            free (lfh->extra);
            free (lfh->name);
            
            return -1;
        
        }
    
    }
    
    return 0;

}

/**
 * See Figure 14-7 in Hacker's Delight (2nd ed.), or
 * crc_reflected() in https://www.zlib.net/crc_v3.txt
 * or Garry S. Brown's implementation e.g. in
 * https://opensource.apple.com/source/xnu/xnu-792.13.8/bsd/libkern/crc32.c
 */
uint32_t crc32 (FILE *fp) {

    uint32_t crc = 0xffffffff;
    
    unsigned char buf[32];
    uint64_t i, n;
    
    while ((n = fread (buf, 1, sizeof (buf), fp))) {
    
        for (i = 0; i < n; i++) {
            crc = (crc >> 8) ^ crc32_tbl[(crc ^ buf[i]) & UCHAR_MAX];
        }
    
    }
    
    return crc ^ 0xffffffff;

}

static void extract_zip (const char *path) {

    struct eocdr eocdr = { 0 };
    
    struct cfh cfh = { 0 };
    struct lfh lfh = { 0 };
    
    char *temp, *p;
    int j;
    
    uint64_t src_used, dst_used;
    uint16_t i;
    
    struct stat sb;
    char ch = '/';
        
    unsigned char *data;
    uint64_t orig_offset;
    
    FILE *outfile;
    
    if (find_eocdr (&eocdr)) {
    
        report_at (program_name, 0, REPORT_ERROR, "%s is not a valid ZIP file", path);
        return;
    
    }
    
    if (eocdr.disk_nbr != 0 || eocdr.cd_start_disk != 0 || eocdr.disk_cd_entries != eocdr.cd_entries) {
    
        report_at (program_name, 0, REPORT_INTERNAL_ERROR, "currently multi-volume archives aren't supported");
        return;
    
    }
    
    printf ("Archive: %s\n", path);
    if (eocdr.comment) { printf ("%s\n", eocdr.comment); }
    
    fseek (fp, eocdr.cd_offset, SEEK_SET);
    
    for (i = 0; i < eocdr.cd_entries; i++) {
    
        if (read_cfh (&cfh)) {
        
            report_at (program_name, 0, REPORT_ERROR, "failed to process Ceneral File Header");
            break;
        
        }
        
        if (cfh.gp_flag & 1) {
        
            report_at (program_name, 0, REPORT_INTERNAL_ERROR, "currently encryption isn't supported");
            break;
        
        }
        
        if (!cfh.name) {
        
            report_at (program_name, 0, REPORT_INTERNAL_ERROR, "bad filename");
            break;
        
        }
        
        for (j = 0; j < state->xlist.length; j++) {
        
            if ((temp = xstrdup (state->xlist.data[j]))) {
            
                if (strchr (temp, '*')) {
                
                    if (wild_compare (temp, cfh.name)) {
                    
                        free (temp);
                        break;
                    
                    }
                
                } else {
                
                    if (strcmp (cfh.name, temp) == 0) {
                    
                        free (temp);
                        break;
                    
                    }
                    
                }
                
                free (temp);
            
            }
        
        }
        
        if (j < state->xlist.length) {
        
            free (cfh.name);
            
            if (cfh.comment) { free (cfh.comment); }
            if (cfh.extra) { free (cfh.extra); }
            
            memset (&cfh, 0, sizeof (cfh));
            continue;
        
        }
        
        p = cfh.name;
        
        if ((p = strrchr (cfh.name, ch))) {
        
            if (p) { *p = '\0'; }
            
            if (state->exdir) {
            
                if (!(temp = malloc (strlen (state->exdir) + 1 + strlen (cfh.name) + 1))) {
                
                    report_at (program_name, 0, REPORT_ERROR, "not enough free memory for name");
                    
                    if (p) { *p = ch; }
                    break;
                
                }
                
                sprintf (temp, "%s%c%s", state->exdir, ch, cfh.name);
                
                if (!is_relative (temp + strlen (state->exdir) + 1, strlen (cfh.name))) {
                
                    free (temp);
                    free (cfh.name);
                    
                    if (cfh.comment) { free (cfh.comment); }
                    if (cfh.extra) { free (cfh.extra); }
                    
                    memset (&cfh, 0, sizeof (cfh));
                    continue;
                
                }
            
            } else {
            
                if (!(temp = malloc (strlen (cfh.name) + 1))) {
                
                    report_at (program_name, 0, REPORT_ERROR, "not enough free memory for name");
                    
                    if (p) { *p = ch; }
                    break;
                
                }
                
                sprintf (temp, "%s", cfh.name);
                
                if (!is_relative (temp, strlen (cfh.name))) {
                
                    free (temp);
                    free (cfh.name);
                    
                    if (cfh.comment) { free (cfh.comment); }
                    if (cfh.extra) { free (cfh.extra); }
                    
                    memset (&cfh, 0, sizeof (cfh));
                    continue;
                
                }
            
            }
            
            if (!stat (temp, &sb)) {
            
                if (!S_ISDIR (sb.st_mode)) {
                
                    report_at (program_name, 0, REPORT_ERROR, "%s exists but is not a directory", temp);
                    
                    free (temp);
                    break;
                
                }
            
            } else {
                printf ("   creating: %s%c\n", temp, ch);
            }
            
            if (make_directory (temp)) {
            
                report_at (program_name, 0, REPORT_ERROR, "failed to create %s", temp);
                
                free (temp);
                break;
            
            }
            
            if (p) { *p = ch; }
            free (temp);
            
            if (cfh.name[cfh.name_len - 1] == ch) {
            
                free (cfh.name);
                
                if (cfh.comment) { free (cfh.comment); }
                if (cfh.extra) { free (cfh.extra); }
                
                memset (&cfh, 0, sizeof (cfh));
                continue;
            
            }
        
        }
        
        if (!is_relative (cfh.name, cfh.name_len)) {
            continue;
        }
        
        if (cfh.method != ZIP_STORE && cfh.method != ZIP_DEFLATE) {
        
            report_at (program_name, 0, REPORT_ERROR, "currently only ZIP_STORE && ZIP_DEFLATE are supported");
            break;
        
        }
        
        if (state->exdir) {
        
            if (!(temp = malloc (strlen (state->exdir) + 1 + cfh.name_len + 1))) {
            
                report_at (program_name, 0, REPORT_ERROR, "not enough free memory for name");
                break;
            
            }
            
            sprintf (temp, "%s%c%s", state->exdir, ch, cfh.name);
        
        } else {
        
            if (!(temp = malloc (cfh.name_len + 1))) {
            
                report_at (program_name, 0, REPORT_ERROR, "not enough free memory for name");
                break;
            
            }
            
            sprintf (temp, "%s", cfh.name);
        
        }
        
        orig_offset = ftell (fp);
        
        if (fseek (fp, cfh.lfh_offset, SEEK_SET)) {
        
            free (temp);
            break;
        
        }
        
        if (read_lfh (&lfh)) {
        
            report_at (program_name, 0, REPORT_ERROR, "failed to process Local File Header");
            
            fseek (fp, orig_offset, SEEK_SET);
            free (temp);
            
            break;
        
        }
        
        if (!(data = malloc (cfh.comp_size))) {
        
            fseek (fp, orig_offset, SEEK_SET);
            free (temp);
            
            break;
        
        }
        
        if (fread (data, 1, cfh.comp_size, fp) != cfh.comp_size) {
        
            fseek (fp, orig_offset, SEEK_SET);
            
            free (data);
            free (temp);
            
            break;
        
        }
        
        if (fseek (fp, orig_offset, SEEK_SET)) {
        
            free (data);
            free (temp);
            
            break;
        
        }
        
        if (cfh.method == ZIP_STORE) {
        
            assert (cfh.comp_size == cfh.uncomp_size);
            
            if (!(outfile = fopen (temp, "w+b"))) {
            
                report_at (program_name, 0, REPORT_ERROR, "failed to open '%s' for writing", temp);
                remove (temp);
                
                free (data);
                free (temp);
                
                break;
            
            }
            
            printf (" extracting: %s\n", temp);
            
            if (fwrite (data, 1, cfh.uncomp_size, outfile) != cfh.uncomp_size) {
            
                report_at (program_name, 0, REPORT_ERROR, "failed to extract %s", temp);
                fclose (outfile);
                
                free (data);
                remove (temp);
                
                break;
            
            }
            
            rewind (outfile);
            
            if (crc32 (outfile) != cfh.crc32) {
            
                report_at (program_name, 0, REPORT_ERROR, "%s: CRC-32 mismatch!", temp);
                fclose (outfile);
                
                free (data);
                remove (temp);
                
                break;
            
            }
            
            free (data);
            fclose (outfile);
        
        } else if (cfh.method == ZIP_DEFLATE) {
        
            if (!(outfile = fopen (temp, "w+b"))) {
            
                report_at (program_name, 0, REPORT_ERROR, "failed to open '%s' for writing", temp);
                remove (temp);
                
                free (data);
                free (temp);
                
                break;
            
            }
            
            printf ("  inflating: %s\n", temp);
            
            if (hwinflate (data, cfh.comp_size, &src_used, outfile, cfh.uncomp_size, &dst_used) != HWINF_OK) {
            
                report_at (program_name, 0, REPORT_ERROR, "failed to extract %s", temp);
                fclose (outfile);
                
                free (data);
                remove (temp);
                
                break;
            
            }
            
            rewind (outfile);
            
            if (crc32 (outfile) != cfh.crc32) {
            
                report_at (program_name, 0, REPORT_ERROR, "%s: CRC-32 mismatch!", temp);
                fclose (outfile);
                
                free (data);
                remove (temp);
                
                break;
            
            }
            
            free (data);
            fclose (outfile);
            
            if (src_used != cfh.comp_size || dst_used != cfh.uncomp_size) {
            
                report_at (program_name, 0, REPORT_ERROR, "failed to extract %s", temp);
                
                remove (temp);
                break;
            
            }
        
        }
        
        if (lfh.extra) { free (lfh.extra); }
        if (lfh.name) { free (lfh.name); }
        
        memset (&lfh, 0, sizeof (lfh));
        
        free (cfh.name);
        free (temp);
        
        if (cfh.comment) { free (cfh.comment); }
        if (cfh.extra) { free (cfh.extra); }
        
        memset (&cfh, 0, sizeof (cfh));
    
    }
    
    if (lfh.extra) { free (lfh.extra); }
    if (lfh.name) { free (lfh.name); }
    
    if (cfh.comment) { free (cfh.comment); }
    if (cfh.extra) { free (cfh.extra); }
    if (cfh.name) { free (cfh.name); }

}

int main (int argc, char **argv) {

    long i;
    
    if (argc && *argv) {
    
        char *p;
        program_name = *argv;
        
        if ((p = strrchr (program_name, '/')) || (p = strrchr (program_name, '\\'))) {
            program_name = (p + 1);
        }
    
    }
    
    state = xmalloc (sizeof (*state));
    parse_args (argc, argv, 1);
    
    if (state->nb_files == 0) {
    
        report_at (program_name, 0, REPORT_ERROR, "no input files provided");
        return EXIT_FAILURE;
    
    }
    
    if (state->exdir) {
    
        if (state->nb_files > 1) {
        
            report_at (program_name, 0, REPORT_ERROR, "only on file can be unzipped when exdir is specified");
            return EXIT_FAILURE;
        
        }
    
    }
    
    for (i = 0; i < state->nb_files; i++) {
    
        if (!(fp = fopen (state->files[i], "r+b"))) {
        
            report_at (program_name, 0, REPORT_ERROR, "failed to open '%s' for reading", state->files[i]);
            continue;
        
        }
        
        if (state->list) {
        
            list_zip (state->files[i]);
            
            fclose (fp);
            continue;
        
        }
        
        if (state->only_comment) {
        
            display_comment (state->files[i]);
            
            fclose (fp);
            continue;
        
        }
        
        if (state->exdir) {
        
            if (make_directory (state->exdir)) {
            
                report_at (program_name, 0, REPORT_ERROR, "failed to create %s", state->exdir);
                
                fclose (fp);
                return EXIT_FAILURE;
            
            }
        
        }
        
        extract_zip (state->files[i]);
        fclose (fp);
    
    }
    
    return (get_error_count () > 0 ? EXIT_FAILURE : EXIT_SUCCESS);

}
