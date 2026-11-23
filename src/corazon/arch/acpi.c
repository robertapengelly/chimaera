/******************************************************************************
 * @file            acpi.c
 *****************************************************************************/
#include    <corazon/tasking/ipc.h>
#include    <corazon/tasking/irq.h>
#include    <corazon/tasking/process.h>
#include    <corazon/utils/list.h>
#include    <corazon/utils/misc.h>
#include    <corazon/acpi.h>
#include    <corazon/mmu.h>
#include    <corazon/regs.h>
#include    <corazon/timer.h>

#include    <stdint.h>
#include    <stdio.h>
#include    <string.h>

extern void *kmalloc (size_t size);
extern void *kfree (void *ptr);

const char *const acpi_parser_base_path = "/Corazon/ACPI";

#define     EBDA_BEGIN                  ((uint8_t *) 0x0009FC00)
#define     EBDA_END                    ((uint8_t *) 0x000A0000)

#define     BIOS_BEGIN                  ((uint8_t *) 0x000E0000)
#define     BIOS_END                    ((uint8_t *) 0x00100000)

#define     ACPI_RSDP_SIG_LEN           8
#define     ACPI_RSDP_V1_LEN            20

const char *const fadtPath = "/fadt";

__attribute__((packed)) typedef struct {

    uint8_t address_space;
    uint8_t bit_width;
    uint8_t bit_offset;
    uint8_t access_size;
    
    uint64_t address;

} generic_address_structure_t;

__attribute__((packed)) typedef struct {

    acpi_header_t header;
    
    uint32_t firmwareCtrl;
    uint32_t dsdt;
    
    uint8_t  reserved;
    
    uint8_t  preferredPowerManagementProfile;
    uint16_t sCI_Interrupt;
    uint32_t sMI_CommandPort;
    uint8_t  acpiEnable;
    uint8_t  acpiDisable;
    uint8_t  S4BIOS_REQ;
    uint8_t  PSTATE_Control;
    uint32_t PM1aEventBlock;
    uint32_t PM1bEventBlock;
    uint32_t PM1aControlBlock;
    uint32_t PM1bControlBlock;
    uint32_t PM2ControlBlock;
    uint32_t PMTimerBlock;
    uint32_t GPE0Block;
    uint32_t GPE1Block;
    uint8_t  PM1EventLength;
    uint8_t  PM1ControlLength;
    uint8_t  PM2ControlLength;
    uint8_t  PMTimerLength;
    uint8_t  GPE0Length;
    uint8_t  GPE1Length;
    uint8_t  GPE1Base;
    uint8_t  CStateControl;
    uint16_t worstC2Latency;
    uint16_t worstC3Latency;
    uint16_t flushSize;
    uint16_t flushStride;
    uint8_t  dutyOffset;
    uint8_t  dutyWidth;
    uint8_t  dayAlarm;
    uint8_t  monthAlarm;
    uint8_t  century;
    
    uint16_t bootArchitectureFlags;     /* reserved in ACPI 1.0; used since ACPI 2.0+ */
    
    uint8_t  reserved2;
    uint32_t flags;
    
    generic_address_structure_t resetReg;
    uint8_t  resetValue;
    
    uint16_t armBootArchitectureFlags;
    uint8_t  minorVersion;
    
    /* 64 bit pointers - available on ACPI 2.0+ */
    uint64_t                  X_FirmwareControl;
    uint64_t                  X_Dsdt;
    
    generic_address_structure_t X_PM1aEventBlock;
    generic_address_structure_t X_PM1bEventBlock;
    generic_address_structure_t X_PM1aControlBlock;
    generic_address_structure_t X_PM1bControlBlock;
    generic_address_structure_t X_PM2ControlBlock;
    generic_address_structure_t X_PMTimerBlock;
    generic_address_structure_t X_GPE0Block;
    generic_address_structure_t X_GPE1Block;

} fadt_table_t;

static uint16_t acpi_pm1a_event = 0;
static uint16_t acpi_pm1b_event = 0;

#define     ACPI_FADT_PWR_BUTTON        (1 << 4)
#define     ACPI_PM1_PWRBTN_STS         (1 << 8)

static uint16_t acpi_pm1a_control = 0;
static uint16_t acpi_pm1b_control = 0;

static uint16_t acpi_slp_typa = 0;
static uint16_t acpi_slp_typb = 0;

static uint16_t acpi_gpe0_block = 0;
static uint8_t acpi_gpe0_length = 0;

static uint16_t acpi_gpe1_block = 0;
static uint8_t acpi_gpe1_length = 0;
static uint8_t acpi_gpe1_base = 0;

static uint8_t *acpi_dsdt_aml = 0;
static uint32_t acpi_dsdt_aml_length = 0;

static uint8_t acpi_power_button_gpe = 0;
static int acpi_power_button_gpe_valid = 0;

#define     ACPI_AML_TABLE_MAX          64

typedef struct {

    uint8_t *aml;
    uint32_t length;

} acpi_aml_table_t;

static acpi_aml_table_t acpi_aml_tables[ACPI_AML_TABLE_MAX];
static unsigned acpi_aml_table_count = 0;

static int acpi_fixed_power_button = 0;
static int acpi_s5_available = 0;

static void acpi_poweroff (void) {

    uint16_t value;
    
    if (!acpi_s5_available || !acpi_pm1a_control) {
        return;
    }
    
    __asm__ __volatile__ (
        "inw %1, %0"
        : "=a" (value)
        : "Nd" (acpi_pm1a_control)
    );
    
    value &= ~((7 << 10) | (1 << 13));
    value |= (acpi_slp_typa << 10) | (1 << 13);
    
    __asm__ __volatile__ (
        "outw %0, %1"
        :
        : "a" (value), "Nd" (acpi_pm1a_control)
    );
    
    if (acpi_pm1b_control) {
    
        __asm__ __volatile__ (
            "inw %1, %0"
            : "=a" (value)
            : "Nd" (acpi_pm1b_control)
        );
        
        value &= ~((7 << 10) | (1 << 13));
        value |= (acpi_slp_typb << 10) | (1 << 13);
        
        __asm__ __volatile__ (
            "outw %0, %1"
            :
            : "a" (value), "Nd" (acpi_pm1b_control)
        );
    
    }

}

uint8_t *lapic_final = 0;
#define     lapic_addr                  lapic_final

static int x2apic_enabled = 0;

static void lapic_write (uint32_t reg, uint32_t value) {

#if     defined (__AMD64__)
    if (x2apic_enabled) {
    
        uint32_t msr = 0x800 + (reg >> 4);
        
        __asm__ __volatile__ (
            "wrmsr"
            :
            : "c" (msr), "a" (value), "d" ((uint32_t) 0)
        );
        
        return;
    
    }
#endif
    
    *(uint32_t *) (lapic_addr + reg) = value;
    __asm__ __volatile__ ("" : : : "memory");

}

void acpi_lapic_ack (void) {
    lapic_write (0x0B0, 0);
}

static uint32_t lapic_read (uint32_t reg) {

#if     defined (__AMD64__)
    if (x2apic_enabled) {
    
        uint32_t lo, hi;
        uint32_t msr = 0x800 + (reg >> 4);
        
        __asm__ __volatile__ (
            "rdmsr"
            : "=a" (lo), "=d" (hi)
            : "c" (msr)
        );
        
        return lo;
    
    }
#endif
    
    return *(uint32_t *) (lapic_addr + reg);

}

static int aml_read_pkg_length (uint8_t **ptr, uint8_t *end, uint32_t *length) {

    uint8_t *p = *ptr;
    
    uint8_t lead;
    uint8_t bytes;
    
    uint32_t value;
    unsigned i;
    
    if (p >= end) {
        return 0;
    }
    
    lead = *p++;
    bytes = lead >> 6;
    
    if (!bytes) {
        value = lead & 0x3F;
    } else {
    
        value = lead & 0x0F;
        
        if (p + bytes > end) {
            return 0;
        }
        
        for (i = 0; i < bytes; ++i) {
            value |= ((uint32_t) *p++) << (4 + (i * 8));
        }
    
    }
    
    *length = value;
    *ptr = p;
    
    return 1;

}

static int aml_read_integer (uint8_t **ptr, uint8_t *end, uint64_t *value) {

    uint8_t *p = *ptr;
    
    if (p >= end) {
        return 0;
    }
    
    switch (*p++) {
    
        case 0x00:                      /* ZeroOp */
        
            *value = 0;
            break;
        
        case 0x01:                      /* OneOp */
        
            *value = 1;
            break;
        
        case 0x0A:                      /* BytePrefix */
        
            if (p >= end) {
                return 0;
            }
            
            *value = *p++;
            break;
        
        case 0x0B:                      /* WordPrefix */
        
            if ((size_t) (end - p) < 2) {
                return 0;
            }
            
            *value = (uint64_t) p[0] | ((uint64_t) p[1] << 8);
            
            p += 2;
            break;
        
        case 0x0C:                      /* DWordPrefix */
        
            if ((size_t) (end - p) < 4) {
                return 0;
            }
            
            *value = (uint64_t) p[0] | ((uint64_t) p[1] << 8) | ((uint64_t) p[2] << 16) | ((uint64_t) p[3] << 24);
            
            p += 4;
            break;
        
        case 0x0E:                      /* QWordPrefix */
        
            if ((size_t) (end - p) < 8) {
                return 0;
            }
            
            *value = (uint64_t) p[0] | ((uint64_t) p[1] << 8) | ((uint64_t) p[2] << 16) | ((uint64_t) p[3] << 24) |
                ((uint64_t) p[4] << 32) | ((uint64_t) p[5] << 40) | ((uint64_t) p[6] << 48) | ((uint64_t) p[7] << 56);
            
            p += 8;
            break;
        
        case 0xFF:                      /* OnesOp */
        
            *value = UINT64_MAX;
            break;
        
        default:
        
            return 0;
    
    }
    
    *ptr = p;
    return 1;

}

static int acpi_add_aml_table (uint8_t *aml, uint32_t length) {

    if (!aml || !length || acpi_aml_table_count >= ACPI_AML_TABLE_MAX) {
        return 0;
    }
    
    acpi_aml_tables[acpi_aml_table_count].aml = aml;
    acpi_aml_tables[acpi_aml_table_count].length = length;
    
    ++acpi_aml_table_count;
    return 1;

}

#define     AML_NAME_MAX_SEGMENTS       32

typedef struct {

    int root;
    uint8_t parents;
    uint8_t segments;
    
    char segment[AML_NAME_MAX_SEGMENTS][4];

} aml_name_t;

typedef struct {

    aml_name_t name;
    int power_button;
    
    int prw_valid;
    uint8_t prw_gpe;

} aml_device_t;

#define     AML_DEVICE_MAX              64

static aml_device_t aml_devices[AML_DEVICE_MAX];
static unsigned aml_device_count = 0;

#define     AML_ALIAS_MAX               256

typedef struct {

    aml_name_t name;
    aml_name_t source;

} aml_alias_t;

static aml_alias_t aml_aliases[AML_ALIAS_MAX];
static unsigned aml_alias_count = 0;

typedef enum {

    AML_SUPER_NAME_NAMESPACE,
    AML_SUPER_NAME_LOCAL,
    AML_SUPER_NAME_ARG,
    AML_SUPER_NAME_DEBUG

} aml_super_name_type_t;

typedef struct {

    aml_super_name_type_t type;
    aml_name_t name;
    
    uint8_t index;

} aml_super_name_t;

typedef enum {

    AML_VALUE_NONE,
    AML_VALUE_INTEGER,
    AML_VALUE_REFERENCE,
    AML_VALUE_BUFFER,
    AML_VALUE_BUFFER_FIELD,
    AML_VALUE_REGION,
    AML_VALUE_FIELD,
    AML_VALUE_INDEX_FIELD,
    AML_VALUE_BANK_FIELD

} aml_value_type_t;

typedef struct {

    aml_value_type_t type;
    uint64_t integer;
    
    aml_name_t reference;
    aml_name_t index_reference;
    
    aml_name_t bank_reference;
    uint64_t bank_value;
    
    uint8_t *buffer;
    uint32_t buffer_length;
    
    uint32_t bit_index;
    uint32_t bit_length;
    
    uint8_t field_flags;
    uint8_t field_access;
    
    uint8_t region_space;
    
    uint64_t region_offset;
    uint64_t region_length;

} aml_value_t;

typedef struct {

    aml_name_t name;
    aml_value_t value;

} aml_named_value_t;

#define     AML_NAMED_VALUE_MAX         512

static aml_named_value_t aml_named_values[AML_NAMED_VALUE_MAX];
static unsigned aml_named_value_count = 0;

static int aml_read_name_string (uint8_t **ptr, uint8_t *end, aml_name_t *name) {

    uint8_t *p = *ptr;
    
    uint8_t segments;
    uint8_t segment;
    
    memset (name, 0, sizeof (*name));
    
    if (p >= end) {
        return 0;
    }
    
    if (*p == 0x5C) {                   /* RootChar */
    
        name->root = 1;
        ++p;
    
    } else {
    
        while (p < end && *p == 0x5E) {                     /* ParentPrefixChar */
        
            ++name->parents;
            ++p;
        
        }
    
    }
    
    if (p >= end) {
        return 0;
    }
    
    if (*p == 0x00) {                   /* NullName */
    
        ++p;
        
        *ptr = p;
        return 1;
    
    }
    
    if (*p == 0x2E) {                   /* DualNamePrefix */
    
        ++p;
        segments = 2;
    
    } else if (*p == 0x2F) {            /* MultiNamePrefix */
    
        ++p;
        
        if (p >= end || !*p) {
            return 0;
        }
        
        segments = *p++;
    
    } else {
        segments = 1;
    }
    
    if (segments > AML_NAME_MAX_SEGMENTS) {
        return 0;
    }
    
    if ((size_t) (end - p) < (size_t) segments * 4) {
        return 0;
    }
    
    for (segment = 0; segment < segments; ++segment) {
    
        uint8_t *seg = p + ((size_t) segment * 4);
        unsigned i;
        
        if (!((seg[0] >= 'A' && seg[0] <= 'Z') || seg[0] == '_')) {
            return 0;
        }
        
        for (i = 1; i < 4; ++i) {
        
            if (!((seg[i] >= 'A' && seg[i] <= 'Z') || (seg[i] >= '0' && seg[i] <= '9') || seg[i] == '_')) {
                return 0;
            }
        
        }
        
        memcpy (name->segment[segment], seg, 4);
    
    }
    
    name->segments = segments;
    p += (size_t) segments * 4;
    
    *ptr = p;
    return 1;

}

static int aml_name_equal (const aml_name_t *left, const aml_name_t *right);

static int aml_find_named_value (const aml_name_t *name, aml_value_t *value) {

    unsigned i;
    
    for (i = 0; i < aml_named_value_count; ++i) {
    
        if (aml_name_equal (&aml_named_values[i].name, name)) {
        
            *value = aml_named_values[i].value;
            return 1;
        
        }
    
    }
    
    return 0;

}

typedef struct {

    aml_value_t local[8];
    aml_value_t arg[7];

} aml_execution_context_t;

static int aml_store_value (const aml_super_name_t *target, const aml_name_t *scope, aml_execution_context_t *context, const aml_value_t *value);
static int aml_invoke_method (uint8_t **ptr, uint8_t *end, const aml_name_t *scope, aml_execution_context_t *context, aml_value_t *value);
static int aml_resolve_name (const aml_name_t *scope, const aml_name_t *name, aml_name_t *result);

static int aml_resolve_super_name (const aml_super_name_t *name, const aml_name_t *scope, const aml_execution_context_t *context, aml_name_t *result) {

    switch (name->type) {
    
        case AML_SUPER_NAME_NAMESPACE:
        
            return aml_resolve_name (scope, &name->name, result);
        
        case AML_SUPER_NAME_LOCAL:
        
            if (context->local[name->index].type != AML_VALUE_REFERENCE) {
                return 0;
            }
            
            *result = context->local[name->index].reference;
            return 1;
        
        case AML_SUPER_NAME_ARG:
        
            if (context->arg[name->index].type != AML_VALUE_REFERENCE) {
                return 0;
            }
            
            *result = context->arg[name->index].reference;
            return 1;
        
        case AML_SUPER_NAME_DEBUG:
        
            return 0;
    
    }
    
    return 0;

}

static int aml_read_super_name (uint8_t **ptr, uint8_t *end, aml_super_name_t *name) {

    uint8_t *p = *ptr;
    memset (name, 0, sizeof (*name));
    
    if (p >= end) {
        return 0;
    }
    
    if (*p >= 0x60 && *p <= 0x67) {     /* Local0Op - Local7Op */
    
        name->type = AML_SUPER_NAME_LOCAL;
        name->index = *p++ - 0x60;
        
        *ptr = p;
        return 1;
    
    }
    
    if (*p >= 0x68 && *p <= 0x6E) {     /* Arg0Op - Arg6Op */
    
        name->type = AML_SUPER_NAME_ARG;
        name->index = *p++ - 0x68;
        
        *ptr = p;
        return 1;
    
    }
    
    if ((size_t) (end - p) >= 2 && p[0] == 0x5B && p[1] == 0x31) {              /* DebugOp */
    
        name->type = AML_SUPER_NAME_DEBUG;
        p += 2;
        
        *ptr = p;
        return 1;
    
    }
    
    if (!aml_read_name_string (&p, end, &name->name)) {
        return 0;
    }
    
    name->type = AML_SUPER_NAME_NAMESPACE;
    
    *ptr = p;
    return 1;

}

static int aml_resolve_named_value (const aml_name_t *scope, const aml_name_t *name, aml_name_t *result, aml_value_t *value) {

    aml_name_t search_scope;
    
    if (name->root || name->parents || name->segments != 1) {
    
        if (!aml_resolve_name (scope, name, result)) {
            return 0;
        }
        
        return aml_find_named_value (result, value);
    
    }
    
    search_scope = *scope;
    
    for (;;) {
    
        if (aml_resolve_name (&search_scope, name, result) && aml_find_named_value (result, value)) {
            return 1;
        }
        
        if (!search_scope.segments) {
            break;
        }
        
        search_scope.segments--;
    
    }
    
    return 0;

}

static int aml_read_buffer_field (const aml_value_t *field, aml_value_t *value) {

    aml_value_t buffer;
    uint64_t integer = 0;
    
    uint32_t bit;
    
    if (field->buffer) {
    
        buffer.type = AML_VALUE_BUFFER;
        buffer.buffer = field->buffer;
        buffer.buffer_length = field->buffer_length;
    
    } else if (!aml_find_named_value (&field->reference, &buffer)) {
        return 0;
    }
    
    if (buffer.type != AML_VALUE_BUFFER) {
        return 0;
    }
    
    if (field->bit_length > 64) {
        return 0;
    }
    
    if ((uint64_t) field->bit_index + field->bit_length >
            (uint64_t) buffer.buffer_length * 8) {
        return 0;
    }
    
    for (bit = 0; bit < field->bit_length; ++bit) {
    
        uint32_t source_bit = field->bit_index + bit;
        
        if (buffer.buffer[source_bit >> 3] & (1 << (source_bit & 7))) {
            integer |= ((uint64_t) 1) << bit;
        }
    
    }
    
    memset (value, 0, sizeof (*value));
    
    value->type = AML_VALUE_INTEGER;
    value->integer = integer;
    
    return 1;

}

static int aml_write_buffer_field (const aml_value_t *field, const aml_value_t *value) {

    aml_value_t buffer;
    uint32_t bit;
    
    if (field->type != AML_VALUE_BUFFER_FIELD || value->type != AML_VALUE_INTEGER) {
        return 0;
    }
    
    if (field->buffer) {
    
        buffer.type = AML_VALUE_BUFFER;
        buffer.buffer = field->buffer;
        buffer.buffer_length = field->buffer_length;
    
    } else if (!aml_find_named_value (&field->reference, &buffer)) {
        return 0;
    }
    
    if (buffer.type != AML_VALUE_BUFFER) {
        return 0;
    }
    
    if (field->bit_length > 64) {
        return 0;
    }
    
    if ((uint64_t) field->bit_index + field->bit_length > (uint64_t) buffer.buffer_length * 8) {
        return 0;
    }
    
    for (bit = 0; bit < field->bit_length; ++bit) {
    
        uint32_t target_bit = field->bit_index + bit;
        uint8_t mask = 1 << (target_bit & 7);
        
        if (value->integer & (((uint64_t) 1) << bit)) {
            buffer.buffer[target_bit >> 3] |= mask;
        } else {
            buffer.buffer[target_bit >> 3] &= ~mask;
        }
    
    }
    
    return 1;

}

static int aml_region_read (const aml_value_t *region, uint64_t offset, unsigned size, uint64_t *value) {

    uint64_t address;
    
    if (region->type != AML_VALUE_REGION || offset + size > region->region_length) {
        return 0;
    }
    
    address = region->region_offset + offset;
    
    switch (region->region_space) {
    
        case 0x00:                      /* SystemMemory */
        
            switch (size) {
            
                case 1:
                
                    *value = *(uint8_t *) (uintptr_t) address;
                    return 1;
                
                case 2:
                
                    *value = *(uint16_t *) (uintptr_t) address;
                    return 1;
                
                case 4:
                
                    *value = *(uint32_t *) (uintptr_t) address;
                    return 1;
                
                case 8:
                
                    *value = *(uint64_t *) (uintptr_t) address;
                    return 1;
            
            }
            
            return 0;
        
        case 0x01: {                    /* SystemIO */
        
            uint32_t result;
            uint16_t port;
            
            if (address > 0xFFFF) {
                return 0;
            }
            
            port = (uint16_t) address;
            
            switch (size) {
            
                case 1:
                
                    __asm__ __volatile__ (
                        "inb %1, %b0"
                        : "=a" (result)
                        : "Nd" (port)
                    );
                    
                    *value = result & 0xFF;
                    return 1;
                
                case 2:
                
                    __asm__ __volatile__ (
                        "inw %1, %w0"
                        : "=a" (result)
                        : "Nd" (port)
                    );
                    
                    *value = result & 0xFFFF;
                    return 1;
                
                case 4:
                
                    __asm__ __volatile__ (
                        "inl %1, %0"
                        : "=a" (result)
                        : "Nd" (port)
                    );
                    
                    *value = result;
                    return 1;
            
            }
            
            return 0;
        
        }
    
    }
    
    return 0;

}

static int aml_region_write (const aml_value_t *region, uint64_t offset, unsigned size, uint64_t value) {

    uint64_t address;
    
    if (region->type != AML_VALUE_REGION || offset + size > region->region_length) {
        return 0;
    }
    
    address = region->region_offset + offset;
    
    switch (region->region_space) {
    
        case 0x00:                      /* SystemMemory */
        
            switch (size) {
            
                case 1:
                
                    *(uint8_t *) (uintptr_t) address = (uint8_t) value;
                    return 1;
                
                case 2:
                
                    *(uint16_t *) (uintptr_t) address = (uint16_t) value;
                    return 1;
                
                case 4:
                
                    *(uint32_t *) (uintptr_t) address = (uint32_t) value;
                    return 1;
                
                case 8:
                
                    *(uint64_t *) (uintptr_t) address = value;
                    return 1;
            
            }
            
            return 0;
        
        case 0x01: {                    /* SystemIO */
        
            uint16_t port;
            
            if (address > 0xFFFF) {
                return 0;
            }
            
            port = (uint16_t) address;
            
            switch (size) {
            
                case 1:
                
                    __asm__ __volatile__ (
                        "outb %b0, %1"
                        :
                        : "a" ((uint8_t) value), "Nd" (port)
                    );
                    
                    return 1;
                
                case 2:
                
                    __asm__ __volatile__ (
                        "outw %w0, %1"
                        :
                        : "a" ((uint16_t) value), "Nd" (port)
                    );
                    
                    return 1;
                
                case 4:
                
                    __asm__ __volatile__ (
                        "outl %0, %1"
                        :
                        : "a" ((uint32_t) value), "Nd" (port)
                    );
                    
                    return 1;
            
            }
            
            return 0;
        
        }
    
    }
    
    return 0;

}

static unsigned aml_field_access_size (const aml_value_t *field) {

    switch (field->field_access) {
    
        case 0x01:                      /* ByteAcc */
        
            return 1;
        
        case 0x02:                      /* WordAcc */
        
            return 2;
        
        case 0x03:                      /* DWordAcc */
        
            return 4;
        
        case 0x04:                      /* QWordAcc */
        
            return 8;
        
        case 0x00:                      /* AnyAcc */
        
            if (field->bit_length <= 8) {
                return 1;
            }
            
            if (field->bit_length <= 16) {
                return 2;
            }
            
            if (field->bit_length <= 32) {
                return 4;
            }
            
            if (field->bit_length <= 64) {
                return 8;
            }
            
            break;
    
    }
    
    return 0;

}

