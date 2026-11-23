/******************************************************************************
 * @file            acpi.h
 *****************************************************************************/
#ifndef     _ACPI_H
#define     _ACPI_H

#include    <stdint.h>

#define     ACPI_TABLE_SIG_LEN          4   /* Generic table signature length */
#define     ACPI_OEMID_LEN              6   /* OemID length */
#define     ACPI_OEMTBLID_LEN           8   /* Oem table ID length */

/* Generic header of every table */
__attribute__((packed)) typedef struct {

    char signature[ACPI_TABLE_SIG_LEN];     /* Table-specific ASCII-Signature (e.g. 'APIC') */
    uint32_t len;                           /* Size of the whole table including the header */
    
    uint8_t rev;
    uint8_t checksum;                       /* Whole table must add up to 0 to be valid */
    
    char oem_id[ACPI_OEMID_LEN];
    char oem_table_iD[ACPI_OEMTBLID_LEN];
    
    uint32_t oemRev;
    uint32_t creatorId;
    uint32_t creatorRev;

} acpi_header_t;

void acpi_lapic_ack (void);

int acpi_route_legacy_irq (uint8_t irq);
int acpi_available (void);

void acpi_set_rsdp (void *ptr);
void acpi_install (void);

int acpi_register_parser (const char* signature, void(*parser) (acpi_header_t* table));
int acpi_unregister_parser (const char* signature, void(*parser) (acpi_header_t*));

void acpi_reload (void);                            /* Reloads the ACPI completely including re-running the RSDT parser */
void acpi_reparse (void);                           /* Re-runs the parsers for the sub-tables only */

void acpi_parse_specific (const char* signtature);  /* Re-Parses a specific table by signature */

typedef struct {

    const char* signature;
    void(*parser) (acpi_header_t*);

} acpi_parser_t;

#endif      /* _ACPI_H */
