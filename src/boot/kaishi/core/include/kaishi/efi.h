/******************************************************************************
 * @file            efi.h
 *****************************************************************************/
#ifndef     _EFI_H
#define     _EFI_H

#include    <stddef.h>
#include    <stdint.h>

typedef     uint8_t                     BOOLEAN;

#if     defined (__AMD64__)
typedef     int64_t                     INTN;
typedef     uint64_t                    UINTN;
#else
typedef     int32_t                     INTN;
typedef     uint32_t                    UINTN;
#endif

typedef     int8_t                      INT8;
typedef     uint8_t                     UINT8;

typedef     int16_t                     INT16;
typedef     uint16_t                    UINT16;

typedef     int32_t                     INT32;
typedef     uint32_t                    UINT32;

typedef     int64_t                     INT64;
typedef     uint64_t                    UINT64;

#define     EFI_PXE_BASE_CODE_PROTOCOL_GUID                 { 0x03c4e603, 0xac28, 0x11d3, 0x9a, 0x2d, { 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } }
#define     EFI_DEVICE_PATH_TO_TEXT_PROTOCOL_GUID           { 0x8b843e20, 0x8132, 0x4852, 0x90, 0xcc, { 0x55, 0x1a, 0x4e, 0x4a, 0x7f, 0x1c } }
#define     EFI_DEVICE_PATH_PROTOCOL_GUID                   { 0x09576e91, 0x6d3f, 0x11d2, 0x8e, 0x39, { 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }
#define     EFI_LOADED_IMAGE_PROTOCOL_GUID                  { 0x5b1b31a1, 0x9562, 0x11d2, 0x8e, 0x3f, { 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }
#define     EFI_BLOCK_IO_PROTOCOL_GUID                      { 0x964e5b21, 0x6459, 0x11d2, 0x8e, 0x39, { 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }
#define     EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID               { 0x9042a9de, 0x23dc, 0x4a38, 0x96, 0xfb, { 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } }
#define     EFI_ACPI_20_TABLE_GUID                          { 0x8868e871, 0xe4f1, 0x11d3, 0xbc, 0x22, { 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 } }
#define     EFI_ACPI_TABLE_GUID                             { 0xeb9d2d30, 0x2d88, 0x11d3, 0x9a, 0x16, { 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } }

typedef     UINTN                       EFI_TPL;

#define     EVT_TIMER                   0x80000000
#define     EFI_EVENT_TIMER             EVT_TIMER

#define     TPL_CALLBACK                8
#define     EFI_TPL_CALLBACK            TPL_CALLBACK

typedef     UINT64                      EFI_PHYSICAL_ADDRESS;
typedef     UINT64                      EFI_VIRTUAL_ADDRESS;

typedef struct {

    UINT32 TimeLow;
    UINT16 TimeMid;
    UINT16 TimeHighAndVersion;
    
    UINT8  ClockSeqHighAndReserved;
    UINT8  ClockSeqLow;
    
    UINT8  Node[6];

} EFI_GUID;

typedef     UINT64                      EFI_LBA;

#define     IN
#define     OUT
#define     EFIAPI
#define     OPTIONAL

#define     CONST                       const
#define     FALSE                       0
#define     TRUE                        1

typedef     int8_t                      CHAR8;
typedef     int16_t                     CHAR16;

typedef     void                        VOID;

typedef     VOID                        *EFI_EVENT;
typedef     VOID                        *EFI_HANDLE;

typedef     UINTN                       EFI_STATUS;

#define     EFI_ERROR(x)                ((INTN) ((UINTN) (x)))

#if     defined (__AMD64__)
# define        EFIERR(x)               (0x8000000000000000LLU | x)
#else
# define        EFIERR(x)               (0x80000000U | x)
#endif

#define     EFI_SUCCESS                 0UL

#define     EFI_UNSUPPORTED             EFIERR (3)
#define     EFI_BUFFER_TOO_SMALL        EFIERR (5)
#define     EFI_NO_MEDIA                EFIERR (12)

