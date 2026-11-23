#include    <stdint.h>
#include    <stdlib.h>
#include    <string.h>

static char **msvcrt_argv = 0;
static char **msvcrt_env = 0;

static int msvcrt_argc = 0;

__attribute__ ((dllimport)) uint32_t __stdcall GetEnvironmentVariableA (const char *name, char *buffer, uint32_t size);
__attribute__ ((dllimport)) int __stdcall SetEnvironmentVariableA (const char *name, const char *value);

char **__initenv = 0;

static int msvcrt_is_space (uint16_t c) {
    return c == ' ' || c == '\t';
}

static size_t msvcrt_wstrlen (const uint16_t *s) {

    size_t n = 0;
    
    while (s[n]) {
        n++;
    }
    
    return n;

}

static void msvcrt_emit (char **p, uint16_t c) {

    if (c < 0x80) {
        *(*p)++ = (char) c;
    } else if (c < 0x800) {
    
        *(*p)++ = (char) (0xC0 | (c >> 6));
        *(*p)++ = (char) (0x80 | (c & 0x3F));
    
    } else {
    
        *(*p)++ = (char) (0xE0 | (c >> 12));
        *(*p)++ = (char) (0x80 | ((c >> 6) & 0x3F));
        *(*p)++ = (char) (0x80 | (c & 0x3F));
    
    }

}

static void msvcrt_free_strings (char **strings) {

    size_t i;
    
    if (!strings) {
        return;
    }
    
    for (i = 0; strings[i]; i++) {
        free (strings[i]);
    }
    
    free (strings);

}

static char *msvcrt_copy_wide (const uint16_t *source) {

    char *result, *out;
    
    if (!(result = malloc (msvcrt_wstrlen (source) * 3 + 1))) {
        return 0;
    }
    
    out = result;
    
    while (*source) {
        msvcrt_emit (&out, *source++);
    }
    
    *out = 0;
    return result;

}

static int msvcrt_read_argv (const uint16_t *command, int *argc, char ***argv) {

    char **result = 0, **expanded;
    char *argument, *out;
    
    const uint16_t *p = command;
    
    size_t count = 0, capacity = 0, slashes, i;
    int quoted;
    
    while (*p) {
    
        while (msvcrt_is_space (*p)) {
            p++;
        }
        
        if (!*p) {
            break;
        }
        
        if (count + 2 > capacity) {
        
            size_t new_capacity = capacity ? capacity * 2 : 8;
            
            if (!(expanded = realloc (result, new_capacity * sizeof (*result)))) {
                goto failed;
            }
            
            result = expanded;
            capacity = new_capacity;
        
        }
        
        if (!(argument = malloc (msvcrt_wstrlen (p) * 3 + 1))) {
            goto failed;
        }
        
        out = argument;
        quoted = 0;
        
        if (!count) {
        
            if (*p == '"') {
            
                p++;
                
                while (*p && *p != '"') {
                    msvcrt_emit (&out, *p++);
                }
                
                if (*p == '"') {
                    p++;
                }
            
            } else {
            
                while (*p && !msvcrt_is_space (*p)) {
                    msvcrt_emit (&out, *p++);
                }
            
            }
        
        } else {
        
            while (*p && (quoted || !msvcrt_is_space (*p))) {
            
                slashes = 0;
                
                while (*p == '\\') {
                
                    slashes++;
                    p++;
                
                }
                
                if (*p == '"') {
                
                    for (i = 0; i < slashes / 2; i++) {
                        msvcrt_emit (&out, '\\');
                    }
                    
                    if (slashes & 1) {
                    
                        msvcrt_emit (&out, '"');
                        p++;
                    
                    } else if (quoted && p[1] == '"') {
                    
                        msvcrt_emit (&out, '"');
                        p += 2;
                    
                    } else {
                    
                        quoted = !quoted;
                        p++;
                    
                    }
                
                } else {
                
                    for (i = 0; i < slashes; i++) {
                        msvcrt_emit (&out, '\\');
                    }
                    
                    if (*p) {
                        msvcrt_emit (&out, *p++);
                    }
                
                }
            
            }
        
        }
        
        *out = 0;
        
        result[count++] = argument;
        result[count] = 0;
    
    }
    
    if (!count) {
        goto failed;
    }
    
    *argc = (int) count;
    *argv = result;
    
    return 0;

failed:

    if (result) {
        result[count] = 0;
    }
    
    msvcrt_free_strings (result);
    return -1;

}

static int msvcrt_read_environment (const uint16_t *environment, char ***env) {

    const uint16_t *p = environment;
    
    char **result;
    size_t count = 0, i;
    
    while (*p) {
    
        count++;
        p += msvcrt_wstrlen (p) + 1;
    
    }
    
    if (!(result = malloc ((count + 1) * sizeof (*result)))) {
        return -1;
    }
    
    p = environment;
    
    for (i = 0; i < count; i++) {
    
        if (!(result[i] = msvcrt_copy_wide (p))) {
        
            result[i] = 0;
            msvcrt_free_strings (result);
            return -1;
        }
        
        p += msvcrt_wstrlen (p) + 1;
    
    }
    
    result[count] = 0;
    
    *env = result;
    return 0;

}