static int aml_read_field (const aml_value_t *field, aml_value_t *value) {

    aml_value_t region;
    
    uint64_t integer = 0;
    uint64_t unit;
    
    uint32_t bit;
    unsigned size;
    
    if (field->type != AML_VALUE_FIELD || field->bit_length > 64) {
        return 0;
    }
    
    if (!aml_find_named_value (&field->reference, &region)) {
        return 0;
    }
    
    if (region.type != AML_VALUE_REGION) {
        return 0;
    }
    
    size = aml_field_access_size (field);
    
    if (!size) {
        return 0;
    }
    
    for (bit = 0; bit < field->bit_length; ++bit) {
    
        uint32_t source_bit = field->bit_index + bit;
        
        uint64_t byte_offset = source_bit >> 3;
        uint64_t unit_offset = byte_offset & ~((uint64_t) size - 1);
        
        unsigned unit_bit = source_bit - (uint32_t) (unit_offset << 3);
        
        if (!aml_region_read (&region, unit_offset, size, &unit)) {
            return 0;
        }
        
        if (unit & (((uint64_t) 1) << unit_bit)) {
            integer |= ((uint64_t) 1) << bit;
        }
    
    }
    
    memset (value, 0, sizeof (*value));
    
    value->type = AML_VALUE_INTEGER;
    value->integer = integer;
    
    return 1;

}

static int aml_write_field (const aml_value_t *field, const aml_value_t *value) {

    aml_value_t region;
    
    uint64_t unit;
    uint64_t unit_offset = UINT64_MAX;
    
    uint32_t bit;
    unsigned size;
    
    if (field->type != AML_VALUE_FIELD || value->type != AML_VALUE_INTEGER || field->bit_length > 64) {
        return 0;
    }
    
    if (!aml_find_named_value (&field->reference, &region)) {
        return 0;
    }
    
    if (region.type != AML_VALUE_REGION) {
        return 0;
    }
    
    size = aml_field_access_size (field);
    
    if (!size) {
        return 0;
    }
    
    for (bit = 0; bit < field->bit_length; ++bit) {
    
        uint32_t target_bit = field->bit_index + bit;
        
        uint64_t byte_offset = target_bit >> 3;
        uint64_t target_offset = byte_offset & ~((uint64_t) size - 1);
        
        unsigned unit_bit = target_bit - (uint32_t) (target_offset << 3);
        
        if (target_offset != unit_offset) {
        
            if (unit_offset != UINT64_MAX) {
            
                if (!aml_region_write (&region, unit_offset, size, unit)) {
                    return 0;
                }
            
            }
            
            unit_offset = target_offset;
            
            switch ((field->field_flags >> 5) & 0x03) {
            
                case 0x00:              /* Preserve */
                
                    if (!aml_region_read (&region, unit_offset, size, &unit)) {
                        return 0;
                    }
                    
                    break;
                
                case 0x01:              /* WriteAsOnes */
                
                    unit = UINT64_MAX;
                    break;
                
                case 0x02:              /* WriteAsZeros */
                
                    unit = 0;
                    break;
                
                default:
                
                    return 0;
            
            }
        
        }
        
        if (value->integer & (((uint64_t) 1) << bit)) {
            unit |= ((uint64_t) 1) << unit_bit;
        } else {
            unit &= ~(((uint64_t) 1) << unit_bit);
        }
    
    }
    
    if (unit_offset != UINT64_MAX) {
    
        if (!aml_region_write (&region, unit_offset, size, unit)) {
            return 0;
        }
    
    }
    
    return 1;

}

static int aml_read_index_field (const aml_value_t *field, aml_value_t *value) {

    aml_value_t *scratch;
    aml_super_name_t target;
    
    uint64_t integer = 0;
    uint32_t bit;
    
    scratch = kmalloc (sizeof (*scratch) * 5);
    
    if (!scratch) {
        return 0;
    }
    
    if (field->type != AML_VALUE_INDEX_FIELD || field->bit_length > 64) {
    
        kfree (scratch);
        return 0;
    
    }
    
    if (!aml_find_named_value (&field->index_reference, &scratch[0])) {
    
        kfree (scratch);
        return 0;
    
    }
    
    if (!aml_find_named_value (&field->reference, &scratch[1])) {
    
        kfree (scratch);
        return 0;
    
    }
    
    memset (&target, 0, sizeof (target));
    
    target.type = AML_SUPER_NAME_NAMESPACE;
    target.name = field->index_reference;
    
    for (bit = 0; bit < field->bit_length; ++bit) {
    
        uint32_t source_bit = field->bit_index + bit;
        uint32_t byte_index = source_bit >> 3;
        uint32_t bit_index = source_bit & 7;
        
        scratch[2].type = AML_VALUE_INTEGER;
        scratch[2].integer = byte_index;
        
        if (!aml_store_value (&target, 0, 0, &scratch[2])) {
        
            kfree (scratch);
            return 0;
        
        }
        
        if (!aml_find_named_value (&field->reference, &scratch[3])) {
        
            kfree (scratch);
            return 0;
        
        }
        
        if (scratch[3].type == AML_VALUE_FIELD) {
        
            scratch[4] = scratch[3];
            
            if (!aml_read_field (&scratch[4], &scratch[3])) {
            
                kfree (scratch);
                return 0;
            
            }
        
        }
        
        if (scratch[3].type != AML_VALUE_INTEGER) {
        
            kfree (scratch);
            return 0;
        
        }
        
        if (scratch[3].integer & (((uint64_t) 1) << bit_index)) {
            integer |= ((uint64_t) 1) << bit;
        }
    
    }
    
    memset (value, 0, sizeof (*value));
    
    value->type = AML_VALUE_INTEGER;
    value->integer = integer;
    
    kfree (scratch);
    return 1;

}

static int aml_write_index_field (const aml_value_t *field, const aml_value_t *value) {

    aml_super_name_t index_target;
    aml_super_name_t data_target;
    
    aml_value_t *scratch;
    uint32_t bit;
    
    if (field->type != AML_VALUE_INDEX_FIELD || value->type != AML_VALUE_INTEGER || field->bit_length > 64) {
        return 0;
    }
    
    scratch = kmalloc (sizeof (*scratch) * 4);
    
    if (!scratch) {
        return 0;
    }
    
    memset (&index_target, 0, sizeof (index_target));
    memset (&data_target, 0, sizeof (data_target));
    
    index_target.type = AML_SUPER_NAME_NAMESPACE;
    index_target.name = field->index_reference;
    
    data_target.type = AML_SUPER_NAME_NAMESPACE;
    data_target.name = field->reference;
    
    for (bit = 0; bit < field->bit_length; ++bit) {
    
        uint32_t target_bit = field->bit_index + bit;
        uint32_t byte_index = target_bit >> 3;
        uint32_t bit_index = target_bit & 7;
        
        scratch[0].type = AML_VALUE_INTEGER;
        scratch[0].integer = byte_index;
        
        if (!aml_store_value (&index_target, 0, 0, &scratch[0])) {
        
            kfree (scratch);
            return 0;
        
        }
        
        if (!aml_find_named_value (&field->reference, &scratch[1])) {
        
            kfree (scratch);
            return 0;
        
        }
        
        if (scratch[1].type == AML_VALUE_FIELD) {
        
            scratch[3] = scratch[1];
            
            if (!aml_read_field (&scratch[3], &scratch[1])) {
                return 0;
            }
        
        }
        
        if (scratch[1].type != AML_VALUE_INTEGER) {
        
            kfree (scratch);
            return 0;
        
        }
        
        scratch[2].type = AML_VALUE_INTEGER;
        scratch[2].integer = scratch[1].integer;
        
        if (value->integer & (((uint64_t) 1) << bit)) {
            scratch[2].integer |= ((uint64_t) 1) << bit_index;
        } else {
            scratch[2].integer &= ~(((uint64_t) 1) << bit_index);
        }
        
        if (!aml_store_value (&data_target, 0, 0, &scratch[2])) {
        
            kfree (scratch);
            return 0;
        
        }
    
    }
    
    kfree (scratch);
    return 1;

}

static int aml_read_bank_field (const aml_value_t *field, aml_value_t *value) {

    aml_value_t *scratch;
    aml_super_name_t bank_target;
    
    uint64_t integer = 0;
    uint64_t unit;
    
    uint32_t bit;
    unsigned size;
    
    if (field->type != AML_VALUE_BANK_FIELD || field->bit_length > 64) {
        return 0;
    }
    
    scratch = kmalloc (sizeof (*scratch) * 2);
    
    if (!scratch) {
        return 0;
    }
    
    if (!aml_find_named_value (&field->reference, &scratch[0])) {
    
        kfree (scratch);
        return 0;
    
    }
    
    if (scratch[0].type != AML_VALUE_REGION) {
    
        kfree (scratch);
        return 0;
    
    }
    
    memset (&bank_target, 0, sizeof (bank_target));
    memset (&scratch[1], 0, sizeof (scratch[1]));
    
    bank_target.type = AML_SUPER_NAME_NAMESPACE;
    bank_target.name = field->bank_reference;
    
    scratch[1].type = AML_VALUE_INTEGER;
    scratch[1].integer = field->bank_value;
    
    if (!aml_store_value (&bank_target, 0, 0, &scratch[1])) {
    
        kfree (scratch);
        return 0;
    
    }
    
    size = aml_field_access_size (field);
    
    if (!size) {
    
        kfree (scratch);
        return 0;
    
    }
    
    for (bit = 0; bit < field->bit_length; ++bit) {
    
        uint32_t source_bit = field->bit_index + bit;
        
        uint64_t byte_offset = source_bit >> 3;
        uint64_t unit_offset = byte_offset & ~((uint64_t) size - 1);
        
        unsigned unit_bit = source_bit - (uint32_t) (unit_offset << 3);
        
        if (!aml_region_read (&scratch[0], unit_offset, size, &unit)) {
        
            kfree (scratch);
            return 0;
        
        }
        
        if (unit & (((uint64_t) 1) << unit_bit)) {
            integer |= ((uint64_t) 1) << bit;
        }
    
    }
    
    memset (value, 0, sizeof (*value));
    
    value->type = AML_VALUE_INTEGER;
    value->integer = integer;
    
    kfree (scratch);
    return 1;

}

static int aml_write_bank_field (const aml_value_t *field, const aml_value_t *value) {

    aml_value_t *scratch;
    aml_super_name_t bank_target;
    
    uint64_t unit;
    uint64_t unit_offset = UINT64_MAX;
    
    uint32_t bit;
    unsigned size;
    
    if (field->type != AML_VALUE_BANK_FIELD || value->type != AML_VALUE_INTEGER || field->bit_length > 64) {
        return 0;
    }
    
    scratch = kmalloc (sizeof (*scratch) * 2);
    
    if (!scratch) {
        return 0;
    }
    
    if (!aml_find_named_value (&field->reference, &scratch[0])) {
    
        kfree (scratch);
        return 0;
    
    }
    
    if (scratch[0].type != AML_VALUE_REGION) {
    
        kfree (scratch);
        return 0;
    
    }
    
    memset (&bank_target, 0, sizeof (bank_target));
    memset (&scratch[1], 0, sizeof (scratch[1]));
    
    bank_target.type = AML_SUPER_NAME_NAMESPACE;
    bank_target.name = field->bank_reference;
    
    scratch[1].type = AML_VALUE_INTEGER;
    scratch[1].integer = field->bank_value;
    
    if (!aml_store_value (&bank_target, 0, 0, &scratch[1])) {
    
        kfree (scratch);
        return 0;
    
    }
    
    size = aml_field_access_size (field);
    
    if (!size) {
    
        kfree (scratch);
        return 0;
    
    }
    
    for (bit = 0; bit < field->bit_length; ++bit) {
    
        uint32_t target_bit = field->bit_index + bit;
        
        uint64_t byte_offset = target_bit >> 3;
        uint64_t target_offset = byte_offset & ~((uint64_t) size - 1);
        
        unsigned unit_bit = target_bit - (uint32_t) (target_offset << 3);
        
        if (target_offset != unit_offset) {
        
            if (unit_offset != UINT64_MAX) {
            
                if (!aml_region_write (&scratch[0], unit_offset, size, unit)) {
                
                    kfree (scratch);
                    return 0;
                
                }
            
            }
            
            unit_offset = target_offset;
            
            switch ((field->field_flags >> 5) & 0x03) {
            
                case 0x00:              /* Preserve */
                
                    if (!aml_region_read (&scratch[0], unit_offset, size, &unit)) {
                    
                        kfree (scratch);
                        return 0;
                    
                    }
                    
                    break;
                
                case 0x01:              /* WriteAsOnes */
                
                    unit = UINT64_MAX;
                    break;
                
                case 0x02:              /* WriteAsZeros */
                
                    unit = 0;
                    break;
                
                default:
                
                    kfree (scratch);
                    return 0;
            
            }
        
        }
        
        if (value->integer & (((uint64_t) 1) << bit)) {
            unit |= ((uint64_t) 1) << unit_bit;
        } else {
            unit &= ~(((uint64_t) 1) << unit_bit);
        }
    
    }
    
    if (unit_offset != UINT64_MAX) {
    
        if (!aml_region_write (&scratch[0], unit_offset, size, unit)) {
        
            kfree (scratch);
            return 0;
        
        }
    
    }
    
    kfree (scratch);
    return 1;

}

static int aml_read_value_internal (uint8_t **ptr, uint8_t *end, const aml_name_t *scope, aml_execution_context_t *context, aml_value_t *value, aml_value_t *scratch);

static int aml_read_value (uint8_t **ptr, uint8_t *end, const aml_name_t *scope, aml_execution_context_t *context, aml_value_t *value) {

    aml_value_t *scratch;
    int result;
    
    scratch = kmalloc (sizeof (*scratch) * 4);
    
    if (!scratch) {
        return 0;
    }
    
    result = aml_read_value_internal (ptr, end, scope, context, value, scratch);
    
    kfree (scratch);
    return result;

}

static int aml_read_value_internal (uint8_t **ptr, uint8_t *end, const aml_name_t *scope, aml_execution_context_t *context, aml_value_t *value, aml_value_t *scratch) {

    uint8_t *p = *ptr;
    uint64_t integer;
    
    memset (value, 0, sizeof (*value));
    
    if (aml_read_integer (&p, end, &integer)) {
    
        value->type = AML_VALUE_INTEGER;
        value->integer = integer;
        
        *ptr = p;
        return 1;
    
    }
    
    p = *ptr;
    
    if (p >= end) {
        return 0;
    }
    
    if (*p >= 0x60 && *p <= 0x67) {     /* Local0Op - Local7Op */
    
        *value = context->local[*p - 0x60];
        
        if (value->type == AML_VALUE_NONE) {
            return 0;
        }
        
        *ptr = p + 1;
        return 1;
    
    }
    
    if (*p >= 0x68 && *p <= 0x6E) {     /* Arg0Op - Arg6Op */
    
        *value = context->arg[*p - 0x68];
        
        if (value->type == AML_VALUE_NONE) {
            return 0;
        }
        
        *ptr = p + 1;
        return 1;
    
    }
    
    if (*p == 0x71) {                   /* RefOfOp */
    
        aml_super_name_t reference;
        ++p;
        
        if (!aml_read_super_name (&p, end, &reference)) {
            return 0;
        }
        
        if (!aml_resolve_super_name (&reference, scope, context, &value->reference)) {
            return 0;
        }
        
        value->type = AML_VALUE_REFERENCE;
        
        *ptr = p;
        return 1;
    
    }
    
    if (*p == 0x83) {                   /* DerefOfOp */
    
        ++p;
        
        if (!aml_read_value (&p, end, scope, context, &scratch[0])) {
            return 0;
        }
        
        if (scratch[0].type != AML_VALUE_REFERENCE) {
            return 0;
        }
        
        if (!aml_find_named_value (&scratch[0].reference, value)) {
            return 0;
        }
        
        *ptr = p;
        return 1;
    
    }
    
    if (*p == 0x72 /* AddOp */ || *p == 0x74 /* SubtractOp */ || *p == 0x77 /* MultiplyOp */ || *p == 0x79 /* ShiftLeftOp */ || *p == 0x7A /* ShiftRightOp */ || *p == 0x7B /* AndOp */ || *p == 0x7C /* NAndOp */ || *p == 0x7D /* OrOp */ || *p == 0x7E /* NOrOp */ || *p == 0x7F /* XorOp */ || *p == 0x85 /* ModOp */) {
    
        uint8_t opcode = *p++;
        aml_super_name_t target;
        
        if (!aml_read_value (&p, end, scope, context, &scratch[0])) {
            return 0;
        }
        
        if (!aml_read_value (&p, end, scope, context, &scratch[1])) {
            return 0;
        }
        
        if (scratch[0].type != AML_VALUE_INTEGER || scratch[1].type != AML_VALUE_INTEGER) {
            return 0;
        }
        
        value->type = AML_VALUE_INTEGER;
        
        switch (opcode) {
        
            case 0x72:                  /* AddOp */
            
                value->integer = scratch[0].integer + scratch[1].integer;
                break;
            
            case 0x74:                  /* SubtractOp */
            
                value->integer = scratch[0].integer - scratch[1].integer;
                break;
            
            case 0x77:                  /* MultiplyOp */
            
                value->integer = scratch[0].integer * scratch[1].integer;
                break;
            
            case 0x79:                  /* ShiftLeftOp */
            
                if (scratch[1].integer >= 64) {
                    value->integer = 0;
                } else {
                    value->integer = scratch[0].integer << scratch[1].integer;
                }
                
                break;
            
            case 0x7A:                  /* ShiftRightOp */
            
                if (scratch[1].integer >= 64) {
                    value->integer = 0;
                } else {
                    value->integer = scratch[0].integer >> scratch[1].integer;
                }
                
                break;
            
            case 0x7B:                  /* AndOp */
            
                value->integer = scratch[0].integer & scratch[1].integer;
                break;
            
            case 0x7C:                  /* NAndOp */
            
                value->integer = ~(scratch[0].integer & scratch[1].integer);
                break;
            
            case 0x7D:                  /* OrOp */
            
                value->integer = scratch[0].integer | scratch[1].integer;
                break;
            
            case 0x7E:                  /* NOrOp */
            
                value->integer = ~(scratch[0].integer | scratch[1].integer);
                break;
            
            case 0x7F:                  /* XorOp */
            
                value->integer = scratch[0].integer ^ scratch[1].integer;
                break;
            
            case 0x85:                  /* ModOp */
            
                if (!scratch[1].integer) {
                    return 0;
                }
                
                value->integer = scratch[0].integer % scratch[1].integer;
                break;
        
        }
        
        if (!aml_read_super_name (&p, end, &target)) {
            return 0;
        }
        
        if (target.type != AML_SUPER_NAME_NAMESPACE || target.name.root || target.name.parents || target.name.segments) {
        
            if (!aml_store_value (&target, scope, context, value)) {
                return 0;
            }
        
        }
        
        *ptr = p;
        return 1;
    
    }
    
    if (*p == 0x75 || *p == 0x76) {     /* IncrementOp / DecrementOp */
    
        uint8_t opcode = *p++;
        aml_super_name_t target;
        
        if (!aml_read_super_name (&p, end, &target)) {
            return 0;
        }
        
        if (target.type == AML_SUPER_NAME_LOCAL) {
            scratch[0] = context->local[target.index];
        } else if (target.type == AML_SUPER_NAME_ARG) {
            scratch[0] = context->arg[target.index];
        } else if (target.type == AML_SUPER_NAME_NAMESPACE) {
        
            aml_name_t resolved;
            
            if (!aml_resolve_name (scope, &target.name, &resolved)) {
                return 0;
            }
            
            if (!aml_find_named_value (&resolved, &scratch[0])) {
                return 0;
            }
        
        } else {
            return 0;
        }
        
        if (scratch[0].type != AML_VALUE_INTEGER) {
            return 0;
        }
        
        value->type = AML_VALUE_INTEGER;
        
        if (opcode == 0x75) {            /* IncrementOp */
            value->integer = scratch[0].integer + 1;
        } else {                         /* DecrementOp */
            value->integer = scratch[0].integer - 1;
        }
        
        if (!aml_store_value (&target, scope, context, value)) {
            return 0;
        }
        
        *ptr = p;
        return 1;
    
    }
    
    if (*p == 0x87) {                   /* SizeOfOp */
    
        ++p;
        
        if (!aml_read_value (&p, end, scope, context, &scratch[0])) {
            return 0;
        }
        
        value->type = AML_VALUE_INTEGER;
        
        switch (scratch[0].type) {
        
            case AML_VALUE_INTEGER:
            
                value->integer = 8;
                break;
            
            case AML_VALUE_BUFFER:
            
                value->integer = scratch[0].buffer_length;
                break;
            
            default:
            
                return 0;
        
        }
        
        *ptr = p;
        return 1;
    
    }
    
    if (*p == 0x78) {                   /* DivideOp */
    
        aml_super_name_t remainder_target;
        aml_super_name_t quotient_target;
        
        ++p;
        
        if (!aml_read_value (&p, end, scope, context, &scratch[0])) {
            return 0;
        }
        
        if (!aml_read_value (&p, end, scope, context, &scratch[1])) {
            return 0;
        }
        
        if (scratch[0].type != AML_VALUE_INTEGER || scratch[1].type != AML_VALUE_INTEGER || !scratch[1].integer) {
            return 0;
        }
        
        if (!aml_read_super_name (&p, end, &remainder_target)) {
            return 0;
        }
        
        if (!aml_read_super_name (&p, end, &quotient_target)) {
            return 0;
        }
        
        scratch[2].type = AML_VALUE_INTEGER;
        scratch[2].integer = scratch[0].integer % scratch[1].integer;
        
        scratch[3].type = AML_VALUE_INTEGER;
        scratch[3].integer = scratch[0].integer / scratch[1].integer;
        
        if (remainder_target.type != AML_SUPER_NAME_NAMESPACE || remainder_target.name.root || remainder_target.name.parents || remainder_target.name.segments) {
        
            if (!aml_store_value (&remainder_target, scope, context, &scratch[2])) {
                return 0;
            }
        
        }
        
        if (quotient_target.type != AML_SUPER_NAME_NAMESPACE || quotient_target.name.root || quotient_target.name.parents || quotient_target.name.segments) {
        
            if (!aml_store_value (&quotient_target, scope, context, &scratch[3])) {
                return 0;
            }
        
        }
        
        *value = scratch[3];
        
        *ptr = p;
        return 1;
    
    }
    
    if (*p == 0x80) {                   /* NotOp */
    
        aml_super_name_t target;
        ++p;
        
        if (!aml_read_value (&p, end, scope, context, &scratch[0])) {
            return 0;
        }
        
        if (scratch[0].type != AML_VALUE_INTEGER) {
            return 0;
        }
        
        value->type = AML_VALUE_INTEGER;
        value->integer = ~scratch[0].integer;
        
        if (!aml_read_super_name (&p, end, &target)) {
            return 0;
        }
        
        if (target.type != AML_SUPER_NAME_NAMESPACE || target.name.root || target.name.parents || target.name.segments) {
        
            if (!aml_store_value (&target, scope, context, value)) {
                return 0;
            }
        
        }
        
        *ptr = p;
        return 1;
    
    }
    
    if (*p == 0x81 || *p == 0x82) {     /* FindSetLeftBitOp / FindSetRightBitOp */
    
        uint8_t opcode = *p++;
        aml_super_name_t target;
        
        if (!aml_read_value (&p, end, scope, context, &scratch[0])) {
            return 0;
        }
        
        if (scratch[0].type != AML_VALUE_INTEGER) {
            return 0;
        }
        
        value->type = AML_VALUE_INTEGER;
        value->integer = 0;
        
        if (scratch[0].integer) {
        
            if (opcode == 0x81) {       /* FindSetLeftBitOp */
            
                uint64_t mask = ((uint64_t) 1) << 63;
                unsigned bit = 64;
                
                while (!(scratch[0].integer & mask)) {
                
                    mask >>= 1;
                    --bit;
                
                }
                
                value->integer = bit;
            
            } else {                    /* FindSetRightBitOp */
            
                uint64_t mask = 1;
                unsigned bit = 1;
                
                while (!(scratch[0].integer & mask)) {
                
                    mask <<= 1;
                    ++bit;
                
                }
                
                value->integer = bit;
            
            }
        
        }
        
        if (!aml_read_super_name (&p, end, &target)) {
            return 0;
        }
        
        if (target.type != AML_SUPER_NAME_NAMESPACE || target.name.root || target.name.parents || target.name.segments) {
        
            if (!aml_store_value (&target, scope, context, value)) {
                return 0;
            }
        
        }
        
        *ptr = p;
        return 1;
    
    }
    
    if (*p == 0x92) {                   /* LNotOp */
    
        ++p;
        
        if (!aml_read_value (&p, end, scope, context, &scratch[0])) {
            return 0;
        }
        
        if (scratch[0].type != AML_VALUE_INTEGER) {
            return 0;
        }
        
        value->type = AML_VALUE_INTEGER;
        value->integer = scratch[0].integer ? 0 : 1;
        
        *ptr = p;
        return 1;
    
    }
    
    if (*p == 0x90 /* LAndOp */ || *p == 0x91 /* LOrOp */ || *p == 0x93 /* LEqualOp */ || *p == 0x94 /* LGreaterOp */ || *p == 0x95) {               /* LLessOp */
    
        uint8_t opcode = *p++;
        
        if (!aml_read_value (&p, end, scope, context, &scratch[0])) {
            return 0;
        }
        
        if (!aml_read_value (&p, end, scope, context, &scratch[1])) {
            return 0;
        }
        
        if (scratch[0].type != AML_VALUE_INTEGER || scratch[1].type != AML_VALUE_INTEGER) {
            return 0;
        }
        
        value->type = AML_VALUE_INTEGER;
        
        switch (opcode) {
        
            case 0x90:                  /* LAndOp */
            
                value->integer = scratch[0].integer && scratch[1].integer;
                break;
            
            case 0x91:                  /* LOrOp */
            
                value->integer = scratch[0].integer || scratch[1].integer;
                break;
            
            case 0x93:                  /* LEqualOp */
            
                value->integer = scratch[0].integer == scratch[1].integer;
                break;
            
            case 0x94:                  /* LGreaterOp */
            
                value->integer = scratch[0].integer > scratch[1].integer;
                break;
            
            case 0x95:                  /* LLessOp */
            
                value->integer = scratch[0].integer < scratch[1].integer;
                break;
        
        }
        
        *ptr = p;
        return 1;
    
    }
    
    if (*p == 0x9D) {                   /* CopyObjectOp */
    
        aml_super_name_t target;
        
        ++p;
        
        if (!aml_read_value (&p, end, scope, context, &scratch[0])) {
            return 0;
        }
        
        if (!aml_read_super_name (&p, end, &target)) {
            return 0;
        }
        
        if (!aml_store_value (&target, scope, context, &scratch[0])) {
            return 0;
        }
        
        *value = scratch[0];
        
        *ptr = p;
        return 1;
    
    }
    
    {
    
        aml_name_t name;
        aml_name_t resolved;
        
        p = *ptr;
        
        if (aml_read_name_string (&p, end, &name)) {
        
            if (aml_resolve_named_value (scope, &name, &resolved, value)) {
            
                if (value->type == AML_VALUE_BUFFER_FIELD) {
                
                    scratch[0] = *value;
                    
                    if (!aml_read_buffer_field (&scratch[0], value)) {
                        return 0;
                    }
                
                } else if (value->type == AML_VALUE_FIELD) {
                
                    scratch[0] = *value;
                    
                    if (!aml_read_field (&scratch[0], value)) {
                        return 0;
                    }
                
                } else if (value->type == AML_VALUE_INDEX_FIELD) {
                
                    scratch[0] = *value;
                    
                    if (!aml_read_index_field (&scratch[0], value)) {
                        return 0;
                    }
                
                } else if (value->type == AML_VALUE_BANK_FIELD) {
                
                    scratch[0] = *value;
                    
                    if (!aml_read_bank_field (&scratch[0], value)) {
                        return 0;
                    }
                
                }
                
                *ptr = p;
                return 1;
            
            }
        
        }
    
    }
    
    p = *ptr;
    
    if (aml_invoke_method (&p, end, scope, context, value)) {
    
        *ptr = p;
        return 1;
    
    }
    
    return 0;

}

