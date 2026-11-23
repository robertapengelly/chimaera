/******************************************************************************
 * @file            console.c
 *****************************************************************************/
#include    <kaishi/efi.h>

extern EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;

#define     EFI_BLACK                   0x00
#define     EFI_BLUE                    0x01
#define     EFI_GREEN                   0x02
#define     EFI_RED                     0x04
#define     EFI_LIGHTGRAY               (EFI_BLUE | EFI_GREEN | EFI_RED)

#define     EFI_TEXT_ATTR(f,b)          ((f) | ((b) << 4))

void console_clear (void) {
    ConOut->ClearScreen (ConOut);
}

void console_clear_line (int x, int y) {

    CHAR16 space[2] = { L' ', 0 };
    UINTN columns, rows;
    
    ConOut->QueryMode (ConOut, ConOut->Mode->Mode, &columns, &rows);
    ConOut->SetCursorPosition (ConOut, (UINTN) x, (UINTN) y);
    
    while (columns--) {
        ConOut->OutputString (ConOut, space);
    }
    
    ConOut->SetCursorPosition (ConOut, 0, (UINTN) y);

}

void console_get_cursor (int *x, int *y) {

    *x = ConOut->Mode->CursorColumn;
    *y = ConOut->Mode->CursorRow;

}

void console_hide_cursor (void) {
    ConOut->EnableCursor (ConOut, FALSE);
}

void console_highlight (int x, int y, int padding) {

    CHAR16 space[2] = { L' ', 0 };
    
    UINTN columns, rows;
    UINTN row;
    
    ConOut->SetCursorPosition (ConOut, (UINTN) (x + padding), (UINTN) y);
    
    ConOut->QueryMode (ConOut, ConOut->Mode->Mode, &columns, &rows);
    row = ConOut->Mode->CursorRow;
    
    ConOut->SetAttribute (ConOut, EFI_TEXT_ATTR (EFI_BLACK, EFI_LIGHTGRAY));
    columns -= (padding * 2);
    
    while (columns--) {
        ConOut->OutputString (ConOut, space);
    }
    
    ConOut->SetCursorPosition (ConOut, (UINTN) (x + padding), row);

}

void console_put_char (CHAR8 ch) {

    CHAR16 buf[2] = { 0 };
    
    if (ch == '\n') {
    
        buf[0] = L'\r';
        
        if (EFI_ERROR (ConOut->OutputString (ConOut, buf))) {
            return;
        }
    
    }
    
    buf[0] = (CHAR16) ch;
    ConOut->OutputString (ConOut, buf);

}

void console_reset_colors (void) {
    ConOut->SetAttribute (ConOut, EFI_TEXT_ATTR (EFI_LIGHTGRAY, EFI_BLACK));
}

void console_show_cursor (void) {
    ConOut->EnableCursor (ConOut, TRUE);
}
