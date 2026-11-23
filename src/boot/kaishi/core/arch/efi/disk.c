/******************************************************************************
 * @file            disk.c
 *****************************************************************************/
#include    <kaishi/vfs/ext2.h>
#include    <kaishi/vfs/msdos.h>
#include    <kaishi/vfs/node.h>
#include    <kaishi/vfs/vfs.h>
#include    <kaishi/fnv1a.h>
#include    <kaishi/efi.h>
#include    <kaishi/misc.h>
#include    <kaishi/stdio.h>
#include    <kaishi/stdlib.h>

#include    <stdio.h>
#include    <string.h>

#define     UEFI                        1
#include    <kaishi/disk.h>

static EFI_GUID DevicePathToTextProtocolGuid = EFI_DEVICE_PATH_TO_TEXT_PROTOCOL_GUID;
static EFI_GUID DevicePathProtocolGuid = EFI_DEVICE_PATH_PROTOCOL_GUID;

static EFI_GUID LoadedImageProtocolGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID BlockIoProtocolGuid = EFI_BLOCK_IO_PROTOCOL_GUID;

static EFI_GUID PxeBaseCodeProtocolGuid = EFI_PXE_BASE_CODE_PROTOCOL_GUID;
extern EFI_BOOT_SERVICES *BootServices;

struct probe {

    void *(*probe) (void *data, void *scratch);
    
    long start_sector;
    size_t sectors_count;

};

static struct probe probes[] = {

    {   &msdos_probe,   0,      1   },
    {   &ext2_probe,    2,      2   },
    {   0,              0,      0   }

};

static UINT8 *scratch = 0;
static UINT8 *temp = 0;

static char *name = 0;
static UINT64 hash = 0;

struct fs_node *node = 0;
struct fs_node *child = 0;

static int disk_read (void *data, void *scratch, EFI_LBA sector, UINTN count) {

    EFI_STATUS status;
    disk_t *disk;
    
    if (!(node = (struct fs_node *) data)) {
        return 0;
    }
    
    if (!(disk = (disk_t *) node->data)) {
        return 0;
    }
    
    status = disk->block_io->ReadBlocks (disk->block_io, disk->block_io->Media->MediaId, (EFI_LBA) (sector - 1), (UINTN) (count * disk->sector_size), scratch);
    return status != EFI_SUCCESS;

}

static int looks_like_mbr (void *data, void *scratch) {

    UINT64 start, end;
    
    UINT8 *p;
    int i;
    
    disk_t *disk;
    
    if (!(node = (struct fs_node *) data)) {
        return 0;
    }
    
    p = (UINT8 *) scratch;
    
    if (!(disk = (disk_t *) node->data)) {
        return 0;
    }
    
    for (i = 0, p += 0x1BE; i < 4; i++, p += 16) {
    
        if (p[0] != 0x00 && p[0] != 0x80) {
            return 0;
        }
        
        start = array_to_int (p + 8, 4);
        end = array_to_int (p + 12, 4);
        
        if (start > disk->total_sectors || start + end > disk->total_sectors) {
            return 0;
        }
    
    }
    
    return 1;

}

static int cmp_mem (void *s1, void *s2, size_t n) {

    UINT8 *p1 = s1, *p2 = s2;

    while (n--) {
    
        if (*p1 != *p2) {
            return (*p1 < *p2) ? -1 : 1;
        }
        
        p1++;
        p2++;
    
    }
    
    return 0;

}

#define     END_DEVICE_PATH_TYPE                            0x7f
#define     END_ENTIRE_DEVICE_PATH_SUBTYPE                  0xff

/* Helper function to check if a whole-disk handle contains a target partition handle */
static int is_parent_of_boot_device (EFI_HANDLE PhysicalDiskHandle, EFI_HANDLE BootPartitionHandle) {

    EFI_STATUS Status;
    
    EFI_DEVICE_PATH_PROTOCOL *ParentPath = NULL;
    EFI_DEVICE_PATH_PROTOCOL *ChildPath = NULL;
    
    EFI_DEVICE_PATH_PROTOCOL *Node;
    UINTN ParentSize = 0;
    
    /* 1. Get binary device paths for both handles */
    Status = BootServices->HandleProtocol (PhysicalDiskHandle, &DevicePathProtocolGuid, (VOID**) &ParentPath);
    
    if (EFI_ERROR (Status) || !ParentPath) {
        return 0;
    }
    
    Status = BootServices->HandleProtocol (BootPartitionHandle, &DevicePathProtocolGuid, (VOID**) &ChildPath);
    
    if (EFI_ERROR (Status) || !ChildPath) {
        return 0;
    }
    
    /* 2. Measure the exact binary size of the parent physical disk path */
    Node = ParentPath;
    
    while (!(Node->Type == END_DEVICE_PATH_TYPE && Node->SubType == END_ENTIRE_DEVICE_PATH_SUBTYPE)) {
    
        ParentSize += (Node->Length[1] << 8) | Node->Length[0];
        Node = (EFI_DEVICE_PATH_PROTOCOL *) ((UINT8 *) Node + ((Node->Length[1] << 8) | Node->Length[0]));
    
    }
    
    /* 3. Compare the binary prefix. The child path must start exactly with the parent's path bytes */
    return cmp_mem (ParentPath, ChildPath, ParentSize) == 0;

}

