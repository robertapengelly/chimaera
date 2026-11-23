/******************************************************************************
 * @file            tar.c
 *****************************************************************************/
#include    <sys/stat.h>

#include    <ctype.h>
#include    <dirent.h>
#include    <fcntl.h>
#include    <limits.h>
#include    <stddef.h>
#include    <stdint.h>
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>
#include    <time.h>
#include    <unistd.h>

#include    "lib.h"
#include    "report.h"
#include    "tar.h"

struct tar_state *state = 0;
const char *program_name = 0;

struct tar_raw_header {

    char name[100];
    char mode[8];
    char owner[8];
    char group[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char type;
    char linkname[100];
    char _padding[255];

};

static unsigned int checksum (struct tar_raw_header *header) {

    unsigned char *p = (unsigned char *) header;
    unsigned int res = 256, i;
    
    for (i = 0; i < offsetof (struct tar_raw_header, checksum); i++) {
        res += p[i];
    }
    
    for (i = offsetof (struct tar_raw_header, type); i < sizeof (*header); i++) {
        res += p[i];
    }
    
    return res;

}

static int write_null_bytes (FILE *fp, int n) {

    char nul = '\0';
    int i;
    
    for (i = 0; i < n; i++) {
    
        if (fwrite (&nul, 1, 1, fp) != 1) {
            return 1;
        }
    
    }
    
    return 0;

}

#ifndef     S_IFDIR
# define        S_IFDIR                 0040000
#endif

#ifndef     S_IFREG
# define        S_IFREG                 0100000
#endif

static void add_entry (FILE *tarfp, const char *root, const char *path) {

    struct tar_raw_header header;
    struct stat stbuf;
    
    const char *p = path;
    char *p2;
    
    unsigned long len;
    int mode;
    
    while (*p == '/') {
        p++;
    }
    
    if (root) {
    
        len = strlen (root);
        
        if (root[len - 1] == '/') {
        
            len += (strlen (p) + 1);
            
            p2 = xmalloc (len);
            sprintf (p2, "%s%s", root, p);
        
        } else {
        
            len += (1 + strlen (p) + 1);
            
            p2 = xmalloc (len);
            sprintf (p2, "%s/%s", root, p);
        
        }
    
    } else {
        p2 = xstrdup (path);
    }
    
    if (stat (p2, &stbuf) < 0) {
    
        report_at (program_name, 0, REPORT_ERROR, "failed to stat '%s'", p);
        return;
    
    }
    
    if (state->mode) {
    
        int o1, o2, o3;
        
        o1 = state->mode[1] - '0';
        o2 = state->mode[2] - '0';
        o3 = state->mode[3] - '0';
        
        mode = (o1 << 6) | (o2 << 3) | o3;
    
    } else {
        mode = stbuf.st_mode & 07777;
    }
    
    memset (&header, 0, sizeof (header));
    strcpy (header.name, p);
    
    sprintf (header.mode, "%07o", mode);
    sprintf (header.owner, "%07o", 0);
    sprintf (header.mtime, "%011llo", (uint64_t) stbuf.st_mtime);
    
    if (stbuf.st_mode & S_IFDIR) {
    
        DIR *dirp;
        
        struct dirent *de;
        char *subpath;
        
        memset (header.size, '0', 11);
        header.type = '5';              /* MTAR_TDIR */
        
        if (header.name[strlen(p) - 1] != '/') {
            header.name[strlen(p)] = '/';
        }
        
        sprintf (header.checksum, "%06o", checksum (&header));
        header.checksum[7] = ' ';
        
        if (fwrite (&header, 1, sizeof (header), tarfp) != sizeof (header)) {
            report_at (program_name, 0, REPORT_ERROR, "failed to write header to '%s'", state->target);
        }
        
        if (!(dirp = opendir (p2))) {
        
            report_at (program_name, 0, REPORT_ERROR, "failed to open directory '%s'", p);
            return;
        
        }
        
        while ((de = readdir (dirp))) {
        
            if (de->d_name[0] == '.' && (de->d_name[1] == '\0' || (de->d_name[1] == '.' && de->d_name[2] == '\0'))) {
                continue;
            }
            
            len = strlen (path);
            
            if (path[len - 1] == '/') {
            
                len += (strlen (de->d_name) + 1);
                
                if (!(subpath = malloc (len))) {
                
                    report_at (program_name, 0, REPORT_ERROR, "failed to allocate memory for '%s'", de->d_name);
                    continue;
                
                }
                
                sprintf (subpath, "%s%s", path, de->d_name);
            
            } else {
            
                len += (1 + strlen (de->d_name) + 1);
                
                if (!(subpath = malloc (len))) {
                
                    report_at (program_name, 0, REPORT_ERROR, "failed to allocate memory for '%s'", de->d_name);
                    continue;
                
                }
                
                sprintf (subpath, "%s/%s", path, de->d_name);
            
            }
            
            add_entry (tarfp, root, subpath);
            free (subpath);
        
        }
        
        closedir (dirp);
    
    } else if (stbuf.st_mode & S_IFREG) {
    
        FILE *fp;
        
        unsigned char *data;
        size_t bytes, read;
        
        if (!(fp = fopen (p2, "r+b"))) {
        
            report_at (program_name, 0, REPORT_WARNING, "failed to open '%s' for reading", path);
            goto _end;
        
        }
        
        fseek (fp, 0, SEEK_END);
        bytes = ftell (fp);
        
        fseek (fp, 0, SEEK_SET);
        
        sprintf (header.size, "%011llo", bytes);
        header.type = '0';              /* MTAR_TREG */
        
        sprintf (header.checksum, "%06o", checksum (&header));
        header.checksum[7] = ' ';
        
        if (fwrite (&header, 1, sizeof (header), tarfp) != sizeof (header)) {
        
            report_at (program_name, 0, REPORT_ERROR, "failed to write header to '%s'", state->target);
            fclose (fp);
            
            goto _end;
        
        }
        
        data = xmalloc (512);
        
        for (;;) {
        
            if (bytes == 0 || feof (fp)) {
                break;
            }
            
            read = (bytes > 512 ? 512 : bytes);
            
            if (fread (data, 1, read, fp) != read) {
            
                report_at (program_name, 0, REPORT_ERROR, "failed to read %lu bytes from %s", read, path);
                break;
            
            }
            
            if (fwrite (data, 1, read, tarfp) != read) {
            
                report_at (program_name, 0, REPORT_ERROR, "failed to write %lu bytes to %s", read, state->target);
                break;
            
            }
            
            bytes -= read;
        
        }
        
        bytes = (ftell (fp) % 512);
        fclose (fp);
        
        if (bytes != 0) {
            write_null_bytes (tarfp, 512 - bytes);
        }
    
    }
    
_end:
    
    free (p2);

}

static void extract_files (FILE *tarfp, char *root) {

    struct tar_raw_header header;
    
    char *path, *p2;
    int fp;
    
    unsigned long len;
    int mode;
    
    if (root) {
    
        len = strlen (root);
        
        while (root[len - 1] == '/') {
        
            root[len - 1] = '\0';
            len--;
        
        }
        
        if (mkdir (root, 0755)) {
        
            report_at (program_name, 0, REPORT_ERROR, "failed to root directory %s", root);
            return;
        
        }
        
        p2 = xmalloc (len + 1);
        sprintf (p2, "%s/", root);
    
    } else {
        p2 = xstrdup ("");
    }
    
    for (;;) {
    
        if (feof (tarfp)) {
            break;
        }
        
        if (fread (&header, 1, sizeof (header), tarfp) != sizeof (header)) {
        
            report_at (program_name, 0, REPORT_ERROR, "failed to read archive header");
            break;
        
        }
        
        if (!header.name[0]) {
            break;
        }
        
        mode = strtol (header.mode, 0, 8);
        
        path = xmalloc (strlen (p2) + strlen (header.name) + 1);
        sprintf (path, "%s%s", p2, header.name);
        
        printf ("extracting: %s\n", path);
        
        if (path[strlen (path) - 1] == '/') {
        
            path[strlen (path) - 1] = '\0';
            
            if (mkdir (path, mode)) {
            
                report_at (program_name, 0, REPORT_ERROR, "failed to create directory %s, %lx", path, ftell (tarfp));
                
                free (path);
                free (p2);
                
                return;
            
            }
        
        } else {
        
            unsigned char *data;
            size_t padding = 0, bytes, read;
            
            if ((fp = open (path, O_CREAT | O_WRONLY | O_TRUNC, mode)) < 0) {
            
                report_at (program_name, 0, REPORT_ERROR, "failed to create %s", path);
                
                free (path);
                free (p2);
                
                return;
            
            }
            
            bytes = strtol (header.size, 0, 8);
            
            if ((bytes % 512) != 0) {
                padding = 512 - (bytes % 512);
            }
            
            data = xmalloc (512);
            
            for (;;) {
            
                memset (data, 0, 512);
                
                if (bytes == 0) {
                    break;
                }
                
                read = (bytes > 512 ? 512 : bytes);
                
                if (fread (data, 1, read, tarfp) != read) {
                
                    report_at (program_name, 0, REPORT_ERROR, "failed to read %lu bytes from %s", read, path);
                    break;
                
                }
                
                if (write (fp, data, read) != (long) read) {
                
                    report_at (program_name, 0, REPORT_ERROR, "failed to write %lu bytes to %s", read);
                    break;
                
                }
                
                bytes -= read;
            
            }
            
            fseek (tarfp, padding, SEEK_CUR);
            
            free (data);
            close (fp);
        
        }
        
        free (path);
    
    }
    
    free (p2);

}


int main (int argc, char **argv) {

    FILE *tarfp;
    long i;
    
    char *mode;
    int action;
    
    if (argc && *argv) {
    
        const char *p;
        program_name = *argv;
        
        if ((p = strrchr (program_name, '/')) || (p = strrchr (program_name, '\\'))) {
            program_name = (p + 1);
        }
    
    }
    
    state = xmalloc (sizeof (*state));
    parse_args (argc, argv, 1);
    
    if (!state->target) {
    
        report_at (program_name, 0, REPORT_ERROR, "missing -f");
        return EXIT_FAILURE;
    
    }
    
    action = (state->create + state->append + state->extract);
    
    if (action != 1) {
    
        if (action < 1) {
            report_at (program_name, 0, REPORT_ERROR, "you must specify one of the '-crx' options");
        } else {
            report_at (program_name, 0, REPORT_ERROR, "you may not specify more than one '-crx' option");
        }
        
        return EXIT_FAILURE;
    
    }
    
    if (state->create) {
        mode = "w+b";
    } else {
        mode = "r+b";
    }
    
    if (!(tarfp = fopen (state->target, mode))) {
    
        report_at (program_name, 0, REPORT_ERROR, "failed to open '%s'", state->target);
        return EXIT_FAILURE;
    
    }
    
    if (state->extract) {
        extract_files (tarfp, state->root);
    } else {
    
        if (state->append) { fseek (tarfp, 0, SEEK_END); }
        
        for (i = 0; i < state->nb_paths; i++) {
            add_entry (tarfp, state->root, state->paths[i]);
        }
        
        write_null_bytes (tarfp, sizeof (struct tar_raw_header) * 2);
    
    }
    
    fclose (tarfp);
    return (get_error_count () > 0 ? EXIT_FAILURE : EXIT_SUCCESS);

}
