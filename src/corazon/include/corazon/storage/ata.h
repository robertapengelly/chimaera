/******************************************************************************
 * @file            ata.h
 *****************************************************************************/
#ifndef     _ATA_H
#define     _ATA_H

#include    <stddef.h>
#include    <stdint.h>

struct ata_regs {

    uint16_t base;
    uint16_t data;
    uint16_t feature;
    uint16_t error;
    uint16_t sectors;
    uint16_t lbalo;
    uint16_t lbamid;
    uint16_t lbahi;
    uint16_t drive;
    uint16_t status;
    uint16_t cmd;
    uint16_t devcontrol;
    uint16_t altstatus;

};

#define     ATA_REG_DATA                0
#define     ATA_REG_FEATURE             1
#define     ATA_REG_ERROR_INFO          1
#define     ATA_REG_SECTOR_COUNT        2
#define     ATA_REG_LBALO               3
#define     ATA_REG_LBAMID              4
#define     ATA_REG_LBAHI               5
#define     ATA_REG_DRIVE               6
#define     ATA_REG_STATUS              7
#define     ATA_REG_CMD                 7

struct ata_port_info {

    int channel;
    int irq;
    
    struct ata_regs regs;
    int slave;

};

int ata_get_reg_ports (uint16_t channel, struct ata_regs *regs);

int ata_is_slave (uint16_t channel, int *slave);
int ata_identify (int channel, uint16_t *output);

int ata_accesss_sectors (uint64_t sector, size_t count, void *scratch, struct ata_port_info *port, int access);
int ata_get_irq (uint16_t channel, int *irq);

#endif      /* _ATA_H */
