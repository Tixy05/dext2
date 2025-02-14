/*
MIT License

Copyright (c) 2025 Vladimir Pirko

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <windows.h>
#include <stdio.h>
#include <stdint.h>

// Logging
#ifdef DEBUG
    #define DEXT2_LOG_DEBUG(format, ...) fprintf(stderr, "[%d] DEBUG: " format "\n", __LINE__, ##__VA_ARGS__)
#else
    #define DEXT2_LOG_DEBUG(format, ...)
#endif // DEBUG

#define DEXT2_LOG_INFO(format, ...) fprintf(stderr, "[%d] INFO: " format "\n",  __LINE__, ##__VA_ARGS__)
#define DEXT2_LOG_ERROR(format, ...) fprintf(stderr, "[%d] ERROR: " format "\n",  __LINE__, ##__VA_ARGS__)

// #ifndef DEXT2_IMPLEMENTATION
#ifdef DEXT2_IMPLEMENTATION
#undef DEXT2_IMPLEMENTATION

// Constants for Ext2
#define DEXT2_SUPER_MAGIC 0xEF53
#define DEXT2_SUPERBLOCK_OFFSET 1024
#define DEXT2_SUPERBLOCK_SIZE 1024
#define DEXT2_GROUP_DESCRIPTOR_ENTRY_SIZE 32
#define DEXT2_MAX_NAME_LEN 255
#define DEXT2_N_BLOCKS 15
#define DEXT2_INODE_SIZE 128

#define DEXT2_INODE_IS_DIR 0x4000 
#define DEXT2_INODE_IS_FILE 0x8000 


#define DEXT2_MAX_PARTITION_COUNT 128

#define KiB 1024
#define MiB ( 1024*KiB )
#define GiB ( 1024*MiB )

#define newline() printf("\n")

typedef enum
{
    DEXT2_NO_ERROR,
    DEXT2_ERROR_INTERNAL,
    DEXT2_ERROR_FILE_MISSING,
    DEXT2_ERROR_READING_DISK,
    DEXT2_ERROR_NOT_EXT2
} DEXT2_ERROR;

/***********************************************************
* ALL STRUCTURES FIELDS SPECIFIC TO ext2 WITH VERSION
* GREATER OR EQUAL TO 1.0 THAT USUALLY PLACED AT THE 
* END OF STRUCTURE ARE OMITTED
* 
* Structs and their fields follow Linux-kernel-style naming
* 'cause it is easier to develop while reading documentation
* with same names
************************************************************/

typedef struct {
    DWORD s_inodes_count;          // Total number of inodes
    DWORD s_blocks_count;          // Total number of blocks
    DWORD s_r_blocks_count;        // Reserved blocks count
    DWORD s_free_blocks_count;     // Free blocks count
    DWORD s_free_inodes_count;     // Free inodes count
    DWORD s_first_data_block;      // First data block
    DWORD s_log_block_size;        // Block size (shifted by 10)
    DWORD s_log_frag_size;         // Fragment size (shifted by 10)
    DWORD s_blocks_per_group;      // Number of blocks per group
    DWORD s_frags_per_group;       // Number of fragments per group
    DWORD s_inodes_per_group;      // Number of inodes per group
    DWORD s_mtime;                 // Last mount time
    DWORD s_wtime;                 // Last write time
    WORD s_mnt_count;              // Mount count
    WORD s_max_mnt_count;          // Max mount count
    WORD s_magic;                  // Filesystem magic number
    WORD s_state;                  // Filesystem state
    WORD s_errors;                 // Behavior when detecting errors
    WORD s_minor_rev_level;        // Minor revision level
    DWORD s_lastcheck;             // Time of last check
    DWORD s_checkinterval;         // Maximum time between checks
    DWORD s_creator_os;            // OS that created filesystem
    DWORD s_rev_level;             // Revision level
} ext2_super_block;

typedef struct {
    DWORD bg_block_bitmap;         // Block bitmap block
    DWORD bg_inode_bitmap;         // Inode bitmap block
    DWORD bg_inode_table;          // Inode table block
    WORD bg_free_blocks_count;     // Free blocks count
    WORD bg_free_inodes_count;     // Free inodes count
    WORD bg_used_dirs_count;       // Directories count
} ext2_group_desc;

