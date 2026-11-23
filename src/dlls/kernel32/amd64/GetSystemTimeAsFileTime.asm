;******************************************************************************
; @file             GetSystemTimeAsFileTime.asm
;******************************************************************************

;******************************************************************************
; @function        GetSystemTimeAsFileTime
;******************************************************************************
global      GetSystemTimeAsFileTime
GetSystemTimeAsFileTime:

    sub     rsp,    40
    call    ZwQuerySystemTime
    
    add     rsp,    40
    ret
