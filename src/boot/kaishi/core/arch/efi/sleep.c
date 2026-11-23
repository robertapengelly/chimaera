/******************************************************************************
 * @file            sleep.c
 *****************************************************************************/
#include    <kaishi/efi.h>

extern EFI_BOOT_SERVICES *BootServices;
extern EFI_SIMPLE_TEXT_INPUT_PROTOCOL *ConIn;

int kbd_get_key (void);

int sleep (size_t seconds) {

    EFI_STATUS  Status;
    UINTN       Index;
    
    EFI_EVENT   TimerEvent;
    EFI_EVENT   Events[2];
    
    Status = BootServices->CreateEvent (EVT_TIMER, TPL_CALLBACK, NULL, NULL, &TimerEvent);
    
    if (EFI_ERROR (Status)) {
        return 0;
    }
    
    Status = BootServices->SetTimer (TimerEvent, TimerRelative, /* One-shot timer */ seconds * 10000000ULL /* Convert seconds to 100-ns units */ );
    
    if (EFI_ERROR (Status)) {
    
        BootServices->CloseEvent (TimerEvent);
        return 0;
    
    }
    
    Events[0] = TimerEvent;
    Events[1] = ConIn->WaitForKey;
    
    Status = BootServices->WaitForEvent (2, /* Number of events in the array */ &Events, /* Array of events */ &Index /* Index of the event that satisfied the wait */ );
    BootServices->CloseEvent (TimerEvent);
    
    if (EFI_ERROR (Status)) {
        return 0;
    }
    
    return kbd_get_key (); 

}
