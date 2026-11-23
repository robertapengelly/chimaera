/******************************************************************************
 * @file            main.c
 *****************************************************************************/
#include    <sys/stat.h>
#include    <sys/wait.h>

#include    <ctype.h>
#include    <errno.h>
#include    <dirent.h>
#include    <fcntl.h>
#include    <stdint.h>
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>
#include    <termios.h>
#include    <time.h>
#include    <unistd.h>

#include    "hashtab.h"
#include    "ll.h"
#include    "vector.h"

static struct termios terminal_original_input = { 0 };
static int terminal_input_mode_changed = 0;
static int pipeline_child = 0;

#define     CAPACITY_INCREMENT          256

static char *command = 0;
static size_t command_capacity = 0, command_index = 0;

static struct hashtab hashtab_builtins = { 0 };
static int replace_char = 0;

static char *skip_whitespace (char *p) {

    while (isspace ((int) *p)) {
        p++;
    }
    
    return p;

}

static char *shell_word_end (char *p) {

    char quote = 0;
    
    while (*p) {
    
        if (*p == '\\' && quote != '\'' && p[1]) {
        
            p += 2;
            continue;
        
        }
        
        if (quote) {
        
            if (*p == quote) {
                quote = 0;
            }
        
        } else if (*p == '\'' || *p == '"') {
            quote = *p;
        } else if (isspace ((int) *p) || *p == '>' || *p == '<') {
            break;
        }
        
        p++;
    
    }
    
    return p;

}

static char *parse_word (char **p) {

    char *start, *read, *write;
    char quote = 0;
    
    read = skip_whitespace (*p);
    
    if (!*read) {
    
        *p = read;
        return 0;
    
    }
    
    start = write = read;
    
    while (*read && (quote || !isspace ((int) *read))) {
    
        if (*read == '\\' && quote != '\'' && read[1]) {
        
            if (quote == '"' && read[1] != '$' && read[1] != '`' && read[1] != '"' && read[1] != '\\' && read[1] != '\n') {
                *write++ = *read++;
                continue;
            }
            
            read++;
            
            if (*read != '\n') {
                *write++ = *read;
            }
            
            read++;
            continue;
        
        }
        
        if (quote) {
        
            if (*read == quote) {
                quote = 0;
                read++;
                continue;
            }
        
        } else if (*read == '\'' || *read == '"') {
        
            quote = *read++;
            continue;
        
        }
        
        *write++ = *read++;
    
    }
    
    *p = *read ? read + 1 : read;
    
    *write = '\0';
    return start;

}

struct builtin {

    const char *name;
    int (*handler) (const char *);

};

static struct vector vec_history = { 0 };
static char prompt[5010] = { 0 };

static struct builtin *find_builtin (const char *name);
static void create_prompt (void);

/******************************************************************************
 * @function        handler_cd
 *****************************************************************************/
int handler_cd (const char *p) {

    char *copy, *next, *path;
    
    if (!p || !(copy = strdup (p))) {
        return 1;
    }
    
    next = copy;
    path = parse_word (&next);
    
    if (!path || parse_word (&next)) {
    
        free (copy);
        return 1;
    }
    
    if (chdir (path) < 0) {
    
        printf ("cd: %s: Not a direcory\n", path);
        
        free (copy);
        return 1;
    
    }
    
    create_prompt ();
    
    free (copy);
    return 0;

}

/******************************************************************************
 * @function        handler_echo
 *****************************************************************************/
int handler_echo (const char *p) {

    char *copy, *next, *word;
    int first = 1;
    
    if (!(copy = strdup (p))) {
        return 1;
    }
    
    next = copy;
    
    while ((word = parse_word (&next))) {
    
        if (!first) {
            printf (" ");
        }
        
        printf ("%s", word);
        first = 0;
    
    }
    
    printf ("\n");
    
    free (copy);
    return 0;

}

/******************************************************************************
 * @function        handler_exit
 *****************************************************************************/
int handler_exit (const char *p) {

    (void) p;
    exit (0);

}

/******************************************************************************
 * @function        handler_pwd
 *****************************************************************************/
int handler_pwd (const char *p) {

    char path[4096];
    
    if (!getcwd (path, sizeof (path))) {
        return 1;
    }
    
    printf ("%.*s\n", sizeof (path), path);
    
    (void) p;
    return 0;

}

/******************************************************************************
 * @function        type_command
 *****************************************************************************/
