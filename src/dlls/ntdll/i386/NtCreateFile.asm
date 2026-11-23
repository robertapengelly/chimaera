;******************************************************************************
; @file             NtCreateFile.asm
;******************************************************************************
.option     segment:use32
.386

%ifndef     HEX
% define        HEX(y)                  0x##y
%endif

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Helper defines
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%define     O_RDONLY                    0
%define     O_WRONLY                    1
%define     O_RDWR                      2

%define     O_APPEND                    02000
%define     O_CREAT                     0100
%define     O_TRUNC                     01000
%define     O_EXCL                      0200

%define     GENERIC_READ                HEX (80000000)
%define     GENERIC_WRITE               HEX (40000000)
%define     GENERIC_ALL                 HEX (10000000)

%define     FILE_READ_DATA              HEX (00000001)
%define     FILE_WRITE_DATA             HEX (00000002)
%define     FILE_APPEND_DATA            HEX (00000004)

%define     STATUS_SUCCESS                                  HEX (00000000)
%define     STATUS_UNSUCCESSFUL                             HEX (C0000001)
%define     STATUS_INVALID_PARAMETER                        HEX (C000000D)
%define     STATUS_OBJECT_NAME_INVALID                      HEX (C0000033)
%define     STATUS_OBJECT_NAME_NOT_FOUND                    HEX (C0000034)
%define     STATUS_NOT_SUPPORTED                            HEX (C00000BB)
%define     STATUS_IO_DEVICE_ERROR                          HEX (C0000185)
%define     STATUS_NO_MEMORY                                HEX (C0000017)
%define     STATUS_ACCESS_DENIED                            HEX (C0000022)
%define     STATUS_ACCESS_VIOLATION                         HEX (C0000005)
%define     STATUS_DEVICE_BUSY                              HEX (80000011)
%define     STATUS_OBJECT_NAME_COLLISION                    HEX (C0000035)
%define     STATUS_NO_SUCH_DEVICE                           HEX (C000000E)
%define     STATUS_NOT_A_DIRECTORY                          HEX (C0000103)
%define     STATUS_FILE_IS_A_DIRECTORY                      HEX (C00000BA)
%define     STATUS_TOO_MANY_OPENED_FILES                    HEX (C000011F)
%define     STATUS_DISK_FULL                                HEX (C000007F)
%define     STATUS_MEDIA_WRITE_PROTECTED                    HEX (C00000A2)
%define     STATUS_NAME_TOO_LONG                            HEX (C0000106)
%define     STATUS_NOT_IMPLEMENTED                          HEX (C0000002)
%define     STATUS_REPARSE_POINT_NOT_RESOLVED               HEX (C0000280)
%define     STATUS_INTEGER_OVERFLOW                         HEX (C0000095)

%define     FILE_OPENED                 1

%define     FILE_SUPERSEDE              0
%define     FILE_OPEN                   1
%define     FILE_CREATE                 2
%define     FILE_OPEN_IF                3
%define     FILE_OVERWRITE              4
%define     FILE_OVERWRITE_IF           5

;******************************************************************************
; @function        _NtCreateFile@44
;******************************************************************************
global      _NtCreateFile@44
_NtCreateFile@44:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Preserve the base pointer.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    ebp
    mov     ebp,    esp
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Reserve temporary pathname and saved argument pointers.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    sub     esp,    1040
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Preserve registers.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    ebx
    push    esi
    push    edi
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Save FileHandle and IoStatusBlock.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     eax,    dword ptr [ebp + 8]
    mov     dword ptr [ebp - 1028],    eax
    
    mov     eax,    dword ptr [ebp + 20]
    mov     dword ptr [ebp - 1032],    eax
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Validate the required NT pointers.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     ebx,    dword ptr [ebp + 16]
    
    cmp     dword ptr [ebp - 1028],    0
    je      .invalid
    
    test    ebx,    ebx
    jz      .invalid
    
    cmp     dword ptr [ebp - 1032],    0
    je      .invalid
    
    cmp     dword ptr [ebx],    24
    jb      .invalid
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Handle-relative names require NT object manager support.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    cmp     dword ptr [ebx + 4],    0
    jne     .not_supported
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Get OBJECT_ATTRIBUTES.ObjectName (PUNICODE_STRING).
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     esi,    dword ptr [ebx + 8]
    
    test    esi,    esi
    jz      .invalid
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; UNICODE_STRING.Length is a UTF-16 byte count, not a character count.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    movzx   ecx,    word ptr [esi]
    
    test    ecx,    ecx
    jz      .name_invalid
    
    test    ecx,    1
    jnz     .name_invalid
    
    cmp     ecx,    1022
    ja      .name_invalid
    
    movzx   eax,    word ptr [esi + 2]
    
    cmp     ecx,    eax
    ja      .name_invalid
    
    mov     esi,    dword ptr [esi + 4]
    
    test    esi,    esi
    jz      .invalid
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Translate ASCII-range UTF-16 to the current VFS pathname format.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    shr     ecx,    1
    lea     edi,    [ebp - 1024]

.copy_name:

    movzx   eax,    word ptr [esi]
    
    test    eax,    eax
    jz      .name_invalid
    
    cmp     eax,    HEX (7F)
    ja      .name_invalid
    
    cmp     eax,    HEX (5C)
    jne     .copy_character
    
    mov     eax,    HEX (2F)