static int msvcrt_initialize_environment (void) {

    const uint16_t *environment;
    uintptr_t params;
    
    if (msvcrt_env) {
        return 0;
    }
    
    params = *(uintptr_t *) (uintptr_t) (0x7FFDF000UL + 0x10);
    environment = params ? *(uint16_t **) (params + 0x48) : 0;
    
    if (!environment || msvcrt_read_environment (environment, &msvcrt_env)) {
        return -1;
    }
    
    __initenv = msvcrt_env;
    return 0;

}

static int msvcrt_environment_matches (const char *entry, const char *name, size_t length) {

    size_t i;
    int a, b;
    
    for (i = 0; i < length; i++) {
    
        a = (unsigned char) entry[i];
        b = (unsigned char) name[i];
        
        if (!a || a == '=') {
            return 0;
        }
        
        if (a >= 'a' && a <= 'z') {
            a -= 'a' - 'A';
        }
        
        if (b >= 'a' && b <= 'z') {
            b -= 'a' - 'A';
        }
        
        if (a != b) {
            return 0;
        }
    
    }
    
    return entry[length] == '=';

}

__attribute__ ((dllexport)) char *getenv (const char *name) {

    size_t i, length;
    
    if (!name || !*name || strchr (name, '=') || msvcrt_initialize_environment ()) {
        return 0;
    }
    
    length = strlen (name);
    
    for (i = 0; msvcrt_env[i]; i++) {
    
        if (msvcrt_environment_matches (msvcrt_env[i], name, length)) {
            return msvcrt_env[i] + length + 1;
        }
    
    }
    
    return 0;

}

__attribute__ ((dllexport)) int _putenv (const char *string) {

    size_t length, count = 0, i, j = 0;
    
    char *name, *value;
    char **variables;
    
    if (!string || !(value = strchr (string, '=')) || value == string || msvcrt_initialize_environment ()) {
        return -1;
    }
    
    length = value - string;
    value++;
    
    if (!(name = strndup (string, length))) {
        return -1;
    }
    
    while (msvcrt_env[count]) {
        count++;
    }
    
    if (!(variables = malloc ((count + 2) * sizeof (*variables)))) {
    
        free (name);
        return -1;
    
    }
    
    for (i = 0; i < count; i++) {
    
        if (!msvcrt_environment_matches (msvcrt_env[i], name, length)) {
        
            if (!(variables[j] = strdup (msvcrt_env[i]))) {
                goto failed;
            }
            
            j++;
        
        }
    
    }
    
    if (*value) {
    
        if (!(variables[j] = strdup (string))) {
            goto failed;
        }
        
        j++;
    
    }
    
    variables[j] = 0;
    
    if (*value || GetEnvironmentVariableA (name, 0, 0)) {
    
        if (!SetEnvironmentVariableA (name, *value ? value : 0)) {
            goto failed;
        }
    
    }
    
    if (msvcrt_env != __initenv) {
        msvcrt_free_strings (msvcrt_env);
    }
    
    msvcrt_env = variables;
    free (name);
    
    return 0;

failed:

    variables[j] = 0;
    
    msvcrt_free_strings (variables);
    free (name);
    
    return -1;

}

__attribute__ ((dllexport)) char ***__p__environ (void) {

    msvcrt_initialize_environment ();
    return &msvcrt_env;

}

static int msvcrt_getmainargs (int *argc, char ***argv, char ***env, int do_wild_card, int *start_info) {

    uint16_t *command;
    uintptr_t params;
    
    char **arguments;
    int count;
    
    size_t i, j;
    (void) start_info;
    
    if (!argc || !argv || !env) {
        return -1;
    }
    
    if (!msvcrt_argv) {
    
        params = *(uintptr_t *) (uintptr_t) (0x7FFDF000UL + 0x10);
        
        if (!params) {
            return -1;
        }
        
        command = *(uint16_t **) (params + 0x44);
        
        if (!command || msvcrt_read_argv (command, &count, &arguments)) {
            return -1;
        }
        
        if (msvcrt_initialize_environment ()) {
        
            msvcrt_free_strings (arguments);
            return -1;
        
        }
        
        msvcrt_argc = count;
        msvcrt_argv = arguments;
    
    }
    
    if (do_wild_card) {
    
        for (i = 1; i < (size_t) msvcrt_argc; i++) {
        
            for (j = 0; msvcrt_argv[i][j]; j++) {
            
                if (msvcrt_argv[i][j] == '*' || msvcrt_argv[i][j] == '?') {
                    return -1;
                }
            
            }
        
        }
    
    }
    
    *argc = msvcrt_argc;
    *argv = msvcrt_argv;
    *env = msvcrt_env;
    
    return 0;

}

__attribute__ ((dllexport)) int __getmainargs (int *_argc, char ***_argv, char ***_env, int _do_wild_card, int *_start_info) {
    return msvcrt_getmainargs (_argc, _argv, _env, _do_wild_card, _start_info);
}