static int type_command (const char *name) {

    struct stat st;
    size_t len;
    
    char *path, *end, *file;
    int found;
    
    if (find_builtin (name)) {
        
        printf ("%s is a shell builtin\n", name);
        return 0;
    
    }
    
    if (strchr (name, '/') || strchr (name, '\\')) {
        
        if (!stat (name, &st) && S_ISREG (st.st_mode)) {
            
            printf ("%s is %s\n", name, name);
            return 0;
        
        }
        
    } else if ((path = getenv ("PATH"))) {
        
        while (*path) {
            
            end = strchr (path, ':');
            len = end ? (size_t) (end - path) : strlen (path);
            
            if (len) {
                
                if (!(file = malloc (len + strlen (name) + 2))) {
                    return 1;
                }
                
                memcpy (file, path, len);
                file[len] = '/';
                strcpy (file + len + 1, name);
                
                found = !stat (file, &st) && S_ISREG (st.st_mode);
                
                if (found) {
                    printf ("%s is %s\n", name, file);
                }
                
                free (file);
                
                if (found) {
                    return 0;
                }
            
            }
            
            if (!end) {
                break;
            }
            
            path = end + 1;
        
        }
    
    }
    
    fprintf (stderr, "%s: not found\n", name);
    return 1;

}

/******************************************************************************
 * @function        handler_type
 *****************************************************************************/
int handler_type (const char *p) {

    char *copy, *next, *name;
    int ret = 0;
    
    if (!(copy = strdup (p))) {
        return 1;
    }
    
    next = copy;
    
    while ((name = parse_word (&next))) {
    
        if (type_command (name)) {
            ret = 1;
        }
    
    }
    
    free (copy);
    return ret;

}

/* Builtin array. */
static struct builtin builtins[] = {

    {   "cd",       &handler_cd     },
    {   "echo",     &handler_echo   },
    {   "exit",     &handler_exit   },
    {   "pwd",      &handler_pwd    },
    {   "type",     &handler_type   },
    {   0,          0               }

};

/******************************************************************************
 * @function        install_builtins
 *****************************************************************************/
static void install_builtins (void) {

    struct builtin *entry;
    struct hashtab_name *key;
    
    for (entry = builtins; entry->name; entry++) {
    
        if (hashtab_get_key (&hashtab_builtins, entry->name)) {
        
            printf ("duplicate entry '%s'", entry->name);
            continue;
        
        }
        
        if (!(key = hashtab_alloc_name (entry->name))) {
        
            printf ("failed to allocate memory for '%s'", entry->name);
            continue;
        
        }
        
        hashtab_put (&hashtab_builtins, key, entry);
    
    }

}

/******************************************************************************
 * @function        find_builtin
 *****************************************************************************/
static struct builtin *find_builtin (const char *name) {

    struct hashtab_name *key;
    struct pp_cond_pseudo_op_entry *entry;
    
    if ((key = hashtab_get_key (&hashtab_builtins, name))) {
    
        if ((entry = hashtab_get (&hashtab_builtins, key))) {
            return (struct builtin *) entry;
        }
    
    }
    
    return 0;

}

struct completion_match {

    char *name;
    uint8_t type;

};

static int completion_tab_pending = 0;

/******************************************************************************
 * @function        completion_add
 *****************************************************************************/
static int completion_add (struct completion_match **matches, size_t *count, size_t *capacity, const char *name, uint8_t type) {

    struct completion_match *temp;
    char *copy;
    
    size_t i, next_capacity;
    
    for (i = 0; i < *count; i++) {
    
        if (!strcmp ((*matches)[i].name, name)) {
            return 1;
        }
    
    }
    
    if (*count == *capacity) {
    
        next_capacity = *capacity ? *capacity * 2 : 16;
        
        if (next_capacity < *capacity || !(temp = realloc (*matches, next_capacity * sizeof (**matches)))) {
            return 0;
        }
        
        *matches = temp;
        *capacity = next_capacity;
    
    }
    
    if (!(copy = strdup (name))) {
        return 0;
    }
    
    (*matches)[*count].name = copy;
    (*matches)[*count].type = type;
    
    (*count)++;
    return 1;

}

/******************************************************************************
 * @function        completion_directory
 *****************************************************************************/
static int completion_directory (struct completion_match **matches, size_t *count, size_t *capacity, const char *directory, const char *prefix, int directories_only) {

    struct dirent *de;
    DIR *dir;
    
    size_t length = strlen (prefix);
    int success = 1;
    
    if (!(dir = opendir (directory))) {
        return 1;
    }
    
    while ((de = readdir (dir))) {
    
        if (!length && de->d_name[0] == '.') {
            continue;
        }
        
        if (strncmp (de->d_name, prefix, length)) {
            continue;
        }
        
        if (directories_only && de->d_type != DT_DIR) {
            continue;
        }
        
        if (!completion_add (matches, count, capacity, de->d_name, de->d_type)) {
        
            success = 0;
            break;
        
        }
    
    }
    
    closedir (dir);
    return success;

}

/******************************************************************************
 * @function        complete_command
 *****************************************************************************/