typedef struct {

    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 Reserved;

} EFI_TABLE_HEADER;

#define     EFI_OPEN_PROTOCOL_GET_PROTOCOL                  0x00000002
#define     EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER           0x00000008

typedef enum {

    EFI_RESERVED_MEMORY_TYPE,
    EFI_LOADER_CODE,
    EFI_LOADER_DATA,
    EFI_BOOT_SERVICES_CODE,
    EFI_BOOT_SERVICES_DATA,
    EFI_RUNTIME_SERVICES_CODE,
    EFI_RUNTIME_SERVICES_DATA,
    EFI_CONVENTIONAL_MEMORY,
    EFI_UNUSABLE_MEMORY,
    EFI_ACPI_RECLAIM_MEMORY,
    EFI_ACPI_MEMORY_NVS,
    EFI_MEMORY_MAPPED_IO,
    EFI_MEMORY_MAPPED_IO_PORTS_PACE,
    EFI_PAL_CODE,
    EFI_PERSISTENT_MEMORY,
    EFI_UNACCEPTED_MEMORY_TYPE,
    EFI_MAX_MEMORY_TYPE

} EFI_MEMORY_TYPE;

typedef enum { AllHandles, ByRegisterNotify, ByProtocol } EFI_LOCATE_SEARCH_TYPE;

typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_POOL) (

    IN EFI_MEMORY_TYPE                  PoolType,
    IN UINTN                            Size,
    
    OUT VOID                            **Buffer

);

typedef EFI_STATUS (EFIAPI *EFI_CLOSE_PROTOCOL) (

    IN EFI_HANDLE                       Handle,
    IN EFI_GUID                         *Protocol,
    IN EFI_HANDLE                       AgentHandle,
    IN EFI_HANDLE                       ControllerHandle

);

typedef EFI_STATUS (EFIAPI *EFI_FREE_POOL) (
    IN VOID                             *Buffer
);

typedef struct {

    UINT32              RedMask;
    UINT32              GreenMask;
    UINT32              BlueMask;
    UINT32              ReservedMask;

} EFI_PIXEL_BITMASK;

typedef enum {

    PIXEL_RED_GREEN_BLUE_RESERVED_8BIT_PER_COLOR,
    PIXEL_BLUE_GREEN_RED_RESERVED_8BIT_PER_COLOR,
    PIXEL_BIT_MASK,
    PIXEL_BLT_ONLY,
    PIXEL_FORMAT_MAX

} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {

    UINT32                      Version;
    UINT32                      HorizontalResolution;
    UINT32                      VerticalResolution;
    
    EFI_GRAPHICS_PIXEL_FORMAT   PixelFormat;
    EFI_PIXEL_BITMASK           PixelInformation;
    
    UINT32                      PixelsPerScanLine;

} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef EFI_STATUS (EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE) (

    IN  EFI_GRAPHICS_OUTPUT_PROTOCOL            *This,
    IN  UINT32                                  ModeNumber,
    
    OUT UINTN                                   *SizeOfInfo,
    OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION    **Info

);

typedef EFI_STATUS (EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE) (

    IN  EFI_GRAPHICS_OUTPUT_PROTOCOL    *This,
    IN  UINT32                          ModeNumber

);

typedef struct {

    UINT8 Blue;
    UINT8 Green;
    UINT8 Red;
    UINT8 Reserved;

} EFI_GRAPHICS_OUTPUT_BLT_PIXEL;

typedef enum {

    EFI_BLT_VIDEO_FILL,
    EFI_BLT_VIDEO_TO_BLT_BUFFER,
    EFI_BLT_BUFFER_TO_VIDEO,
    EFI_BLT_VIDEO_TO_VIDEO,
    EFI_GRAPHICS_OUTPUT_BLT_OPERATION_MAX

} EFI_GRAPHICS_OUTPUT_BLT_OPERATION;

