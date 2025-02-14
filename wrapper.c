#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif

#include <stdbool.h>
#define DEXT2_IMPLEMENTATION
#include "dext2.h"

HANDLE hExt2 = INVALID_HANDLE_VALUE;
ext2_inode currentInode = {0};

EXPORT bool wListDisks(char*** disks, int** disksNumbers, int* size) {
    return GetAvailableDisks((LPSTR**) disks, (PDWORD*) disksNumbers, (PDWORD) size);
}

EXPORT void wFreeDisks(char** disks, int* disksNumbers, int size) {
    FreeDiskArray((LPSTR*) disks, (PDWORD) disksNumbers, (DWORD) size);
}

EXPORT bool wInitHandle(int diskNum) {
    char drive[50];
    snprintf(drive, sizeof(drive), "\\\\.\\PhysicalDrive%d\0", diskNum);
    hExt2 = CreateFileA(drive, GENERIC_READ, 
                               0, // no sharing
                               NULL, OPEN_EXISTING, 0, NULL);
    if (hExt2 == INVALID_HANDLE_VALUE) {
        return false;
    }
    return true;
}

EXPORT bool wListPartitions(unsigned long long** offsets, unsigned long long** partitionsLengths, int* size) {
    PPARTITION_INFORMATION_EX partitions;
    DWORD partitionsCount;
    if (!GetPartitions(hExt2, &partitions, &partitionsCount)) {
        return false;
    }

    *offsets = malloc(DEXT2_MAX_PARTITION_COUNT * sizeof(unsigned long long));
    *partitionsLengths = malloc(DEXT2_MAX_PARTITION_COUNT * sizeof(unsigned long long));
    *size = 0;
    int j = 0;
    for (int i = 0; i < partitionsCount; i++) {
        g_partitionStart = partitions[i].StartingOffset.QuadPart;
        DEXT2_ERROR status = InitSuperblock(hExt2);
        switch (status)
        {
        case DEXT2_ERROR_INTERNAL:
            free(partitions);
            return false;
            break;
        case DEXT2_ERROR_NOT_EXT2:
            continue;
            break;
        case DEXT2_NO_ERROR:
            (*offsets)[j] = partitions[i].StartingOffset.QuadPart;
            (*partitionsLengths)[j] = partitions[i].PartitionLength.QuadPart;
            j++;
            break;
        }
    }
    free(partitions);

    *size = j;
    return true;
}

// size added for consistency
EXPORT void wFreePartitions(unsigned long long* offsets, unsigned long long* partitionsLengths, int size) {
    free(offsets);
    free(partitionsLengths);
}

EXPORT void wInitPartition(unsigned long long partitionStart) {
    g_partitionStart = partitionStart;
}

EXPORT bool wInitSuperblock(void) {
    return InitSuperblock(hExt2) == DEXT2_NO_ERROR;
}

EXPORT bool wInitFilesystem(void) {
    return GetInodeByNumber(hExt2, 2, &currentInode);
}

// EXPORT bool wGetChilds(char*** subDirs, int* size) {
//     ext2_dir_entry* des;
//     ULONGLONG desSize;
//     if (GetChilds(hExt2, &currentInode, &des, &desSize) != DEXT2_NO_ERROR) {
//         free(des);
//         return false;
//     } else {
//         for (int i = 0; i < desSize; i++) {
//             printf("%s\n", des[i].name);
//         }
//     }
//     *size = (int) desSize;
//     *subDirs = (char**) malloc(desSize * sizeof(char*));
//     for (int i = 0; i < desSize; i++) {
//         size_t dirNameLen = strnlen(des[i].name, 256);
//         (*subDirs)[i] = malloc(dirNameLen * sizeof(char));
//         strncpy((*subDirs)[i], des[i].name, 256);
//     }
//     for (int i = 0; i < desSize; i++) {
//         printf("%s\n", des[i].name);
//     }
//     free(des);
//     return true;
// }