static int aml_store_value (const aml_super_name_t *target, const aml_name_t *scope, aml_execution_context_t *context, const aml_value_t *value) {

    aml_name_t resolved;
    unsigned i;
    
    switch (target->type) {
    
        case AML_SUPER_NAME_LOCAL:
        
            context->local[target->index] = *value;
            return 1;
        
        case AML_SUPER_NAME_ARG:
        
            context->arg[target->index] = *value;
            return 1;
        
        case AML_SUPER_NAME_NAMESPACE: {
        
            aml_value_t current;
            
            if (!aml_resolve_named_value (scope, &target->name, &resolved, &current)) {
                return 0;
            }
            
            if (current.type == AML_VALUE_BUFFER_FIELD) {
                return aml_write_buffer_field (&current, value);
            }
            
            if (current.type == AML_VALUE_FIELD) {
                return aml_write_field (&current, value);
            }
            
            if (current.type == AML_VALUE_INDEX_FIELD) {
                return aml_write_index_field (&current, value);
            }
            
            if (current.type == AML_VALUE_BANK_FIELD) {
                return aml_write_bank_field (&current, value);
            }
            
            for (i = 0; i < aml_named_value_count; ++i) {
            
                if (aml_name_equal (&aml_named_values[i].name, &resolved)) {
                
                    aml_named_values[i].value = *value;
                    return 1;
                
                }
            
            }
            
            return 0;
        
        }
        
        case AML_SUPER_NAME_DEBUG:
        
            return 0;
    
    }
    
    return 0;

}

static int aml_skip_target (uint8_t **ptr, uint8_t *end) {

    uint8_t *p = *ptr;
    aml_name_t name;
    
    if (p >= end) {
        return 0;
    }
    
    if (*p == 0x00) {                   /* NullName */
    
        *ptr = p + 1;
        return 1;
    
    }
    
    if (*p >= 0x60 && *p <= 0x67) {     /* Local0Op - Local7Op */
    
        *ptr = p + 1;
        return 1;
    
    }
    
    if (*p >= 0x68 && *p <= 0x6E) {     /* Arg0Op - Arg6Op */
    
        *ptr = p + 1;
        return 1;
    
    }
    
    if (aml_read_name_string (&p, end, &name)) {
    
        *ptr = p;
        return 1;
    
    }
    
    return 0;

}

static int aml_skip_term_arg (uint8_t **ptr, uint8_t *end) {

    uint8_t *p = *ptr;
    uint64_t value;
    
    if (aml_read_integer (&p, end, &value)) {
    
        *ptr = p;
        return 1;
    
    }
    
    p = *ptr;
    
    if (p >= end) {
        return 0;
    }
    
    if (*p == 0x0D) {                   /* StringPrefix */
    
        ++p;
        
        while (p < end && *p) {
            ++p;
        }
        
        if (p >= end) {
            return 0;
        }
        
        ++p;
        *ptr = p;
        
        return 1;
    
    }
    
    /* Local0Op through Local7Op. */
    if (*p >= 0x60 && *p <= 0x67) {
    
        *ptr = p + 1;
        return 1;
    
    }
    
    /* Arg0Op through Arg6Op. */
    if (*p >= 0x68 && *p <= 0x6E) {
    
        *ptr = p + 1;
        return 1;
    
    }
    
    switch (*p) {
    
        case 0x72:                      /* AddOp */
        case 0x74:                      /* SubtractOp */
        case 0x77:                      /* MultiplyOp */
        case 0x79:                      /* ShiftLeftOp */
        case 0x7A:                      /* ShiftRightOp */
        case 0x7B:                      /* AndOp */
        case 0x7C:                      /* NAndOp */
        case 0x7D:                      /* OrOp */
        case 0x7E:                      /* NOrOp */
        case 0x7F:                      /* XorOp */
        case 0x85:                      /* ModOp */
        
            ++p;
            
            if (!aml_skip_term_arg (&p, end)) {
                return 0;
            }
            
            if (!aml_skip_term_arg (&p, end)) {
                return 0;
            }
            
            if (!aml_skip_target (&p, end)) {
                return 0;
            }
            
            *ptr = p;
            return 1;
        
        case 0x75:                      /* IncrementOp */
        case 0x76:                      /* DecrementOp */
        
            ++p;
            
            if (!aml_skip_target (&p, end)) {
                return 0;
            }
            
            *ptr = p;
            return 1;
        
        case 0x78:                      /* DivideOp */
        
            ++p;
            
            if (!aml_skip_term_arg (&p, end)) {
                return 0;
            }
            
            if (!aml_skip_term_arg (&p, end)) {
                return 0;
            }
            
            if (!aml_skip_target (&p, end)) {
                return 0;
            }
            
            if (!aml_skip_target (&p, end)) {
                return 0;
            }
            
            *ptr = p;
            return 1;
        
        case 0x80:                      /* NotOp */
        
            ++p;
            
            if (!aml_skip_term_arg (&p, end)) {
                return 0;
            }
            
            if (!aml_skip_target (&p, end)) {
                return 0;
            }
            
            *ptr = p;
            return 1;
        
        case 0x81:                      /* FindSetLeftBitOp */
        case 0x82:                      /* FindSetRightBitOp */
        
            ++p;
            
            if (!aml_skip_term_arg (&p, end)) {
                return 0;
            }
            
            if (!aml_skip_target (&p, end)) {
                return 0;
            }
            
            *ptr = p;
            return 1;
        
        case 0x83:                      /* DerefOfOp */
        
            ++p;
            
            if (!aml_skip_term_arg (&p, end)) {
                return 0;
            }
            
            *ptr = p;
            return 1;
        
        case 0x87:                      /* SizeOfOp */
        
            ++p;
            
            if (!aml_skip_term_arg (&p, end)) {
                return 0;
            }
            
            *ptr = p;
            return 1;
        
        case 0x90:                      /* LAndOp */
        case 0x91:                      /* LOrOp */
        case 0x93:                      /* LEqualOp */
        case 0x94:                      /* LGreaterOp */
        case 0x95:                      /* LLessOp */
        
            ++p;
            
            if (!aml_skip_term_arg (&p, end)) {
                return 0;
            }
            
            if (!aml_skip_term_arg (&p, end)) {
                return 0;
            }
            
            *ptr = p;
            return 1;
        
        case 0x92:                      /* LNotOp */
        
            ++p;
            
            if (!aml_skip_term_arg (&p, end)) {
                return 0;
            }
            
            *ptr = p;
            return 1;
        
        case 0x9D:                      /* CopyObjectOp */
        
            ++p;
            
            if (!aml_skip_term_arg (&p, end)) {
                return 0;
            }
            
            if (!aml_skip_target (&p, end)) {
                return 0;
            }
            
            *ptr = p;
            return 1;
    
    }
    
    /**
     * A NameString can itself be a TermArg.  We only need to consume
     * the name here; evaluation of the referenced object is separate.
     */
    {
    
        aml_name_t name;
        
        if (aml_read_name_string (&p, end, &name)) {
        
            *ptr = p;
            return 1;
        
        }
    
    }
    
    return 0;

}

static int aml_resolve_name (const aml_name_t *scope,  const aml_name_t *name, aml_name_t *result) {

    uint8_t segment;
    uint8_t base;
    
    memset (result, 0, sizeof (*result));
    result->root = 1;
    
    if (name->root) {
    
        if (name->segments > AML_NAME_MAX_SEGMENTS) {
            return 0;
        }
        
        result->segments = name->segments;
        
        for (segment = 0; segment < name->segments; ++segment) {
            memcpy (result->segment[segment], name->segment[segment], 4);
        }
        
        return 1;
    
    }
    
    base = scope->segments;
    
    if (name->parents > base) {
        return 0;
    }
    
    base -= name->parents;
    
    if ((unsigned) base + name->segments > AML_NAME_MAX_SEGMENTS) {
        return 0;
    }
    
    for (segment = 0; segment < base; ++segment) {
        memcpy (result->segment[segment], scope->segment[segment], 4);
    }
    
    for (segment = 0; segment < name->segments; ++segment) {
        memcpy (result->segment[base + segment], name->segment[segment], 4);
    }
    
    result->segments = base + name->segments;
    return 1;

}

static int aml_name_equal (const aml_name_t *left, const aml_name_t *right) {

    uint8_t segment;
    
    if (left->segments != right->segments) {
        return 0;
    }
    
    for (segment = 0; segment < left->segments; ++segment) {
    
        if (memcmp (left->segment[segment], right->segment[segment], 4) != 0) {
            return 0;
        }
    
    }
    
    return 1;

}

static int aml_name_last_equal (const aml_name_t *name, const char segment[4]) {

    if (!name->segments) {
        return 0;
    }
    
    return memcmp (name->segment[name->segments - 1], segment, 4) == 0;

}

static int aml_eisa_id (uint64_t value, const char id[7]) {

    uint32_t expected;
    uint16_t vendor;
    uint16_t product;
    
    vendor = ((uint16_t) (id[0] - '@') << 10) | ((uint16_t) (id[1] - '@') << 5) | (uint16_t) (id[2] - '@');
    product = ((uint16_t) (id[3] >= 'A' ? id[3] - 'A' + 10 : id[3] - '0') << 12) | ((uint16_t) (id[4] >= 'A' ? id[4] - 'A' + 10 : id[4] - '0') << 8) | ((uint16_t) (id[5] >= 'A' ? id[5] - 'A' + 10 : id[5] - '0') << 4) | (uint16_t) (id[6] >= 'A' ? id[6] - 'A' + 10 : id[6] - '0');
    
    expected = ((uint32_t) (vendor & 0x00FF) << 0) | ((uint32_t) (vendor & 0xFF00) << 0) | ((uint32_t) (product & 0x00FF) << 16) | ((uint32_t) (product & 0xFF00) << 16);
    expected = ((expected & 0x000000FF) << 8) | ((expected & 0x0000FF00) >> 8) | ((expected & 0x00FF0000) << 8) | ((expected & 0xFF000000) >> 8);
    
    return (uint32_t) value == expected;

}

static int aml_hid_is_power_button (uint8_t **ptr, uint8_t *end) {

    uint8_t *p = *ptr;
    uint64_t value;
    
    if (aml_read_integer (&p, end, &value)) {
    
        *ptr = p;
        return aml_eisa_id (value, "PNP0C0C");
    
    }
    
    p = *ptr;
    
    if (p >= end || *p++ != 0x0D) {     /* StringPrefix */
        return 0;
    }
    
    if ((size_t) (end - p) < 8) {
        return 0;
    }
    
    if (memcmp (p, "PNP0C0C", 7) != 0 || p[7] != 0) {
        return 0;
    }
    
    p += 8;
    *ptr = p;
    
    return 1;

}

static int aml_read_prw (uint8_t *ptr, uint8_t *end, uint8_t *gpe) {

    uint8_t *package;
    uint8_t *package_end;
    
    uint32_t length;
    uint64_t value;
    
    if (ptr >= end || *ptr++ != 0x12) {     /* PackageOp */
        return 0;
    }
    
    package = ptr;
    
    if (!aml_read_pkg_length (&ptr, end, &length)) {
        return 0;
    }
    
    if (length > (uint32_t) (end - package)) {
        return 0;
    }
    
    package_end = package + length;
    
    if (ptr >= package_end) {
        return 0;
    }
    
    ++ptr;                                  /* NumElements */
    
    if (!aml_read_integer (&ptr, package_end, &value)) {
        return 0;
    }
    
    if (value > 0xFF) {
        return 0;
    }
    
    *gpe = (uint8_t) value;
    return 1;

}

static int aml_device_is_power_button (const aml_name_t *name) {

    const aml_name_t *path = name;
    unsigned i, depth;
    
    for (depth = 0; depth <= aml_alias_count; ++depth) {
    
        if (!path->root || !path->segments) {
            return 0;
        }
        
        for (i = 0; i < aml_device_count; ++i) {
        
            if (aml_devices[i].power_button && aml_devices[i].name.root && aml_devices[i].name.segments && aml_name_equal (&aml_devices[i].name, path)) {
                return 1;
            }
        
        }
        
        for (i = 0; i < aml_alias_count; ++i) {
        
            if (aml_name_equal (&aml_aliases[i].name, path)) {
            
                path = &aml_aliases[i].source;
                break;
            
            }
        
        }
        
        if (i == aml_alias_count) {
            break;
        }
    
    }
    
    return 0;

}

static int aml_skip_string (uint8_t **ptr, uint8_t *end) {

    uint8_t *p = *ptr;
    
    if (p >= end || *p++ != 0x0D) {     /* StringPrefix */
        return 0;
    }
    
    while (p < end && *p) {
        ++p;
    }
    
    if (p >= end) {
        return 0;
    }
    
    ++p;
    *ptr = p;
    
    return 1;

}

static int aml_skip_package (uint8_t **ptr, uint8_t *end, unsigned opcode_length) {

    uint8_t *p = *ptr;
    uint8_t *package;
    
    uint32_t length;
    
    if ((size_t) (end - p) < opcode_length) {
        return 0;
    }
    
    p += opcode_length;
    package = p;
    
    if (!aml_read_pkg_length (&p, end, &length)) {
        return 0;
    }
    
    if (length > (uint32_t) (end - package)) {
        return 0;
    }
    
    *ptr = package + length;
    return 1;

}

static int aml_skip_data_object (uint8_t **ptr, uint8_t *end) {

    uint8_t *p = *ptr;
    uint64_t value;
    
    if (aml_read_integer (&p, end, &value)) {
    
        *ptr = p;
        return 1;
    
    }
    
    p = *ptr;
    
    if (p >= end) {
        return 0;
    }
    
    if (*p == 0x0D) {                   /* StringPrefix */
        return aml_skip_string (ptr, end);
    }
    
    if (*p == 0x11 ||                   /* BufferOp */
            *p == 0x12 ||               /* PackageOp */
            *p == 0x13) {               /* VarPackageOp */
        return aml_skip_package (ptr, end, 1);
    }
    
    return 0;

}

static int aml_skip_data_ref_object (uint8_t **ptr, uint8_t *end) {

    uint8_t *p = *ptr;
    aml_name_t name;
    
    if (aml_skip_data_object (&p, end)) {
    
        *ptr = p;
        return 1;
    
    }
    
    p = *ptr;
    
    if (aml_read_name_string (&p, end, &name)) {
    
        *ptr = p;
        return 1;
    
    }
    
    return 0;

}

static int aml_skip_name_string (uint8_t **ptr, uint8_t *end) {

    aml_name_t name;
    return aml_read_name_string (ptr, end, &name);

}

static int aml_skip_namespace_object (uint8_t **ptr, uint8_t *end) {

    uint8_t *p = *ptr;
    uint8_t *q;
    
    aml_name_t name;
    
    if (p >= end) {
        return 0;
    }
    
    switch (*p) {
    
        case 0x06:                      /* AliasOp */
        
            q = p + 1;
            
            if (!aml_skip_name_string (&q, end) ||
                    !aml_skip_name_string (&q, end)) {
                return 0;
            }
            
            *ptr = q;
            return 1;
        
        case 0x08:                      /* NameOp */
        
            q = p + 1;
            
            if (!aml_read_name_string (&q, end, &name)) {
                return 0;
            }
            
            if (!aml_skip_data_ref_object (&q, end)) {
                return 0;
            }
            
            *ptr = q;
            return 1;
        
        case 0x10:                      /* ScopeOp */
        case 0x11:                      /* BufferOp */
        case 0x12:                      /* PackageOp */
        case 0x13:                      /* VarPackageOp */
        case 0x14:                      /* MethodOp */
        
            return aml_skip_package (ptr, end, 1);
        
        case 0x15:                      /* ExternalOp */
        
            q = p + 1;
            
            if (!aml_skip_name_string (&q, end)) {
                return 0;
            }
            
            if ((size_t) (end - q) < 2) {
                return 0;
            }
            
            q += 2;                     /* ObjectType, ArgumentCount */
            
            *ptr = q;
            return 1;
        
        case 0xA0:                      /* IfOp */
        case 0xA1:                      /* ElseOp */
        case 0xA2:                      /* WhileOp */
        
            return aml_skip_package (ptr, end, 1);
            
        case 0x5B:                      /* ExtOpPrefix */
        
            if ((size_t) (end - p) < 2) {
                return 0;
            }
            
            switch (p[1]) {
            
                case 0x01:              /* MutexOp */
                
                    q = p + 2;
                    
                    if (!aml_skip_name_string (&q, end)) {
                        return 0;
                    }
                    
                    if (q >= end) {
                        return 0;
                    }
                    
                    ++q;                /* SyncFlags */
                    
                    *ptr = q;
                    return 1;
                
                case 0x02:              /* EventOp */
                
                    q = p + 2;
                    
                    if (!aml_skip_name_string (&q, end)) {
                        return 0;
                    }
                    
                    *ptr = q;
                    return 1;
                
                case 0x13:              /* CreateFieldOp */
                
                    q = p + 2;
                    
                    if (!aml_skip_term_arg (&q, end)) {
                        return 0;
                    }
                    
                    if (!aml_skip_term_arg (&q, end)) {
                        return 0;
                    }
                    
                    if (!aml_skip_term_arg (&q, end)) {
                        return 0;
                    }
                    
                    if (!aml_skip_name_string (&q, end)) {
                        return 0;
                    }
                    
                    *ptr = q;
                    return 1;
                
                case 0x80:              /* OpRegionOp */
                
                    q = p + 2;
                    
                    if (!aml_skip_name_string (&q, end)) {
                        return 0;
                    }
                    
                    if (q >= end) {
                        return 0;
                    }
                    
                    ++q;                /* RegionSpace */
                    
                    if (!aml_skip_term_arg (&q, end)) {
                        return 0;
                    }
                    
                    if (!aml_skip_term_arg (&q, end)) {
                        return 0;
                    }
                    
                    *ptr = q;
                    return 1;
                
                case 0x81:              /* FieldOp */
                case 0x82:              /* DeviceOp */
                case 0x83:              /* ProcessorOp */
                case 0x84:              /* PowerResOp */
                case 0x85:              /* ThermalZoneOp */
                case 0x86:              /* IndexFieldOp */
                case 0x87:              /* BankFieldOp */
                
                    return aml_skip_package (ptr, end, 2);
                
                case 0x88:              /* DataRegionOp */
                
                    q = p + 2;
                    
                    if (!aml_skip_name_string (&q, end)) {
                        return 0;
                    }
                    
                    if (!aml_skip_term_arg (&q, end)) {
                        return 0;
                    }
                    
                    if (!aml_skip_term_arg (&q, end)) {
                        return 0;
                    }
                    
                    if (!aml_skip_term_arg (&q, end)) {
                        return 0;
                    }
                    
                    *ptr = q;
                    return 1;
                
                default:
                
                    return 0;
            
            }
        
        case 0x8B:                      /* CreateWordFieldOp */
        case 0x8C:                      /* CreateByteFieldOp */
        case 0x8E:                      /* CreateDWordFieldOp */
        case 0x8F:                      /* CreateQWordFieldOp */
        
            q = p + 1;
            
            if (!aml_skip_term_arg (&q, end)) {
                return 0;
            }
            
            if (!aml_skip_term_arg (&q, end)) {
                return 0;
            }
            
            if (!aml_skip_name_string (&q, end)) {
                return 0;
            }
            
            *ptr = q;
            return 1;
        
        case 0x8D:                      /* CreateBitFieldOp */
        
            q = p + 1;
            
            if (!aml_skip_term_arg (&q, end)) {
                return 0;
            }
            
            if (!aml_skip_term_arg (&q, end)) {
                return 0;
            }
            
            if (!aml_skip_name_string (&q, end)) {
                return 0;
            }
            
            *ptr = q;
            return 1;
        
        default:
        
            return 0;
    
    }

}