typedef EFI_STATUS (EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_BLT) (

    IN  EFI_GRAPHICS_OUTPUT_PROTOCOL            *This,
    IN  EFI_GRAPHICS_OUTPUT_BLT_PIXEL           *BltBuffer,   OPTIONAL
    IN  EFI_GRAPHICS_OUTPUT_BLT_OPERATION       BltOperation,
    
    IN  UINTN                                   SourceX,
    IN  UINTN                                   SourceY,
    IN  UINTN                                   DestinationX,
    IN  UINTN                                   DestinationY,
    IN  UINTN                                   Width,
    IN  UINTN                                   Height,
    IN  UINTN                                   Delta         OPTIONAL
);

typedef struct {

    UINT32                                  MaxMode;
    UINT32                                  Mode;
  
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION    *Info;
    UINTN                                   SizeOfInfo;
  
    EFI_PHYSICAL_ADDRESS                    FrameBufferBase;
    UINTN                                   FrameBufferSize;

} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct _EFI_GRAPHICS_OUTPUT_PROTOCOL {

    EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE  QueryMode;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE    SetMode;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_BLT         Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE        *Mode;

} EFI_GRAPHICS_OUTPUT_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE) (

    IN  EFI_GRAPHICS_OUTPUT_PROTOCOL            *This,
    IN  UINT32                                  ModeNumber,
    
    OUT UINTN                                   *SizeOfInfo,
    OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION    **Info

);

typedef EFI_STATUS (EFIAPI *EFI_HANDLE_PROTOCOL) (

    IN  EFI_HANDLE                      Handle,
    IN  EFI_GUID                        *Protocol,
    
    OUT VOID                            **Interface

);

typedef EFI_STATUS (EFIAPI *EFI_LOCATE_PROTOCOL) (

    IN  EFI_GUID                        *Protocol,
    IN  VOID                            *Registration,
    
    OUT VOID                            **Interface

);

typedef EFI_STATUS (EFIAPI *EFI_OPEN_PROTOCOL) (

    IN EFI_HANDLE                       Handle,
    IN EFI_GUID                         *Protocol,
    
    OUT VOID                            **Interface,
    
    IN EFI_HANDLE                       AgentHandle,
    IN EFI_HANDLE                       ControllerHandle,
    IN UINT32                           Attributes

);

typedef VOID (EFIAPI *EFI_EVENT_NOTIFY) (

    IN EFI_EVENT                        Event,
    IN VOID                             *Context

);

typedef EFI_STATUS (EFIAPI *EFI_CREATE_EVENT) (
    
    IN UINT32                           Type,
    
    IN EFI_TPL                          NotifyTpl,
    IN EFI_EVENT_NOTIFY                 NotifyFunction,
    
    IN VOID                             *NotifyContext,
    OUT EFI_EVENT                       *Event

);

typedef enum {

    TimerCancel,
    TimerPeriodic,
    TimerRelative,
    TimerTypeMax

} EFI_TIMER_DELAY;

typedef EFI_STATUS (EFIAPI *EFI_SET_TIMER) (

    IN EFI_EVENT                        Event,
    IN EFI_TIMER_DELAY                  Type,
    IN UINT64                           TriggerTime

);

typedef EFI_STATUS (EFIAPI *EFI_WAIT_FOR_EVENT) (

    IN UINTN                            NumberOfEvents,
    IN EFI_EVENT                        *Event,
    
    OUT UINTN                           *Index

);

typedef EFI_STATUS (EFIAPI *EFI_CLOSE_EVENT) (
    IN EFI_EVENT                        Event
);

typedef EFI_STATUS (EFIAPI *EFI_LOCATE_HANDLE) (

    IN EFI_LOCATE_SEARCH_TYPE           SearchType,
    IN EFI_GUID                         *Protocol,
    IN VOID                             *SearchKey,
    
    IN OUT UINTN                        *BufferSize,
    OUT EFI_HANDLE                      *Buffer

);

typedef enum {

    AllocateAnyPages,
    AllocateMaxAddress,
    AllocateAddress,
    MaxAllocateType

} EFI_ALLOCATE_TYPE;

typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_PAGES) (

    IN EFI_ALLOCATE_TYPE                Type,
    IN EFI_MEMORY_TYPE                  MemoryType,
    
    IN UINTN                            NoPages,
    OUT EFI_PHYSICAL_ADDRESS            *Memory

);

typedef struct {

    UINT32                              Type;               /* Field size is 32 bits followed by 32 bit pad */
    UINT32                              Pad;
    
    EFI_PHYSICAL_ADDRESS                PhysicalStart;      /* Field size is 64 bits */
    EFI_VIRTUAL_ADDRESS                 VirtualStart;       /* Field size is 64 bits */
    
    UINT64                              NumberOfPages;      /* Field size is 64 bits */
    UINT64                              Attribute;          /* Field size is 64 bits */

} EFI_MEMORY_DESCRIPTOR;

typedef EFI_STATUS (EFIAPI *EFI_GET_MEMORY_MAP) (

    IN OUT UINTN                        *MemoryMapSize,
    IN OUT EFI_MEMORY_DESCRIPTOR        *MemoryMap,
    
    OUT UINTN                           *MapKey,
    OUT UINTN                           *DescriptorSize,
    OUT UINT32                          *DescriptorVersion

);

typedef EFI_STATUS (EFIAPI *EFI_EXIT_BOOT_SERVICES) (

    IN EFI_HANDLE                       ImageHandle,
    IN UINTN                            MapKey

);

typedef struct {

    EFI_TABLE_HEADER Hdr;
    
    /* Task Priority Services */
    VOID                                *RaiseTPL;
    VOID                                *RestoreTPL;
    
    /* Memory Services */
    EFI_ALLOCATE_PAGES                  AllocatePages;
    VOID                                *FreePages;
    EFI_GET_MEMORY_MAP                  GetMemoryMap;
    
    EFI_ALLOCATE_POOL                   AllocatePool;
    EFI_FREE_POOL                       FreePool;
    
    /* Event & Timer Services */
    EFI_CREATE_EVENT                    CreateEvent;
    EFI_SET_TIMER                       SetTimer;
    EFI_WAIT_FOR_EVENT                  WaitForEvent;
    VOID                                *SignalEvent;
    EFI_CLOSE_EVENT                     CloseEvent;
    VOID                                *CheckEvent;
    
    /* Protocol Handler Services */
    VOID                                *InstallProtocolInterface;
    VOID                                *ReinstallProtocolInterface;
    VOID                                *UninstallProtocolInterface;
    EFI_HANDLE_PROTOCOL                 HandleProtocol;
    VOID                                *Reserved;
    VOID                                *RegisterProtocolNotify;
    EFI_LOCATE_HANDLE                   LocateHandle;
    VOID                                *LocateDevicePath;
    VOID                                *InstallConfigurationTable;
    
    /* Image Services */
    VOID                                *LoadImage;
    VOID                                *StartImage;
    VOID                                *Exit;
    VOID                                *UnloadImage;
    EFI_EXIT_BOOT_SERVICES              ExitBootServices;
    
    /* Miscellaneous Services */
    VOID                                *GetNextMonotonicCount;
    VOID                                *Stall;
    VOID                                *SetWatchdogTimer;
    
    /* DriverSupport Services */
    VOID                                *ConnectController;
    VOID                                *DisconnectController;
    
    /* Open and Close Protocol Services */
    EFI_OPEN_PROTOCOL                   OpenProtocol;
    EFI_CLOSE_PROTOCOL                  CloseProtocol;
    VOID                                *OpenProtocolInformation;
    
    /* Library Services */
    VOID                                *ProtocolsPerHandle;
    VOID                                *LocateHandleBuffer;
    EFI_LOCATE_PROTOCOL                 LocateProtocol;
    VOID                                *InstallMultipleProtocolInterfaces;
    VOID                                *UninstallMultipleProtocolInterfaces;
    
    /* 32-bit CRC Services */
    VOID                                *CalculateCrc32;
    
    /* Miscellaneous Services */
    VOID                                *CopyMem;
    VOID                                *SetMem;
    VOID                                *CreateEventEx;

} EFI_BOOT_SERVICES;