EXPORT bool wGetChilds(char*** subDirs, bool** isDirs, int* size) {
    ext2_dir_entry* des;
    ULONGLONG desSize;

    if (GetChilds(hExt2, &currentInode, &des, &desSize) != DEXT2_NO_ERROR) {
        return false;
    }

    *size = (int) desSize;
    *subDirs = (char**) malloc(desSize * sizeof(char*));
    *isDirs = (bool*) malloc(desSize * sizeof(bool));
    if (!*subDirs || !*isDirs) {
        free(des);
        free(*isDirs);
        free(*subDirs);
        return false;
    }

    for (int i = 0; i < desSize; i++) {
        size_t dirNameLen = strnlen(des[i].name, 256);
        (*subDirs)[i] = malloc((dirNameLen + 1) * sizeof(char));
        if (!(*subDirs)[i]) {
            for (int j = 0; j < i; j++) free((*subDirs)[j]);
            free(*subDirs);
            free(*isDirs);
            free(des);
            return false;
        }
        strncpy((*subDirs)[i], des[i].name, dirNameLen);
        (*subDirs)[i][dirNameLen] = '\0';
        ext2_inode tmp;
        if (!GetInodeByNumber(hExt2, des[i].inode, &tmp)) {
            free(*subDirs);
            free(*isDirs);
            free(des);
            return false;
        }
        (*isDirs)[i] = tmp.i_mode & DEXT2_INODE_IS_DIR;
    }

    free(des);
    return true;
}

EXPORT void wFreeChilds(char** subDirs, bool* isDirs, int size) {
    if (!subDirs) return;
    for (int i = 0; i < size; i++) {
        free(subDirs[i]);
    }
    free(subDirs);
    free(isDirs);
}

EXPORT bool cdToDir(char* path) {
    if (path[0] != '/') {
        switch (_ResolvePathInner(hExt2, (LPSTR) path, &currentInode))
        {
            case DEXT2_ERROR_INTERNAL:
                return false;
                break;
            case DEXT2_ERROR_READING_DISK:
                return false;
                break;
            case DEXT2_ERROR_FILE_MISSING:
                return false;
                break;
            case DEXT2_NO_ERROR:
                break;
            default:
                return false;
        }
    } else { 
        switch (ResolvePath(hExt2, (LPSTR) path, &currentInode))
        {
            case DEXT2_ERROR_INTERNAL:
                return false;
                break;
            case DEXT2_ERROR_READING_DISK:
                return false;
                break;
            case DEXT2_ERROR_FILE_MISSING:
                return false;
                break;
            case DEXT2_NO_ERROR:
                break;
            default:
                return false;
        }
    }
    return true;
}

EXPORT bool readFileToWindows(const char* extPath, const char* winPath) {
    ext2_inode tmpInode = currentInode;
    if (extPath[0] != '/') {
        switch (_ResolvePathInner(hExt2, extPath, &tmpInode))
        {
            case DEXT2_ERROR_INTERNAL:
                return false;
                break;
            case DEXT2_ERROR_READING_DISK:
                return false;
                break;
            case DEXT2_ERROR_FILE_MISSING:
                return false;
                break;
            case DEXT2_NO_ERROR:
                break;
            default:
                return false;
        }
    } else { 
        switch (ResolvePath(hExt2, extPath, &tmpInode))
        {
            case DEXT2_ERROR_INTERNAL:
                return false;
                break;
            case DEXT2_ERROR_READING_DISK:
                return false;
                break;
            case DEXT2_ERROR_FILE_MISSING:
                return false;
                break;
            case DEXT2_NO_ERROR:
                break;
            default:
                return false;
        }
    }
    HANDLE hWinFile = CreateFileA(
        winPath, 
        GENERIC_WRITE, 
        0, // no sharing
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hWinFile == INVALID_HANDLE_VALUE) {
        return false;
    }
    ReadDataFromInode(hExt2, hWinFile, &tmpInode);
    CloseHandle(hWinFile);

    return true;
}