static void aml_scan_devices (uint8_t *aml, uint8_t *end, const aml_name_t *scope) {

    uint8_t *p = aml;
    
    while (p + 2 <= end) {
    
        uint8_t *package;
        uint8_t *package_end;
        uint8_t *q;
        
        uint32_t length;
        
        aml_name_t device_name;
        aml_name_t device_path;
        
        if (p[0] == 0x10) {             /* ScopeOp */
        
            aml_name_t name;
            aml_name_t new_scope;
            
            package = p + 1;
            q = package;
            
            if (!aml_read_pkg_length (&q, end, &length)) {
                return;
            }
            
            if (length > (uint32_t) (end - package)) {
                return;
            }
            
            package_end = package + length;
            
            if (!aml_read_name_string (&q, package_end, &name) || !aml_resolve_name (scope, &name, &new_scope)) {
                return;
            }
            
            aml_scan_devices (q, package_end, &new_scope);
            
            p = package_end;
            continue;
        
        }
        
        if (p[0] != 0x5B || p[1] != 0x82) {
        
            uint8_t *next = p;
            
            if (!aml_skip_namespace_object (&next, end) || next <= p) {
                break;
            }
            
            p = next;
            continue;
        
        }
        
        package = p + 2;
        q = package;
        
        if (!aml_read_pkg_length (&q, end, &length)) {
            return;
        }
        
        if (length > (uint32_t) (end - package)) {
            return;
        }
        
        package_end = package + length;
        
        if (!aml_read_name_string (&q, package_end, &device_name) || !aml_resolve_name (scope, &device_name, &device_path)) {
            return;
        }
        
        {
        
            uint8_t *object = q;
            
            int power_button = 0;
            int prw_valid = 0;
            
            uint8_t prw_gpe = 0;
            
            while (object < package_end) {
            
                aml_name_t object_name;
                
                uint8_t *value_ptr;
                uint8_t *next;
                
                if (*object == 0x08) {                      /* NameOp */
                
                    value_ptr = object + 1;
                    
                    if (!aml_read_name_string (&value_ptr, package_end, &object_name)) {
                        break;
                    }
                    
                    {
                    
                        aml_name_t object_path;
                        
                        uint8_t *value = value_ptr;
                        uint64_t integer;
                        
                        if (aml_resolve_name (&device_path, &object_name, &object_path) &&
                                aml_read_integer (&value, package_end, &integer) &&
                                    aml_named_value_count < AML_NAMED_VALUE_MAX) {
                        
                            aml_named_values[aml_named_value_count].name = object_path;
                            aml_named_values[aml_named_value_count].value.type = AML_VALUE_INTEGER;
                            aml_named_values[aml_named_value_count].value.integer = integer;
                            
                            ++aml_named_value_count;
                        
                        }
                    
                    }
                    
                    if (aml_name_last_equal (&object_name, "_HID")) {
                    
                        uint8_t *value = value_ptr;
                        
                        if (aml_hid_is_power_button (&value, package_end)) {
                            power_button = 1;
                        }
                    
                    }
                    
                    if (aml_name_last_equal (&object_name, "_PRW")) {
                    
                        uint8_t gpe;
                        
                        if (aml_read_prw (value_ptr, package_end, &gpe)) {
                        
                            prw_valid = 1;
                            prw_gpe = gpe;
                        
                        }
                    
                    }
                
                }
                
                next = object;
                
                if (!aml_skip_namespace_object (&next, package_end) || next <= object) {
                    break;
                }
                
                object = next;
            
            }
            
            if (power_button && aml_device_count < AML_DEVICE_MAX) {
            
                aml_devices[aml_device_count].name = device_path;
                aml_devices[aml_device_count].power_button = 1;
                aml_devices[aml_device_count].prw_valid = prw_valid;
                aml_devices[aml_device_count].prw_gpe = prw_gpe;
                
                ++aml_device_count;
            
            }
        
        }
        
        /**
         * A Device creates a namespace scope of its own.  Search its
         * contents as well so nested power-button devices are resolved
         * against the Device's absolute path.
         */
        aml_scan_devices (q, package_end, &device_path);
        p = package_end;
    
    }

}

static int aml_add_named_value (const aml_name_t *name, const aml_value_t *value) {

    unsigned i;
    
    for (i = 0; i < aml_named_value_count; ++i) {
    
        if (aml_name_equal (&aml_named_values[i].name, name)) {
        
            aml_named_values[i].value = *value;
            return 1;
        
        }
    
    }
    
    if (aml_named_value_count >= AML_NAMED_VALUE_MAX) {
        return 0;
    }
    
    aml_named_values[aml_named_value_count].name = *name;
    aml_named_values[aml_named_value_count].value = *value;
    
    ++aml_named_value_count;
    return 1;

}

static void aml_scan_named_values (uint8_t *aml, uint8_t *end, const aml_name_t *scope);

static void aml_scan_named_values_internal (uint8_t *aml, uint8_t *end, const aml_name_t *scope, aml_value_t *scratch) {

    uint8_t *p = aml;
    
    while (p < end) {
    
        uint8_t *package_end, *package, *q;
        uint32_t length;
        
        if (*p == 0x08) {               /* NameOp */
        
            aml_name_t name;
            aml_name_t path;
            
            uint64_t integer;
            
            q = p + 1;
            
            if (!aml_read_name_string (&q, end, &name)) {
                return;
            }
            
            if (aml_resolve_name (scope, &name, &path)) {
            
                uint8_t *value = q;
                
                if (aml_read_integer (&value, end, &integer)) {
                
                    unsigned i;
                    
                    for (i = 0; i < aml_named_value_count; ++i) {
                    
                        if (aml_name_equal (&aml_named_values[i].name, &path)) {
                        
                            aml_named_values[i].value.type = AML_VALUE_INTEGER;
                            aml_named_values[i].value.integer = integer;
                            break;
                        
                        }
                    
                    }
                    
                    if (i == aml_named_value_count && aml_named_value_count < AML_NAMED_VALUE_MAX) {
                    
                        aml_named_values[aml_named_value_count].name = path;
                        aml_named_values[aml_named_value_count].value.type = AML_VALUE_INTEGER;
                        aml_named_values[aml_named_value_count].value.integer = integer;
                        
                        ++aml_named_value_count;
                    
                    }
                
                } else if (value < end && *value == 0x11) {                     /* BufferOp */
                
                    uint8_t *buffer_package;
                    uint8_t *buffer_end;
                    uint8_t *buffer_data;
                    
                    uint32_t buffer_package_length;
                    uint64_t buffer_length;
                    
                    aml_value_t *buffer_value = scratch;
                    unsigned i;
                    
                    ++value;
                    buffer_package = value;
                    
                    if (!aml_read_pkg_length (&value, end, &buffer_package_length)) {
                        return;
                    }
                    
                    if (buffer_package_length > (uint32_t) (end - buffer_package)) {
                        return;
                    }
                    
                    buffer_end = buffer_package + buffer_package_length;
                    
                    if (!aml_read_integer (&value, buffer_end, &buffer_length)) {
                        return;
                    }
                    
                    if (buffer_length > UINT32_MAX) {
                        return;
                    }
                    
                    memset (buffer_value, 0, sizeof (*buffer_value));
                    
                    buffer_value->type = AML_VALUE_BUFFER;
                    buffer_value->buffer_length = (uint32_t) buffer_length;
                    
                    if (buffer_value->buffer_length) {
                    
                        uint32_t initial_length;
                        
                        if (!(buffer_value->buffer = kmalloc (buffer_value->buffer_length))) {
                            return;
                        }
                        
                        memset (buffer_value->buffer, 0, buffer_value->buffer_length);
                        buffer_data = value;
                        
                        initial_length = (uint32_t) (buffer_end - buffer_data);
                        
                        if (initial_length > buffer_value->buffer_length) {
                            initial_length = buffer_value->buffer_length;
                        }
                        
                        memcpy (buffer_value->buffer, buffer_data, initial_length);
                    
                    }
                    
                    for (i = 0; i < aml_named_value_count; ++i) {
                    
                        if (aml_name_equal (&aml_named_values[i].name, &path)) {
                        
                            aml_named_values[i].value = *buffer_value;
                            break;
                        
                        }
                    
                    }
                    
                    if (i == aml_named_value_count && aml_named_value_count < AML_NAMED_VALUE_MAX) {
                    
                        aml_named_values[aml_named_value_count].name = path;
                        aml_named_values[aml_named_value_count].value = *buffer_value;
                        
                        ++aml_named_value_count;
                    
                    }
                
                }
            
            }
            
            
            if (!aml_skip_namespace_object (&p, end)) {
                return;
            }
            
            continue;
        
        }
        
        if (*p == 0x10) {               /* ScopeOp */
        
            aml_name_t name;
            aml_name_t new_scope;
            
            package = p + 1;
            q = package;
            
            if (!aml_read_pkg_length (&q, end, &length)) {
                return;
            }
            
            if (length > (uint32_t) (end - package)) {
                return;
            }
            
            package_end = package + length;
            
            if (!aml_read_name_string (&q, package_end, &name)) {
                return;
            }
            
            if (!aml_resolve_name (scope, &name, &new_scope)) {
                return;
            }
            
            aml_scan_named_values (q, package_end, &new_scope);
            
            p = package_end;
            continue;
        
        }
        
        if ((size_t) (end - p) >= 2 && p[0] == 0x5B && (p[1] == 0x82 /* DeviceOp */ || p[1] == 0x83 /* ProcessorOp */ || p[1] == 0x84 /* PowerResOp */ || p[1] == 0x85 /* ThermalZoneOp */)) {
        
            aml_name_t name;
            aml_name_t new_scope;
            
            package = p + 2;
            q = package;
            
            if (!aml_read_pkg_length (&q, end, &length)) {
                return;
            }
            
            if (length > (uint32_t) (end - package)) {
                return;
            }
            
            package_end = package + length;
            
            if (!aml_read_name_string (&q, package_end, &name)) {
                return;
            }
            
            if (!aml_resolve_name (scope, &name, &new_scope)) {
                return;
            }
            
            if (p[1] == 0x83) {         /* ProcessorOp */
            
                if ((size_t) (package_end - q) < 6) {
                    return;
                }
                
                q += 6;
            
            } else if (p[1] == 0x84) {  /* PowerResOp */
            
                if ((size_t) (package_end - q) < 3) {
                    return;
                }
                
                q += 3;
            
            }
            
            aml_scan_named_values (q, package_end, &new_scope);
            
            p = package_end;
            continue;
        
        }
        
        if ((size_t) (end - p) >= 2 && p[0] == 0x5B && p[1] == 0x80) {          /* OpRegionOp */
        
            aml_name_t name;
            aml_name_t path;
            
            aml_value_t *region = scratch;
            q = p + 2;
            
            if (!aml_read_name_string (&q, end, &name)) {
                return;
            }
            
            if (q >= end) {
                return;
            }
            
            memset (region, 0, sizeof (*region));
            
            region->type = AML_VALUE_REGION;
            region->region_space = *q++;
            
            if (!aml_read_integer (&q, end, &region->region_offset)) {
                return;
            }
            
            if (!aml_read_integer (&q, end, &region->region_length)) {
                return;
            }
            
            if (!aml_resolve_name (scope, &name, &path)) {
                return;
            }
            
            if (!aml_add_named_value (&path, region)) {
                return;
            }
            
            p = q;
            continue;
        
        }
        
        if ((size_t) (end - p) >= 2 && p[0] == 0x5B && p[1] == 0x81) {          /* FieldOp */
        
            aml_name_t region_name;
            aml_name_t region_path;
            
            uint64_t bit_index = 0;
            uint8_t *field;
            
            uint8_t field_flags;
            uint8_t field_access;
            
            package = p + 2;
            q = package;
            
            if (!aml_read_pkg_length (&q, end, &length)) {
                return;
            }
            
            if (length > (uint32_t) (end - package)) {
                return;
            }
            
            package_end = package + length;
            
            if (!aml_read_name_string (&q, package_end, &region_name)) {
                return;
            }
            
            if (q >= package_end) {
                return;
            }
            
            field_flags = *q++;
            field_access = field_flags & 0x0F;
            
            if (!aml_resolve_name (scope, &region_name, &region_path)) {
                return;
            }
            
            field = q;
            
            while (field < package_end) {
            
                if (*field == 0x00) {                       /* ReservedField */
                
                    uint32_t bits;
                    
                    ++field;
                    
                    if (!aml_read_pkg_length (&field, package_end, &bits)) {
                        return;
                    }
                    
                    bit_index += bits;
                    continue;
                
                }
                
                if (*field == 0x01) {                       /* AccessField */
                
                    if ((size_t) (package_end - field) < 3) {
                        return;
                    }
                    
                    field_access = field[1] & 0x0F;
                    field += 3;
                    
                    continue;
                
                }
                
                if (*field == 0x02) {                       /* ConnectField */
                
                    ++field;
                    
                    if (field >= package_end) {
                        return;
                    }
                    
                    if (*field == 0x11) {                   /* BufferData */
                    
                        if (!aml_skip_package (&field, package_end, 1)) {
                            return;
                        }
                    
                    } else {
                    
                        aml_name_t connection;
                        
                        if (!aml_read_name_string (&field, package_end, &connection)) {
                            return;
                        }
                    
                    }
                    
                    continue;
                
                }
                
                if (*field == 0x03) {                       /* ExtendedAccessField */
                
                    if ((size_t) (package_end - field) < 4) {
                        return;
                    }
                    
                    field_access = field[1] & 0x0F;
                    field += 4;
                    
                    continue;
                
                }
                
                {
                
                    aml_name_t field_name, field_path;
                    
                    uint32_t bit_length;
                    unsigned i;
                    
                    aml_value_t *field_value = scratch;
                    
                    if ((size_t) (package_end - field) < 4) {
                        return;
                    }
                    
                    memset (&field_name, 0, sizeof (field_name));
                    
                    memcpy (field_name.segment[0], field, 4);
                    field_name.segments = 1;
                    
                    field += 4;
                    
                    if (!aml_read_pkg_length (&field, package_end, &bit_length)) {
                        return;
                    }
                    
                    if (bit_index > UINT32_MAX || bit_length > UINT32_MAX || bit_index + bit_length > UINT32_MAX) {
                        return;
                    }
                    
                    if (!aml_resolve_name (scope, &field_name, &field_path)) {
                        return;
                    }
                    
                    memset (field_value, 0, sizeof (*field_value));
                    
                    field_value->type = AML_VALUE_FIELD;
                    field_value->reference = region_path;
                    
                    field_value->bit_index = (uint32_t) bit_index;
                    field_value->bit_length = bit_length;
                    
                    field_value->field_flags = field_flags;
                    field_value->field_access = field_access;
                    
                    for (i = 0; i < aml_named_value_count; ++i) {
                    
                        if (aml_name_equal (&aml_named_values[i].name, &field_path)) {
                        
                            aml_named_values[i].value = *field_value;
                            break;
                        
                        }
                    
                    }
                    
                    if (i == aml_named_value_count && aml_named_value_count < AML_NAMED_VALUE_MAX) {
                    
                        aml_named_values[aml_named_value_count].name = field_path;
                        aml_named_values[aml_named_value_count].value = *field_value;
                        
                        ++aml_named_value_count;
                    
                    }
                    
                    bit_index += bit_length;
                
                }
            
            }
            
            p = package_end;
            continue;
        
        }
        
        if ((size_t) (end - p) >= 2 && p[0] == 0x5B && p[1] == 0x86) {          /* IndexFieldOp */
        
            aml_name_t index_name;
            aml_name_t data_name;
            
            aml_name_t index_path;
            aml_name_t data_path;
            
            uint64_t bit_index = 0;
            uint8_t *field;
            
            uint8_t field_flags;
            uint8_t field_access;
            
            package = p + 2;
            q = package;
            
            if (!aml_read_pkg_length (&q, end, &length)) {
                return;
            }
            
            if (length > (uint32_t) (end - package)) {
                return;
            }
            
            package_end = package + length;
            
            if (!aml_read_name_string (&q, package_end, &index_name)) {
                return;
            }
            
            if (!aml_read_name_string (&q, package_end, &data_name)) {
                return;
            }
            
            if (q >= package_end) {
                return;
            }
            
            field_flags = *q++;
            field_access = field_flags & 0x0F;
            
            if (!aml_resolve_name (scope, &index_name, &index_path)) {
                return;
            }
            
            if (!aml_resolve_name (scope, &data_name, &data_path)) {
                return;
            }
            
            field = q;
            
            while (field < package_end) {
            
                if (*field == 0x00) {                       /* ReservedField */
                
                    uint32_t bits;
                    
                    ++field;
                    
                    if (!aml_read_pkg_length (&field, package_end, &bits)) {
                        return;
                    }
                    
                    bit_index += bits;
                    continue;
                
                }
                
                if (*field == 0x01) {                       /* AccessField */
                
                    if ((size_t) (package_end - field) < 3) {
                        return;
                    }
                    
                    field_access = field[1] & 0x0F;
                    field += 3;
                    
                    continue;
                
                }
                
                if (*field == 0x02) {                       /* ConnectField */
                
                    ++field;
                    
                    if (field >= package_end) {
                        return;
                    }
                    
                    if (*field == 0x11) {                   /* BufferData */
                    
                        if (!aml_skip_package (&field, package_end, 1)) {
                            return;
                        }
                    
                    } else {
                    
                        aml_name_t connection;
                        
                        if (!aml_read_name_string (&field, package_end, &connection)) {
                            return;
                        }
                    
                    }
                    
                    continue;
                
                }
                
                if (*field == 0x03) {                       /* ExtendedAccessField */
                
                    if ((size_t) (package_end - field) < 4) {
                        return;
                    }
                    
                    field_access = field[1] & 0x0F;
                    field += 4;
                    
                    continue;
                
                }
                
                {
                
                    aml_name_t field_name, field_path;
                    
                    uint32_t bit_length;
                    unsigned i;
                    
                    aml_value_t *field_value = scratch;
                    
                    if ((size_t) (package_end - field) < 4) {
                        return;
                    }
                    
                    memset (&field_name, 0, sizeof (field_name));
                    
                    memcpy (field_name.segment[0], field, 4);
                    field_name.segments = 1;
                    
                    field += 4;
                    
                    if (!aml_read_pkg_length (&field, package_end, &bit_length)) {
                        return;
                    }
                    
                    if (bit_index > UINT32_MAX || bit_length > UINT32_MAX || bit_index + bit_length > UINT32_MAX) {
                        return;
                    }
                    
                    if (!aml_resolve_name (scope, &field_name, &field_path)) {
                        return;
                    }
                    
                    memset (field_value, 0, sizeof (*field_value));
                    
                    field_value->type = AML_VALUE_INDEX_FIELD;
                    field_value->reference = data_path;
                    field_value->index_reference = index_path;
                    
                    field_value->bit_index = (uint32_t) bit_index;
                    field_value->bit_length = bit_length;
                    
                    field_value->field_flags = field_flags;
                    field_value->field_access = field_access;
                    
                    for (i = 0; i < aml_named_value_count; ++i) {
                    
                        if (aml_name_equal (&aml_named_values[i].name, &field_path)) {
                        
                            aml_named_values[i].value = *field_value;
                            break;
                        
                        }
                    
                    }
                    
                    if (i == aml_named_value_count && aml_named_value_count < AML_NAMED_VALUE_MAX) {
                    
                        aml_named_values[aml_named_value_count].name = field_path;
                        aml_named_values[aml_named_value_count].value = *field_value;
                        
                        ++aml_named_value_count;
                    
                    }
                    
                    bit_index += bit_length;
                
                }
            
            }
            
            p = package_end;
            continue;
        
        }
        
        if ((size_t) (end - p) >= 2 && p[0] == 0x5B && p[1] == 0x87) {          /* BankFieldOp */
        
            aml_name_t region_name;
            aml_name_t bank_name;
            
            aml_name_t region_path;
            aml_name_t bank_path;
            
            uint64_t bank_value;
            uint64_t bit_index = 0;
            uint8_t *field;
            
            uint8_t field_flags;
            uint8_t field_access;
            
            package = p + 2;
            q = package;
            
            if (!aml_read_pkg_length (&q, end, &length)) {
                return;
            }
            
            if (length > (uint32_t) (end - package)) {
                return;
            }
            
            package_end = package + length;
            
            if (!aml_read_name_string (&q, package_end, &region_name)) {
                return;
            }
            
            if (!aml_read_name_string (&q, package_end, &bank_name)) {
                return;
            }
            
            if (!aml_read_integer (&q, package_end, &bank_value)) {
                return;
            }
            
            if (q >= package_end) {
                return;
            }
            
            field_flags = *q++;
            field_access = field_flags & 0x0F;
            
            if (!aml_resolve_name (scope, &region_name, &region_path)) {
                return;
            }
            
            if (!aml_resolve_name (scope, &bank_name, &bank_path)) {
                return;
            }
            
            field = q;
            
            while (field < package_end) {
            
                if (*field == 0x00) {                       /* ReservedField */
                
                    uint32_t bits;
                    
                    ++field;
                    
                    if (!aml_read_pkg_length (&field, package_end, &bits)) {
                        return;
                    }
                    
                    bit_index += bits;
                    continue;
                
                }
                
                if (*field == 0x01) {                       /* AccessField */
                
                    if ((size_t) (package_end - field) < 3) {
                        return;
                    }
                    
                    field_access = field[1] & 0x0F;
                    field += 3;
                    
                    continue;
                
                }
                
                if (*field == 0x02) {                       /* ConnectField */
                
                    ++field;
                    
                    if (field >= package_end) {
                        return;
                    }
                    
                    if (*field == 0x11) {                   /* BufferData */
                    
                        if (!aml_skip_package (&field, package_end, 1)) {
                            return;
                        }
                    
                    } else {
                    
                        aml_name_t connection;
                        
                        if (!aml_read_name_string (&field, package_end, &connection)) {
                            return;
                        }
                    
                    }
                    
                    continue;
                
                }
                
                if (*field == 0x03) {                       /* ExtendedAccessField */
                
                    if ((size_t) (package_end - field) < 4) {
                        return;
                    }
                    
                    field_access = field[1] & 0x0F;
                    field += 4;
                    
                    continue;
                
                }
                
                {
                
                    aml_name_t field_name, field_path;
                    
                    uint32_t bit_length;
                    unsigned i;
                    
                    aml_value_t *field_value = scratch;
                    
                    if ((size_t) (package_end - field) < 4) {
                        return;
                    }
                    
                    memset (&field_name, 0, sizeof (field_name));
                    
                    memcpy (field_name.segment[0], field, 4);
                    field_name.segments = 1;
                    
                    field += 4;
                    
                    if (!aml_read_pkg_length (&field, package_end, &bit_length)) {
                        return;
                    }
                    
                    if (bit_index > UINT32_MAX || bit_length > UINT32_MAX || bit_index + bit_length > UINT32_MAX) {
                        return;
                    }
                    
                    if (!aml_resolve_name (scope, &field_name, &field_path)) {
                        return;
                    }
                    
                    memset (field_value, 0, sizeof (*field_value));
                    
                    field_value->type = AML_VALUE_BANK_FIELD;
                    field_value->reference = region_path;
                    field_value->bank_reference = bank_path;
                    field_value->bank_value = bank_value;
                    
                    field_value->bit_index = (uint32_t) bit_index;
                    field_value->bit_length = bit_length;
                    
                    field_value->field_flags = field_flags;
                    field_value->field_access = field_access;
                    
                    for (i = 0; i < aml_named_value_count; ++i) {
                    
                        if (aml_name_equal (&aml_named_values[i].name, &field_path)) {
                        
                            aml_named_values[i].value = *field_value;
                            break;
                        
                        }
                    
                    }
                    
                    if (i == aml_named_value_count && aml_named_value_count < AML_NAMED_VALUE_MAX) {
                    
                        aml_named_values[aml_named_value_count].name = field_path;
                        aml_named_values[aml_named_value_count].value = *field_value;
                        
                        ++aml_named_value_count;
                    
                    }
                    
                    bit_index += bit_length;
                
                }
            
            }
            
            p = package_end;
            continue;
        
        }
        
        if (*p >= 0x8B && *p <= 0x8F) {                     /* CreateBitFieldOp - CreateQWordFieldOp */
        
            uint8_t opcode = *p;
            
            aml_name_t source_name;
            aml_name_t source_path;
            aml_name_t field_name;
            aml_name_t field_path;
            
            unsigned i;
            uint64_t index;
            
            uint64_t bit_index;
            uint32_t bit_length;
            
            aml_value_t *field = scratch;
            q = p + 1;
            
            if (!aml_read_name_string (&q, end, &source_name)) {
                return;
            }
            
            if (!aml_read_integer (&q, end, &index)) {
                return;
            }
            
            if (!aml_read_name_string (&q, end, &field_name)) {
                return;
            }
            
            switch (opcode) {
            
                case 0x8B:              /* CreateWordFieldOp */
                
                    if (index > UINT32_MAX / 8) {
                        return;
                    }
                    
                    bit_index = index * 8;
                    bit_length = 16;
                    
                    break;
                
                case 0x8C:              /* CreateByteFieldOp */
                
                    if (index > UINT32_MAX / 8) {
                        return;
                    }
                    
                    bit_index = index * 8;
                    bit_length = 8;
                    
                    break;
                
                case 0x8D:              /* CreateBitFieldOp */
                
                    bit_index = index;
                    bit_length = 1;
                    
                    break;
                
                case 0x8E:              /* CreateDWordFieldOp */
                
                    if (index > UINT32_MAX / 8) {
                        return;
                    }
                    
                    bit_index = index * 8;
                    bit_length = 32;
                    
                    break;
                
                case 0x8F:              /* CreateQWordFieldOp */
                
                    if (index > UINT32_MAX / 8) {
                        return;
                    }
                    
                    bit_index = index * 8;
                    bit_length = 64;
                    
                    break;
                
                default:
                
                    return;
            
            }
            
            if (bit_index > UINT32_MAX) {
                return;
            }
            
            if (!aml_resolve_name (scope, &source_name, &source_path)) {
                return;
            }
            
            if (!aml_resolve_name (scope, &field_name, &field_path)) {
                return;
            }
            
            memset (field, 0, sizeof (*field));
            
            field->type = AML_VALUE_BUFFER_FIELD;
            field->reference = source_path;
            
            field->bit_index = (uint32_t) bit_index;
            field->bit_length = bit_length;
            
            for (i = 0; i < aml_named_value_count; ++i) {
            
                if (aml_name_equal (&aml_named_values[i].name, &field_path)) {
                
                    aml_named_values[i].value = *field;
                    break;
                
                }
            
            }
            
            if (i == aml_named_value_count && aml_named_value_count < AML_NAMED_VALUE_MAX) {
            
                aml_named_values[aml_named_value_count].name = field_path;
                aml_named_values[aml_named_value_count].value = *field;
                
                ++aml_named_value_count;
            
            }
            
            p = q;
            continue;
        
        }
        
        {
        
            uint8_t *next = p;
            
            if (!aml_skip_namespace_object (&next, end) || next <= p) {
                return;
            }
            
            p = next;
        
        }
    
    }

}

