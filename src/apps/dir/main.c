/******************************************************************************
 * @file            main.c
 *****************************************************************************/
#include    <sys/ioctl.h>
#include    <sys/stat.h>

#include    <dirent.h>
#include    <stdint.h>
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>
#include    <unistd.h>

typedef struct dir_entry {

    char *name;
    uint8_t type;

};

/******************************************************************************
 * @function        dir_compare
 *****************************************************************************/
static int dir_compare (const struct dir_entry *a, const struct dir_entry *b) {

    if (a->type == DT_DIR && b->type != DT_DIR) {
        return -1;
    }
    
    if (a->type != DT_DIR && b->type == DT_DIR) {
        return 1;
    }
    
    return strcmp (a->name, b->name);

}

/******************************************************************************
 * @function        dir_print_time
 *****************************************************************************/
static void dir_print_time (int64_t seconds) {

    static const uint8_t month_days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    
    uint32_t year = 1970, month = 1, days_in_year, days_in_month;
    uint32_t day, hour, minute;
    
    uint64_t days;
    
    if (seconds <= 0) {
    
        printf ("--/--/----  --:--");
        return;
    
    }
    
    days = (uint64_t) seconds / 86400;
    hour = (uint32_t) (((uint64_t) seconds / 3600) % 24);
    minute = (uint32_t) (((uint64_t) seconds / 60) % 60);
    
    for (;;) {
    
        days_in_year = 365 + (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
        
        if (days < days_in_year) {
            break;
        }
        
        days -= days_in_year;
        year++;
    
    }
    
    for (;;) {
    
        days_in_month = month_days[month - 1];
        
        if (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) {
            days_in_month++;
        }
        
        if (days < days_in_month) {
            break;
        }
        
        days -= days_in_month;
        month++;
    
    }
    
    day = (uint32_t) days + 1;
    printf ("%02u/%02u/%04u  %02u:%02u", day, month, year, hour, minute);

}

/******************************************************************************
 * @function        list_directory
 *****************************************************************************/
static int list_directory (const char *path, int multi, int depth) {

    struct dir_entry *names = 0, *temp;
    
    size_t count = 0, capacity = 0, j, k;
    int failed = 0;
    
    size_t base_length, directories, files;
    uint64_t total_bytes;
    
    struct dirent *de;
    DIR *dir;
    
    if (!(dir = opendir (path))) {
        return 1;
    }
    
    if (multi) {
    
        if (depth > 1) { printf ("\n"); }
        printf (" Directory of %s\n\n", path);
    
    }
    
    while ((de = readdir (dir))) {
    
        size_t len = strlen (de->d_name);
        char *name;
        
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
        
        names[count].type = de->d_type;
        names[count].name = name;
        
        count++;
    
    }
    
    if (!failed) {
    
        for (j = 1; j < count; j++) {
        
            struct dir_entry entry = names[j];
            k = j;
            
            while (k > 0 && dir_compare (&names[k - 1], &entry) > 0) {
            
                names[k] = names[k - 1];
                k--;
            
            }
            
            names[k] = entry;
        
        }
        
        total_bytes = 0;
        
        directories = 0;
        files = 0;
        
        base_length = strlen (path);
        
        for (j = 0; j < count; j++) {
        
            size_t name_length = strlen (names[j].name);
            
            struct stat st;
            char *entry;
            
            int found = 0;
            
            if (base_length <= SIZE_MAX - name_length - 2 && (entry = malloc (base_length + name_length + 2))) {
            
                memcpy (entry, path, base_length);
                
                if (base_length && path[base_length - 1] != '/') {
                
                    entry[base_length] = '/';
                    memcpy (entry + base_length + 1, names[j].name, name_length + 1);
                
                } else {
                    memcpy (entry + base_length, names[j].name, name_length + 1);
                }
                
                found = stat (entry, &st) == 0;
                free (entry);
            
            }
            
            dir_print_time (found ? st.st_mtime : 0);
            
            if (names[j].type == DT_DIR) {
            
                printf ("    <DIR>          %s\n", names[j].name);
                directories++;
            
            } else {
            
                printf ("    %14lld %s\n", (long long) (found ? st.st_size : 0), names[j].name);
                
                if (found && st.st_size > 0) {
                    total_bytes += (uint64_t) st.st_size;
                }
                
                files++;
            
            }
        
        }
        
        printf ("%16lu File(s) %llu bytes\n", (unsigned long) files, (unsigned long long) total_bytes);
        printf ("%16lu Dir(s)\n", (unsigned long) directories);
    
    }
    
    closedir (dir);
    
    for (j = 0; j < count; j++) {
        free (names[j].name);
    }
    
    free (names);
    
    if (failed) {
        return 1;
    }

}

/******************************************************************************
 * @function        main
 *****************************************************************************/
int main (int argc, char **argv) {

    char path[4097] = { 0 };
    
    if (argc > 1) {
    
        int ret = 0, i;
        
        for (i = 1; i < argc; i++) {
        
            if ((ret = list_directory (argv[i], argc > 1, i))) {
                return ret;
            }
        
        }
        
        return 0;
    
    }
    
    if (!getcwd (path, 4096)) {
        return 1;
    }
    
    return list_directory (path, 0, 0);

}
