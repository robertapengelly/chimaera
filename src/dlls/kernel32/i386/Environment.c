/******************************************************************************
 * @file            Environment.c
 *****************************************************************************/
#include    <stddef.h>
#include    <stdint.h>

#define     MEM_COMMIT                  0x1000
#define     MEM_RESERVE                 0x2000
#define     MEM_RELEASE                 0x8000
#define     PAGE_READWRITE              0x04

#define     ERROR_NOT_ENOUGH_MEMORY     8
#define     ERROR_INVALID_PARAMETER     87
#define     ERROR_ENVVAR_NOT_FOUND      203

__attribute__ ((dllimport)) uint32_t __stdcall ZwAllocateVirtualMemory (void *process, void **address, uintptr_t zero_bits, size_t *size, uint32_t type, uint32_t protect);
__attribute__ ((dllimport)) uint32_t __stdcall ZwFreeVirtualMemory (void *process, void **address, size_t *size, uint32_t type);

static uint16_t *allocated_environment = 0;

static uint16_t **environment_slot (void) {

    uintptr_t params = *(uintptr_t *) (uintptr_t) (0x7FFDF000UL + 0x10);
    return params ? (uint16_t **) (params + 0x48) : 0;

}

static void environment_error (uint32_t error) {
    *(uint32_t *) (uintptr_t) (0x7FFDE000UL + 0x34) = error;
}

static uint16_t environment_character (const void *p, size_t i, int wide) {
    return wide ? ((const uint16_t *) p)[i] : ((const uint8_t *) p)[i];
}

static uint16_t environment_upper (uint16_t c) {
    return c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c;
}

static size_t environment_length (const void *p, int wide) {

    size_t i = 0;
    
    while (environment_character (p, i, wide)) {
        i++;
    }
    
    return i;

}

static int environment_valid_name (const void *name, int wide) {

    size_t i;
    
    if (!name || !environment_character (name, 0, wide)) {
        return 0;
    }
    
    for (i = 0; environment_character (name, i, wide); i++) {
    
        if (environment_character (name, i, wide) == '=') {
            return 0;
        }
    
    }
    
    return 1;

}

static int environment_matches (const uint16_t *entry, const void *name, int wide) {

    size_t i;
    
    for (i = 0; environment_character (name, i, wide); i++) {
    
        if (!entry[i] || entry[i] == '=' || environment_upper (entry[i]) != environment_upper (environment_character (name, i, wide))) {
            return 0;
        }
    
    }
    
    return entry[i] == '=';

}

static uint32_t get_environment_variable (const void *name, void *buffer, uint32_t size, int wide) {

    uint16_t **slot, *p;
    size_t length, i;
    
    if (!environment_valid_name (name, wide) || (size && !buffer) || !(slot = environment_slot ())) {
    
        environment_error (ERROR_INVALID_PARAMETER);
        return 0;
    
    }
    
    for (p = *slot; p && *p; p += environment_length (p, 1) + 1) {
    
        if (environment_matches (p, name, wide)) {
        
            p += environment_length (name, wide) + 1;
            length = environment_length (p, 1);
            
            if (size <= length) {
                return (uint32_t) (length + 1);
            }
            
            for (i = 0; i <= length; i++) {
            
                if (wide) {
                    ((uint16_t *) buffer)[i] = p[i];
                } else {
                    ((char *) buffer)[i] = p[i] <= 0xFF ? (char) p[i] : '?';
                }
            
            }
            
            environment_error (0);
            return (uint32_t) length;
        
        }
    
    }
    
    environment_error (ERROR_ENVVAR_NOT_FOUND);
    return 0;

}

static int set_environment_variable (const void *name, const void *value, int wide) {

    uint16_t **slot, *p, *old, *out;
    void *memory = 0, *released;
    
    size_t count = 1, length, name_length, value_length = 0, size, i;
    int found = 0;
    
    if (!environment_valid_name (name, wide) || !(slot = environment_slot ())) {
    
        environment_error (ERROR_INVALID_PARAMETER);
        return 0;
    
    }
    
    old = *slot;
    name_length = environment_length (name, wide);
    
    if (value) {
    
        value_length = environment_length (value, wide);
        
        if (name_length > 32767 || value_length > 32767) {
            goto too_large;
        }
        
        count += name_length + value_length + 2;
    
    }
    
    for (p = old; p && *p; p += length) {
    
        length = environment_length (p, 1) + 1;
        
        if (environment_matches (p, name, wide)) {
            found = 1;
        } else {
            count += length;
        }
        
        if (count > 32767) {
            goto too_large;
        }
    
    }
    
    if (!value && !found) {
    
        environment_error (ERROR_ENVVAR_NOT_FOUND);
        return 0;
    
    }
    
    if (count > 32767) {
        goto too_large;
    }
    
    if (count == 1) {
        count = 2;
    }
    
    size = count * sizeof (*out);
    
    if (ZwAllocateVirtualMemory ((void *) (uintptr_t) -1, &memory, 0, &size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE) != 0) {
        goto too_large;
    }
    
    out = memory;
    
    for (p = old; p && *p; p += length) {
    
        length = environment_length (p, 1) + 1;
        
        if (!environment_matches (p, name, wide)) {
        
            for (i = 0; i < length; i++) {
                *out++ = p[i];
            }
        
        }
    
    }
    
    if (value) {
    
        for (i = 0; i < name_length; i++) {
            *out++ = environment_character (name, i, wide);
        }
        
        *out++ = '=';
        
        for (i = 0; i < value_length; i++) {
            *out++ = environment_character (value, i, wide);
        }
        
        *out++ = 0;
    
    }
    
    *out++ = 0;
    
    if (out == (uint16_t *) memory + 1) {
        *out = 0;
    }
    
    *slot = memory;
    
    if (allocated_environment && old == allocated_environment) {
    
        released = allocated_environment;
        size = 0;
        
        ZwFreeVirtualMemory ((void *) (uintptr_t) -1, &released, &size, MEM_RELEASE);
    
    }
    
    allocated_environment = memory;
    return 1;

too_large:

    environment_error (ERROR_NOT_ENOUGH_MEMORY);
    return 0;

}

__attribute__ ((dllexport)) uint32_t __stdcall GetEnvironmentVariableA (const char *name, char *buffer, uint32_t size) {
    return get_environment_variable (name, buffer, size, 0);
}

__attribute__ ((dllexport)) uint32_t __stdcall GetEnvironmentVariableW (const uint16_t *name, uint16_t *buffer, uint32_t size) {
    return get_environment_variable (name, buffer, size, 1);
}

__attribute__ ((dllexport)) int __stdcall SetEnvironmentVariableA (const char *name, const char *value) {
    return set_environment_variable (name, value, 0);
}

__attribute__ ((dllexport)) int __stdcall SetEnvironmentVariableW (const uint16_t *name, const uint16_t *value) {
    return set_environment_variable (name, value, 1);
}