static void aml_scan_named_values (uint8_t *aml, uint8_t *end, const aml_name_t *scope) {

    aml_value_t *scratch;
    
    if (!(scratch = kmalloc (sizeof (*scratch)))) {
        return;
    }
    
    aml_scan_named_values_internal (aml, end, scope, scratch);
    kfree (scratch);

}

#define     AML_METHOD_MAX              1024

typedef struct {

    aml_name_t name;
    aml_name_t scope;
    
    uint8_t *method;

} aml_method_entry_t;

static aml_method_entry_t aml_methods[AML_METHOD_MAX];
static unsigned aml_method_count = 0;

static void aml_scan_methods (uint8_t *aml, uint8_t *end, const aml_name_t *scope) {

    uint8_t *p = aml;
    
    while (p < end) {
    
        uint8_t *package;
        uint8_t *package_end;
        uint8_t *q;
        
        uint32_t length;
        
        if (*p == 0x06) {               /* AliasOp */
        
            aml_name_t source_name;
            aml_name_t alias_name;
            
            q = p + 1;
            
            if (!aml_read_name_string (&q, end, &source_name) || !aml_read_name_string (&q, end, &alias_name)) {
                return;
            }
            
            if (aml_alias_count < AML_ALIAS_MAX && aml_resolve_name (scope, &source_name, &aml_aliases[aml_alias_count].source) && aml_resolve_name (scope, &alias_name, &aml_aliases[aml_alias_count].name)) {
                ++aml_alias_count;
            }
            
            p = q;
            continue;
        
        }
        
        if (*p == 0x14) {               /* MethodOp */
        
            aml_name_t name;
            aml_name_t path;
            
            package = p + 1;
            q = package;
            
            if (!aml_read_pkg_length (&q, end, &length)) {
                return;
            }
            
            if (length > (uint32_t) (end - package)) {
                return;
            }
            
            package_end = package + length;
            
            if (!aml_read_name_string (&q, package_end, &name)) {
                return;
            }
            
            if (aml_resolve_name (scope, &name, &path) && aml_method_count < AML_METHOD_MAX) {
            
                aml_methods[aml_method_count].name = path;
                aml_methods[aml_method_count].scope = path;
                
                if (aml_methods[aml_method_count].scope.segments) {
                    aml_methods[aml_method_count].scope.segments--;
                }
                
                aml_methods[aml_method_count].method = p;
                ++aml_method_count;
            
            }
            
            p = package_end;
            continue;
        
        }
        
        if (*p == 0x10) {               /* ScopeOp */
        
            aml_name_t name;
            aml_name_t new_scope;
            
            package = p + 1;
            q = package;
            
            if (!aml_read_pkg_length (&q, end, &length)) {
                return;
            }
            
            if (length > (uint32_t) (end - package)) {
                return;
            }
            
            package_end = package + length;
            
            if (!aml_read_name_string (&q, package_end, &name)) {
                return;
            }
            
            if (!aml_resolve_name (scope, &name, &new_scope)) {
                return;
            }
            
            aml_scan_methods (q, package_end, &new_scope);
            
            p = package_end;
            continue;
        
        }
        
        if ((size_t) (end - p) >= 2 && p[0] == 0x5B && (p[1] == 0x82 || p[1] == 0x83 || p[1] == 0x84 || p[1] == 0x85)) {
        
            aml_name_t name;
            aml_name_t new_scope;
            
            package = p + 2;
            q = package;
            
            if (!aml_read_pkg_length (&q, end, &length)) {
                return;
            }
            
            if (length > (uint32_t) (end - package)) {
                return;
            }
            
            package_end = package + length;
            
            if (!aml_read_name_string (&q, package_end, &name)) {
                return;
            }
            
            if (!aml_resolve_name (scope, &name, &new_scope)) {
                return;
            }
            
            if (p[1] == 0x83) {                             /* ProcessorOp */
            
                if ((size_t) (package_end - q) < 6) {
                    return;
                }
                
                q += 6;
            
            } else if (p[1] == 0x84) {                      /* PowerResOp */
            
                if ((size_t) (package_end - q) < 3) {
                    return;
                }
                
                q += 3;
            
            }
            
            aml_scan_methods (q, package_end, &new_scope);
            
            p = package_end;
            continue;
        
        }
        
        {
        
            uint8_t *next = p;
            
            if (aml_skip_namespace_object (&next, end) && next > p) {
            
                p = next;
                continue;
            
            }
            
            ++p;
        
        }
    
    }

}

static uint8_t *aml_find_method (uint8_t *aml, uint8_t *end, const aml_name_t *scope, const aml_name_t *target);
static int aml_find_method_scope (uint8_t *target, uint8_t *aml, uint8_t *end, const aml_name_t *scope, aml_name_t *result);

static int aml_find_indexed_method (const aml_name_t *scope, const aml_name_t *name, uint8_t **method, aml_name_t *method_scope) {

    aml_name_t search_scope;
    aml_name_t target;
    
    unsigned table;
    unsigned i;
    
    search_scope = *scope;
    
    for (;;) {
    
        if (!aml_resolve_name (&search_scope, name, &target)) {
            return 0;
        }
        
        for (i = 0; i < aml_method_count; ++i) {
        
            if (aml_name_equal (&aml_methods[i].name, &target)) {
            
                *method = aml_methods[i].method;
                *method_scope = aml_methods[i].scope;
                
                return 1;
            
            }
        
        }
        
        for (table = 0; table < acpi_aml_table_count; ++table) {
        
            uint8_t *aml = acpi_aml_tables[table].aml;
            uint8_t *end = aml + acpi_aml_tables[table].length;
            
            aml_name_t root;
            uint8_t *found;
            
            memset (&root, 0, sizeof (root));
            root.root = 1;
            
            found = aml_find_method (aml, end, &root, &target);
            
            if (found) {
            
                if (!aml_find_method_scope (found, aml, end, &root, method_scope)) {
                    return 0;
                }
                
                *method = found;
                return 1;
            
            }
        
        }
        
        if (name->root || name->parents || !search_scope.segments) {
            break;
        }
        
        search_scope.segments--;
    
    }
    
    return 0;

}

static void aml_scan_namespace (void) {

    aml_name_t root;
    unsigned table;
    
    memset (&root, 0, sizeof (root));
    root.root = 1;
    
    aml_device_count = 0;
    aml_method_count = 0;
    
    aml_named_value_count = 0;
    aml_alias_count = 0;
    
    for (table = 0; table < acpi_aml_table_count; ++table) {
    
        uint8_t *aml = acpi_aml_tables[table].aml;
        uint8_t *end = aml + acpi_aml_tables[table].length;
        
        aml_scan_methods (aml, end, &root);
    
    }
    
    for (table = 0; table < acpi_aml_table_count; ++table) {
    
        uint8_t *aml = acpi_aml_tables[table].aml;
        uint8_t *end = aml + acpi_aml_tables[table].length;
        
        aml_scan_named_values (aml, end, &root);
    
    }
    
    for (table = 0; table < acpi_aml_table_count; ++table) {
    
        uint8_t *aml = acpi_aml_tables[table].aml;
        uint8_t *end = aml + acpi_aml_tables[table].length;
        
        aml_scan_devices (aml, end, &root);
    
    }

}

typedef struct {

    uint8_t *body;
    uint8_t *end;
    
    uint8_t flags;
    aml_name_t name;

} aml_method_t;

static int aml_read_method (uint8_t *method, uint8_t *end, aml_method_t *result) {

    uint8_t *package;
    uint8_t *p;
    uint8_t *package_end;
    
    uint32_t length;
    
    if (!method || !result || method >= end || *method != 0x14) {
        return 0;
    }
    
    package = method + 1;
    p = package;
    
    if (!aml_read_pkg_length (&p, end, &length)) {
        return 0;
    }
    
    if (length > (uint32_t) (end - package)) {
        return 0;
    }
    
    package_end = package + length;
    
    if (!aml_read_name_string (&p, package_end, &result->name)) {
        return 0;
    }
    
    if (p >= package_end) {
        return 0;
    }
    
    result->flags = *p++;
    result->body = p;
    result->end = package_end;
    
    return 1;

}

static int aml_find_method_scope (uint8_t *target, uint8_t *aml, uint8_t *end, const aml_name_t *scope, aml_name_t *result) {

    uint8_t *p = aml;
    
    while (p < end) {
    
        uint8_t *package;
        uint8_t *package_end;
        uint8_t *q;
        
        uint32_t length;
        
        if (*p == 0x14) {               /* MethodOp */
        
            aml_name_t name;
            aml_name_t path;
            
            package = p + 1;
            q = package;
            
            if (!aml_read_pkg_length (&q, end, &length) || length > (uint32_t) (end - package)) {
                return 0;
            }
            
            package_end = package + length;
            
            if (!aml_read_name_string (&q, package_end, &name)) {
                return 0;
            }
            
            if (p == target) {
            
                if (!aml_resolve_name (scope, &name, &path) || !path.segments) {
                    return 0;
                }
                
                *result = path;
                --result->segments;
                
                return 1;
            
            }
            
            p = package_end;
            continue;
        
        }
        
        if (*p == 0x10) {               /* ScopeOp */
        
            aml_name_t name;
            aml_name_t new_scope;
            
            package = p + 1;
            q = package;
            
            if (!aml_read_pkg_length (&q, end, &length) || length > (uint32_t) (end - package)) {
                return 0;
            }
            
            package_end = package + length;
            
            if (!aml_read_name_string (&q, package_end, &name) || !aml_resolve_name (scope, &name, &new_scope)) {
                return 0;
            }
            
            if (aml_find_method_scope (target, q, package_end, &new_scope, result)) {
                return 1;
            }
            
            p = package_end;
            continue;
        
        }
        
        if (p + 2 <= end && p[0] == 0x5B && (p[1] == 0x82 /* DeviceOp */ || p[1] == 0x83 /* ProcessorOp */ || p[1] == 0x84 /* PowerResOp */ || p[1] == 0x85 /* ThermalZoneOp */)) {
        
            aml_name_t name;
            aml_name_t new_scope;
            
            package = p + 2;
            q = package;
            
            if (!aml_read_pkg_length (&q, end, &length) || length > (uint32_t) (end - package)) {
                return 0;
            }
            
            package_end = package + length;
            
            if (!aml_read_name_string (&q, package_end, &name) || !aml_resolve_name (scope, &name, &new_scope)) {
                return 0;
            }
            
            if (p[1] == 0x83) {         /* ProcessorOp */
            
                if ((size_t) (package_end - q) < 6) {
                    return 0;
                }
                
                q += 6;
            
            } else if (p[1] == 0x84) {  /* PowerResOp */
            
                if ((size_t) (package_end - q) < 3) {
                    return 0;
                }
                
                q += 3;
            }
            
            if (aml_find_method_scope (target, q, package_end, &new_scope, result)) {
                return 1;
            }
            
            p = package_end;
            continue;
        
        }
        
        {
        
            uint8_t *next = p;
            
            if (!aml_skip_namespace_object (&next, end) || next <= p) {
                return 0;
            }
            
            p = next;
        
        }
    
    }
    
    return 0;

}

static int aml_find_alias (uint8_t *aml, uint8_t *end, const aml_name_t *scope, const aml_name_t *target, aml_name_t *source) {

    uint8_t *p = aml;
    
    while (p < end) {
    
        uint8_t *package;
        uint8_t *package_end;
        uint8_t *q;
        
        uint32_t length;
        
        if (*p == 0x06) {               /* AliasOp */
        
            aml_name_t source_name;
            aml_name_t alias_name;
            aml_name_t source_path;
            aml_name_t alias_path;
            
            q = p + 1;
            
            if (!aml_read_name_string (&q, end, &source_name) || !aml_read_name_string (&q, end, &alias_name)) {
                return 0;
            }
            
            if (!aml_resolve_name (scope, &source_name, &source_path) || !aml_resolve_name (scope, &alias_name, &alias_path)) {
                return 0;
            }
            
            if (aml_name_equal (&alias_path, target)) {
            
                *source = source_path;
                return 1;
            
            }
            
            p = q;
            continue;
        
        }
        
        if (*p == 0x10) {               /* ScopeOp */
        
            aml_name_t name;
            aml_name_t new_scope;
            
            package = p + 1;
            q = package;
            
            if (!aml_read_pkg_length (&q, end, &length) || length > (uint32_t) (end - package)) {
                return 0;
            }
            
            package_end = package + length;
            
            if (!aml_read_name_string (&q, package_end, &name) || !aml_resolve_name (scope, &name, &new_scope)) {
                return 0;
            }
            
            if (aml_find_alias (q, package_end, &new_scope, target, source)) {
                return 1;
            }
            
            p = package_end;
            continue;
        
        }
        
        if ((size_t) (end - p) >= 2 && p[0] == 0x5B && (p[1] == 0x82 || p[1] == 0x83 || p[1] == 0x84 || p[1] == 0x85)) {
        
            aml_name_t name;
            aml_name_t new_scope;
            
            package = p + 2;
            q = package;
            
            if (!aml_read_pkg_length (&q, end, &length) || length > (uint32_t) (end - package)) {
                return 0;
            }
            
            package_end = package + length;
            
            if (!aml_read_name_string (&q, package_end, &name) || !aml_resolve_name (scope, &name, &new_scope)) {
                return 0;
            }
            
            if (p[1] == 0x83) {         /* ProcessorOp */
            
                if ((size_t) (package_end - q) < 6) {
                    return 0;
                }
                
                q += 6;
            
            } else if (p[1] == 0x84) {  /* PowerResOp */
            
                if ((size_t) (package_end - q) < 3) {
                    return 0;
                }
                
                q += 3;
            
            }
            
            if (aml_find_alias (q, package_end, &new_scope, target, source)) {
                return 1;
            }
            
            p = package_end;
            continue;
        
        }
        
        {
        
            uint8_t *next = p;
            
            if (!aml_skip_namespace_object (&next, end) || next <= p) {
                return 0;
            }
            
            p = next;
        
        }
    
    }
    
    return 0;

}

static uint8_t *aml_find_method (uint8_t *aml, uint8_t *end, const aml_name_t *scope, const aml_name_t *target) {

    uint8_t *p = aml;
    
    while (p < end) {
    
        uint8_t *package;
        uint8_t *package_end;
        uint8_t *q;
        
        uint32_t length;
        
        if (*p == 0x14) {               /* MethodOp */
        
            aml_name_t name;
            aml_name_t path;
            
            package = p + 1;
            q = package;
            
            if (!aml_read_pkg_length (&q, end, &length) || length > (uint32_t) (end - package)) {
                return 0;
            }
            
            package_end = package + length;
            
            if (!aml_read_name_string (&q, package_end, &name)) {
                return 0;
            }
            
            if (!aml_resolve_name (scope, &name, &path)) {
                return 0;
            }
            
            if (aml_name_equal (&path, target)) {
                return p;
            }
            
            p = package_end;
            continue;
        }
        
        if (*p == 0x10) {               /* ScopeOp */
        
            aml_name_t name;
            aml_name_t new_scope;
            
            uint8_t *method;
            
            package = p + 1;
            q = package;
            
            if (!aml_read_pkg_length (&q, end, &length) || length > (uint32_t) (end - package)) {
                return 0;
            }
            
            package_end = package + length;
            
            if (!aml_read_name_string (&q, package_end, &name) || !aml_resolve_name (scope, &name, &new_scope)) {
                return 0;
            }
            
            method = aml_find_method (q, package_end, &new_scope, target);
            
            if (method) {
                return method;
            }
            
            p = package_end;
            continue;
        }
        
        if (p + 2 <= end && p[0] == 0x5B && (p[1] == 0x82 /* DeviceOp */ || p[1] == 0x83 /* ProcessorOp */ || p[1] == 0x84 /* PowerResOp */ || p[1] == 0x85 /* ThermalZoneOp */)) {
        
            aml_name_t name;
            aml_name_t new_scope;
            
            uint8_t *method;
            
            package = p + 2;
            q = package;
            
            if (!aml_read_pkg_length (&q, end, &length) || length > (uint32_t) (end - package)) {
                return 0;
            }
            
            package_end = package + length;
            
            if (!aml_read_name_string (&q, package_end, &name) || !aml_resolve_name (scope, &name, &new_scope)) {
                return 0;
            }
            
            if (p[1] == 0x83) {         /* ProcessorOp */
            
                if ((size_t) (package_end - q) < 6) {
                    return 0;
                }
                
                q += 6;
            
            } else if (p[1] == 0x84) {  /* PowerResOp */
            
                if ((size_t) (package_end - q) < 3) {
                    return 0;
                }
                
                q += 3;
            }
            
            method = aml_find_method (q, package_end, &new_scope, target);
            
            if (method) {
                return method;
            }
            
            p = package_end;
            continue;
        }
        
        {
        
            uint8_t *next = p;
            
            if (!aml_skip_namespace_object (&next, end) || next <= p) {
                return 0;
            }
            
            p = next;
        }
    
    }
    
    return 0;

}

/*static uint8_t *acpi_find_method (const aml_name_t *scope, const aml_name_t *name) {

    aml_name_t search_scope;
    aml_name_t target;
    
    unsigned table;
    search_scope = *scope;
    
    for (;;) {
    
        if (!aml_resolve_name (&search_scope, name, &target)) {
            return 0;
        }
        
        for (table = 0; table < acpi_aml_table_count; ++table) {
        
            uint8_t *aml = acpi_aml_tables[table].aml;
            uint8_t *end = aml + acpi_aml_tables[table].length;
            
            aml_name_t root;
            uint8_t *method;
            
            memset (&root, 0, sizeof (root));
            root.root = 1;
            
            method = aml_find_method (aml, end, &root, &target);
            
            if (method) {
                return method;
            }
        
        }
        
        {
        
            aml_name_t source;
            int alias = 0;
            
            for (table = 0; table < acpi_aml_table_count; ++table) {
            
                uint8_t *aml = acpi_aml_tables[table].aml;
                uint8_t *end = aml + acpi_aml_tables[table].length;
                
                aml_name_t root;
                
                memset (&root, 0, sizeof (root));
                root.root = 1;
                
                if (aml_find_alias (aml, end, &root, &target, &source)) {
                
                    alias = 1;
                    break;
                
                }
            
            }
            
            if (alias) {
            
                for (table = 0; table < acpi_aml_table_count; ++table) {
                
                    uint8_t *aml = acpi_aml_tables[table].aml;
                    uint8_t *end = aml + acpi_aml_tables[table].length;
                    
                    aml_name_t root;
                    uint8_t *method;
                    
                    memset (&root, 0, sizeof (root));
                    root.root = 1;
                    
                    method = aml_find_method (aml, end, &root, &source);
                    
                    if (method) {
                        return method;
                    }
                
                }
            
            }
        
        }
        
        if (name->root || name->parents || !search_scope.segments) {
            break;
        }
        
        search_scope.segments--;
    
    }
    
    return 0;

}*/

static int aml_execute_term_list (uint8_t **ptr, uint8_t *end, const aml_name_t *scope, aml_execution_context_t *context, int *returned, aml_value_t *return_value);

typedef struct {

    aml_name_t method_scope;
    aml_name_t name;
    aml_method_t object;

} aml_invoke_scratch_t;

static int aml_invoke_method (uint8_t **ptr, uint8_t *end, const aml_name_t *scope, aml_execution_context_t *context, aml_value_t *value) {

    uint8_t *p = *ptr, *body;
    uint8_t *method;
    
    aml_invoke_scratch_t *scratch;
    unsigned table;
    
    scratch = kmalloc (sizeof (*scratch));
    
    if (!scratch) {
        return 0;
    }
    
    if (!aml_read_name_string (&p, end, &scratch->name)) {
    
        kfree (scratch);
        return 0;
    
    }
    
    if (!aml_find_indexed_method (scope, &scratch->name, &method, &scratch->method_scope)) {
    
        kfree (scratch);
        return 0;
    
    }
    
    for (table = 0; table < acpi_aml_table_count; ++table) {
    
        uint8_t *aml = acpi_aml_tables[table].aml;
        uint8_t *aml_end = aml + acpi_aml_tables[table].length;
        
        unsigned argument;
        unsigned arguments;
        
        aml_execution_context_t *method_context;
        int returned = 0;
        
        if (method < aml || method >= aml_end) {
            continue;
        }
        
        if (!aml_read_method (method, aml_end, &scratch->object)) {
        
            kfree (scratch);
            return 0;
        
        }
        
        arguments = scratch->object.flags & 0x07;
        
        if (!(method_context = kmalloc (sizeof (*method_context)))) {
        
            kfree (scratch);
            return 0;
        
        }
        
        memset (method_context, 0, sizeof (*method_context));
        
        for (argument = 0; argument < arguments; ++argument) {
        
            if (!aml_read_value (&p, end, scope, context, &method_context->arg[argument])) {
            
                kfree (method_context);
                kfree (scratch);
                
                return 0;
            
            }
        
        }
        
        body = scratch->object.body;
        
        if (!aml_execute_term_list (&body, scratch->object.end, &scratch->method_scope, method_context, &returned, value)) {
        
            kfree (method_context);
            kfree (scratch);
            
            return 0;
        
        }
        
        if (!returned) {
        
            memset (value, 0, sizeof (*value));
            
            value->type = AML_VALUE_INTEGER;
            value->integer = 0;
        
        }
        
        kfree (method_context);
        *ptr = p;
        
        kfree (scratch);
        return 1;
    
    }
    
    kfree (scratch);
    return 0;

}