typedef     struct _EFI_BLOCK_IO_PROTOCOL                   EFI_BLOCK_IO_PROTOCOL;

typedef struct {

    UINT32                              MediaId;
    
    BOOLEAN                             RemovableMedia;
    BOOLEAN                             MediaPresent;
    BOOLEAN                             LogicalPartition;
    BOOLEAN                             ReadOnly;
    BOOLEAN                             WriteCaching;
    
    UINT32                              BlockSize;
    UINT32                              IoAlign;
    
    EFI_LBA                             LastBlock;
    EFI_LBA                             LowestAlignedLba;
    
    UINT32                              LogicalBlocksPerPhysicalBlock;
    UINT32                              OptimalTransferLengthGranularity;

} EFI_BLOCK_IO_MEDIA;

typedef EFI_STATUS (EFIAPI *EFI_BLOCK_FLUSH) (
    IN EFI_BLOCK_IO_PROTOCOL            *This
);

typedef EFI_STATUS (EFIAPI *EFI_BLOCK_READ) (

    IN EFI_BLOCK_IO_PROTOCOL            *This,
    IN UINT32                           MediaId,
    IN EFI_LBA                          LBA,
    IN UINTN                            BufferSize,
    
    OUT VOID                            *Buffer

);

typedef EFI_STATUS (EFIAPI *EFI_BLOCK_RESET) (

    IN EFI_BLOCK_IO_PROTOCOL            *This,
    IN BOOLEAN                          ExtendedVerification

);

typedef EFI_STATUS (EFIAPI *EFI_BLOCK_WRITE) (

    IN EFI_BLOCK_IO_PROTOCOL            *This,
    IN UINT32                           MediaId,
    IN EFI_LBA                          LBA,
    IN UINTN                            BufferSize,
    IN VOID                             *Buffer

);

struct _EFI_BLOCK_IO_PROTOCOL {

    UINT64                              Revision;
    
    EFI_BLOCK_IO_MEDIA                  *Media;
    EFI_BLOCK_RESET                     Reset;
    EFI_BLOCK_READ                      ReadBlocks;
    EFI_BLOCK_WRITE                     WriteBlocks;
    EFI_BLOCK_FLUSH                     FlushBlocks;

};

typedef     struct _EFI_BLOCK_IO_PROTOCOL                   EFI_BLOCK_IO;
typedef     struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL          EFI_SIMPLE_TEXT_INPUT_PROTOCOL;

typedef struct {

    UINT16  ScanCode;
    CHAR16  UnicodeChar;

} EFI_INPUT_KEY;

typedef EFI_STATUS (EFIAPI *EFI_INPUT_READ_KEY) (

    IN EFI_SIMPLE_TEXT_INPUT_PROTOCOL   *This,
    OUT EFI_INPUT_KEY                   *Key

);

typedef EFI_STATUS (EFIAPI *EFI_INPUT_RESET) (

    IN EFI_SIMPLE_TEXT_INPUT_PROTOCOL   *This,
    IN BOOLEAN                          ExtendedVerification

);

struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL {

    EFI_INPUT_RESET                     Reset;
    EFI_INPUT_READ_KEY                  ReadKeyStroke;
    EFI_EVENT                           WaitForKey;

};

typedef     struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL         EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_TEXT_CLEAR_SCREEN) (
    IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This 
);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_QUERY_MODE) (

    IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
    IN UINTN                            ModeNumber,
    
    OUT UINTN                           *Columns,
    OUT UINTN                           *Rows

);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_RESET) (

    IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
    IN BOOLEAN                          ExtendedVerification

);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_SET_ATTRIBUTE) (

    IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
    IN UINTN                            Attribute

);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_SET_CURSOR_POSITION) (

    IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
    IN UINTN                            Column,
    IN UINTN                            Row

);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_SET_MODE) (

    IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
    IN UINTN                            ModeNumber

);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_STRING) (

    IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
    IN CHAR16                           *String

);

