/******************************************************************************
 * @file            screen.c
 *****************************************************************************/
#include    <kaishi/efi.h>

extern EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;

size_t get_screen_height (void) {

    UINTN columns, rows;
    
    ConOut->QueryMode (ConOut, ConOut->Mode->Mode, &columns, &rows);
    return rows;

}

size_t get_screen_width (void) {

    UINTN columns, rows;
    
    ConOut->QueryMode (ConOut, ConOut->Mode->Mode, &columns, &rows);
    return columns;

}