typedef struct {
    WORD i_mode;                   // File mode
    WORD i_uid;                    // Owner UID
    DWORD i_size;                  // Size in bytes

    DWORD i_atime;                 // Access time
    DWORD i_ctime;                 // Creation time
    DWORD i_mtime;                 // Modification time
    DWORD i_dtime;                 // Deletion time

    WORD i_gid;                    // Group ID
    WORD i_links_count;            // Links count
    DWORD i_blocks;                // Blocks count (512-byte units)
    DWORD i_flags;                 // Flags
    DWORD i_reserved1;             // Reserved
    DWORD i_block[DEXT2_N_BLOCKS]; // Pointers to blocks
    DWORD i_generation;            // File version (for NFS)
    DWORD i_file_acl;              // File ACL (not used in version 0)
    DWORD i_dir_acl;               // Directory ACL (not used in version 0)
    DWORD i_faddr;                 // Fragment address
} ext2_inode;

typedef struct {
    DWORD inode;                   // Inode number
    WORD rec_len;                  // Directory entry length
    WORD name_len;                 // Name length
    CHAR name[DEXT2_MAX_NAME_LEN]; // File name
} ext2_dir_entry;


BOOL GetDataBlocks(HANDLE hExt2, ext2_inode* pInode, OUT PDWORD* dataBlocks, OUT PULONGLONG dataBlocksSize);
BOOL GetInodeByNumber(HANDLE hExt2, DWORD inodeNumber, OUT ext2_inode* lpInode);

ext2_super_block g_mainSuperBlock = {0};
#define llBlockSize ( (LONGLONG) (1024 << g_mainSuperBlock.s_log_block_size) )
#define dwBlockSize ( (DWORD) (1024 << g_mainSuperBlock.s_log_block_size) )
LONGLONG g_partitionStart = 0;

BOOL ReadBytes(HANDLE hFile, LONGLONG fromWhereToRead, DWORD nBytesToRead, OUT LPVOID destination) {
    DWORD startingOffset = fromWhereToRead % 512;
    fromWhereToRead -= (LONGLONG) startingOffset;
    nBytesToRead += startingOffset;
    LARGE_INTEGER li = { .QuadPart = fromWhereToRead };
    if (!SetFilePointerEx(hFile, li, NULL, FILE_BEGIN)) {
        DEXT2_LOG_DEBUG("Fucked up file pointer");
        return FALSE;
    }

    DWORD bufferSize = nBytesToRead % 512 != 0 ?
        (nBytesToRead/512 + 1) * 512 :
        nBytesToRead; 
    PBYTE buffer = (PBYTE) malloc((size_t) bufferSize);
    DWORD bytesRead;
    
    if (!ReadFile(hFile, (LPVOID) buffer, bufferSize, &bytesRead, NULL) || bytesRead < nBytesToRead) {
        DEXT2_LOG_DEBUG("Fucked up while trying to read file");
        free(buffer);
        return FALSE;
    } else {
        memcpy(destination, buffer + startingOffset, nBytesToRead - startingOffset);
        free(buffer);
        return TRUE;
    }
}

BOOL GetInodeByNumber(HANDLE hExt2, DWORD inodeNumber, OUT ext2_inode* lpInode) {
    DWORD inodesPerGroup = g_mainSuperBlock.s_inodes_per_group;
    DWORD blockGroupNumber = (inodeNumber - 1) / inodesPerGroup;
    LONGLONG blockGroupDescriptorTableLocation;
    if (llBlockSize == 1024)
        blockGroupDescriptorTableLocation =  2 * llBlockSize;
    else
        blockGroupDescriptorTableLocation =  1 * llBlockSize;
    LONGLONG entryLocation = blockGroupDescriptorTableLocation
                             + (LONGLONG) DEXT2_GROUP_DESCRIPTOR_ENTRY_SIZE*blockGroupNumber;
    ext2_group_desc descriptor;
    if (!ReadBytes(hExt2, g_partitionStart + entryLocation, sizeof(ext2_group_desc), &descriptor)) {
        DEXT2_LOG_DEBUG("GetInodeByNumber fail");
        return FALSE;
    }
    LONGLONG inodeTableLocation = (LONGLONG) descriptor.bg_inode_table * llBlockSize;
    DWORD inodeIndex = (inodeNumber - 1) % inodesPerGroup;
    LONGLONG inodePhysicalLocation = inodeTableLocation + DEXT2_INODE_SIZE*((LONGLONG) inodeIndex);
    if(!ReadBytes(hExt2, g_partitionStart + inodePhysicalLocation, sizeof(ext2_inode), lpInode)) {
        DEXT2_LOG_DEBUG("GetInodeByNumber fail");
        return FALSE;
    }
    return TRUE;
}