static void complete_command (void) {

    struct completion_match *matches = 0, entry;
    struct builtin *builtin;
    
    char *word, *directory = 0, *prefix, *path, *end, *part;
    char *replacement = 0, *new_command;
    
    size_t start, length, count = 0, capacity = 0, i, j, common, add, old_length;
    int first_word, directories_only, success = 1;
    
    uint8_t type = DT_UNKNOWN;
    
    if (!command) {
    
        if (!(command = malloc (CAPACITY_INCREMENT))) {
            return;
        }
        
        command_capacity = CAPACITY_INCREMENT;
        memset (command, 0, command_capacity);
    
    }
    
    start = command_index;
    
    while (start > 0 && !isspace ((int) command[start - 1])) {
        start--;
    }
    
    length = command_index - start;
    first_word = 1;
    
    for (i = 0; i < start; i++) {
    
        if (!isspace ((int) command[i])) {
        
            first_word = 0;
            break;
        
        }
    
    }
    
    directories_only = 0;
    
    if (!first_word && start >= 3 && !strncmp (command, "cd", 2) && isspace ((int) command[2])) {
        directories_only = 1;
    }
    
    if (!(word = strndup (command + start, length))) {
        return;
    }
    
    prefix = strrchr (word, '/');
    
    if (prefix) {
    
        prefix++;
        
        if (!(directory = strndup (word, prefix - word))) {
            success = 0;
        } else {
            success = completion_directory (&matches, &count, &capacity, directory, prefix, directories_only);
        }
    
    } else if (!first_word) {
    
        prefix = word;
        success = completion_directory (&matches, &count, &capacity, ".", prefix, directories_only);
    
    } else {
    
        prefix = word;
        
        for (builtin = builtins; builtin->name; builtin++) {
        
            if (!strncmp (builtin->name, prefix, length) && !completion_add (&matches, &count, &capacity, builtin->name, DT_REG)) {
            
                success = 0;
                break;
            
            }
        
        }
        
        path = getenv ("PATH");
        
        while (success && path && *path) {
        
            end = strchr (path, ':');
            
            if (!(part = strndup (path, end ? (size_t) (end - path) : strlen (path)))) {
            
                success = 0;
                break;
            
            }
            
            success = completion_directory (&matches, &count, &capacity, *part ? part : ".", prefix, 0);
            free (part);
            
            if (!end) {
                break;
            }
            
            path = end + 1;
        
        }
    
    }
    
    if (!success || !count) {
        goto done;
    }
    
    for (i = 1; i < count; i++) {
    
        entry = matches[i];
        j = i;
        
        while (j > 0 && strcmp (matches[j - 1].name, entry.name) > 0) {
        
            matches[j] = matches[j - 1];
            j--;
        
        }
        
        matches[j] = entry;
    
    }
    
    common = strlen (matches[0].name);
    
    for (i = 1; i < count; i++) {
        
        for (j = 0; j < common && matches[0].name[j] && matches[i].name[j] == matches[0].name[j]; j++) {}
        common = j;
    
    }
    
    if (common < strlen (prefix)) {
        goto done;
    }
    
    add = common - strlen (prefix);
    type = count == 1 ? matches[0].type : DT_UNKNOWN;
    
    if (count == 1) {
        add += 1;
    }
    
    if (add) {
        
        if (!(replacement = malloc (add + 1))) {
            goto done;
        }
        
        memcpy (replacement, matches[0].name + strlen (prefix), common - strlen (prefix));
        
        if (count == 1) {
            replacement[add - 1] = type == DT_DIR ? '/' : ' ';
        }
        
        replacement[add] = '\0';
        old_length = strlen (command);
        
        if (old_length + add + 1 > command_capacity) {
        
            size_t new_capacity = command_capacity;
            
            while (new_capacity < old_length + add + 1) {
                new_capacity += CAPACITY_INCREMENT;
            }
            
            if (!(new_command = realloc (command, new_capacity))) {
                goto done;
            }
            
            command = new_command;
            command_capacity = new_capacity;
        
        }
        
        memmove (command + command_index + add, command + command_index, old_length - command_index + 1);
        memcpy (command + command_index, replacement, add);
        
        command_index += add;
        printf ("\r\033[2K%s%s", prompt, command);
        
        if (old_length + add > command_index) {
            printf ("\033[%zuD", old_length + add - command_index);
        }
        
        completion_tab_pending = 0;
    
    } else if (count > 1 && completion_tab_pending) {
    
        printf ("\n");
        
        for (i = 0; i < count; i++) {
            printf ("%s%s  ", matches[i].name, matches[i].type == DT_DIR ? "/" : "");
        }
        
        printf ("\n%s%s", prompt, command);
        old_length = strlen (command);
        
        if (old_length > command_index) {
            printf ("\033[%zuD", old_length - command_index);
        }
        
        completion_tab_pending = 0;
    
    } else if (count > 1) {
        completion_tab_pending = 1;
    }

done:

    free (replacement);
    free (directory);
    free (word);
    
    for (i = 0; i < count; i++) {
        free (matches[i].name);
    }
    
    free (matches);

}

