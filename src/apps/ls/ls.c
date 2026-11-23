/******************************************************************************
 * @file            ls.c
 *****************************************************************************/
#include    <sys/ioctl.h>
#include    <sys/stat.h>

#include    <dirent.h>
#include    <stdint.h>
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>
#include    <time.h>
#include    <unistd.h>

#include    "lib.h"
#include    "ls.h"

typedef struct ls_entry {

    char *name;
    
    uint8_t type;
    struct stat st;

};

struct ls_state *state = 0;
const char *program_name = 0;

static uint32_t line_width = 80;
static size_t count = 0, width = 0;

static uint32_t columns;

/******************************************************************************
 * @function        ls_compare
 *****************************************************************************/
static int ls_compare (const struct ls_entry *a, const struct ls_entry *b) {

    if (a->type == DT_DIR && b->type != DT_DIR) {
        return -1;
    }
    
    if (a->type != DT_DIR && b->type == DT_DIR) {
        return 1;
    }
    
    return strcmp (a->name, b->name);

}

/******************************************************************************
 * @function        list_file
 *****************************************************************************/
static int list_file (const char *path, struct stat *st, int depth) {

    int k;
    
    if (state->long_listing) {
    
        char mode[11] = "----------";
        char date[32] = "??? ?? ??:??";
        
        time_t modified = (time_t) st->st_mtime;
        struct tm *tm = localtime (&modified);
        
        switch (st->st_mode & S_IFMT) {
        
            case S_IFDIR:
            
                mode[0] = 'd';
                break;
            
            case S_IFLNK:
            
                mode[0] = 'l';
                break;
            
            case S_IFCHR:
            
                mode[0] = 'c';
                break;
            
            case S_IFBLK:
            
                mode[0] = 'b';
                break;
            
            case S_IFIFO:
            
                mode[0] = 'p';
                break;
            
            case S_IFSOCK:
            
                mode[0] = 's';
                break;
        
        }
        
        for (k = 0; k < 9; k++) {
        
            if (st->st_mode & (0400 >> k)) {
                mode[k + 1] = "rwx"[k % 3];
            }
        
        }
        
        if (st->st_mode & 04000) {
            mode[3] = st->st_mode & 0100 ? 's' : 'S';
        }
        
        if (st->st_mode & 02000) {
            mode[6] = st->st_mode & 0010 ? 's' : 'S';
        }
        
        if (st->st_mode & 01000) {
            mode[9] = st->st_mode & 0001 ? 't' : 'T';
        }
        
        if (tm) {
            strftime (date, sizeof (date), "%b %d %H:%M", tm);
        }
        
        printf ("%s %2u %u %u %8lld %s %s\n", mode, (unsigned) st->st_nlink, (unsigned) st->st_uid, (unsigned) st->st_gid, (long long) st->st_size, date, path);
    
    } else {
    
        columns = width + 2 >= line_width ? 1 : line_width / (uint32_t) (width + 2);
        printf ("%s", path);
        
        if ((depth + 1) % columns == 0 || depth + 1 == count) {
            printf ("\n");
        } else {
            printf ("%*s", (int) (width + 2 - strlen (path)), "");
        }
    
    }
    
    return 0;

}

/******************************************************************************
 * @function        list_directory
 *****************************************************************************/