DEXT2_ERROR SeekInodeByFileName(HANDLE hExt2, LPCSTR fileName, ext2_inode* pInode, OUT ext2_inode* pNewInode) {
    if ((pInode->i_mode & DEXT2_INODE_IS_DIR) == 0) {
        return DEXT2_ERROR_FILE_MISSING;
    }
    PDWORD dataBlocks = NULL;
    ULONGLONG dataBlocksSize;
    if(!GetDataBlocks(hExt2, pInode, &dataBlocks, &dataBlocksSize)) {
        return DEXT2_ERROR_READING_DISK;
    }
    PBYTE buffer = (PBYTE) malloc(llBlockSize);
    PBYTE dePointer = buffer;
    for (DWORD i = 0; i < dataBlocksSize; i++) {
        if(!ReadBytes(hExt2, g_partitionStart + llBlockSize*dataBlocks[i], dwBlockSize, buffer)) {
            free(buffer);
            free(dataBlocks);
            return DEXT2_ERROR_READING_DISK;
        }
        dePointer = buffer;
        while (TRUE) {
            ext2_dir_entry de = *(ext2_dir_entry*) dePointer;
            if ((LONGLONG) (dePointer - buffer) >= llBlockSize) {
                break;
            }
            if (de.rec_len == 0) {
                free(buffer);
                free(dataBlocks);
                return DEXT2_ERROR_FILE_MISSING;
            }
            if (strncmp(fileName, de.name, 255) == 0) {
                if (!GetInodeByNumber(hExt2, de.inode, pNewInode)) {
                    free(buffer);
                    free(dataBlocks);
                    return DEXT2_ERROR_READING_DISK;
                }
                free(buffer);
                free(dataBlocks);
                return DEXT2_NO_ERROR;
            }
            dePointer += de.rec_len;
        }
    }
    
    free(buffer);
    free(dataBlocks);
    return DEXT2_ERROR_FILE_MISSING;
}
DEXT2_ERROR GetChilds(HANDLE hExt2, ext2_inode* pInode, OUT ext2_dir_entry** directoryEntries, OUT PULONGLONG arraySize) {
    *arraySize = 32;
    *directoryEntries = (ext2_dir_entry*) malloc((*arraySize) * sizeof(ext2_dir_entry));
    if (*directoryEntries == NULL) {
        return DEXT2_ERROR_INTERNAL;
    }

    ULONGLONG deIndex = 0;
    if ((pInode->i_mode & DEXT2_INODE_IS_DIR) == 0) {
        free(*directoryEntries);
        return DEXT2_ERROR_FILE_MISSING;
    }

    PDWORD dataBlocks = NULL;
    ULONGLONG dataBlocksSize;
    if (!GetDataBlocks(hExt2, pInode, &dataBlocks, &dataBlocksSize)) {
        free(*directoryEntries);
        return DEXT2_ERROR_READING_DISK;
    }

    PBYTE buffer = (PBYTE) malloc(llBlockSize);
    if (buffer == NULL) {
        free(*directoryEntries);
        free(dataBlocks);
        return DEXT2_ERROR_INTERNAL;
    }

    for (DWORD i = 0; i < dataBlocksSize; i++) {
        if (!ReadBytes(hExt2, g_partitionStart + llBlockSize * dataBlocks[i], dwBlockSize, buffer)) {
            free(buffer);
            free(dataBlocks);
            free(*directoryEntries);
            return DEXT2_ERROR_READING_DISK;
        }

        PBYTE dePointer = buffer;
        while (TRUE) {
            ext2_dir_entry de = *(ext2_dir_entry*) dePointer;
            if ((LONGLONG)(dePointer - buffer) >= llBlockSize) {
                break;
            }
            if (de.rec_len == 0) {
                free(buffer);
                free(dataBlocks);
                free(*directoryEntries);
                return DEXT2_ERROR_FILE_MISSING;
            }

            (*directoryEntries)[deIndex] = de;
            deIndex++;
            if (deIndex >= *arraySize) {
                ext2_dir_entry* temp = (ext2_dir_entry*) realloc(*directoryEntries, (*arraySize) * 2 * sizeof(ext2_dir_entry));
                if (temp == NULL) {
                    free(*directoryEntries);
                    free(buffer);
                    free(dataBlocks);
                    return DEXT2_ERROR_INTERNAL;
                }
                *directoryEntries = temp;
                *arraySize *= 2;
            }
            dePointer += de.rec_len;
        }
    }

    *arraySize = deIndex;
    free(buffer);
    free(dataBlocks);
    return DEXT2_NO_ERROR;
}