/******************************************************************************
 * @function        add_character
 *****************************************************************************/
static void add_character (int ch) {

    size_t len = 0;
    
    if (ch == 127) {
    
        if (!command || command_index == 0) {
            return;
        }
        
        len = strlen (command);
        
        if (command_index < len) {
        
            char *p = command + command_index;
            size_t pos;
            
            memmove (p - 1, p, len - command_index + 1);
            pos = len - command_index;
            
            printf ("\r\033[2K%s%s", prompt, command);
            command_index--;
            
            printf ("\033[%zuD", pos);
            return;
        
        }
        
        command[--command_index] = '\0';
        
        printf ("\b \b");
        return;
    
    }
    
    if (command) {
        len = strlen (command);
    }
    
    if ((replace_char && len >= command_capacity) || (command_index + 1 >= command_capacity)) {
    
        command_capacity += CAPACITY_INCREMENT;
        
        if (!(command = realloc (command, command_capacity))) {
        
            printf ("not enough memory for command\n");
            exit (1);
        
        }
        
        memset (command + command_index, 0, command_capacity - command_index);
    
    }
    
    if (!replace_char && command_index < len) {
    
        char *p = command + command_index;
        size_t pos;
        
        memmove (p + 1, p, len - command_index);
        *p = ch;
        
        printf ("\r\033[2K%s%s", prompt, command);
        pos = len - command_index;
        
        printf ("\033[%zuD", pos);
        command_index++;
        
        return;
    
    }
    
    command[command_index++] = ch;
    printf ("%c", ch);

}

/******************************************************************************
 * @function        create_prompt
 *****************************************************************************/
static void create_prompt (void) {

    char path[4096];
    size_t index = 0;
    
    memset (prompt, 0, sizeof (prompt));
    
    if (getcwd (path, sizeof (path))) {
        index = sprintf (prompt, "\x1b[94m[%.*s]\x1b[0m", sizeof (path), path);
    }
    
    sprintf (prompt + index, " $ ");

}

/******************************************************************************
 * @function        initialize_terminal_input
 *****************************************************************************/
static void initialize_terminal_input (void) {

    struct termios mode;
    
    if (!isatty (STDIN_FILENO) || tcgetattr (STDIN_FILENO, &mode) != 0) {
        return;
    }
    
    terminal_original_input = mode;
    
    mode.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
    mode.c_iflag &= ~(IXON | ICRNL);
    
    mode.c_cc[VMIN] = 1;
    mode.c_cc[VTIME] = 0;
    
    if (tcsetattr (STDIN_FILENO, TCSANOW, &mode) == 0) {
        terminal_input_mode_changed = 1;
    }

}

static void terminal_set_signal_mode (int enabled) {

    struct termios mode;
    
    if (!terminal_input_mode_changed || tcgetattr (STDIN_FILENO, &mode) != 0) {
        return;
    }
    
    if (enabled) {
    
        mode = terminal_original_input;
        mode.c_lflag |= ISIG;
    
    } else {
    
        mode.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
        mode.c_iflag &= ~(IXON | ICRNL);
        
        mode.c_cc[VMIN] = 1;
        mode.c_cc[VTIME] = 0;
    
    }
    
    tcsetattr (STDIN_FILENO, TCSANOW, &mode);

}

/******************************************************************************
 * @function        restore_terminal_input
 *****************************************************************************/
static void restore_terminal_input (void) {

    if (terminal_input_mode_changed) {
        tcsetattr (STDIN_FILENO, TCSANOW, &terminal_original_input);
    }

}

/******************************************************************************
 * @function        execute
 *****************************************************************************/