static int aml_execute_term_list (uint8_t **ptr, uint8_t *end, const aml_name_t *scope, aml_execution_context_t *context, int *returned, aml_value_t *return_value) {

    uint8_t *p = *ptr;
    
    while (p < end) {
    
        *ptr = p;
        
        switch (*p) {
        
            case 0x5B: {                /* ExtOpPrefix */
            
                aml_name_t mutex;
                
                if ((size_t) (end - p) < 2) {
                    return 0;
                }
                
                if (p[1] == 0x27) {     /* ReleaseOp */
                
                    p += 2;
                    
                    if (!aml_read_name_string (&p, end, &mutex)) {
                        return 0;
                    }
                    
                    break;
                
                }
                
                if (p[1] != 0x23) {     /* AcquireOp */
                    return 0;
                }
                
                p += 2;
                
                if (!aml_read_name_string (&p, end, &mutex)) {
                    return 0;
                }
                
                if ((size_t) (end - p) < 2) {
                    return 0;
                }
                
                /**
                 * AML execution is currently synchronous, so there can be
                 * no competing AML execution holding this mutex.
                 *
                 * Consume the Timeout operand and treat the acquisition as
                 * successful.
                 */
                p += 2;
                break;
            
            }
            
            case 0x70: {                /* StoreOp */
            
                aml_value_t value;
                aml_super_name_t target;
                
                ++p;
                
                if (!aml_read_value (&p, end, scope, context, &value)) {
                    return 0;
                }
                
                if (!aml_read_super_name (&p, end, &target)) {
                    return 0;
                }
                
                if (!aml_store_value (&target, scope, context, &value)) {
                    return 0;
                }
                
                break;
            
            }
            
            case 0x75:  /* IncrementOp */   case 0x76: {    /* DecrementOp */
            
                aml_value_t *value;
                uint8_t *q = p;
                
                value = kmalloc (sizeof (*value));
                
                if (!value) {
                    return 0;
                }
                
                if (!aml_read_value (&q, end, scope, context, value)) {
                
                    kfree (value);
                    return 0;
                
                }
                
                kfree (value);
                
                p = q;
                break;
            
            }
            
            case 0x8B:  /* CreateWordFieldOp */     case 0x8C:  /* CreateByteFieldOp */     case 0x8D:  /* CreateBitFieldOp */      case 0x8E:  /* CreateDWordFieldOp */    case 0x8F: {    /* CreateQWordFieldOp */
            
                uint8_t opcode = *p++;
                
                aml_name_t field_name;
                aml_name_t field_path;
                
                aml_value_t *value;
                
                uint64_t bit_index;
                uint32_t bit_length;
                
                value = kmalloc (sizeof (*value) * 2);
                
                if (!value) {
                    return 0;
                }
                
                if (!aml_read_value (&p, end, scope, context, &value[0])) {
                
                    kfree (value);
                    return 0;
                
                }
                
                if (value[0].type != AML_VALUE_BUFFER) {
                
                    kfree (value);
                    return 0;
                
                }
                
                if (!aml_read_value (&p, end, scope, context, &value[1])) {
                
                    kfree (value);
                    return 0;
                
                }
                
                if (value[1].type != AML_VALUE_INTEGER) {
                
                    kfree (value);
                    return 0;
                
                }
                
                if (!aml_read_name_string (&p, end, &field_name)) {
                
                    kfree (value);
                    return 0;
                
                }
                
                if (!aml_resolve_name (scope, &field_name, &field_path)) {
                
                    kfree (value);
                    return 0;
                
                }
                
                switch (opcode) {
                
                    case 0x8B:          /* CreateWordFieldOp */
                    
                        if (value[1].integer > UINT32_MAX / 8) {
                        
                            kfree (value);
                            return 0;
                        
                        }
                        
                        bit_index = value[1].integer * 8;
                        bit_length = 16;
                        
                        break;
                    
                    case 0x8C:          /* CreateByteFieldOp */

                        if (value[1].integer > UINT32_MAX / 8) {

                            kfree (value);
                            return 0;

                        }

                        bit_index = value[1].integer * 8;
                        bit_length = 8;

                        break;
                    
                    case 0x8D:          /* CreateBitFieldOp */
                    
                        bit_index = value[1].integer;
                        bit_length = 1;
                        
                        break;
                    
                    case 0x8E:          /* CreateDWordFieldOp */
                    
                        if (value[1].integer > UINT32_MAX / 8) {
                        
                            kfree (value);
                            return 0;
                        
                        }
                        
                        bit_index = value[1].integer * 8;
                        bit_length = 32;
                        
                        break;
                    
                    case 0x8F:          /* CreateQWordFieldOp */
                    
                        if (value[1].integer > UINT32_MAX / 8) {
                        
                            kfree (value);
                            return 0;
                        
                        }
                        
                        bit_index = value[1].integer * 8;
                        bit_length = 64;
                        
                        break;
                    
                    default:
                    
                        kfree (value);
                        return 0;
                
                }
                
                if (bit_index > UINT32_MAX) {
                
                    kfree (value);
                    return 0;
                
                }
                
                if ((uint64_t) bit_index + bit_length > (uint64_t) value[0].buffer_length * 8) {
                
                    kfree (value);
                    return 0;
                
                }
                
                memset (&value[1], 0, sizeof (value[1]));
                
                value[1].type = AML_VALUE_BUFFER_FIELD;
                value[1].buffer = value[0].buffer;
                value[1].buffer_length = value[0].buffer_length;
                
                value[1].bit_index = (uint32_t) bit_index;
                value[1].bit_length = bit_length;
                
                if (!aml_add_named_value (&field_path, &value[1])) {
                
                    kfree (value);
                    return 0;
                
                }
                
                kfree (value);
                break;
            
            }
            
            case 0x86: {                /* NotifyOp */
            
                aml_super_name_t target;
                aml_value_t value;
                
                ++p;
                
                if (!aml_read_super_name (&p, end, &target)) {
                    return 0;
                }
                
                if (!aml_read_value (&p, end, scope, context, &value)) {
                    return 0;
                }
                
                if (value.type == AML_VALUE_INTEGER && value.integer == 0x80) {
                
                    aml_name_t target_path;
                    
                    if (target.type == AML_SUPER_NAME_NAMESPACE) {
                    
                        aml_name_t search_scope = *scope;
                        
                        for (;;) {
                        
                            if (aml_resolve_name (&search_scope, &target.name, &target_path) && aml_device_is_power_button (&target_path)) {
                            
                                acpi_poweroff ();
                                break;
                            
                            }
                            
                            if (target.name.root || target.name.parents || !search_scope.segments) {
                                break;
                            }
                            
                            search_scope.segments--;
                        
                        }
                    
                    } else if (aml_resolve_super_name (&target, scope, context, &target_path) && aml_device_is_power_button (&target_path)) {
                        acpi_poweroff ();
                    }
                
                }
                
                break;
            
            }
            
            case 0xA0: {                /* IfOp */
            
                uint8_t *package;
                uint8_t *package_end;
                uint8_t *body;
                
                uint32_t length;
                
                aml_value_t *predicate;
                int branch_returned = 0;
                
                predicate = kmalloc (sizeof (*predicate));
                
                if (!predicate) {
                    return 0;
                }
                
                ++p;
                package = p;
                
                if (!aml_read_pkg_length (&p, end, &length)) {
                
                    kfree (predicate);
                    return 0;
                
                }
                
                if (length > (uint32_t) (end - package)) {
                
                    kfree (predicate);
                    return 0;
                
                }
                
                package_end = package + length;
                
                if (!aml_read_value (&p, package_end, scope, context, predicate)) {
                
                    kfree (predicate);
                    return 0;
                
                }
                
                if (predicate->type != AML_VALUE_INTEGER) {
                
                    kfree (predicate);
                    return 0;
                
                }
                
                body = p;
                p = package_end;
                
                if (predicate->integer) {
                
                    if (!aml_execute_term_list (&body, package_end, scope, context, &branch_returned, return_value)) {
                    
                        kfree (predicate);
                        return 0;
                    
                    }
                    
                    if (branch_returned) {
                    
                        kfree (predicate);
                        
                        *returned = 1;
                        *ptr = p;
                        
                        return 1;
                    
                    }
                
                }
                
                if (p < end && *p == 0xA1) {             /* ElseOp */
                
                    uint8_t *else_package;
                    uint8_t *else_end;
                    uint8_t *else_body;
                    
                    uint32_t else_length;
                    
                    ++p;
                    else_package = p;
                    
                    if (!aml_read_pkg_length (&p, end, &else_length)) {
                    
                        kfree (predicate);
                        return 0;
                    
                    }
                    
                    if (else_length > (uint32_t) (end - else_package)) {
                    
                        kfree (predicate);
                        return 0;
                    
                    }
                    
                    else_end = else_package + else_length;
                    else_body = p;
                    
                    p = else_end;
                    
                    if (!predicate->integer) {
                    
                        if (!aml_execute_term_list (&else_body, else_end, scope, context, &branch_returned, return_value)) {
                        
                            kfree (predicate);
                            return 0;
                        
                        }
                        
                        if (branch_returned) {
                        
                            kfree (predicate);
                            
                            *returned = 1;
                            *ptr = p;
                            
                            return 1;
                        
                        }
                    
                    }
                
                }
                
                kfree (predicate);
                break;
            
            }
            
            case 0xA2: {                /* WhileOp */
            
                uint8_t *package;
                uint8_t *package_end;
                uint8_t *predicate_start;
                uint8_t *body;
                
                uint32_t length;
                
                aml_value_t *predicate;
                int branch_returned;
                
                predicate = kmalloc (sizeof (*predicate));
                
                if (!predicate) {
                    return 0;
                }
                
                ++p;
                package = p;
                
                if (!aml_read_pkg_length (&p, end, &length)) {
                
                    kfree (predicate);
                    return 0;
                
                }
                
                if (length > (uint32_t) (end - package)) {
                
                    kfree (predicate);
                    return 0;
                
                }
                
                package_end = package + length;
                predicate_start = p;
                
                for (;;) {
                
                    p = predicate_start;
                    
                    if (!aml_read_value (&p, package_end, scope, context, predicate)) {
                    
                        kfree (predicate);
                        return 0;
                    
                    }
                    
                    if (predicate->type != AML_VALUE_INTEGER) {
                    
                        kfree (predicate);
                        return 0;
                    
                    }
                    
                    if (!predicate->integer) {
                        break;
                    }
                    
                    body = p;
                    branch_returned = 0;
                    
                    if (!aml_execute_term_list (&body, package_end, scope, context, &branch_returned, return_value)) {
                    
                        kfree (predicate);
                        return 0;
                    
                    }
                    
                    if (branch_returned) {
                    
                        kfree (predicate);
                        
                        *returned = 1;
                        *ptr = package_end;
                        
                        return 1;
                    
                    }
                
                }
                
                kfree (predicate);
                
                p = package_end;
                break;
            
            }
            
            case 0xA3:                  /* NoopOp */
            
                ++p;
                break;
            
            case 0xA4: {                /* ReturnOp */
            
                aml_value_t value;
                ++p;
                
                if (!aml_read_value (&p, end, scope, context, &value)) {
                    return 0;
                }
                
                if (return_value) {
                    *return_value = value;
                }
                
                *returned = 1;
                *ptr = p;
                
                return 1;
            
            }
            
            default: {
            
                aml_value_t *value;
                uint8_t *q = p;
                
                value = kmalloc (sizeof (*value));
                
                if (!value) {
                    return 0;
                }
                
                if (aml_read_value (&q, end, scope, context, value)) {
                
                    kfree (value);
                    
                    p = q;
                    break;
                
                }
                
                kfree (value);
                return 0;
            
            }
        
        }
    
    }
    
    *ptr = p;
    return 1;

}

static int aml_execute_method (uint8_t *method) {

    unsigned table;
    
    for (table = 0; table < acpi_aml_table_count; ++table) {
    
        uint8_t *aml = acpi_aml_tables[table].aml;
        uint8_t *end = aml + acpi_aml_tables[table].length;
        
        aml_method_t object;
        uint8_t *p;
        
        aml_name_t root;
        aml_name_t scope;
        
        aml_execution_context_t *context;
        
        memset (&root, 0, sizeof (root));
        root.root = 1;
        
        if (method < aml || method >= end) {
            continue;
        }
        
        if (!aml_read_method (method, end, &object)) {
            return 0;
        }
        
        if (!aml_find_method_scope ( method, aml, end, &root, &scope)) {
            return 0;
        }
        
        /**
         * GPE control methods are invoked by the ACPI subsystem with
         * no arguments.
         */
        if ((object.flags & 0x07) != 0) {
            return 0;
        }
        
        context = kmalloc (sizeof (*context));
        
        if (!context) {
            return 0;
        }
        
        memset (context, 0, sizeof (*context));
        
        {
        
            int returned = 0;
            p = object.body;
            
            if (!aml_execute_term_list (&p, object.end, &scope, context, &returned, 0)) {
            
                kfree (context);
                return 0;
            
            }
        
        }
        
        kfree (context);
        return 1;
    
    }
    
    return 0;

}

static uint8_t *aml_find_gpe_method (uint8_t *aml, uint8_t *end, const aml_name_t *scope, uint8_t gpe, char type) {

    uint8_t *p = aml;
    
    char name[5];
    snprintf (name, sizeof (name), "_%c%02X", type, gpe);
    
    while (p < end) {
    
        uint8_t *package;
        uint8_t *package_end;
        uint8_t *q;
        
        uint32_t length;
        
        if (*p == 0x14) {               /* MethodOp */
        
            uint8_t *method = p;
            
            aml_name_t method_name;
            aml_name_t method_path;
            
            package = p + 1;
            q = package;
            
            if (!aml_read_pkg_length (&q, end, &length) || length > (uint32_t) (end - package)) {
                return 0;
            }
            
            package_end = package + length;
            
            if (!aml_read_name_string (&q, package_end, &method_name)) {
                return 0;
            }
            
            if (q >= package_end) {
                return 0;
            }
            
            if (!aml_resolve_name (scope, &method_name, &method_path)) {
                return 0;
            }
            
            if (method_path.segments == 2 && memcmp (method_path.segment[0], "_GPE", 4) == 0 && memcmp (method_path.segment[1], name, 4) == 0) {
                return method;
            }
            
            p = package_end;
            continue;
        
        }
        
        if (*p == 0x10) {               /* ScopeOp */
        
            uint8_t *method;
            
            aml_name_t scope_name;
            aml_name_t new_scope;
            
            package = p + 1;
            q = package;
            
            if (!aml_read_pkg_length (&q, end, &length) || length > (uint32_t) (end - package)) {
                return 0;
            }
            
            package_end = package + length;
            
            if (!aml_read_name_string (&q, package_end, &scope_name) || !aml_resolve_name (scope, &scope_name, &new_scope)) {
                return 0;
            }
            
            method = aml_find_gpe_method (q, package_end, &new_scope, gpe, type);
            
            if (method) {
                return method;
            }
            
            p = package_end;
            continue;
        
        }
        
        if ((size_t) (end - p) >= 2 && p[0] == 0x5B && (p[1] == 0x82 /* DeviceOp */ || p[1] == 0x83 /* ProcessorOp */ || p[1] == 0x84 /* PowerResOp */ || p[1] == 0x85 /* ThermalZoneOp */)) {
        
            uint8_t *method;
            
            aml_name_t object_name;
            aml_name_t new_scope;
            
            package = p + 2;
            q = package;
            
            if (!aml_read_pkg_length (&q, end, &length) || length > (uint32_t) (end - package)) {
                return 0;
            }
            
            package_end = package + length;
            
            if (!aml_read_name_string (&q, package_end, &object_name) || !aml_resolve_name (scope, &object_name, &new_scope)) {
                return 0;
            }
            
            /**
             * ProcessorOp and PowerResOp have fixed fields between
             * their NameString and TermList.
             */
            if (p[1] == 0x83) {          /* ProcessorOp */
            
                if ((size_t) (package_end - q) < 6) {
                    return 0;
                }
                
                q += 6;
            
            } else if (p[1] == 0x84) {   /* PowerResOp */
            
                if ((size_t) (package_end - q) < 3) {
                    return 0;
                }
                
                q += 3;
            
            }
            
            method = aml_find_gpe_method (q, package_end, &new_scope, gpe, type);
            
            if (method) {
                return method;
            }
            
            p = package_end;
            continue;
        
        }
        
        {
        
            uint8_t *next = p;
            
            if (!aml_skip_namespace_object (&next, end) || next <= p) {
                return 0;
            }
            
            p = next;
        
        }
    
    }
    
    return 0;

}

static uint8_t *acpi_find_gpe_method (uint8_t gpe, char type) {

    aml_name_t root;
    unsigned table;
    
    memset (&root, 0, sizeof (root));
    root.root = 1;
    
    for (table = 0; table < acpi_aml_table_count; ++table) {
    
        uint8_t *aml = acpi_aml_tables[table].aml;
        uint8_t *end = aml + acpi_aml_tables[table].length;
        
        uint8_t *method = aml_find_gpe_method (aml, end, &root, gpe, type);
        
        if (method) {
            return method;
        }
    
    }
    
    return 0;

}

static int acpi_service_gpe_block (uint16_t block, uint8_t length, uint8_t base) {

    uint8_t registers;
    uint8_t offset;
    
    int handled = 0;
    
    if (!block || !length) {
        return 0;
    }
    
    registers = length / 2;
    
    for (offset = 0; offset < registers; ++offset) {
    
        uint16_t status_port = block + offset;
        uint16_t enable_port = block + registers + offset;
        
        uint8_t status;
        uint8_t enable;
        uint8_t active;
        uint8_t bit;
        
        __asm__ __volatile__ (
            "inb %1, %0"
            : "=a" (status)
            : "Nd" (status_port)
        );
        
        __asm__ __volatile__ (
            "inb %1, %0"
            : "=a" (enable)
            : "Nd" (enable_port)
        );
        
        active = status & enable;
        
        for (bit = 0; bit < 8; ++bit) {
        
            uint8_t mask = (uint8_t) (1 << bit);
            uint8_t gpe;
            uint8_t *method;
            
            if (!(active & mask)) {
                continue;
            }
            
            gpe = base + (offset * 8) + bit;
            method = acpi_find_gpe_method (gpe, 'E');
            
            if (method) {
            
                /**
                 * Edge-triggered GPEs are cleared before evaluating
                 * their control method.
                 */
                __asm__ __volatile__ (
                    "outb %0, %1"
                    :
                    : "a" (mask), "Nd" (status_port)
                );
                
                aml_execute_method (method);
                
                handled = 1;
                continue;
            
            }
            
            method = acpi_find_gpe_method (gpe, 'L');
            
            if (method) {
            
                /**
                 * Disable a level-triggered GPE while its control
                 * method is being evaluated.
                 */
                enable &= ~mask;
                
                __asm__ __volatile__ (
                    "outb %0, %1"
                    :
                    : "a" (enable), "Nd" (enable_port)
                );
                
                if (!aml_execute_method (method)) {
                
                    __asm__ __volatile__ (
                        "outb %0, %1"
                        :
                        : "a" (mask), "Nd" (status_port)
                    );
                    
                    handled = 1;
                    continue;
                
                }
                
                __asm__ __volatile__ (
                    "outb %0, %1"
                    :
                    : "a" (mask), "Nd" (status_port)
                );
                
                __asm__ __volatile__ (
                    "inb %1, %0"
                    : "=a" (status)
                    : "Nd" (status_port)
                );
                
                enable |= mask;
                
                __asm__ __volatile__ (
                    "outb %0, %1"
                    :
                    : "a" (enable), "Nd" (enable_port)
                );
                
                handled = 1;
                continue;
            
            }
            
            /**
             * Disable a GPE without a control method so an event
             * that immediately reasserts cannot continuously fire SCI.
             */
            enable &= ~mask;
            
            __asm__ __volatile__ (
                "outb %0, %1"
                :
                : "a" (enable), "Nd" (enable_port)
            );
            
            __asm__ __volatile__ (
                "outb %0, %1"
                :
                : "a" (mask), "Nd" (status_port)
            );
            
            __asm__ __volatile__ (
                "inb %1, %0"
                : "=a" (status)
                : "Nd" (status_port)
            );
        
        }
    
    }
    
    return handled;

}

static int acpi_sci_ioapic_routed = 0;
static uint16_t acpi_sci_irq = 0;

static uint32_t acpi_sci_gsi = 0;
static struct thread *acpi_thread = 0;

static volatile int acpi_sci_pending = 0;
static uint8_t acpi_sci_pic_mask = 0;

static void acpi_sci_set_mask (int masked);

static void acpi_sci_service (void) {

    int power_button = 0;
    uint16_t status;
    
    if (acpi_fixed_power_button && acpi_pm1a_event) {
    
        __asm__ __volatile__ (
            "inw %1, %0"
            : "=a" (status)
            : "Nd" (acpi_pm1a_event)
        );
        
        if (status & ACPI_PM1_PWRBTN_STS) {
        
            __asm__ __volatile__ (
                "outw %0, %1"
                :
                : "a" ((uint16_t) ACPI_PM1_PWRBTN_STS), "Nd" (acpi_pm1a_event)
            );
            
            power_button = 1;
        
        }
    
    }
    
    if (acpi_fixed_power_button && acpi_pm1b_event) {
    
        __asm__ __volatile__ (
            "inw %1, %0"
            : "=a" (status)
            : "Nd" (acpi_pm1b_event)
        );
        
        if (status & ACPI_PM1_PWRBTN_STS) {
        
            __asm__ __volatile__ (
                "outw %0, %1"
                :
                : "a" ((uint16_t) ACPI_PM1_PWRBTN_STS), "Nd" (acpi_pm1b_event)
            );
            
            power_button = 1;
        
        }
    
    }
    
    acpi_service_gpe_block (acpi_gpe0_block, acpi_gpe0_length, 0);
    acpi_service_gpe_block (acpi_gpe1_block, acpi_gpe1_length, acpi_gpe1_base);
    
    if (power_button) {
        acpi_poweroff ();
    }

}

static void acpi_sci_thread (void) {

    uintptr_t flags;
    
    for (;;) {
    
        __asm__ __volatile__ ("pushf");
        __asm__ __volatile__ ("pop %0" : "=r" (flags));
        
        __asm__ __volatile__ ("cli");
        
        if (!acpi_sci_pending) {
        
            acpi_thread->state = TS_WAITIO;
            
            if (flags & 0x200) {
            
                __asm__ __volatile__ ("sti");
                __asm__ __volatile__ ("hlt");
            
            } else {
            
#if     defined (__AMD64__)
                __asm__ __volatile__ ("pushq %0" : : "r" (flags));
                __asm__ __volatile__ ("popfq");
#else
                __asm__ __volatile__ ("pushd %0" : : "r" (flags));
                __asm__ __volatile__ ("popfd");
#endif
            
            }
            
            continue;
        
        }
        
        acpi_sci_pending = 0;
        
#if     defined (__AMD64__)
                __asm__ __volatile__ ("pushq %0" : : "r" (flags));
                __asm__ __volatile__ ("popfq");
#else
                __asm__ __volatile__ ("pushd %0" : : "r" (flags));
                __asm__ __volatile__ ("popfd");
#endif
        
        acpi_sci_service ();
        
#if     defined (__AMD64__)
        __asm__ __volatile__ ("pushfq");
        __asm__ __volatile__ ("popq %0" : "=r" (flags));
#else
        __asm__ __volatile__ ("pushfd");
        __asm__ __volatile__ ("popd %0" : "=r" (flags));
#endif
        
        __asm__ __volatile__ ("cli");
        
        acpi_sci_set_mask (0);
        
#if     defined (__AMD64__)
                __asm__ __volatile__ ("pushq %0" : : "r" (flags));
                __asm__ __volatile__ ("popfq");
#else
                __asm__ __volatile__ ("pushd %0" : : "r" (flags));
                __asm__ __volatile__ ("popfd");
#endif

    }

}

static int acpi_sci_handler (struct regs *r) {

    (void) r;
    
    if (!acpi_sci_pending && acpi_thread) {
    
        acpi_sci_set_mask (1);
        
        acpi_sci_pending = 1;
        acpi_thread->state = TS_RUN;
    
    }
    
    irq_ack ();
    
    if (acpi_sci_ioapic_routed) {
        acpi_lapic_ack ();
    }
    
    return 1;

}

static int acpi_phys_address_usable (uint64_t paddr) {

    /**
     * A normal i386 uintptr_t cannot describe a physical address above 4 GiB.
     * AMD64 accepts the full address.
     */
    if (sizeof (uintptr_t) == 4 && paddr > 0xFFFFFFFFULL) {
        return 0;
    }
    
    return 1;

}

static void *acpi_try_alloc (uint64_t paddr) {

    if (!acpi_phys_address_usable (paddr)) {
        return 0;
    }
    
    return (void *) align_down ((uintptr_t) paddr, PAGE_SIZE);;
}

static int acpi_checksum (const void *ptr, size_t len) {

    const uint8_t *p = ptr;
    uint8_t sum = 0;

    while (len--) {
        sum += *p++;
    }

    return sum == 0;

}

