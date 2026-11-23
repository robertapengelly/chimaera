/******************************************************************************
 * @file            kbd.c
 *****************************************************************************/
#include    <kaishi/pc/bios.h>
#include    <string.h>

static int bios_to_scancode (uint8_t scan) {

    switch (scan) {
    
        case 0x48: return 0x01;      /* SCAN_UP */
        case 0x50: return 0x02;      /* SCAN_DOWN */
        case 0x4D: return 0x03;      /* SCAN_RIGHT */
        case 0x4B: return 0x04;      /* SCAN_LEFT */
        
        case 0x47: return 0x05;      /* SCAN_HOME */
        case 0x4F: return 0x06;      /* SCAN_END */
        case 0x52: return 0x07;      /* SCAN_INSERT */
        case 0x53: return 0x08;      /* SCAN_DELETE */
        case 0x49: return 0x09;      /* SCAN_PAGE_UP */
        case 0x51: return 0x0A;      /* SCAN_PAGE_DOWN */
        
        case 0x3B: return 0x0B;      /* SCAN_F1 */
        case 0x3C: return 0x0C;      /* SCAN_F2 */
        case 0x3D: return 0x0D;      /* SCAN_F3 */
        case 0x3E: return 0x0E;      /* SCAN_F4 */
        case 0x3F: return 0x0F;      /* SCAN_F5 */
        case 0x40: return 0x10;      /* SCAN_F6 */
        case 0x41: return 0x11;      /* SCAN_F7 */
        case 0x42: return 0x12;      /* SCAN_F8 */
        case 0x43: return 0x13;      /* SCAN_F9 */
        case 0x44: return 0x14;      /* SCAN_F10 */
        case 0x85: return 0x15;      /* SCAN_F11 */
        case 0x86: return 0x16;      /* SCAN_F12 */
        
        case 0x01: return 0x17;      /* SCAN_ESC */
    
    }
    
    return 0;

}

int kbd_get_key (void) {

    REGS regs = { 0 };
    int scancode;
    
    regs.b.ah = 0x01;
    int386 (0x16, &regs, &regs);
    
    if (regs.w.flags & 0x40) {
        return 0;
    }
    
    memset (&regs, 0, sizeof (regs));
    int386 (0x16, &regs, &regs);
    
    if ((scancode = bios_to_scancode (regs.b.ah))) {
        return scancode;
    }
    
    return regs.b.al;

}

int kbd_wait_key (void) {

    REGS regs = { 0 };
    int scancode;
    
    int386 (0x16, &regs, &regs);
    
    if ((scancode = bios_to_scancode (regs.b.ah))) {
        return scancode;
    }
    
    return regs.b.al;

}