static int execute (char *prog, char *args, int saved_stdout) {

    char *p_args, *temp, *word;
    
    char **argv = 0;
    size_t argc = 1;
    
    int wstatus;
    size_t i = 1;
    
    for (temp = args; *temp; temp++) {
    
        if (isspace ((int) *temp)) {
            argc++;
        }
    
    }
    
    if (!(p_args = strdup (args))) {
        return 2;
    }
    
    temp = p_args;
    
    if (!(argv = malloc (sizeof (*argv) * (argc + 2)))) {
    
        free (p_args);
        return 2;
    
    }
    
    memset (argv, 0, sizeof (*argv) * (argc + 2));
    argv[0] = prog;
    
    while ((word = parse_word (&temp))) {
        argv[i++] = word;
    }
    
    if (strchr (prog, '/') || strchr (prog, '\\')) {
    
        if (pipeline_child) {
        
            if (saved_stdout >= 0) {
                close (saved_stdout);
            }
            
            execve (prog, argv, __environ);
            
            free (p_args);
            free (argv);
            
            return 127;
        
        }
        
        terminal_set_signal_mode (1);
        
        if (!fork ()) {
        
            if (saved_stdout >= 0) {
                close (saved_stdout);
            }
            
            execve (prog, argv, __environ);
            _exit (127);
        
        }
    
    } else {
    
        char *path, *file, *p;
        int ret;
        
        free (p_args);
        free (argv);
        
        if (!(path = getenv ("PATH"))) {
            return 127;
        }
        
        for (;;) {
        
            p = path;
            
            if (!*p) {
                return 127;
            }
            
            while (*p && *p != ':') {
                p++;
            }
            
            if (p - path == 0) {
            
                path = p + 1;
                continue;
            
            }
            
            if ((temp = strndup (path, p - path))) {
            
                if (!(file = malloc (strlen (temp) + 1 + strlen (prog) + 1))) {
                
                    free (temp);
                    
                    if (*p == ':') {
                        path = p + 1;
                    } else {
                        path = p;
                    }
                    
                    continue;
                
                }
                
                sprintf (file, "%s/%s", temp, prog);
                free (temp);
                
                ret = execute (file, args, saved_stdout);
                
                if (ret != 127) {
                
                    free (file);
                    return ret;
                
                }
                
                free (file);
            
            }
            
            if (*p == ':') {
                path = p + 1;
            } else {
                path = p;
            }
        
        }    
    
    }
    
    wait (&wstatus);
    
    free (p_args);
    free (argv);
    
    terminal_set_signal_mode (0);
    return (wstatus & 0x7F) ? 128 + (wstatus & 0x7F) : (wstatus >> 8) & 0xFF;

}

/******************************************************************************
 * @function        execute_command
 *****************************************************************************/
static int execute_command (const char *command, int saved_stdout) {

    char *copy, *name, *p;
    
    struct builtin *builtin;
    int ret = 0;
    
    if (!(copy = strdup (command))) {
    
        printf ("not enough memory to get command\n");
        return 1;
    
    }
    
    p = copy;
    name = parse_word (&p);
    
    if (name) {
    
        if ((builtin = find_builtin (name))) {
            ret = builtin->handler (p);
        } else if ((ret = execute (name, p, saved_stdout)) == 127) {
            fprintf (stderr, "%s: command not found\n", name);
        }
    
    }
    
    free (copy);
    return ret;

}

/******************************************************************************
 * @function        execute_command_with_environment
 *****************************************************************************/
static int execute_command_with_environment (const char *command, int saved_stdout) {

    char *start, *p, *end, *copy, *entry, *equal;
    char **saved_environ = __environ, **envp;
    
    size_t assignments = 0, count, i, length;
    int ret;
    
    start = skip_whitespace ((char *) command);
    p = start;
    
    for (;;) {
    
        end = p;
        
        if (!isalpha ((int) *end) && *end != '_') {
            break;
        }
        
        for (end++; isalnum ((int) *end) || *end == '_'; end++) {}
        
        if (*end != '=') {
            break;
        }
        
        end = shell_word_end (end);
        assignments++;
        
        p = skip_whitespace (end);
    
    }
    
    if (!assignments) {
        return execute_command (command, saved_stdout);
    }
    
    if (!*p) {
    
        fprintf (stderr, "missing command after environment assignment\n");
        return 1;
    
    }
    
    if (!(copy = strndup (start, p - start))) {
        return 1;
    }
    
    for (count = 0; saved_environ[count]; count++) {}
    
    if (!(envp = malloc ((count + assignments + 1) * sizeof (*envp)))) {
    
        free (copy);
        return 1;
    
    }
    
    memcpy (envp, saved_environ, count * sizeof (*envp));
    envp[count] = 0;
    
    for (entry = copy; *entry; entry = skip_whitespace (end)) {
    
        char *word = entry;
        end = shell_word_end (entry);
        
        if (*end) {
            *end++ = '\0';
        }
        
        parse_word (&word);
        
        equal = strchr (entry, '=');
        length = equal - entry;
        
        for (i = 0; i < count; i++) {
        
            if (!strncmp (envp[i], entry, length) && envp[i][length] == '=') {
                break;
            }
        
        }
        
        envp[i] = entry;
        
        if (i == count) {
        
            count++;
            envp[count] = 0;
        
        }
    
    }
    
    __environ = envp;
    
    ret = execute_command (p, saved_stdout);
    __environ = saved_environ;
    
    free (envp);
    free (copy);
    
    return ret;

}