DEXT2_ERROR _ResolvePathInner(HANDLE hExt2, LPCSTR path, ext2_inode* pInode) {
    CHAR fileName[256];
    DWORD i = 0;
    while (path[i] != '\0' && path[i] != '/') {
        if (i >= 256) {
            return DEXT2_ERROR_INTERNAL;
        }
        fileName[i] = path[i];
        i++;   
    }
    fileName[i] = '\0';
    ext2_inode newInode;
    DEXT2_ERROR status = SeekInodeByFileName(hExt2, fileName, pInode, &newInode);
    if (status != DEXT2_NO_ERROR) {
        return status;
    }
    *pInode = newInode;
    DWORD pathLength = strnlen(path, 256);
    if (pathLength == i || (path[pathLength-1] == '/' && pathLength - 1 == i)) {
        return DEXT2_NO_ERROR;
    }
    return _ResolvePathInner(hExt2, path + i + 1, pInode);
}

DEXT2_ERROR ResolvePath(HANDLE hExt2, LPCSTR path, OUT ext2_inode* pInode) {
    if (path[0] != '/') {
        return DEXT2_ERROR_FILE_MISSING;
    }
    if (!GetInodeByNumber(hExt2, 2, pInode)) {
        return DEXT2_ERROR_READING_DISK;
    }
    if (path[1] == '\0') {
        return DEXT2_NO_ERROR;
    }
    return _ResolvePathInner(hExt2, path + 1, pInode);
}

BOOL GetPartitions(HANDLE hDisk, OUT PPARTITION_INFORMATION_EX* partitions, OUT PDWORD arrayLength) {
    DWORD bytesReturned;
    size_t bufferSize = sizeof(DRIVE_LAYOUT_INFORMATION_EX) 
        + DEXT2_MAX_PARTITION_COUNT * sizeof(PARTITION_INFORMATION_EX);
    PDRIVE_LAYOUT_INFORMATION_EX driveLayout = (PDRIVE_LAYOUT_INFORMATION_EX) malloc(bufferSize);

    if (!DeviceIoControl(
            hDisk,
            IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
            NULL,
            0,
            driveLayout,
            (DWORD) bufferSize,
            &bytesReturned,
            NULL)
    ) {
        free(driveLayout);
        return FALSE;
    }

    if (
        driveLayout->PartitionStyle != PARTITION_STYLE_GPT
        && driveLayout->PartitionStyle != PARTITION_STYLE_MBR
    ) {
        free(driveLayout);
        return FALSE;
    }

    *arrayLength = driveLayout->PartitionCount;
    size_t partitionsSize = *arrayLength * sizeof(PARTITION_INFORMATION_EX);
    *partitions = (PPARTITION_INFORMATION_EX) malloc(partitionsSize);
    memcpy(*partitions, driveLayout->PartitionEntry, partitionsSize);
    free(driveLayout);
    return TRUE;
}

BOOL IsPartitionEmpty(PPARTITION_INFORMATION_EX partition) {
    if (partition->PartitionStyle == PARTITION_STYLE_MBR) {
        return partition->Mbr.PartitionType == PARTITION_ENTRY_UNUSED; 
    } 
    else if (partition->PartitionStyle == PARTITION_STYLE_GPT) {
        GUID emptyGuid = {0};
        return memcmp(&partition->Gpt.PartitionType, &emptyGuid, sizeof(GUID)) == 0;
    }
    return FALSE;
}

