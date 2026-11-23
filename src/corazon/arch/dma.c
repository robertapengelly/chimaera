/******************************************************************************
 * @file            dma.c
 *****************************************************************************/
#include    <corazon/dma.h>

const dma_channel_t dma_channel[4] = {

    { 0x87, 0x00, 0x01, 0 },
    { 0x83, 0x02, 0x03, 1 },
    { 0x81, 0x04, 0x05, 2 },
    { 0x82, 0x06, 0x07, 3 }

};

#define     outportb(port, val)         __asm__ __volatile__ ("outb %0, %1" : : "a" ((uint8_t) (val)), "d" ((uint16_t) (port)))

static void dma_action (void *address, uint16_t length, const dma_channel_t *channel, uint8_t mode) {

    length--;                           /* ISA DMA counts from 0 */

    outportb (DMA_MASKCHANNEL_MASTER,  MASK | channel->portNum   );             // Mask channel: bit2: 1 deactivate 0 activate, bit 0-1: portNum
    outportb (channel->pagePort,       (uintptr_t) address >> 16 );             // Address: Bits 16-23 (External page register. Allows us to address up to 16 MiB)
    outportb (DMA_FLIPFLOP_MASTER,     0                         );             // Reset flip-flop
    outportb (channel->addressPort,    (uintptr_t) address       );             // Address: Bits 0-7
    outportb (channel->addressPort,    (uintptr_t) address >> 8  );             // Address: Bits 8-15
    outportb (DMA_FLIPFLOP_MASTER,     0                         );             // Reset flip-flop
    outportb (channel->counterPort,    length                    );             // Length: Lower byte
    outportb (channel->counterPort,    length >> 8               );             // Length: Higher byte
    outportb (DMA_TRANSFERMODE_MASTER, mode | channel->portNum   );             // Transfer Mode
    outportb (DMA_MASKCHANNEL_MASTER,  channel->portNum          );             // Unmask channel: bit2: 1 deactivate 0 activate, bit 0-1: portNum

}

// Prepare the DMA for the read transfer from hardware
void dma_read (void *dest, uint16_t length, dma_channel_t *channel, DMA_TRANSFERMODE_t mode) {
    dma_action (dest, length, channel, mode | DMA_WRITE);
}

// Prepare the DMA for the write transfer to hardware
void dma_write (void *src, uint16_t length, dma_channel_t *channel, DMA_TRANSFERMODE_t mode) {
    dma_action ((void *) src, length, channel, mode | DMA_READ);
}
