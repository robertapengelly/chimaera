/******************************************************************************
 * @file            kbd.c
 *****************************************************************************/
#include    <kaishi/efi.h>

extern EFI_BOOT_SERVICES *BootServices;
extern EFI_SIMPLE_TEXT_INPUT_PROTOCOL *ConIn;

int kbd_get_key (void) {

    static EFI_INPUT_KEY key = { 0 };
    
    if (EFI_ERROR (ConIn->ReadKeyStroke (ConIn, &key))) {
        return 0;
    }
    
    if (key.UnicodeChar == L'\r') {
        return '\r';
    }
    
    return key.ScanCode;

}

int kbd_wait_key (void) {

    static EFI_INPUT_KEY key = { 0 };
    
    EFI_EVENT events[1] = { 0 };
    UINTN index = 0;
    
    events[0] = ConIn->WaitForKey;
    BootServices->WaitForEvent (1, events, &index);
    
    ConIn->ReadKeyStroke (ConIn, &key);
    
    if (key.UnicodeChar == L'\r') {
        return '\r';
    }
    
    return key.ScanCode;

}
