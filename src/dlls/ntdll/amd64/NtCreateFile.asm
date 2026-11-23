;******************************************************************************
; @file             NtCreateFile.asm
;******************************************************************************
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

%define     GENERIC_READ                                    HEX (80000000)
%define     GENERIC_WRITE                                   HEX (40000000)
%define     GENERIC_ALL                                     HEX (10000000)

%define     FILE_READ_DATA                                  HEX (00000001)
%define     FILE_WRITE_DATA                                 HEX (00000002)
%define     FILE_APPEND_DATA                                HEX (00000004)

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
; @function        NtOpenFile
;******************************************************************************
global      NtCreateFile
NtCreateFile:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Preserve registers.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    push    rbp
    mov     rbp,    rsp
    
    push    rbx
    push    rsi
    push    rdi
    push    r12
    push    r13
    push    r14
    push    r15
    
    sub     rsp,    1024
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Save FileHandle, ObjectAttributes, IoStatusBlock and DesiredAccess.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     r12,    rcx
    mov     r13,    r8
    mov     r14,    r9
    mov     r15d,   edx
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Validate the required NT pointers.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    test    r12,    r12
    jz      .invalid
    
    test    r13,    r13
    jz      .invalid
    
    test    r14,    r14
    jz      .invalid
    
    cmp     dword ptr [r13],    48
    jb      .invalid
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Handle-relative names require NT object manager support.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    cmp     qword ptr [r13 + 8],    0
    jne     .not_supported
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Get OBJECT_ATTRIBUTES.ObjectName (PUNICODE_STRING).
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     rbx,    qword ptr [r13 + 16]
    
    test    rbx,    rbx
    jz      .invalid
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; UNICODE_STRING.Length is a UTF-16 byte count, not a character count.
    ;; The temporary path can hold 511 ASCII characters plus a terminator.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    movzx   ecx,    word ptr [rbx]
    
    test    ecx,    ecx
    jz      .name_invalid
    
    test    ecx,    1
    jnz     .name_invalid
    
    cmp     ecx,    1022
    ja      .name_invalid
    
    movzx   eax,    word ptr [rbx + 2]
    
    cmp     ecx,    eax
    ja      .name_invalid
    
    mov     rsi,    qword ptr [rbx + 8]
    
    test    rsi,    rsi
    jz      .invalid
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Translate ASCII-range UTF-16 to the current VFS pathname format.
    ;; Non-ASCII names need a proper UTF-8 conversion in the future.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    shr     ecx,    1
    mov     rdi,    rsp

.copy_name:

    movzx   eax,    word ptr [rsi]
    
    test    eax,    eax
    jz      .name_invalid
    
    cmp     eax,    HEX (7F)
    ja      .name_invalid
    
    cmp     eax,    HEX (5C)
    jne     .copy_character
    
    mov     eax,    HEX (2F)

.copy_character:

    mov     byte ptr [rdi],    al
    
    add     rsi,    2
    inc     rdi
    
    dec     ecx
    jnz     .copy_name
    
    mov     byte ptr [rdi],    0
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Translate DesiredAccess and CreateDisposition to Linux open flags.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     esi,    O_RDONLY
    
    test    r15d,   GENERIC_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA | GENERIC_ALL
    jz      .open
    
    mov     esi,    O_WRONLY
    
    test    r15d,   GENERIC_READ | FILE_READ_DATA | GENERIC_ALL
    jz      .open
    
    mov     esi,    O_RDWR

.open:

    test    r15d,   FILE_APPEND_DATA
    jz      .disposition
    
    or      esi,    O_APPEND

.disposition:

    mov     ebx,    dword ptr [rbp + 72]
    
    cmp     ebx,    FILE_OVERWRITE_IF
    ja      .invalid
    
    cmp     ebx,    FILE_OPEN
    je      .open_syscall
    
    cmp     ebx,    FILE_CREATE
    je      .create_exclusive
    
    cmp     ebx,    FILE_OPEN_IF
    je      .create
    
    cmp     ebx,    FILE_OVERWRITE
    je      .truncate
    
    or      esi,    O_CREAT | O_TRUNC
    jmp     .open_syscall

.create_exclusive:

    or      esi,    O_CREAT | O_EXCL
    jmp     .open_syscall

.create:

    or      esi,    O_CREAT
    jmp     .open_syscall

.truncate:

    or      esi,    O_TRUNC

.open_syscall:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Linux-compatible AMD64 open(path, flags, mode) syscall.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     rdi,    rsp
    mov     edx,    HEX (01B6)
    
    mov     eax,    HEX (02)
    syscall
    
    test    rax,    rax
    js      .open_failed
    
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Return the descriptor as an NT handle for the current bridge.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov     qword ptr [r12 + 0],    rax
    
    mov     dword ptr [r14 + 0],    STATUS_SUCCESS
    mov     qword ptr [r14 + 8],    FILE_OPENED
    
    xor     eax,    eax
    jmp     .done

.open_failed:

    mov     edx,    STATUS_OBJECT_NAME_NOT_FOUND
    
    cmp     rax,    -2
    je      .open_status
    
    mov     edx,    STATUS_IO_DEVICE_ERROR
    
    cmp     rax,    -5
    je      .open_status
    
    mov     edx,    STATUS_NO_MEMORY
    
    cmp     rax,    -12
    je      .open_status
    
    mov     edx,    STATUS_ACCESS_DENIED
    
    cmp     rax,    -13
    je      .open_status
    
    mov     edx,    STATUS_ACCESS_VIOLATION
    
    cmp     rax,    -14
    je      .open_status
    
    mov     edx,    STATUS_DEVICE_BUSY
    
    cmp     rax,    -16
    je      .open_status
    
    mov     edx,    STATUS_OBJECT_NAME_COLLISION
    
    cmp     rax,    -17
    je      .open_status
    
    mov     edx,    STATUS_NO_SUCH_DEVICE
    
    cmp     rax,    -19
    je      .open_status
    
    mov     edx,    STATUS_NOT_A_DIRECTORY
    
    cmp     rax,    -20
    je      .open_status
    
    mov     edx,    STATUS_FILE_IS_A_DIRECTORY
    
    cmp     rax,    -21
    je      .open_status
    
    mov     edx,    STATUS_INVALID_PARAMETER
    
    cmp     rax,    -22
    je      .open_status
    
    mov     edx,    STATUS_TOO_MANY_OPENED_FILES
    
    cmp     rax,    -24
    je      .open_status
    
    mov     edx,    STATUS_DISK_FULL
    
    cmp     rax,    -28
    je      .open_status
    
    mov     edx,    STATUS_MEDIA_WRITE_PROTECTED
    
    cmp     rax,    -30
    je      .open_status
    
    mov     edx,    STATUS_NAME_TOO_LONG
    
    cmp     rax,    -36
    je      .open_status
    
    mov     edx,    STATUS_NOT_IMPLEMENTED
    
    cmp     rax,    -38
    je      .open_status
    
    mov     edx,    STATUS_REPARSE_POINT_NOT_RESOLVED
    
    cmp     rax,    -40
    je      .open_status
    
    mov     edx,    STATUS_INTEGER_OVERFLOW
    
    cmp     rax,    -75
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

    test    r14,    r14
    jz      .done
    
    mov     dword ptr [r14 + 0],    eax
    mov     qword ptr [r14 + 8],    0

.done:

    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ;; Restore registers.
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    add     rsp,    1024
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rdi
    pop     rsi
    pop     rbx
    pop     rbp
    ret