/******************************************************************************
 * @function        process_simple_command
 *****************************************************************************/
static int process_simple_command (const char *command) {

    const char *p;
    char quote = 0;
    
    char *copy, *redirect, *path, *end, *tail, *word;
    int fd, flags, saved_stdout, ret;
    
    for (p = command; *p; p++) {
    
        if (*p == '\\' && quote != '\'' && p[1]) {
        
            p++;
            continue;
        
        }
        
        if (quote) {
        
            if (*p == quote) {
                quote = 0;
            }
        
        } else if (*p == '\'' || *p == '"') {
            quote = *p;
        } else if (*p == '>') {
            break;
        }
    
    }
    
    if (!*p) {
        return execute_command_with_environment (command, -1);
    }
    
    if (!(copy = strdup (command))) {
        return 1;
    }
    
    redirect = copy + (p - command);
    
    if (redirect[1] == '>') {
    
        flags = O_APPEND;
        path = skip_whitespace (redirect + 2);
    
    } else {
    
        flags = O_TRUNC;
        path = skip_whitespace (redirect + 1);
    
    }
    
    end = shell_word_end (path);
    
    if (end == path || *end == '<' || strchr (end, '>')) {
    
        fprintf (stderr, "syntax error near '>'\n");
        free (copy);
        
        return 1;
    
    }
    
    if (!(path = strndup (path, end - path))) {
    
        free (copy);
        return 1;
    
    }
    
    word = path;
    parse_word (&word);
    
    memmove (redirect, end, strlen (end) + 1);
    tail = copy + strlen (copy);
    
    while (tail > copy && isspace ((int) tail[-1])) {
        *--tail = '\0';
    }
    
    fflush (stdout);
    
    if ((saved_stdout = dup (STDOUT_FILENO)) < 0) {
    
        fprintf (stderr, "cannot save standard output\n");
        
        free (path);
        free (copy);
        
        return 1;
    
    }
    
    fd = open (path, O_WRONLY | O_CREAT | flags, 0666);
    
    if (fd < 0 || dup2 (fd, STDOUT_FILENO) < 0) {
    
        fprintf (stderr, "%s: cannot redirect output\n", path);
        
        if (fd >= 0) {
            close (fd);
        }
        
        close (saved_stdout);
        
        free (path);
        free (copy);
        
        return 1;
    
    }
    
    close (fd);
    free (path);
    
    ret = execute_command_with_environment (copy, saved_stdout);
    fflush (stdout);
    
    dup2 (saved_stdout, STDOUT_FILENO);
    close (saved_stdout);
    
    free (copy);
    return ret;

}

/******************************************************************************
 * @function        process_pipeline
 *****************************************************************************/
static int process_pipeline (char *command) {

    char quote = 0;
    char *p, *start;
    
    size_t count = 1, launched = 0, i;
    pid_t *pids, pid;
    
    int fds[2], input = -1, ret = 0, wstatus;
    start = command;
    
    for (p = command; *p; p++) {
    
        if (*p == '\\' && quote != '\'' && p[1]) {
            p++;
        } else if (quote) {
        
            if (*p == quote) {
                quote = 0;
            }
        
        } else if (*p == '\'' || *p == '"') {
            quote = *p;
        } else if (*p == '|') {
        
            *p = '\0';
            
            if (!*skip_whitespace (start)) {
            
                fprintf (stderr, "syntax error near '|'\n");
                return 2;
            
            }
            
            count++;
            start = p + 1;
        
        }
    
    }
    
    if (count == 1) {
        return process_simple_command (command);
    }
    
    if (!*skip_whitespace (start)) {
    
        fprintf (stderr, "syntax error near '|'\n");
        return 2;
    
    }
    
    if (!(pids = malloc (count * sizeof (*pids)))) {
        return 1;
    }
    
    fflush (stdout);
    fflush (stderr);
    
    terminal_set_signal_mode (1);
    
    for (start = command; launched < count; start += strlen (start) + 1) {
    
        if (launched + 1 < count && pipe (fds) < 0) {
        
            fprintf (stderr, "cannot create pipe\n");
            ret = 1;
            
            break;
        
        }
        
        pid = fork ();
        
        if (!pid) {
        
            pipeline_child = 1;
            terminal_input_mode_changed = 0;
            
            if (launched + 1 < count) {
                close (fds[0]);
            }
            
            if (input >= 0 && input != STDIN_FILENO) {
            
                if (dup2 (input, STDIN_FILENO) < 0) {
                    _exit (1);
                }
                
                close (input);
            
            }
            
            if (launched + 1 < count && fds[1] != STDOUT_FILENO) {
            
                if (dup2 (fds[1], STDOUT_FILENO) < 0) {
                    _exit (1);
                }
                
                close (fds[1]);
            
            }
            
            ret = process_simple_command (start);
            
            fflush (stdout);
            fflush (stderr);
            
            _exit (ret);
        
        }
        
        if (input >= 0) {
        
            close (input);
            input = -1;
        
        }
        
        if (launched + 1 < count) {
        
            close (fds[1]);
            input = fds[0];
        
        }
        
        if (pid < 0) {
        
            fprintf (stderr, "cannot fork\n");
            ret = 1;
            
            break;
        
        }
        
        pids[launched++] = pid;
    
    }
    
    if (input >= 0) {
        close (input);
    }
    
    for (i = 0; i < launched; i++) {
    
        do {
            pid = waitpid (pids[i], &wstatus, 0);
        } while (pid < 0 && errno == EINTR);
        
        if (pid < 0) {
            ret = 1;
        } else if (i + 1 == count) {
            ret = (wstatus & 0x7F) ? 128 + (wstatus & 0x7F) : (wstatus >> 8) & 0xFF;
        }
    
    }
    
    terminal_set_signal_mode (0);
    
    free (pids);
    return ret;

}