static void acpi_parser_fadt (acpi_header_t *table) {

    fadt_table_t *tbl;
    
    char *path;
    size_t base_path_length;
    
    /* Set up IPC path */
    base_path_length = strlen (acpi_parser_base_path) + strlen (fadtPath);
    path = kmalloc (base_path_length + 1);
    
    strcpy (path, acpi_parser_base_path);
    strcat (path, fadtPath);
    
    if (ipc_get_node (path)) {
        ipc_delete_key (path);
    }
    
    ipc_set_task_working_node (path, 1);
    tbl = (fadt_table_t*) table;
    
    {
    
        uintptr_t offset;
        uint64_t dsdt;
        
        uint32_t header_pages;
        uint32_t pages;
        
        acpi_header_t *header;
        uint8_t *mapped;
        
        if (tbl->header.rev >= 2 && tbl->X_Dsdt) {
            dsdt = tbl->X_Dsdt;
        } else {
            dsdt = tbl->dsdt;
        }
        
        offset = (uintptr_t) dsdt & (PAGE_SIZE - 1);
        
        header_pages = (uint32_t) (align_up (offset + sizeof (acpi_header_t), PAGE_SIZE) / PAGE_SIZE );
        mapped = acpi_try_alloc (dsdt);
        
        if (mapped) {
        
            header = (acpi_header_t *) (mapped + offset);
            pages = (uint32_t) (align_up (offset + header->len, PAGE_SIZE) / PAGE_SIZE);
            
            if (pages != header_pages) {
            
                mapped = acpi_try_alloc (dsdt);
                
                if (mapped) {
                    header = (acpi_header_t *) (mapped + offset);
                }
            
            }
            
            if (mapped) {
            
                uint8_t *aml = (uint8_t *) header + sizeof (acpi_header_t);
                uint8_t *end = (uint8_t *) header + header->len;
                
                uint8_t *p;
                
                acpi_dsdt_aml_length = header->len - sizeof (acpi_header_t);
                acpi_dsdt_aml = kmalloc (acpi_dsdt_aml_length);
                
                memcpy (acpi_dsdt_aml, aml, acpi_dsdt_aml_length);
                acpi_add_aml_table (acpi_dsdt_aml, acpi_dsdt_aml_length);
                
                for (p = aml; p + 4 <= end; ++p) {
                
                    if (memcmp (p, "_S5_", 4) == 0) {
                    
                        uint8_t *q;
                        
                        uint32_t package_length;
                        uint64_t typa, typb;
                        
                        /* Skip the _S5_ NameSeg. */
                        q = p + 4;
                        
                        /* The object following _S5_ must be a PackageOp. */
                        if (q >= end || *q++ != 0x12) {
                            break;
                        }
                        
                        if (!aml_read_pkg_length (&q, end, &package_length)) {
                            break;
                        }
                        
                        /* q now points at NumElements. */
                        if (q >= end || *q++ < 2) {
                            break;
                        }
                        
                        if (aml_read_integer (&q, end, &typa) && aml_read_integer (&q, end, &typb)) {
                        
                            acpi_slp_typa = (uint16_t) typa;
                            acpi_slp_typb = (uint16_t) typb;
                            
                            acpi_s5_available = 1;
                        
                        }

                        break;

                    }
                
                }
            
            }
        
        }
    
    }
    
    if (tbl->header.rev > 1) {
    
        int64_t bootArchitecture = tbl->bootArchitectureFlags;
        ipc_set_int (">/boot_architecture", &bootArchitecture, IPC_QWORD);
    
    }
    
    if (tbl->header.rev >= 2 && tbl->X_PM1aEventBlock.address_space == 1 && tbl->X_PM1aEventBlock.address) {
        acpi_pm1a_event = (uint16_t) tbl->X_PM1aEventBlock.address;
    } else if (tbl->PM1aEventBlock) {
        acpi_pm1a_event = (uint16_t) tbl->PM1aEventBlock;
    }
    
    if (tbl->header.rev >= 2 && tbl->X_PM1bEventBlock.address_space == 1 && tbl->X_PM1bEventBlock.address) {
        acpi_pm1b_event = (uint16_t) tbl->X_PM1bEventBlock.address;
    } else if (tbl->PM1bEventBlock) {
        acpi_pm1b_event = (uint16_t) tbl->PM1bEventBlock;
    }
    
    if (tbl->header.rev >= 2 && tbl->X_PM1aControlBlock.address_space == 1 && tbl->X_PM1aControlBlock.address) {
        acpi_pm1a_control = (uint16_t) tbl->X_PM1aControlBlock.address;
    } else if (tbl->PM1aControlBlock) {
        acpi_pm1a_control = (uint16_t) tbl->PM1aControlBlock;
    }
    
    if (tbl->header.rev >= 2 && tbl->X_PM1bControlBlock.address_space == 1 && tbl->X_PM1bControlBlock.address) {
        acpi_pm1b_control = (uint16_t) tbl->X_PM1bControlBlock.address;
    } else if (tbl->PM1bControlBlock) {
        acpi_pm1b_control = (uint16_t) tbl->PM1bControlBlock;
    }
    
    if (tbl->header.rev >= 2 && tbl->X_GPE0Block.address_space == 1 && tbl->X_GPE0Block.address) {
        acpi_gpe0_block = (uint16_t) tbl->X_GPE0Block.address;
    } else if (tbl->GPE0Block) {
        acpi_gpe0_block = (uint16_t) tbl->GPE0Block;
    }
    
    acpi_gpe0_length = tbl->GPE0Length;
    
    if (tbl->header.rev >= 2 && tbl->X_GPE1Block.address_space == 1 && tbl->X_GPE1Block.address) {
        acpi_gpe1_block = (uint16_t) tbl->X_GPE1Block.address;
    } else if (tbl->GPE1Block) {
        acpi_gpe1_block = (uint16_t) tbl->GPE1Block;
    }
    
    acpi_gpe1_length = tbl->GPE1Length;
    acpi_gpe1_base = tbl->GPE1Base;
    
    /* Enter ACPI mode. */
    if (tbl->sMI_CommandPort && tbl->acpiEnable && acpi_pm1a_control) {
    
        uint32_t timeout;
        uint16_t value;
        
        __asm__ __volatile__ (
            "inw %1, %0"
            : "=a" (value)
            : "Nd" (acpi_pm1a_control)
        );
        
        if (!(value & 0x0001)) {
        
            __asm__ __volatile__ (
                "outb %0, %1"
                :
                : "a" (tbl->acpiEnable), "Nd" ((uint16_t) tbl->sMI_CommandPort)
            );
            
            /**
             * Wait for firmware to enter ACPI mode.
             * SCI_EN is bit 0 of PM1_CNT.
             */
            for (timeout = 0; timeout < 1000000; ++timeout) {
            
                __asm__ __volatile__ (
                    "inw %1, %0"
                    : "=a" (value)
                    : "Nd" (acpi_pm1a_control)
                );
                
                if (value & 0x0001) {
                    break;
                }
                
#if     defined (__AMD64__)
                __asm__ __volatile__ ("pause");
#else
                __asm__ __volatile__ ("hlt");
#endif
            
            }
            
            if (!(value & 0x0001)) {
                return;
            }
        
        }
    
    }
    
    if (acpi_pm1a_event) {
    
        __asm__ __volatile__ (
            "outw %0, %1"
            :
            : "a" ((uint16_t) ACPI_PM1_PWRBTN_STS), "Nd" (acpi_pm1a_event)
        );
    
    }
    
    if (acpi_pm1b_event) {
    
        __asm__ __volatile__ (
            "outw %0, %1"
            :
            : "a" ((uint16_t) ACPI_PM1_PWRBTN_STS), "Nd" (acpi_pm1b_event)
        );
    
    }
    
    aml_scan_namespace ();
    acpi_fixed_power_button = !(tbl->flags & ACPI_FADT_PWR_BUTTON) && !aml_device_count;
    
    /* Enable the fixed ACPI power-button event. */
    if (acpi_fixed_power_button && acpi_pm1a_event && tbl->PM1EventLength) {
    
        uint16_t pm1a_enable;
        uint16_t value;
        
        pm1a_enable = acpi_pm1a_event + (tbl->PM1EventLength / 2);
        
        __asm__ __volatile__ (
            "inw %1, %0"
            : "=a" (value)
            : "Nd" (pm1a_enable)
        );
        
        value |= ACPI_PM1_PWRBTN_STS;
        
        __asm__ __volatile__ (
            "outw %0, %1"
            :
            : "a" (value), "Nd" (pm1a_enable)
        );

    }
    
    if (acpi_fixed_power_button && acpi_pm1b_event && tbl->PM1EventLength) {
    
        uint16_t pm1b_enable;
        uint16_t value;
        
        pm1b_enable = acpi_pm1b_event + (tbl->PM1EventLength / 2);
        
        __asm__ __volatile__ (
            "inw %1, %0"
            : "=a" (value)
            : "Nd" (pm1b_enable)
        );
        
        value |= ACPI_PM1_PWRBTN_STS;
        
        __asm__ __volatile__ (
            "outw %0, %1"
            :
            : "a" (value), "Nd" (pm1b_enable)
        );
    
    }
    
    if (acpi_gpe0_block || acpi_gpe1_block) {
        
        unsigned limit, gpe, i;
        
        for (i = 0; i < aml_device_count; ++i) {
        
            if (aml_devices[i].power_button) {
            
                if (aml_devices[i].prw_valid) {
                
                    acpi_power_button_gpe = aml_devices[i].prw_gpe;
                    acpi_power_button_gpe_valid = 1;
                
                }
            
            }
        
        }
        
        limit = (unsigned) ((acpi_gpe0_length / 2) * 8), gpe;
        
        if (acpi_gpe1_block && acpi_gpe1_length) {
        
            unsigned last = (unsigned) acpi_gpe1_base + ((acpi_gpe1_length / 2) * 8);
            
            if (last > limit) {
                limit = last;
            }
        
        }
        
        if (limit > 256) {
            limit = 256;
        }
        
        for (gpe = 0; gpe < limit; ++gpe) {
        
            uint16_t block;
            uint16_t status_port;
            uint16_t enable_port;
            
            uint8_t base;
            uint8_t length;
            uint8_t offset;
            uint8_t mask;
            uint8_t value;
            
            if (acpi_gpe0_block && acpi_gpe0_length && gpe < (unsigned) ((acpi_gpe0_length / 2) * 8)) {
            
                block = acpi_gpe0_block;
                base = 0;
                length = acpi_gpe0_length;
            
            } else if (acpi_gpe1_block && acpi_gpe1_length && gpe >= acpi_gpe1_base && gpe < (unsigned) acpi_gpe1_base + ((acpi_gpe1_length / 2) * 8)) {
            
                block = acpi_gpe1_block;
                base = acpi_gpe1_base;
                length = acpi_gpe1_length;
            
            } else {
                continue;
            }
            
            if (!acpi_find_gpe_method ((uint8_t) gpe, 'E') && !acpi_find_gpe_method ((uint8_t) gpe, 'L')) {
                continue;
            }
            
            offset = ((uint8_t) gpe - base) / 8;
            mask = (uint8_t) (1 << (((uint8_t) gpe - base) & 7));
            
            status_port = block + offset;
            enable_port = block + (length / 2) + offset;
            
            /**
             * GPE status bits are write-one-to-clear.  Clear any
             * event left pending by the firmware before enabling
             * the control method.
             */
            __asm__ __volatile__ (
                "outb %0, %1"
                :
                : "a" (mask), "Nd" (status_port)
            );
            
            __asm__ __volatile__ (
                "inb %1, %0"
                : "=a" (value)
                : "Nd" (enable_port)
            );
            
            value |= mask;
            
            __asm__ __volatile__ (
                "outb %0, %1"
                :
                : "a" (value), "Nd" (enable_port)
            );
        
        }
    
    }
    
    acpi_sci_irq = tbl->sCI_Interrupt;

}

#define     MADT_LOCAL_APIC                                 0
#define     MADT_IO_APIC                                    1
#define     MADT_INTERRUPT_SOURCE_OVERRIDE                  2
#define     MADT_NMI                                        3
#define     MADT_LOCAL_APIC_NMI                             4
#define     MADT_LOCAL_APIC_ADDRESS_OVERRIDE                5
#define     MADT_LOCAL_X2APIC                               9
#define     MADT_LOCAL_X2APIC_NMI                           10
#define     MADT_MULTIPROCESSOR_WAKEUP                      16

const char *const madtPath = "/madt";
#define     MADT_FLAGS_PCAT_COMPAT      1                   /* System has usual dual PIC set up as well */

__attribute__((packed)) typedef struct {

    acpi_header_t header;
    
    uint32_t local_apic_addr;
    uint32_t flags;

} madt_table_t;

__attribute__((packed)) typedef struct {

    uint8_t type;
    uint8_t length;

} madt_table_entry_header_t;

#define     MADT_ENTRY_LOCAL_APIC_FLAGS_ENABLED             (1 << 0)
#define     MADT_ENTRY_LOCAL_APIC_FLAGS_ONLINE_CAPABLE      (1 << 1)

__attribute__((packed)) typedef struct {

    madt_table_entry_header_t header;
    
    uint8_t processor_id;
    uint8_t id;
    
    uint32_t flags;

} madt_entry_local_apic_t;

__attribute__((packed)) typedef struct {

    madt_table_entry_header_t header;
    
    uint8_t id;
    uint8_t reserved_;
    
    uint32_t addr;
    uint32_t interrupt_base;            /* Global int num at wich IO apic's interrupts will begin being mapped */

} madt_entry_io_apic_t;

__attribute__((packed)) typedef struct {

    madt_table_entry_header_t header;
    
    uint8_t bus;                        /* Will always be 0 (ISA) */
    uint8_t picirq;                     /* Bus relative interrupt (e.g. no + 32) */
    
    uint32_t apicirq;                   /* Int no when mapped on IO-APIC */
    uint16_t flags;

} madt_entry_int_override_t;

__attribute__((packed)) typedef struct {

    madt_table_entry_header_t header;
    
    uint16_t flags;                     /* See madt.h */
    uint32_t intno;                     /* Interrupt that will fire */

} madt_entry_nmi_t;

__attribute__((packed)) typedef struct {

    madt_table_entry_header_t header;
    uint8_t processor_id;               /* 0xFF = all Processors on the System */
    
    uint16_t flags;
    uint8_t lint_pin;                   /* LINT pin of local apic NMI is connected to */

} madt_entry_lapic_nmi_t;

__attribute__((packed)) typedef struct {

    madt_table_entry_header_t header;
    
    uint16_t reserved_;
    uint64_t addr;

} madt_entry_lapic_addr_override_t;

__attribute__((packed)) typedef struct {

    madt_table_entry_header_t header;
    
    uint16_t reserved_;
    uint32_t id;
    uint32_t flags;
    uint32_t processor_id;

} madt_entry_local_x2apic_t;

__attribute__((packed)) typedef struct {

    madt_table_entry_header_t header;
    
    uint16_t flags;
    uint32_t processor_id;
    uint8_t lint_pin;
    uint8_t reserved_[3];

} madt_entry_local_x2apic_nmi_t;

__attribute__((packed)) typedef struct {

    madt_table_entry_header_t header;
    
    uint16_t mailbox_version;
    uint32_t reserved_;
    uint64_t mailbox_address;

} madt_entry_multiprocessor_wakeup_t;

#define     IOAPIC_MAX                  16

typedef struct {

    volatile uint32_t *addr;
    
    uint32_t interrupt_base;
    uint32_t interrupt_count;

} ioapic_t;

static ioapic_t ioapics[IOAPIC_MAX];
static unsigned ioapic_count = 0;

static uint32_t ioapic_read (ioapic_t *ioapic, uint8_t reg) {

    ioapic->addr[0] = reg;
    return ioapic->addr[4];

}

static void ioapic_write (ioapic_t *ioapic, uint8_t reg, uint32_t value);

static void acpi_sci_set_mask (int masked) {

    uint16_t port = (acpi_sci_irq < 8) ? 0x21 : 0xA1;
    
    uint8_t bit = (uint8_t) (1 << (acpi_sci_irq & 7));
    uint8_t value;
    
    unsigned i;
    
    if (acpi_sci_ioapic_routed) {
    
        for (i = 0; i < ioapic_count; ++i) {
        
            ioapic_t *ioapic = &ioapics[i];
            uint32_t entry, low;
            
            if (acpi_sci_gsi < ioapic->interrupt_base || acpi_sci_gsi >= ioapic->interrupt_base + ioapic->interrupt_count) {
                continue;
            }
            
            entry = acpi_sci_gsi - ioapic->interrupt_base;
            low = ioapic_read (ioapic, (uint8_t) (0x10 + entry * 2));
            
            if (masked) {
                low |= (1 << 16);
            } else {
                low &= ~(1 << 16);
            }
            
            ioapic_write (ioapic, (uint8_t) (0x10 + entry * 2), low);
            break;
        
        }
    
    }
    
    __asm__ __volatile__ (
        "inb %1, %0"
        : "=a" (value)
        : "Nd" (port)
    );
    
    if (masked) {
    
        acpi_sci_pic_mask = value & bit;
        value |= bit;
    
    } else {
        value = (value & ~bit) | acpi_sci_pic_mask;
    }
    
    __asm__ __volatile__ (
        "outb %0, %1"
        :
        : "a" (value), "Nd" (port)
    );

}

static void ioapic_write (ioapic_t *ioapic, uint8_t reg, uint32_t value) {

    ioapic->addr[0] = reg;
    ioapic->addr[4] = value;

}

static int ioapic_route (uint32_t gsi, uint8_t vector, uint8_t lapic_id, uint16_t flags) {

    ioapic_t *ioapic = 0;
    
    uint32_t entry, high;
    uint32_t low = vector;
    
    unsigned i;
    
    for (i = 0; i < ioapic_count; ++i) {
    
        if (gsi >= ioapics[i].interrupt_base && gsi < ioapics[i].interrupt_base + ioapics[i].interrupt_count) {
        
            ioapic = &ioapics[i];
            break;
        
        }
    
    }
    
    if (!ioapic) {
        return 0;
    }
    
    entry = gsi - ioapic->interrupt_base;
    
    /**
     * MADT polarity:
     *     0 = conforming
     *     1 = active high
     *     3 = active low
     */
    if ((flags & 3) == 3) {
        low |= (1 << 13);
    }
    
    /**
     * MADT trigger mode:
     *     0 = conforming
     *     1 = edge
     *     3 = level
     */
    if (((flags >> 2) & 3) == 3) {
        low |= (1 << 15);
    }
    
    high = ((uint32_t) lapic_id) << 24;
    
    ioapic_write (ioapic, (uint8_t) (0x10 + (entry * 2) + 1), high);
    ioapic_write (ioapic, (uint8_t) (0x10 + (entry * 2)), low);
    
    return 1;

}

static void ioapic_route_nmi (uint32_t gsi, uint16_t flags) {

    ioapic_t *ioapic = 0;
    
    uint32_t entry, high;
    uint32_t low = (4 << 8);
    
    unsigned i;
    
    for (i = 0; i < ioapic_count; ++i) {
    
        if (gsi >= ioapics[i].interrupt_base && gsi < ioapics[i].interrupt_base + ioapics[i].interrupt_count) {
        
            ioapic = &ioapics[i];
            break;
        
        }
    
    }
    
    if (!ioapic) {
        return;
    }
    
    entry = gsi - ioapic->interrupt_base;
    
    if ((flags & 3) == 3) {
        low |= (1 << 13);
    }
    
    if (((flags >> 2) & 3) == 3) {
        low |= (1 << 15);
    }
    
    high = 0;
    
    ioapic_write (ioapic, (uint8_t) (0x10 + (entry * 2) + 1), high);
    ioapic_write (ioapic, (uint8_t) (0x10 + (entry * 2)), low);

}

#if     defined (__AMD64__)
extern uint64_t read_tsc (void);

static void lapic_timer_initialize (int is_compatible) {

    uint64_t before, after;
    uint64_t us;
    uint64_t target;
    
    /* Calibrate the TSC against PIT channel 2. */
    extern uint64_t tsc_mhz;
    
    if (!tsc_mhz) {
        return;
    }
    
    /**
     * Enable the local APIC.
     *
     * Vector 0x27 is used for the spurious interrupt.
     */
    lapic_write (0x0F0, 0x127);
    
    /* Set the LAPIC timer vector to 123 and divide by 4. */
    lapic_write (0x320, 0x7B);
    lapic_write (0x3E0, 1);
    
    /* Measure one million LAPIC timer ticks against the TSC. */
    before = read_tsc ();
    
    lapic_write (0x380, 1000000);
    
    while (lapic_read (0x390)) {
    
        if (read_tsc () - before >= tsc_mhz * 1000000ULL) {
        
            lapic_write (0x320, 0x7B | 0x10000);
            return;
        
        }
        
        __asm__ __volatile__ ("pause");
    
    }
    
    after = read_tsc ();
    
    /* Convert the TSC difference to microseconds. */
    us = (after - before) / tsc_mhz;
    
    if (!us) {
        return;
    }
    
    target = 1000000000ULL / us;
    
    /* Vector 123, periodic mode. */
    lapic_write (0x3E0, 1);
    lapic_write (0x320, 0x7B | 0x20000);
    lapic_write (0x380, (uint32_t) target);
    
    if (is_compatible) {
    
        __asm__ __volatile__ (
            "outb %0, %1"
            :
            : "a" ((uint8_t) 0xFF), "Nd" ((uint16_t) 0x21)
        );
        
        __asm__ __volatile__ (
            "outb %0, %1"
            :
            : "a" ((uint8_t) 0xFF), "Nd" ((uint16_t) 0xA1)
        );
    
    }

}
#endif

static uint32_t acpi_processor_id = 0xFFFFFFFF;
static uint32_t local_apic_id = 0;

static void parse_lapic_entry (madt_entry_local_apic_t *entry, unsigned *id) {

    int64_t lapicid, procid;
    char buffer[30];
    
    if (!(entry->flags & (MADT_ENTRY_LOCAL_APIC_FLAGS_ENABLED | MADT_ENTRY_LOCAL_APIC_FLAGS_ONLINE_CAPABLE))) {
        return;
    }
    
    snprintf (buffer, 30, ">/lapic/%u", (*id)++);
    ipc_set_task_working_node (buffer, 1);
    
    lapicid = entry->id;
    ipc_set_int (">/id", &lapicid, IPC_UINT);
    
    procid = entry->processor_id;
    ipc_set_int (">/processor_id", &procid, IPC_UINT);
    
    if (local_apic_id == entry->id) {
        acpi_processor_id = entry->processor_id;
    }

}

static void parse_x2apic_entry (madt_entry_local_x2apic_t *entry, unsigned *id) {

    int64_t lapicid, procid;
    char buffer[30];
    
    if (!(entry->flags & (MADT_ENTRY_LOCAL_APIC_FLAGS_ENABLED | MADT_ENTRY_LOCAL_APIC_FLAGS_ONLINE_CAPABLE))) {
        return;
    }
    
    snprintf (buffer, 30, ">/lapic/%u", (*id)++);
    ipc_set_task_working_node (buffer, 1);
    
    lapicid = entry->id;
    ipc_set_int (">/id", &lapicid, IPC_UINT);
    
    procid = entry->processor_id;
    ipc_set_int (">/processor_id", &procid, IPC_UINT);
    
    if (local_apic_id == entry->id) {
        acpi_processor_id = entry->processor_id;
    }

}

static void parse_x2apic_nmi_entry (madt_entry_local_x2apic_nmi_t *entry, unsigned *id) {

    int64_t processor, flags, lint;
    char buffer[30];
    
    snprintf (buffer, 30, ">/lapic_nmi/%u", (*id)++);
    ipc_set_task_working_node (buffer, 1);
    
    processor = entry->processor_id;
    ipc_set_int (">/processor", &processor, IPC_UINT);
    
    flags = entry->flags;
    ipc_set_int (">/flags", &flags, IPC_QWORD);
    
    lint = entry->lint_pin;
    ipc_set_int (">/lint", &lint, IPC_UINT);
    
    if (lapic_addr && (entry->processor_id == 0xFFFFFFFF || entry->processor_id == acpi_processor_id)) {
    
        uint32_t value = (4 << 8);
        
        if ((entry->flags & 3) == 3) {
            value |= (1 << 13);
        }
        
        if (((entry->flags >> 2) & 3) == 3) {
            value |= (1 << 15);
        }
        
        if (entry->lint_pin == 0) {
            lapic_write (0x350, value);
        } else if (entry->lint_pin == 1) {
            lapic_write (0x360, value);
        }
    
    }

}