BOOL GetDataBlocks(HANDLE hExt2, ext2_inode* pInode, OUT PDWORD* dataBlocks, OUT PULONGLONG dataBlocksSize) {
    DWORD fileSize = pInode->i_size;
    DWORD nDataBlocks = fileSize / dwBlockSize + ((fileSize % dwBlockSize) != 0);
    DWORD addressesPerBlock = dwBlockSize / sizeof(DWORD);
    *dataBlocksSize = nDataBlocks;
    *dataBlocks = (PDWORD) malloc(nDataBlocks * sizeof(DWORD));

    PDWORD indirect = (PDWORD) malloc(llBlockSize);
    PDWORD doublyIndirect = (PDWORD) malloc(llBlockSize);
    PDWORD treblyIndirect = (PDWORD) malloc(llBlockSize);

    DWORD currentBlockIndex = 0;
    { // direct
        for (; currentBlockIndex < nDataBlocks && currentBlockIndex < 12; currentBlockIndex++) {
            (*dataBlocks)[currentBlockIndex] = pInode->i_block[currentBlockIndex];
        }
    }
    { // singly indirect
        LONGLONG indirectBlockLocation1 = (LONGLONG) pInode->i_block[12] * llBlockSize;
        if (!ReadBytes(hExt2, indirectBlockLocation1 + g_partitionStart, dwBlockSize, indirect)) {
            goto fail;
        }
        for (DWORD i = 0; currentBlockIndex < nDataBlocks && i < addressesPerBlock; currentBlockIndex++, i++) {
            (*dataBlocks)[currentBlockIndex] = indirect[i];
        }
    }

    { // doubly indirect
        LONGLONG indirectBlockLocation2 = (LONGLONG) pInode->i_block[13] * llBlockSize;
        if (!ReadBytes(hExt2, indirectBlockLocation2 + g_partitionStart, dwBlockSize, doublyIndirect)) {
            goto fail;
        }
        for (DWORD j = 0; currentBlockIndex < nDataBlocks && j < addressesPerBlock; currentBlockIndex++, j++) {
            LONGLONG indirectBlockLocation1 = (LONGLONG) doublyIndirect[j] * llBlockSize;
            if (!ReadBytes(hExt2, indirectBlockLocation1 + g_partitionStart, dwBlockSize, indirect)) {
                goto fail;
            }
            for (DWORD i = 0; currentBlockIndex < nDataBlocks && i < addressesPerBlock; currentBlockIndex++, i++) {
                (*dataBlocks)[currentBlockIndex] = indirect[i];
            }
        }
    }

    { // trebly indirect
        LONGLONG indirectBlockLOcation3 = (LONGLONG) pInode->i_block[14] * llBlockSize;
        if (!ReadBytes(hExt2, indirectBlockLOcation3 + g_partitionStart, dwBlockSize, treblyIndirect)) {
            goto fail;
        }
        for (DWORD k = 0; currentBlockIndex < nDataBlocks && k < addressesPerBlock; currentBlockIndex++, k++) {
            LONGLONG indirectBlockLocation2 = (LONGLONG) treblyIndirect[k] * llBlockSize;
            if (!ReadBytes(hExt2, indirectBlockLocation2 + g_partitionStart, dwBlockSize, doublyIndirect)) {
                goto fail;
            }
            for (DWORD j = 0; currentBlockIndex < nDataBlocks && j < addressesPerBlock; currentBlockIndex++, j++) {
                LONGLONG indirectBlockLocation1 = (LONGLONG) doublyIndirect[j] * llBlockSize;
                if (!ReadBytes(hExt2, indirectBlockLocation1 + g_partitionStart, dwBlockSize, indirect)) {
                    goto fail;
                }
                for (DWORD i = 0; currentBlockIndex < nDataBlocks && i < addressesPerBlock; currentBlockIndex++, i++) {
                    (*dataBlocks)[currentBlockIndex] = indirect[i];
                }
            }
        }
    }

    success:
        free(indirect);
        free(doublyIndirect);
        free(treblyIndirect);
        return TRUE;
    fail:
        free(indirect);
        free(doublyIndirect);
        free(treblyIndirect);
        return FALSE;
}

BOOL GetAvailableDisks(LPSTR** disks, PDWORD* disksNumbers, PDWORD arraySize) {
    DWORD drives = GetLogicalDrives();
    if (drives == 0) {
        DEXT2_LOG_DEBUG("GetLogicalDrives failed with error: %lu\n", GetLastError());
        return FALSE;
    }

    DWORD count = 0;
    for (int i = 0; i < 26; i++) {
        if (drives & (1 << i)) {
            count++;
        }
    }

    if (count == 0) {
        return FALSE;
    }

    *disks = (LPSTR*) malloc(count * sizeof(LPSTR));
    *disksNumbers = (PDWORD) malloc(count * sizeof(DWORD));

    DWORD index = 0;
    for (int i = 0; i < 26; i++) {
        if (drives & (1 << i)) {
            char* driveName = (char*)malloc(4 * sizeof(char)); // "C:\0"
            snprintf(driveName, 4, "%c:\\", 'A' + i);
            (*disks)[index] = driveName;
            (*disksNumbers)[index] = index;
            index++;
        }
    }

    *arraySize = count;
    return TRUE;
}

void FreeDiskArray(LPSTR* disks, PDWORD disksNumbers, DWORD arraySize) {
    for (DWORD i = 0; i < arraySize; i++) {
        free((void*)disks[i]);
    }
    free(disks);
    free(disksNumbers);
}