typedef struct {

    INT32   MaxMode;

    INT32   Mode;
    INT32   Attribute;
    INT32   CursorColumn;
    INT32   CursorRow;
    
    BOOLEAN CursorVisible;

} SIMPLE_TEXT_OUTPUT_MODE;

typedef EFI_STATUS (EFIAPI *EFI_TEXT_ENABLE_CURSOR) (

    IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
    IN BOOLEAN                          Enable

);

struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {

    EFI_TEXT_RESET                      Reset;
    EFI_TEXT_STRING                     OutputString;
    
    VOID                                *TestString;
    
    EFI_TEXT_QUERY_MODE                 QueryMode;
    EFI_TEXT_SET_MODE                   SetMode;
    EFI_TEXT_SET_ATTRIBUTE              SetAttribute;
    EFI_TEXT_CLEAR_SCREEN               ClearScreen;
    EFI_TEXT_SET_CURSOR_POSITION        SetCursorPosition;
    
    EFI_TEXT_ENABLE_CURSOR              EnableCursor;
    SIMPLE_TEXT_OUTPUT_MODE             *Mode;

};

typedef struct _EFI_DEVICE_PATH_PROTOCOL {

    UINT8                               Type;
    UINT8                               SubType;
    UINT8                               Length[2];

} EFI_DEVICE_PATH_PROTOCOL;

typedef CHAR16 *(EFIAPI *EFI_DEVICE_PATH_TO_TEXT_NODE) (

    IN CONST EFI_DEVICE_PATH_PROTOCOL   *DeviceNode,
    IN BOOLEAN                          DisplayOnly,
    IN BOOLEAN                          AllowShortcuts

);

typedef CHAR16 *(EFIAPI *EFI_DEVICE_PATH_TO_TEXT_PATH) (

    IN CONST EFI_DEVICE_PATH_PROTOCOL   *DevicePath,
    IN BOOLEAN                          DisplayOnly,
    IN BOOLEAN                          AllowShortcuts

);

typedef struct _EFI_DEVICE_PATH_TO_TEXT_PROTOCOL {

    EFI_DEVICE_PATH_TO_TEXT_NODE ConvertDeviceNodeToText;
    EFI_DEVICE_PATH_TO_TEXT_PATH ConvertDevicePathToText;

} EFI_DEVICE_PATH_TO_TEXT_PROTOCOL;

typedef struct _EFI_CONFIGURATION_TABLE {

    EFI_GUID                            VendorGuid;
    VOID                                *VendorTable;

} EFI_CONFIGURATION_TABLE;

typedef struct {

    EFI_TABLE_HEADER                    Hdr;
    
    CHAR16                              *FirmwareVendor;
    UINT32                              FirmwareRevision;
    
    EFI_HANDLE                          ConsoleInHandle;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL      *ConIn;
    EFI_HANDLE                          ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL     *ConOut;
    EFI_HANDLE                          StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL     *StdErr;
    EFI_HANDLE                          *RuntimeServices;
    EFI_BOOT_SERVICES                   *BootServices;
    
    UINTN                               NumberOfTableEntries;
    EFI_CONFIGURATION_TABLE             *ConfigurationTable;

} EFI_SYSTEM_TABLE;

typedef struct {

    UINT32                              Revision;
    
    EFI_HANDLE                          ParentHandle;
    EFI_SYSTEM_TABLE                    *SystemTable;
    
    EFI_HANDLE                          DeviceHandle;
    
    VOID                                *FilePath;
    VOID                                *Reserved;
    
    UINT32                              LoadOptionsSize;
    
    VOID                                *LoadOptions;
    VOID                                *ImageBase;
    
    UINT64                              ImageSize;
    
    EFI_MEMORY_TYPE                     ImageCodeType;
    EFI_MEMORY_TYPE                     ImageDataType;
    
    VOID                                *Unload;

} EFI_LOADED_IMAGE_PROTOCOL;

#endif      /* _EFI_H */
