;******************************************************************************
; @file             GetSystemTimeAsFileTime.asm
;******************************************************************************
.option     segment:use32
.386

;******************************************************************************
; @function        _GetSystemTimeAsFileTime
;******************************************************************************
global      _GetSystemTimeAsFileTime
_GetSystemTimeAsFileTime:

    push    dword ptr [esp + 4]
    call    _ZwQuerySystemTime@4
    
    ret     4