static void parse_io_acpi_entry (madt_entry_io_apic_t *entry, unsigned *id) {
    
    int64_t ioapicid, addr, base;
    char buffer[30];
    
    if (ioapic_count < IOAPIC_MAX) {
    
        ioapic_t *ioapic = &ioapics[ioapic_count++];
        
        ioapic->addr = (volatile uint32_t *) (uintptr_t) entry->addr;
        ioapic->interrupt_base = entry->interrupt_base;
        ioapic->interrupt_count = ((ioapic_read (ioapic, 1) >> 16) & 0xFF) + 1;
    
    }
    
    snprintf (buffer, 30, ">/ioapic/%u", (*id)++);
    ipc_set_task_working_node (buffer, 1);
    
    ioapicid = entry->id;
    ipc_set_int (">/id", &ioapicid, IPC_UINT);
    
    addr = (uint32_t)entry->addr;
    ipc_set_int (">/address", &addr, IPC_QWORD);
    
    base = entry->interrupt_base;
    ipc_set_int (">/int_base", &base, IPC_UINT);

}

static uint16_t acpi_sci_flags = 0;

static uint32_t isa_irq_gsi[16];
static uint16_t isa_irq_flags[16];

static void parse_int_override_entry (madt_entry_int_override_t *entry, unsigned *id) {

    int64_t picno, apicno, flags;
    char buffer[30];
    
    snprintf (buffer, 30, ">/intoverride/%u", (*id)++);
    ipc_set_task_working_node (buffer, 1);
    
    picno = entry->picirq;
    ipc_set_int (">/irq", &picno, IPC_UINT);
    
    apicno = entry->apicirq;
    ipc_set_int (">/apicmapped", &apicno, IPC_UINT);
    
    flags = entry->flags;               /* Use defines in madt.h to get additional info */
    ipc_set_int (">/flags", &flags, IPC_QWORD);
    
    if (entry->bus == 0 && entry->picirq < 16) {
    
        isa_irq_gsi[entry->picirq] = entry->apicirq;
        isa_irq_flags[entry->picirq] = entry->flags;
    
    }
    
    if (entry->picirq == acpi_sci_irq) {
    
        acpi_sci_gsi = entry->apicirq;
        acpi_sci_flags = entry->flags;
    
    }

}

int acpi_route_legacy_irq (uint8_t irq) {

    if (irq >= 16 || !lapic_addr || !ioapic_count) {
        return 0;
    }
    
    return ioapic_route (isa_irq_gsi[irq], 32 + irq, (uint8_t) local_apic_id, isa_irq_flags[irq]);

}

static void parse_nmi_entry (madt_entry_nmi_t *entry, unsigned *id) {

    int64_t intno, flags;
    char buffer[30];
    
    snprintf (buffer, 30, ">/nmi/%u", (*id)++);
    ipc_set_task_working_node (buffer, 1);
    
    intno = entry->intno;
    ipc_set_int (">/int", &intno, IPC_UINT);
    
    flags = entry->flags;               /* Use defines in madt.h to get additional info */
    ipc_set_int (">/flags", &flags, IPC_QWORD);
    
    if (ioapic_count) {
        ioapic_route_nmi (entry->intno, entry->flags);
    }

}

static void parse_lapic_nmi_entry (madt_entry_lapic_nmi_t *entry, unsigned *id) {

    int64_t processor, flags, lint;
    char buffer[30];
    
    snprintf (buffer, 30, ">/lapic_nmi/%u", (*id)++);
    ipc_set_task_working_node (buffer, 1);
    
    processor = entry->processor_id;
    ipc_set_int (">/processor", &processor, IPC_UINT);
    
    flags = entry->flags;               /* Use defines in madt.h to get additional info */
    ipc_set_int (">/flags", &flags, IPC_QWORD);
    
    lint = entry->lint_pin;
    ipc_set_int (">/lint", &lint, IPC_UINT);
    
    if (lapic_addr && (entry->processor_id == 0xFF || entry->processor_id == acpi_processor_id)) {
    
        uint32_t value = (4 << 8);
        
        if ((entry->flags & 3) == 3) {
            value |= (1 << 13);
        }
        
        if (((entry->flags >> 2) & 3) == 3) {
            value |= (1 << 15);
        }
        
        if (entry->lint_pin == 0) {
            lapic_write (0x350, value);
        } else if (entry->lint_pin == 1) {
            lapic_write (0x360, value);
        }
    
    }

}

extern int cpu_has_apic (void);

static void acpi_parser_madt (acpi_header_t *table) {

    int64_t local_addr, is_compatible;
    
    char *path;
    size_t base_path_length;
    
    unsigned ioapicid = 0, lapicid = 0, intoverrideid = 0, nmiid = 0, lapicnmiid = 0;
    
    uint8_t *it;
    unsigned isa_irq;
    
    madt_table_t *tbl;
    acpi_sci_gsi = acpi_sci_irq;
    
    for (isa_irq = 0; isa_irq < 16; isa_irq++) {
    
        isa_irq_gsi[isa_irq] = isa_irq;
        isa_irq_flags[isa_irq] = 0;
    
    }
    
    base_path_length = strlen (acpi_parser_base_path) + strlen (madtPath);
    path = kmalloc (base_path_length + 1);
    
    strcpy (path, acpi_parser_base_path);
    strcat (path, madtPath);
    
    if (ipc_get_node (path)) {
        ipc_delete_key (path);
    }
    
    ipc_set_task_working_node (path, 1);
    
    tbl = (void *) table;
    local_addr = tbl->local_apic_addr;
    
    for (it = (uint8_t *) table + sizeof (madt_table_t); it + sizeof (madt_table_entry_header_t) <= (uint8_t *) table + table->len; it += ((madt_table_entry_header_t *) it)->length) {
    
        madt_table_entry_header_t *header = (madt_table_entry_header_t *) it;
        
        if (header->length < sizeof (madt_table_entry_header_t) || it + header->length > (uint8_t *) table + table->len) {
            break;
        }
        
        if (header->type == MADT_LOCAL_APIC_ADDRESS_OVERRIDE && header->length >= sizeof (madt_entry_lapic_addr_override_t)) {
        
            local_addr = ((madt_entry_lapic_addr_override_t *) it)->addr;
            break;
        
        }
    
    }
    
    ipc_set_int (">/lapic_addr", &local_addr, IPC_QWORD);
    lapic_addr = (volatile uint8_t *) (uintptr_t) local_addr;
    
#if     defined (__AMD64__)
    {
    
        uint32_t eax, ebx, ecx, edx;
        uint64_t apic_base;
        
        eax = 1;
        
        __asm__ __volatile__ (
            "cpuid"
            : "+a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
        );
        
        if (ecx & (1 << 21)) {
        
            uint32_t lo, hi;
            
            __asm__ __volatile__ (
                "rdmsr"
                : "=a" (lo), "=d" (hi)
                : "c" ((uint32_t) 0x1B)
            );
            
            apic_base = ((uint64_t) hi << 32) | lo;
            x2apic_enabled = (apic_base & (1 << 10)) != 0;
            
            if (x2apic_enabled) {
            
                __asm__ __volatile__ (
                    "rdmsr"
                    : "=a" (lo), "=d" (hi)
                    : "c" ((uint32_t) 0x802)
                );
                
                local_apic_id = lo;
            
            } else {
                local_apic_id = lapic_read (0x020) >> 24;
            }
        
        } else {
            local_apic_id = lapic_read (0x020) >> 24;
        }
    
    }
#else
    local_apic_id = lapic_read (0x020) >> 24;
    
    if (cpu_has_apic ()) {
    
        /**
         * Enable the local APIC.
         *
         * Vector 0x27 is used for the spurious interrupt.
         */
        lapic_write (0x0F0, 0x127);
    
    }
#endif
    
    is_compatible = (tbl->flags & MADT_FLAGS_PCAT_COMPAT) ? 1 : 0;
    ipc_set_int (">/has_dualpic", &is_compatible, IPC_UINT);
    
    it = (uint8_t *) table + sizeof (madt_table_t);
    
    for (; it + sizeof (madt_table_entry_header_t) <= (uint8_t *) table + table->len; it += ((madt_table_entry_header_t *) it)->length) {
    
        madt_table_entry_header_t *header = (madt_table_entry_header_t *) it;
        
        if (header->length < sizeof (madt_table_entry_header_t) || it + header->length > (uint8_t *) table + table->len) {
            break;
        }
        
        switch (header->type) {
        
            case MADT_LOCAL_APIC:
            
                if (header->length < sizeof (madt_entry_local_apic_t)) {
                    break;
                }
                
                parse_lapic_entry ((madt_entry_local_apic_t *) it, &lapicid);
                
                ipc_set_task_working_node (path, 0);
                break;
            
            case MADT_IO_APIC:
            
                if (header->length < sizeof (madt_entry_io_apic_t)) {
                    break;
                }
                
                parse_io_acpi_entry ((madt_entry_io_apic_t *) it, &ioapicid);
                
                ipc_set_task_working_node (path, 0);
                break;
            
            case MADT_INTERRUPT_SOURCE_OVERRIDE:
            
                if (header->length < sizeof (madt_entry_int_override_t)) {
                    break;
                }
                
                parse_int_override_entry ((madt_entry_int_override_t *) it, &intoverrideid);
                
                ipc_set_task_working_node (path, 0);
                break;
            
            case MADT_NMI:
            
                if (header->length < sizeof (madt_entry_nmi_t)) {
                    break;
                }
                
                parse_nmi_entry ((madt_entry_nmi_t *) it, &nmiid);
                
                ipc_set_task_working_node (path, 0);
                break;
            
            case MADT_LOCAL_APIC_NMI:
            
                if (header->length < sizeof (madt_entry_lapic_nmi_t)) {
                    break;
                }
                
                parse_lapic_nmi_entry ((madt_entry_lapic_nmi_t *) it, &lapicnmiid);
                
                ipc_set_task_working_node (path, 0);
                break;
            
            case MADT_LOCAL_APIC_ADDRESS_OVERRIDE:
            
                if (header->length < sizeof (madt_entry_lapic_addr_override_t)) {
                    break;
                }
                
                /* ACPI Spec 5.2.12.8 Local APIC Address Override Structure */
                {
                
                    int64_t newaddr = ((madt_entry_lapic_addr_override_t *) it)->addr;
                    ipc_set_int (">/lapic_addr", &newaddr, IPC_QWORD);
                
                }
                
                break;
            
            case MADT_LOCAL_X2APIC:
            
                if (header->length < sizeof (madt_entry_local_x2apic_t)) {
                    break;
                }
                
                parse_x2apic_entry ((madt_entry_local_x2apic_t *) it, &lapicid);
                
                ipc_set_task_working_node (path, 0);
                break;
            
            case MADT_LOCAL_X2APIC_NMI:
            
                if (header->length < sizeof (madt_entry_local_x2apic_nmi_t)) {
                    break;
                }
                
                parse_x2apic_nmi_entry ((madt_entry_local_x2apic_nmi_t *) it, &lapicnmiid);
                
                ipc_set_task_working_node (path, 0);
                break;
            
            case MADT_MULTIPROCESSOR_WAKEUP:
            
                if (header->length < sizeof (madt_entry_multiprocessor_wakeup_t)) {
                    break;
                }
                
                {
                
                    madt_entry_multiprocessor_wakeup_t *entry = (madt_entry_multiprocessor_wakeup_t *) it;
                    
                    int64_t version = entry->mailbox_version;
                    int64_t address = entry->mailbox_address;
                    
                    ipc_set_int (">/wakeup_mailbox_version", &version, IPC_UINT);
                    ipc_set_int (">/wakeup_mailbox_address", &address, IPC_QWORD);
                
                }
                
                break;
        
        }
    
    }
    
    acpi_sci_ioapic_routed = 0;
    
    if (ioapic_count) {
        acpi_sci_ioapic_routed = ioapic_route (acpi_sci_gsi, 32 + acpi_sci_irq, local_apic_id, acpi_sci_flags);
    }
    
#if     defined (__AMD64__)
    if (local_addr) {
        lapic_timer_initialize (is_compatible);
    }
#endif

}

acpi_parser_t acpi_base_parsers[] = {

    { "FACP",   &acpi_parser_fadt },
    { "APIC",   &acpi_parser_madt },
    { "",       0                 }

};

static const char *ACPI_RSDP_SIG = "RSD PTR ";

/**
 * ACPI defines the RSDT address as a 32-bit physical address even on AMD64.
 * Do not use void  *here or the structure layout changes between i386/AMD64.
 */
__attribute__((packed)) typedef struct {

    char sig[ACPI_RSDP_SIG_LEN];
    
    uint8_t checksum;
    char oem_id[ACPI_OEMID_LEN];
    
    uint8_t rev;
    uint32_t root_system_table;
    
    uint32_t length;
    uint64_t extended_root_system_table_addr;
    
    uint8_t checksum_ex;
    uint8_t reserved[3];

} acpi_rsdp_t;

static acpi_rsdp_t *find_root_system_descriptor_pointer (void *itbeg, void *itend) {

    uint8_t *it;
    
    for (it = (uint8_t *) itbeg; it < (uint8_t *) itend; it += 16) {
    
        acpi_rsdp_t *rsdp = (acpi_rsdp_t *) it;
        
        if (memcmp (rsdp->sig, ACPI_RSDP_SIG, ACPI_RSDP_SIG_LEN) != 0) {
            continue;
        }
        
        if (!acpi_checksum (rsdp, ACPI_RSDP_V1_LEN)) {
            continue;
        }
        
        if (rsdp->rev >= 2) {
        
            if (rsdp->length < sizeof (acpi_rsdp_t)) {
                continue;
            }
            
            if (!acpi_checksum (rsdp, rsdp->length)) {
                continue;
            }
        
        }
        
        return rsdp;
    
    }
    
    return 0;

}

/**
 * Legacy BIOS discovery.
 *
 * The word at 0x40E is an EBDA segment, so convert segment:0 to a physical
 * address by shifting left four bits.
 *
 * For EFI boot, the boot loader should pass the RSDP obtained from the EFI
 * configuration table instead of relying on this legacy scan.
 */
static acpi_rsdp_t *get_root_system_descriptor_pointer (void) {

    uintptr_t ebda_addr = ((uintptr_t) (*(uint16_t *) 0x40E)) << 4;
    acpi_rsdp_t *ret;
    
    if (ebda_addr) {
    
        ret = find_root_system_descriptor_pointer ((void *) ebda_addr, (void *) (ebda_addr + 1024));
        
        if (ret) {
            return ret;
        }
    
    }
    
    ret = find_root_system_descriptor_pointer (BIOS_BEGIN, BIOS_END);
    
    if (ret) {
        return ret;
    }
    
    /* Keep the original fixed EBDA-area fallback as a last resort. */
    return find_root_system_descriptor_pointer (EBDA_BEGIN, EBDA_END);

}

const char ACPI_TABSIG_RSDT[ACPI_TABLE_SIG_LEN] = "RSDT";
const char ACPI_TABSIG_XSDT[ACPI_TABLE_SIG_LEN] = "XSDT";

__attribute__((packed)) typedef struct {
    acpi_header_t header;
} acpi_rsdt_t;

__attribute__((packed)) typedef struct {
    acpi_header_t header;
} acpi_xsdt_t;

__attribute__((packed)) typedef struct {

    char *signature;
    
    /**
     * Physical address, not a directly dereferenceable C pointer.
     * uint64_t is used so XSDT entries are representable on both builds.
     */
    uint64_t begin;
    uint32_t len;

} acpi_table_t;

static acpi_table_t *acpi_analyze_table (uint64_t paddr) {

    uintptr_t addr;
    uintptr_t offset;
    
    acpi_header_t *header;
    acpi_table_t *entry;
    
    uint8_t *mapped;
    
    if (!acpi_phys_address_usable (paddr)) {
        return 0;
    }
    
    addr = (uintptr_t) paddr;
    offset = addr & (PAGE_SIZE - 1);
    
    if (!(mapped = acpi_try_alloc (paddr))) {
        return 0;
    }
    
    header = (acpi_header_t *) (mapped + offset);
    entry = kmalloc (sizeof (acpi_table_t));
    
    entry->begin = paddr;
    entry->len = header->len;
    
    entry->signature = kmalloc (ACPI_TABLE_SIG_LEN + 1);
    
    memcpy (entry->signature, &header->signature, ACPI_TABLE_SIG_LEN);
    entry->signature[ACPI_TABLE_SIG_LEN] = '\0';
    
    return entry;

}

static void acpi_parse_root_table (uint64_t paddr, int xsdt, list_t *list) {

    uintptr_t addr;
    uintptr_t offset;
    
    uint32_t headerPages;
    uint32_t pages;
    
    uint32_t entrySize;
    uint32_t entries;
    
    uint32_t i;
    
    uint8_t *mapped;
    acpi_header_t *header;
    
    if (!acpi_phys_address_usable (paddr)) {
        return;
    }
    
    addr = (uintptr_t) paddr;
    
    offset = addr & (PAGE_SIZE - 1);
    headerPages = (uint32_t) (align_up (offset + sizeof (acpi_header_t), PAGE_SIZE) / PAGE_SIZE);
    
    if (!(mapped = acpi_try_alloc (paddr))) {
        return;
    }
    
    header = (acpi_header_t *) (mapped + offset);
    
    if (xsdt) {
    
        if (memcmp (header->signature, ACPI_TABSIG_XSDT, ACPI_TABLE_SIG_LEN) != 0) {
            return;
        }
    
    } else {
    
        if (memcmp (header->signature, ACPI_TABSIG_RSDT, ACPI_TABLE_SIG_LEN) != 0) {
            return;
        }
    
    }
    
    if (header->len < sizeof (acpi_header_t)) {
        return;
    }
    
    pages = (uint32_t) (align_up (offset + header->len, PAGE_SIZE) / PAGE_SIZE);
    
    if (pages != headerPages) {
    
        if (!(mapped = acpi_try_alloc (paddr))) {
            return;
        }
        
        header = (acpi_header_t *) (mapped + offset);
    
    }
    
    if (!acpi_checksum (header, header->len)) {
        return;
    }
    
    entrySize = xsdt ? sizeof (uint64_t) : sizeof (uint32_t);
    entries = (header->len - sizeof (acpi_header_t)) / entrySize;
    
    for (i = 0; i < entries; ++i) {
    
        uint8_t *entryPtr = (uint8_t *) header + sizeof (acpi_header_t) + i  *entrySize;
        uint64_t tableAddr;
        
        if (xsdt) {
        
            /**
             * ACPI tables are packed. memcpy avoids an unaligned uint64_t
             * dereference on the XSDT entry.
             */
            memcpy (&tableAddr, entryPtr, sizeof (tableAddr));
        
        } else {
        
            uint32_t tableAddr32;
            memcpy (&tableAddr32, entryPtr, sizeof (tableAddr32));
            
            tableAddr = tableAddr32;
        
        }
        
        if (tableAddr != 0 && acpi_phys_address_usable (tableAddr)) {
        
            acpi_table_t *entry = acpi_analyze_table (tableAddr);
            if (entry) { list_append (list, entry); }
        
        }
    
    }

}

/* Preserve the original externally visible RSDT parser. */
void acpi_parse_rsdt (acpi_rsdt_t *tbl, list_t *list) {
    acpi_parse_root_table ((uint64_t) (uintptr_t) tbl, 0, list);
}

static acpi_rsdp_t rdsp_data = { 0 };
static acpi_rsdp_t *rdsp = 0;

static list_t tables = { 0 };
static list_t parsers = { 0 };

int acpi_available (void) {

    if (!rdsp) {
        rdsp = get_root_system_descriptor_pointer ();
    }
    
    return rdsp != 0;

}

/**
 * This is useful for EFI or another boot protocol which already discovered
 * the RSDP. The pointer supplied here must be mapped and accessible.
 */
void acpi_set_rsdp (void *ptr) {

    memcpy (&rdsp_data, ptr, sizeof (rdsp_data));
    rdsp = &rdsp_data;

}

static void acpi_parse_root (list_t *list) {

    /**
     * ACPI 2.0+ prefers XSDT. On i386, use it only if the XSDT itself is
     * addressable; otherwise fall back to the 32-bit RSDT.
     */
    if (rdsp->rev >= 2 && rdsp->extended_root_system_table_addr != 0 && acpi_phys_address_usable (rdsp->extended_root_system_table_addr)) {
    
        acpi_parse_root_table (rdsp->extended_root_system_table_addr, 1, list);
        return;
    
    }
    
    if (rdsp->root_system_table != 0) {
        acpi_parse_root_table (rdsp->root_system_table, 0, list);
    }

}

void acpi_install (void) {

    acpi_parser_t *it;
    
    for (it = acpi_base_parsers; it->parser != 0; ++it) {
        list_append (&parsers, it);
    }
    
    if (!rdsp) {
        rdsp = get_root_system_descriptor_pointer ();
    }
    
    if (rdsp) {
        acpi_parse_root (&tables);
    }
    
    acpi_reparse ();
    
    if (acpi_sci_irq) {
    
        acpi_thread = thread_create_kthread (acpi_sci_thread);
        
        if (acpi_thread) {
            acpi_thread->state = TS_WAITIO;
        }
    
    }

}

int acpi_register_parser (const char *signature, void (*parser) (acpi_header_t *table)) {

    acpi_parser_t entry = {signature, parser};
    
    acpi_parser_t *lstentry;
    list_node_t *it;
    
    /* Avoid double-registering the same parser */
    for (it = parsers.head; it; it = it->next) {
    
        if (memcmp (it->value, &entry, sizeof (acpi_parser_t)) == 0) {
            return 0;
        }
    
    }
    
    lstentry = kmalloc (sizeof (acpi_parser_t));
    memcpy (lstentry, &entry, sizeof (acpi_parser_t));
    
    list_append (&parsers, lstentry);
    return 1;

}

int acpi_unregister_parser (const char *signature, void (*parser) (acpi_header_t *)) {

    acpi_parser_t entry = {signature, parser};
    list_node_t *it;
    
    for (it = parsers.head; it; it = it->next) {
    
        if (memcmp(it->value, &entry, sizeof (acpi_parser_t)) == 0) {
        
            kfree (it->value);
            
            list_remove (&parsers, it);
            return 1;
        
        }
    
    }
    
    return 0;

}

void acpi_reload (void) {

    list_node_t *it;
    
    for (it = tables.head; it != 0; it = it->next) {
    
        acpi_table_t *tbl = it->value;
        
        kfree (tbl->signature);
        kfree (tbl);
    
    }
    
    list_destruct (&tables);
    list_construct (&tables);
    
    if (rdsp != 0) {
        acpi_parse_root (&tables);
    }
    
    acpi_reparse ();

}

static void pass_to_parser (acpi_table_t *table) {

    uintptr_t addr;
    uintptr_t offset;
    
    list_node_t *it;
    acpi_header_t *hdr;
    
    uint8_t *mapped;
    
    if (!acpi_phys_address_usable (table->begin)) {
        return;
    }
    
    addr = (uintptr_t) table->begin;
    offset = addr & (PAGE_SIZE - 1);
    
    if (!(mapped = acpi_try_alloc (table->begin))) {
        return;
    }
    
    hdr = (acpi_header_t *) (mapped + offset);
    
    for (it = parsers.head; it; it = it->next) {
    
        acpi_parser_t *parser = it->value;
        
        if (strcmp (table->signature, parser->signature) == 0) {
            parser->parser (hdr);
        }
    
    }

}

void acpi_reparse (void) {

    list_node_t *it;
    
    /**
     * Load all SSDT AML before parsing the FADT. The FADT setup searches
     * the AML namespace for _Lxx/_Exx methods when enabling GPEs.
     */
    for (it = tables.head; it != 0; it = it->next) {
    
        acpi_table_t *table = it->value;
        
        if (strcmp (table->signature, "SSDT") == 0) {
            pass_to_parser (table);
        }
    
    }
    
    acpi_parse_specific ("FACP");
    
    for (it = tables.head; it != 0; it = it->next) {
    
        acpi_table_t *table = it->value;
        
        if (strcmp (table->signature, "FACP") != 0 && strcmp (table->signature, "SSDT") != 0) {
            pass_to_parser (table);
        }

    }
    
    if (acpi_sci_irq) {
        irq_install_handler (acpi_sci_irq, acpi_sci_handler, "acpi");
    }

}

void acpi_parse_specific (const char *signtature) {

    list_node_t *it;
    
    for (it = tables.head; it != 0; it = it->next) {
    
        acpi_table_t *table = it->value;
        
        if (strcmp (table->signature, signtature) == 0) {
            pass_to_parser (table);
        }
    
    }

}