/******************************************************************************
 * @function        process_command
 *****************************************************************************/
static int process_command (const char *command) {

    char quote = 0, op = 0, next;
    
    char *copy, *start, *p;
    int ret = 0;
    
    if (!(copy = strdup (command))) {
        return 1;
    }
    
    start = copy;
    
    for (p = copy; ; p++) {
    
        if (!*p && quote) {
        
            fprintf (stderr, "syntax error: unterminated quote\n");
            
            ret = 2;
            break;
        
        }
        
        if (!*p || (!quote && ((*p == '&' && p[1] == '&') || (*p == '|' && p[1] == '|')))) {
        
            next = *p;
            *p = '\0';
            
            if (!*skip_whitespace (start)) {
            
                if (op || next) {
                    fprintf (stderr, "syntax error near '%c%c'\n", next ? next : op, next ? next : op);
                    ret = 2;
                }
                
                break;
            
            }
            
            if (!op || (op == '&' && !ret) || (op == '|' && ret)) {
                ret = process_pipeline (start);
            }
            
            if (!next) {
                break;
            }
            
            op = next;
            p++;
            start = p + 1;
        
        } else if (*p == '\\' && quote != '\'' && p[1]) {
            p++;
        } else if (quote) {
        
            if (*p == quote) {
                quote = 0;
            }
        
        } else if (*p == '\'' || *p == '"') {
            quote = *p;
        }
    
    }
    
    free (copy);
    return ret;

}

/******************************************************************************
 * @function        main
 *****************************************************************************/