void disk_create_index (EFI_HANDLE ImageHandle) {

    EFI_STATUS Status = EFI_SUCCESS;
    EFI_BLOCK_IO_PROTOCOL *Drive;
    
    EFI_HANDLE tmp_handles[1], *handles;
    UINTN handles_size, handles_count, i, j;
    
    UINT8 *p;
    UINT64 start;
    
    struct probe *probe;
    disk_t *disk;
    
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage = 0;
    EFI_HANDLE BootDeviceHandle = 0;
    
    struct fs_node *local;
    size_t sector_size;
    
    Status = BootServices->OpenProtocol (ImageHandle, &LoadedImageProtocolGuid, (VOID **) &LoadedImage, ImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    
    if (!EFI_ERROR (Status)) {
        BootDeviceHandle = LoadedImage->DeviceHandle;
    }
    
    handles = tmp_handles;
    handles_size = sizeof (tmp_handles);
    
    Status = BootServices->LocateHandle (ByProtocol, &BlockIoProtocolGuid, NULL, &handles_size, handles);
    
    if (Status != EFI_BUFFER_TOO_SMALL && Status != EFI_SUCCESS) {
    
        Status = BootServices->LocateHandle (ByProtocol, &PxeBaseCodeProtocolGuid, NULL, &handles_size, handles);
        
        if (Status == EFI_BUFFER_TOO_SMALL || Status == EFI_SUCCESS) {
            return;
        }

        panic ("LocateHandle for BLOCK_IO_PROTOCOL failed\r\n");
    
    }
    
    handles = kmalloc (handles_size);
    
    if ((Status = BootServices->LocateHandle (ByProtocol, &BlockIoProtocolGuid, NULL, &handles_size, handles)) != EFI_SUCCESS) {
        panic ("LocateHandle for BLOCK_IO_PROTOCOL failed\r\n");
    }
    
    handles_count = handles_size / sizeof (EFI_HANDLE);
    
    for (i = 0; i < handles_count; i++) {
    
        Status = BootServices->HandleProtocol (handles[i], &BlockIoProtocolGuid, (void **) &Drive);
        
        if (Status != EFI_SUCCESS || !Drive || !Drive->Media->LastBlock) {
            continue;
        }
        
        if (!Drive->Media->BlockSize || Drive->Media->LogicalPartition) {
            continue;
        }
        
        if (Drive->Media->BlockSize == UINT64_MAX) {
            continue;
        }
        
        scratch = kmalloc (Drive->Media->BlockSize);
        
        if ((Status = Drive->ReadBlocks (Drive, Drive->Media->MediaId, (EFI_LBA) 0, (UINTN) Drive->Media->BlockSize, scratch))) {
        
            kfree (scratch);
            continue;
        
        }
        
        disk = kmalloc (sizeof (*disk));
        
        disk->efi_handle = handles[i];
        disk->block_io = Drive;
        
        disk->total_sectors = (Drive->Media->LastBlock + 1) * (Drive->Media->BlockSize / 512);
        disk->sector_size = Drive->Media->BlockSize;
        
        node = kmalloc (sizeof (*node));
        
        node->read = disk_read;
        node->data = disk;
        
        if (looks_like_mbr (node, scratch)) {
        
            name = kmalloc (1 + 3 + 1 + 4 + 2 + 1);
            sprintf (name, "/dev/disk%02d", i + 1);
            
            vfs_mount (name, node);
            kfree (name);
            
            for (j = 0, p = scratch + 0x1BE; j < 4; j++, p += 16) {
            
                start = array_to_int (p + 8, 4);
                
                if (start > 0) {
                
                    for (probe = &probes; probe->probe; probe++) {
                    
                        temp = kmalloc (sector_size = disk->sector_size * probe->sectors_count);
                        
                        if (!disk_read (node, temp, (uint64_t) start + probe->start_sector + 1, probe->sectors_count)) {
                        
                            hash = checksum64_fnv1a (temp, sector_size);
                            
                            if ((child = probe->probe (node, temp))) {
                            
                                child->checksum_hash = hash;
                                
                                name = kmalloc (1 + 3 + 1 + 4 + 2 + 1 + 2 + 1);
                                sprintf (name, "/dev/disk%02dp%d", i + 1, j + 1);
                                
                                child->flags |= VFS_DIRECTORY;
                                vfs_mount (name, child);
                                
                                kfree (name);
                                
                                if (p[0] == 0x80) {
                                
                                    if (handles[i] == BootDeviceHandle || is_parent_of_boot_device (handles[i], BootDeviceHandle)) {
                                        vfs_mount ("/", child);
                                    }
                                
                                }
                                
                                break;
                            
                            }
                        
                        }
                        
                        kfree (temp);
                    
                    }
                
                }
            
            }
        
        } else {
        
            hash = checksum64_fnv1a (scratch, disk->sector_size);
            
            name = kmalloc (1 + 3 + 1 + 4 + 4 + 1);
            sprintf (name, "/dev/disk%02d", i + 1);
            
            local = node;
            
            for (probe = &probes; probe->probe; probe++) {
            
                if ((child = probe->probe (node, scratch))) {
                
                    child->checksum_hash = hash;
                    
                    child->flags |= VFS_DIRECTORY;
                    local = child;
                    
                    break;
                
                }
            
            }
            
            vfs_mount (name, local);
            kfree (name);
            
            if (handles[i] == BootDeviceHandle || is_parent_of_boot_device (handles[i], BootDeviceHandle)) {
                vfs_mount ("/", local);
            }
        
        }
        
        kfree (scratch);
    
    }

}

UINT32 disk_get_sector_size (void *data) {

    disk_t *disk;
    
    if (!(disk = (disk_t *) data)) {
        return 0;
    }
    
    return disk->sector_size;

}