static int list_directory (const char *path, int multi, int depth) {

    struct ls_entry *names = 0, *temp;
    size_t count = 0, width = 0, capacity = 0, j, k;
    
    size_t path_length = strlen (path);
    int64_t total_blocks = 0;
    
    struct dirent *de;
    DIR *dir;
    
    int failed = 0;
    
    if (!(dir = opendir (path))) {
        return 1;
    }
    
    if (multi) {
    
        if (depth > 0) { printf ("\n"); }
        printf ("\033[92m%s\033[0m:\n", path);
    
    }
    
    while ((de = readdir (dir))) {
    
        char *name;
        size_t len;
        
        if (!state->list_hidden && de->d_name[0] == '.') {
            continue;
        }
        
        len = strlen (de->d_name);
        
        if (count == capacity) {
        
            size_t next_capacity = capacity ? capacity * 2 : 16;
            
            if (capacity > ((size_t) -1 / sizeof (*names)) / 2 || !(temp = realloc (names, next_capacity * sizeof (*names)))) {
            
                failed = 1;
                break;
            
            }
            
            names = temp;
            capacity = next_capacity;
        
        }
        
        if (!(name = malloc (len + 1))) {
        
            failed = 1;
            break;
        
        }
        
        memcpy (name, de->d_name, len + 1);
        
        if (state->long_listing) {
        
            char *fullpath;
            
            if (!(fullpath = malloc (path_length + len + 2))) {
            
                free (name);
                
                failed = 1;
                break;
            
            }
            
            sprintf (fullpath, "%s%s%s", path, path_length && path[path_length - 1] == '/' ? "" : "/", name);
            
            if (stat (fullpath, &names[count].st) < 0) {
            
                fprintf (stderr, "%s: cannot access '%s'\n", program_name, fullpath);
                
                free (fullpath);
                free (name);
                
                failed = 1;
                break;
            
            }
            
            free (fullpath);
            total_blocks += names[count].st.st_blocks;
        
        }
        
        names[count].type = de->d_type;
        names[count].name = name;
        
        count++;
        
        if (len > width) {
            width = len;
        }
    
    }
    
    closedir (dir);
    
    if (!failed) {
    
        for (j = 1; j < count; j++) {
        
            struct ls_entry entry = names[j];
            k = j;
            
            while (k > 0 && ls_compare (&names[k - 1], &entry) > 0) {
            
                names[k] = names[k - 1];
                k--;
            
            }
            
            names[k] = entry;
        
        }
        
        if (state->long_listing) {
        
            printf ("total %lld\n", (long long) ((total_blocks + 1) / 2));
            
            for (j = 0; j < count; j++) {
            
                char mode[11] = "----------";
                char date[32] = "??? ?? ??:??";
                
                time_t modified = (time_t) names[j].st.st_mtime;
                struct tm *tm = localtime (&modified);
                
                switch (names[j].st.st_mode & S_IFMT) {
                
                    case S_IFDIR:
                    
                        mode[0] = 'd';
                        break;
                    
                    case S_IFLNK:
                    
                        mode[0] = 'l';
                        break;
                    
                    case S_IFCHR:
                    
                        mode[0] = 'c';
                        break;
                    
                    case S_IFBLK:
                    
                        mode[0] = 'b';
                        break;
                    
                    case S_IFIFO:
                    
                        mode[0] = 'p';
                        break;
                    
                    case S_IFSOCK:
                    
                        mode[0] = 's';
                        break;
                
                }
                
                for (k = 0; k < 9; k++) {
                
                    if (names[j].st.st_mode & (0400 >> k)) {
                        mode[k + 1] = "rwx"[k % 3];
                    }
                
                }
                
                if (names[j].st.st_mode & 04000) {
                    mode[3] = names[j].st.st_mode & 0100 ? 's' : 'S';
                }
                
                if (names[j].st.st_mode & 02000) {
                    mode[6] = names[j].st.st_mode & 0010 ? 's' : 'S';
                }
                
                if (names[j].st.st_mode & 01000) {
                    mode[9] = names[j].st.st_mode & 0001 ? 't' : 'T';
                }
                
                if (tm) {
                    strftime (date, sizeof (date), "%b %d %H:%M", tm);
                }
                
                printf ("%s %2u %u %u %8lld %s ", mode, (unsigned) names[j].st.st_nlink, (unsigned) names[j].st.st_uid, (unsigned) names[j].st.st_gid, (long long) names[j].st.st_size, date);
                
                if (names[j].type == DT_DIR) {
                    printf ("\033[94m%s\033[0m", names[j].name);
                } else {
                    printf ("%s", names[j].name);
                }
                
                printf ("\n");
            
            }
        
        } else {
        
            columns = width + 2 >= line_width ? 1 : line_width / (uint32_t) (width + 2);
            
            for (j = 0; j < count; j++) {
            
                if (names[j].type == DT_DIR) {
                    printf ("\033[94m%s\033[0m", names[j].name);
                } else {
                    printf ("%s", names[j].name);
                }
                
                if ((j + 1) % columns == 0 || j + 1 == count) {
                    printf ("\n");
                } else {
                    printf ("%*s", (int) (width + 2 - strlen (names[j].name)), "");
                }
            
            }
        
        }
    
    }
    
    for (j = 0; j < count; j++) {
        free (names[j].name);
    }
    
    free (names);
    
    if (failed) {
        return 1;
    }
    
    return 0;

}

/******************************************************************************
 * @function        main
 *****************************************************************************/
int main (int argc, char **argv) {
    
    struct winsize size;
    char path[4097] = { 0 };
    
    if (argc && *argv) {
    
        const char *p;
        program_name = *argv;
        
        if ((p = strrchr (program_name, '/')) || (p = strrchr (program_name, '\\'))) {
            program_name = (p + 1);
        }
    
    }
    
    state = xmalloc (sizeof (*state));
    parse_args (argc, argv, 1);
    
    if (ioctl (STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col) {
        line_width = size.ws_col;
    }
    
    if (state->nb_paths > 0) {
    
        struct stat st;
        size_t len, i;
        
        char *path;
        int ret = 0, need_path = 0, index = 0;
        
        for (i = 0; i < (size_t) state->nb_paths; i++) {
        
            path = state->paths[i];
            
            if (stat (path, &st) < 0) {
            
                fprintf (stderr, "%s: cannot access '%s'\n", program_name, path);
                continue;
            
            }
            
            if ((st.st_mode & S_IFMT) == S_IFDIR) {
            
                need_path = i > 0;
                continue;
            
            }
            
            len = strlen (path);
            
            if (len > width) {
                width = len;
            }
            
            count++;
        
        }
        
        for (i = 0; i < (size_t) state->nb_paths; i++) {
        
            path = state->paths[i];
            
            if (stat (path, &st) < 0) {
                continue;
            }
            
            if ((st.st_mode & S_IFMT) != S_IFDIR) {
            
                if ((ret = list_file (path, &st, index))) {
                    return ret;
                }
                
                index++;
            
            }
        
        }
        
        for (i = 0; i < (size_t) state->nb_paths; i++) {
        
            path = state->paths[i];
            
            if (stat (path, &st) < 0) {
                continue;
            }
            
            if ((st.st_mode & S_IFMT) == S_IFDIR) {
            
                if ((ret = list_directory (path, need_path, index))) {
                    return ret;
                }
                
                index++;
            
            }
        
        }
        
        return 0;
    
    }
    
    if (!getcwd (path, 4096)) {
        return 1;
    }
    
    return list_directory (path, 0, 0);

}
