/******************************************************************************
 * @file            dma.h
 *****************************************************************************/
#ifndef     _DMA_H
#define     _DMA_H

#include    <stdint.h>

#define     DMA_MASKCHANNEL_MASTER      0x0A
#define     DMA_TRANSFERMODE_MASTER     0x0B
#define     DMA_FLIPFLOP_MASTER         0x0C
#define     DMA_RESET_MASTER            0x0D

#define     DMA_MASKCHANNEL_SLAVE       0xD4
#define     DMA_TRANSFERMODE_SLAVE      0xD6
#define     DMA_FLIPFLOP_SLAVE          0xD8
#define     DMA_RESET_SLAVE             0xDA

#define     MASK                        (1U << 2)

typedef enum {

    DMA_WRITE     = (1U << 2),
    DMA_READ      = (1U << 3),
    DMA_AUTOINIT  = (1U << 4),
    DMA_INCREMENT = 0,
    DMA_DECREMENT = (1U << 5),
    DMA_DEMAND    = 0,
    DMA_SINGLE    = (1U << 6),
    DMA_BLOCK     = (1U << 7)

} DMA_TRANSFERMODE_t;

typedef struct {

    uint16_t pagePort;
    uint16_t addressPort;
    uint16_t counterPort;
    
    uint8_t  portNum;

} dma_channel_t;

extern const dma_channel_t dma_channel[4];

void dma_read (void *dest, uint16_t length, dma_channel_t *channel, DMA_TRANSFERMODE_t mode);
void dma_write (void *src, uint16_t length, dma_channel_t *channel, DMA_TRANSFERMODE_t mode);

#endif      /* _DMA_H */