.copy_character:

    mov     byte ptr [edi],    al
    
    add     esi,    2
    inc     edi
    
    dec     ecx
    jnz     .copy_name
    
    mov     byte ptr [edi],    0
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Translate DesiredAccess and CreateDisposition to Linux open flags.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     ecx,    O_RDONLY
    mov     edx,    dword ptr [ebp + 12]
    
    test    edx,    GENERIC_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA | GENERIC_ALL
    jz      .open
    
    mov     ecx,    O_WRONLY
    
    test    edx,    GENERIC_READ | FILE_READ_DATA | GENERIC_ALL
    jz      .open
    
    mov     ecx,    O_RDWR

.open:

    test    edx,    FILE_APPEND_DATA
    jz      .disposition
    
    or      ecx,    O_APPEND

.disposition:

    mov     edx,    dword ptr [ebp + 36]
    
    cmp     edx,    FILE_OVERWRITE_IF
    ja      .invalid
    
    cmp     edx,    FILE_OPEN
    je      .open_syscall
    
    cmp     edx,    FILE_CREATE
    je      .create_exclusive
    
    cmp     edx,    FILE_OPEN_IF
    je      .create
    
    cmp     edx,    FILE_OVERWRITE
    je      .truncate
    
    or      ecx,    O_CREAT | O_TRUNC
    jmp     .open_syscall

.create_exclusive:

    or      ecx,    O_CREAT | O_EXCL
    jmp     .open_syscall

.create:

    or      ecx,    O_CREAT
    jmp     .open_syscall

.truncate:

    or      ecx,    O_TRUNC

.open_syscall:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Linux-compatible i386 open(path, flags, mode) syscall.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    lea     ebx,    [ebp - 1024]
    mov     edx,    HEX (01B6)
    
    mov     eax,    HEX (05)
    int     HEX (80)
    
    test    eax,    eax
    js      .open_failed
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Return the descriptor as an NT handle for the current bridge.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     ebx,    dword ptr [ebp - 1028]
    mov     dword ptr [ebx],    eax
    
    mov     edi,    dword ptr [ebp - 1032]
    
    mov     dword ptr [edi + 0],    STATUS_SUCCESS
    mov     dword ptr [edi + 4],    FILE_OPENED
    
    xor     eax,    eax
    jmp     .done

.open_failed:

    mov     edx,    STATUS_OBJECT_NAME_NOT_FOUND
    
    cmp     eax,    -2
    je      .open_status
    
    mov     edx,    STATUS_IO_DEVICE_ERROR
    
    cmp     eax,    -5
    je      .open_status
    
    mov     edx,    STATUS_NO_MEMORY
    
    cmp     eax,    -12
    je      .open_status
    
    mov     edx,    STATUS_ACCESS_DENIED
    
    cmp     eax,    -13
    je      .open_status
    
    mov     edx,    STATUS_ACCESS_VIOLATION
    
    cmp     eax,    -14
    je      .open_status
    
    mov     edx,    STATUS_DEVICE_BUSY
    
    cmp     eax,    -16
    je      .open_status
    
    mov     edx,    STATUS_OBJECT_NAME_COLLISION
    
    cmp     eax,    -17
    je      .open_status
    
    mov     edx,    STATUS_NO_SUCH_DEVICE
    
    cmp     eax,    -19
    je      .open_status
    
    mov     edx,    STATUS_NOT_A_DIRECTORY
    
    cmp     eax,    -20
    je      .open_status
    
    mov     edx,    STATUS_FILE_IS_A_DIRECTORY
    
    cmp     eax,    -21
    je      .open_status
    
    mov     edx,    STATUS_INVALID_PARAMETER
    
    cmp     eax,    -22
    je      .open_status
    
    mov     edx,    STATUS_TOO_MANY_OPENED_FILES
    
    cmp     eax,    -24
    je      .open_status
    
    mov     edx,    STATUS_DISK_FULL
    
    cmp     eax,    -28
    je      .open_status
    
    mov     edx,    STATUS_MEDIA_WRITE_PROTECTED
    
    cmp     eax,    -30
    je      .open_status
    
    mov     edx,    STATUS_NAME_TOO_LONG
    
    cmp     eax,    -36
    je      .open_status
    
    mov     edx,    STATUS_NOT_IMPLEMENTED
    
    cmp     eax,    -38
    je      .open_status
    
    mov     edx,    STATUS_REPARSE_POINT_NOT_RESOLVED
    
    cmp     eax,    -40
    je      .open_status
    
    mov     edx,    STATUS_INTEGER_OVERFLOW
    
    cmp     eax,    -75
    je      .open_status
    
    mov     edx,    STATUS_UNSUCCESSFUL

.open_status:

    mov     eax,    edx
    jmp     .status

.name_invalid:

    mov     eax,    STATUS_OBJECT_NAME_INVALID
    jmp     .status

.not_supported:

    mov     eax,    STATUS_NOT_SUPPORTED
    jmp     .status

.invalid:

    mov     eax,    STATUS_INVALID_PARAMETER

.status:

    mov     edi,    dword ptr [ebp - 1032]
    
    test    edi,    edi
    jz      .done
    
    mov     dword ptr [edi + 0],    eax
    mov     dword ptr [edi + 4],    0

.done:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Restore registers and clean up the stack.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    pop     edi
    pop     esi
    pop     ebx
    mov     esp,    ebp
    pop     ebp
    ret     44
