/******************************************************************************
 * @file            console.c
 *****************************************************************************/
#include    <kaishi/pc/bios.h>

#include    <stdint.h>
#include    <string.h>

void console_clear (void) {

    REGS regs = { 0 };
    
    regs.w.ax = 0x0600;
    regs.b.bh = 0x07;
    regs.b.dh = 0x19;
    regs.b.dl = 0x50;
    
    int386 (0x10, &regs, &regs);
    memset (&regs, 0, sizeof (regs));
    
    regs.b.ah = 0x02;
    int386 (0x10, &regs, &regs);

}

void console_clear_line (int x, int y) {

    REGS regs = { 0 };
    
    regs.b.ah = 0x02;
    regs.b.dl = x;
    regs.b.dh = y;
    
    int386 (0x10, &regs, &regs);
    memset (&regs, 0, sizeof (&regs));
    
    regs.w.ax = 0x0920;
    regs.b.bl = 0x07;
    regs.w.cx = 80;
    
    int386 (0x10, &regs, &regs);

}

void console_get_cursor (int *x, int *y) {

    REGS regs = { 0 };
    
    regs.b.ah = 0x03;
    int386 (0x10, &regs, &regs);
    
    *x = regs.b.dl;
    *y = regs.b.dh;

}

void console_hide_cursor (void) {

    REGS regs = { 0 };
    
    regs.b.ah = 0x01;
    regs.w.cx = 0x2607;
    
    int386 (0x10, &regs, &regs);

}

void console_highlight (int x, int y, int padding) {

    REGS regs = { 0 };
    
    regs.b.ah = 0x02;
    regs.b.dh = y;
    
    regs.b.dl = x + padding;
    int386 (0x10, &regs, &regs);
    
    memset (&regs, 0, sizeof (&regs));
    
    regs.w.ax = 0x0920;
    regs.b.bl = 0x70;
    
    regs.w.cx = 80 - (padding * 2);
    int386 (0x10, &regs, &regs);
    
    memset (&regs, 0, sizeof (&regs));
    
    regs.b.ah = 0x03;
    int386 (0x10, &regs, &regs);
    
    regs.b.ah = 0x02;
    
    regs.b.dl = x + padding;
    int386 (0x10, &regs, &regs);

}

void console_put_char (char ch) {

    REGS regs = { 0 };
    
    if (ch == '\n') {
        console_put_char ('\r');
    }
    
    regs.b.ah = 0x0E;
    regs.b.al = ch;
    regs.w.bx = 1;
    
    int386 (0x10, &regs, &regs);
    return;

}

void console_reset_colors (void) {
    /* This is just to stop the compiler complaining as there's nothing we really need to do here. */
}

void console_show_cursor (void) {

    REGS regs = { 0 };
    
    regs.b.ah = 0x01;
    regs.w.cx = 0x0607;
    
    int386 (0x10, &regs, &regs);

}