BOOL ReadDataFromInode(HANDLE hExt2, HANDLE hWinFile, ext2_inode* pInode) {
    PDWORD dataBlocks = NULL;
    ULONGLONG dataBlocksSize = 0;
    PBYTE buffer = (PBYTE) malloc(llBlockSize);
    GetDataBlocks(hExt2, pInode, &dataBlocks, &dataBlocksSize);
    for (ULONGLONG i = 0; i < dataBlocksSize - 1; i++) {
        LONGLONG dataLocation = (LONGLONG) dataBlocks[i] * llBlockSize;
        if (!ReadBytes(hExt2, g_partitionStart + dataLocation, llBlockSize, buffer)) {
            DEXT2_LOG_DEBUG("Error reading data block");
            free(dataBlocks);
            free(buffer);
            return FALSE;
        }
        DWORD written;
        if (!WriteFile(hWinFile, buffer, dwBlockSize, &written, NULL)) {
            DEXT2_LOG_DEBUG("Error writing to file");
            free(dataBlocks);
            free(buffer);
            return FALSE;
        }
        if (written < dwBlockSize) {
            DEXT2_LOG_DEBUG("Error writing to file");
            free(dataBlocks);
            free(buffer);
            return FALSE;
        }
    }

    LONGLONG dataLocation = (LONGLONG) dataBlocks[dataBlocksSize - 1] * llBlockSize;
    if (!ReadBytes(hExt2, g_partitionStart + dataLocation, llBlockSize, buffer)) {
        DEXT2_LOG_DEBUG("Error reading data block");
        free(dataBlocks);
        free(buffer);
        return FALSE;
    }
    DWORD written;
    DWORD nBytesToWrite = pInode->i_size % dwBlockSize;
    if (!WriteFile(hWinFile, buffer, nBytesToWrite, &written, NULL)) {
        DEXT2_LOG_DEBUG("Error writing last block to file");
        free(dataBlocks);
        free(buffer);
        return FALSE;
    }
    if (written < nBytesToWrite) {
        DEXT2_LOG_DEBUG("Error writing last block to file");
        free(dataBlocks);
        free(buffer);
        return FALSE;
    }
    free(buffer);
    free(dataBlocks);
    return TRUE;
}

DEXT2_ERROR CopyFileToWindows(HANDLE hExt2, LPCSTR ext2FilePath, LPCSTR winFilePath) {
    DEXT2_ERROR status;
    ext2_inode inode;
    status = ResolvePath(hExt2, ext2FilePath, &inode);
    if (status != DEXT2_NO_ERROR) {
        return status;
    }

    HANDLE hWinFile = CreateFileA(
        winFilePath, 
        GENERIC_WRITE, 
        0, // no sharing
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hWinFile == INVALID_HANDLE_VALUE) {
        return DEXT2_ERROR_INTERNAL;
    }

    if (!ReadDataFromInode(hExt2, hWinFile, &inode)) {
        return DEXT2_ERROR_INTERNAL;
    }

    if (!CloseHandle(hWinFile)) {
        return DEXT2_ERROR_INTERNAL;
    }

    return DEXT2_NO_ERROR;
}

DEXT2_ERROR CopyInodeDataToWindows(HANDLE hExt2, ext2_inode* pInode, LPCSTR winFilePath) {
    DEXT2_ERROR status;
    ext2_inode inode;

    HANDLE hWinFile = CreateFileA(
        winFilePath, 
        GENERIC_WRITE, 
        0, // no sharing
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hWinFile == INVALID_HANDLE_VALUE) {
        return DEXT2_ERROR_INTERNAL;
    }

    if (!ReadDataFromInode(hExt2, hWinFile, &inode)) {
        return DEXT2_ERROR_INTERNAL;
    }

    if (!CloseHandle(hWinFile)) {
        return DEXT2_ERROR_INTERNAL;
    }

    return DEXT2_NO_ERROR;
}

DEXT2_ERROR InitSuperblock(HANDLE hDisk) {
    if (!ReadBytes(hDisk, g_partitionStart + DEXT2_SUPERBLOCK_OFFSET, sizeof(ext2_super_block), &g_mainSuperBlock)) {
        return DEXT2_ERROR_INTERNAL;
    }
    if (g_mainSuperBlock.s_magic != DEXT2_SUPER_MAGIC) return DEXT2_ERROR_NOT_EXT2;
    return DEXT2_NO_ERROR;
}

#endif // DEXT2_IMPLEMENTATION