int main (int argc, char **argv) {

    char *temp;
    int ch, history_index;
    
    install_builtins ();
    
    atexit (restore_terminal_input);
    initialize_terminal_input ();
    
    if (argc > 1) {
    
        int i;
        
        for (i = 1; i < argc; i++) {
        
            FILE *fp;
            
            char *line, *line_end, *real_line;
            unsigned long real_line_len;
            
            unsigned long new_line_number = 1;
            unsigned long newlines;
            
            void *load_line_internal_data = NULL;
            int ret = 0;
            
            if (!(fp = fopen (argv[i], "r"))) {
                continue;
            }
            
            load_line_internal_data = load_line_create_internal_data (&new_line_number);
            
            while (!load_line (&line, &line_end, &real_line, &real_line_len, &newlines, fp, &load_line_internal_data)) {
            
                line = skip_whitespace (line);
                
                if (*line == '\n' || *line == '\0') {
                    continue;
                }
                
                *line_end = '\0';
                
                if ((ret = process_command (line))) {
                    break;
                }
            
            }
            
            load_line_destroy_internal_data (load_line_internal_data);
            fclose (fp);
            
            if (ret) {
                return ret;
            }
        
        }
        
        return 0;
    
    }
    
    create_prompt ();
    
    for (;;) {
    
        if (command) {
            memset (command, 0, command_capacity);
        }
        
        completion_tab_pending = 0;
        command_index = 0;
        
        history_index = vec_history.length;
        printf ("%s", prompt);
        
        while ((ch = getchar ()) != EOF) {
        
            if (ch == '\t') {
            
                complete_command ();
                continue;
            
            } else {
                completion_tab_pending = 0;
            }
            
            if (ch == '\n' || ch == '\r') {
            
                printf ("\n");
                break;
            
            }
            
            if (ch == '\x01') {
            
                if (command) {
                
                    command_index = 0;
                    printf ("\r%s", prompt);
                
                }
                
                continue;
            
            }
            
            if (ch == '\x03') {
            
                if (command) { memset (command, 0, command_capacity); }
                printf ("^C\n");
                
                break;
            
            }
            
            if (ch == '\x05') {
            
                if (command) {
                
                    command_index = strlen (command);
                    printf ("\r%s%s", prompt, command);
                
                }
                
                continue;
            
            }
            
            if (ch == '\x0B') {
            
                if (command) {
                
                    size_t len = strlen (command);
                    
                    if (command_index < len) {
                    
                        command[command_index] = '\0';
                        printf ("\033[0K");
                    
                    }
                
                }
                
                continue;
            
            }
            
            if (ch == '\x15') {
            
                if (command && command_index > 0) {
                
                    size_t len = strlen (command);
                    
                    memmove (command, command + command_index, len - command_index + 1);
                    printf ("\r\033[2K%s%s", prompt, command);
                    
                    len -= command_index;
                    
                    if (len > 0) {
                        printf ("\033[%zuD", len);
                    }
                    
                    command_index = 0;
                
                }
                
                continue;
            
            }
            
            if (ch == '\x17') {
            
                if (command && command_index > 0) {
                
                    char *p = command + command_index - 1;
                    size_t len = strlen (command);
                    
                    while (p > command && *p == ' ') {
                        p--;
                    }
                    
                    while (p > command && *p != ' ') {
                        p--;
                    }
                    
                    if (*p == ' ') {
                        p++;
                    }
                    
                    memmove (p, command + command_index, len - command_index + 1);
                    printf ("\r\033[2K%s%s", prompt, command);
                    
                    len -= command_index;
                    
                    if (len > 0) {
                        printf ("\033[%zuD", len);
                    }
                    
                    command_index = p - command;
                
                }
                
                continue;
            
            }
            
            if (ch == '\x1B') {
            
                if ((ch = getchar ()) != '[') {
                    continue;
                }
                
                switch (getchar ()) {
                
                    case '2': {
                    
                        if (getchar () != '~' || !command) {
                            break;
                        }
                        
                        replace_char = !replace_char;
                        break;
                    
                    }
                    
                    case '3': {
                    
                        size_t len;
                        
                        if (getchar () != '~' || !command) {
                            break;
                        }
                        
                        len = strlen (command);
                        
                        if (command_index >= len) {
                            break;
                        }
                        
                        memmove (command + command_index, command + command_index + 1, len - command_index);
                        printf ("\r\033[2K%s%s", prompt, command);
                        
                        len -= command_index;
                        
                        if (len > 1) {
                            printf ("\033[%zuD", len - 1);
                        }
                        
                        break;
                    
                    }
                    
                    case 'A': {
                    
                        if (history_index == 0) {
                            break;
                        }
                        
                        memset (command, 0, command_index);
                        
                        temp = vec_history.data[--history_index];
                        printf ("\r\033[2K%s", prompt);
                        
                        command_index = 0;
                        while (*temp) { add_character (*temp); temp++; }
                        
                        break;
                    
                    }
                    
                    case 'B': {
                    
                        if (history_index < vec_history.length) {
                        
                            memset (command, 0, command_index);
                            
                            printf ("\r\033[2K%s", prompt);
                            command_index = 0;
                            
                            if (++history_index == vec_history.length) {
                                break;
                            }
                            
                            temp = vec_history.data[history_index];
                            while (*temp) { add_character (*temp); temp++; }
                        
                        }
                        
                        break;
                    
                    }
                    
                    case 'C': {
                    
                        if (command && command_index < strlen (command)) {
                        
                            printf ("\033[1C");
                            command_index++;
                        
                        }
                        
                        break;
                    
                    }
                    
                    case 'D': {
                    
                        if (command_index > 0) {
                        
                            printf ("\033[1D");
                            command_index--;
                        
                        }
                        
                        break;
                    
                    }
                    
                    case 'F': {
                    
                        if (command) {
                        
                            command_index = strlen (command);
                            printf ("\r%s%s", prompt, command);
                        
                        }
                        
                        break;
                    
                    }
                    
                    case 'H': {
                    
                        if (command) {
                        
                            printf ("\033[%zuD", command_index);
                            command_index = 0;
                        
                        }
                        
                        break;
                    
                    }
                
                }
                
                continue;
            
            }
            
            if (ch >= 1 && ch <= 26 && ch != 127 && ch != '\t') {
            
                add_character ('^');
                add_character ('@' + ch);
                
                continue;
            
            }
            
            add_character (ch);
        
        }
        
        if (!command || !*command) {
            continue;
        }
        
        if (vec_history.length > 0) {
        
            temp = vec_history.data[vec_history.length - 1];
            
            if (strcmp (temp, command)) {
                vec_push (&vec_history, strdup (command));
            }
        
        } else {
            vec_push (&vec_history, strdup (command));
        }
        
        process_command (command);
    
    }
    
    return 0;

}
