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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define DEXT2_IMPLEMENTATION
#include "dext2.h"

#define MAX_INPUT 1024
#define MAX_ARGS 3

// WARNING - bad code
// helper functions are written by AI
char *trim_whitespace(char *str) {
    while (isspace((unsigned char)*str)) str++; // Trim leading
    if (*str == 0) return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--; // Trim trailing
    *(end + 1) = '\0';
    return str;
}

int parse_input(char *input, char *args[], int max_args) {
    int arg_count = 0;
    char *ptr = input;
    while (*ptr != '\0' && arg_count < max_args) {
        while (isspace((unsigned char)*ptr)) ptr++; // Skip spaces
        if (*ptr == '\0') break;
        if (*ptr == '"') { // Handle quoted strings
            ptr++;
            args[arg_count++] = ptr;
            while (*ptr != '\0' && *ptr != '"') ptr++;
            if (*ptr == '"') *ptr++ = '\0';
        } else { // Handle normal words
            args[arg_count++] = ptr;
            while (*ptr != '\0' && !isspace((unsigned char)*ptr)) ptr++;
            if (*ptr != '\0') *ptr++ = '\0';
        }
    }
    return arg_count;
}

int main(void) {
    CHAR drive[50];
    {
        printf("Select disk\n");
        LPSTR* disks = NULL;
        PDWORD disksNumbers;
        DWORD disksLength;
        if (!GetAvailableDisks(&disks, &disksNumbers, &disksLength)) {
            printf("Could not get available disks\n");
            return 1;
        }
        disk_selection:
        printf("%-8s   %-8s\n", "Number", "Name");
        for (DWORD i = 0; i < disksLength; i++) {
            printf("%-8d | %-8s\n", i+1, disks[i]);
        }
        DWORD selectedDisk = -1;
        scanf("%d", &selectedDisk);
        if (selectedDisk < 1 || selectedDisk > disksLength) {
            printf("Wrong disk number! Try again\n");
            goto disk_selection;
        }
        snprintf(drive, sizeof(drive), "\\\\.\\PhysicalDrive%d\0", disksNumbers[selectedDisk-1]);

        printf("Opening %s as %s...\n", disks[selectedDisk-1],  drive);
        FreeDiskArray(disks, disksNumbers, disksLength);
    }
    HANDLE hDisk = CreateFileA(drive, GENERIC_READ, 
                               0, // no sharing
                               NULL, OPEN_EXISTING, 0, NULL);
    if (hDisk == INVALID_HANDLE_VALUE) {
        printf("Could not open disk.\n");
        return 1;
    }

    PPARTITION_INFORMATION_EX partitions;
    DWORD partitionsCount;
    if (!GetPartitions(hDisk, &partitions, &partitionsCount)) {
        printf("Could not read partitions\n");
        return 1;
    }

    partition_selection:
    printf("Select partition\n");
    printf("%-13s   %-13s   %-13s\n", "Number", "Offset (MiB)", "Size (MiB)");
    DWORD j = 1;
    PLONG jToi = malloc((partitionsCount + 1) * sizeof(DWORD));
    for (DWORD i = 0; i < partitionsCount + 1; i++) {
        jToi[i] = -1;
    }
    BOOL flag = FALSE;
    for (DWORD i = 0; i < partitionsCount; i++) {
        g_partitionStart = partitions[i].StartingOffset.QuadPart;
        DEXT2_ERROR status = InitSuperblock(hDisk);
        switch (status)
        {
        case DEXT2_ERROR_INTERNAL:
            printf("Error reading partition\n");
            free(jToi);
            return 1;
            break;
        case DEXT2_ERROR_NOT_EXT2:
            continue;
            break;
        case DEXT2_NO_ERROR:
            flag = TRUE;
            printf("%-13d | %-13lld | %-13lld\n", j, partitions[i].StartingOffset.QuadPart / MiB, partitions[i].PartitionLength.QuadPart / MiB);
            jToi[j] = i;
            j++;
            break;
        }
    }
    if (!flag) {
        printf("No ext2 partitions was found");
        free(jToi);
        return 1;
    }
    DWORD selectedPartition = -1;
    scanf("%d", &selectedPartition);
    if (jToi[selectedPartition] == -1) {
        printf("Wrong partition number! Try again\n");
        goto partition_selection;
    }

    g_partitionStart = g_partitionStart = partitions[jToi[selectedPartition]].StartingOffset.QuadPart;
    DEXT2_ERROR status = InitSuperblock(hDisk);
    if (status != DEXT2_NO_ERROR) {
        printf("Error reading file systems superblock");
        free(jToi);
        return 1;
    }
    free(jToi);

    char input[MAX_INPUT];
    char *args[MAX_ARGS];

    ext2_inode currentInode;
    if (!GetInodeByNumber(hDisk, 2, &currentInode)) {
        printf("Error reading file system");
    }
    while (1) {
        if (!fgets(input, sizeof(input), stdin)) break;
        input[strcspn(input, "\n")] = '\0'; // Remove newline

        int arg_count = parse_input(input, args, MAX_ARGS);
        if (arg_count == 0) continue;

        if (strcmp(args[0], "cd") == 0) {
            if (arg_count != 2) {
                printf("Usage: cd <path>\n");
                continue;
            }
            if (args[1][0] != '/') {
                switch (_ResolvePathInner(hDisk, args[1], &currentInode))
                {
                    case DEXT2_ERROR_INTERNAL:
                        printf("Internal error\n");
                        return 1;
                        break;
                    case DEXT2_ERROR_READING_DISK:
                        printf("Unable to read disk\n");
                        return 1;
                        break;
                    case DEXT2_ERROR_FILE_MISSING:
                        printf("No such directory\n");
                        break;
                    case DEXT2_NO_ERROR:
                        break;
                    default:
                        printf("Something went wrong\n");
                        return 1;
                }
            } else { 
                switch (ResolvePath(hDisk, args[1], &currentInode))
                {
                    case DEXT2_ERROR_INTERNAL:
                        printf("Internal error\n");
                        return 1;
                        break;
                    case DEXT2_ERROR_READING_DISK:
                        printf("Unable to read disk\n");
                        return 1;
                        break;
                    case DEXT2_ERROR_FILE_MISSING:
                        printf("No such directory\n");
                        break;
                    case DEXT2_NO_ERROR:
                        break;
                    default:
                        printf("Something went wrong\n");
                        return 1;
                }
            }

        } else if (strcmp(args[0], "dir") == 0) {
            if (arg_count != 1) {
                printf("Usage: dir\n");
                continue;
            }
            ext2_dir_entry* des = NULL;
            ULONGLONG desSize;
            if (GetChilds(hDisk, &currentInode, &des, &desSize) != DEXT2_NO_ERROR) {
                printf("Error reading directory\n");
                return 1;
            }
            for (DWORD i = 0; i < desSize; i++) {
                printf("%s\n", des[i].name);
            }
            free(des);

        } else if (strcmp(args[0], "read") == 0) {
            if (arg_count != 3) {
                printf("Usage: read <path1> <path2>\n");
                continue;
            }
            ext2_inode tmpInode = currentInode;
            if (args[1][0] != '/') {
                switch (_ResolvePathInner(hDisk, args[1], &tmpInode))
                {
                    case DEXT2_ERROR_INTERNAL:
                        printf("Internal error\n");
                        return 1;
                        break;
                    case DEXT2_ERROR_READING_DISK:
                        printf("Unable to read disk\n");
                        return 1;
                        break;
                    case DEXT2_ERROR_FILE_MISSING:
                        printf("No such directory\n");
                        break;
                    case DEXT2_NO_ERROR:
                        break;
                    default:
                        printf("Something went wrong\n");
                        return 1;
                }
            } else { 
                switch (ResolvePath(hDisk, args[1], &tmpInode))
                {
                    case DEXT2_ERROR_INTERNAL:
                        printf("Internal error\n");
                        return 1;
                        break;
                    case DEXT2_ERROR_READING_DISK:
                        printf("Unable to read disk\n");
                        return 1;
                        break;
                    case DEXT2_ERROR_FILE_MISSING:
                        printf("No such directory\n");
                        break;
                    case DEXT2_NO_ERROR:
                        break;
                    default:
                        printf("Something went wrong\n");
                        return 1;
                }
            }
            HANDLE hWinFile = CreateFileA(
                args[2], 
                GENERIC_WRITE, 
                0, // no sharing
                NULL,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                NULL
            );

            if (hWinFile == INVALID_HANDLE_VALUE) {
                printf("Error creating file on Windows\n");
                return 1;
            }
            ReadDataFromInode(hDisk, hWinFile, &tmpInode);
            CloseHandle(hWinFile);

        } else if (strcmp(args[0], "exit") == 0) {
            break;
        } else {
            printf("Unknown command: %s\n", args[0]);
        }
    }

    return 0;
    // ext2_inode inode;
    // ext2_inode newInode;
    // g_partitionStart = 1024*1024;
    // if (initSuperblock(hDisk) != DEXT2_NO_ERROR) {
    //     printf("gg");
    //     return -1;
    // }
    // Suppose the ext2 partition starts at offset 1048576 (for example)
    // And we want inode number 2 (the root inode is usually 2)
    // if (GetInodeByNumber(hDisk, 2, &inode)) {
    //     printf("Successfully read inode 2.\n");
    //     // printf("%lu\n", inode.i_block[0]);
    //     LARGE_INTEGER li = {.QuadPart = g_partitionStart + inode.i_block[0]*dwBlockSize};
    //     printf("block number %d\n", inode.i_block[0]);
    //     SetFilePointerEx(hDisk, li, NULL, FILE_BEGIN);
    //     uint8_t* buf = malloc(dwBlockSize);
    //     ReadFile(hDisk, buf, dwBlockSize, NULL, NULL);
    //     for (int i = 0; i < 4; i++) {
    //         ext2_dir_entry de = * ((ext2_dir_entry*) buf);
    //         printf("main %s\n", de.name);
    //         printf("main %lu\n", de.inode);
    //         // if (strncmp(de.name, "test.txt", 20)) {
    //         //     PDWORD dataBlocks = NULL;
    //         //     ULONGLONG dataBlocksSize;
    //         //     ext2_inode inode;
    //             // GetInodeByNumber(hDisk, de.inode, &inode);
    //             // GetDataBlocks(hDisk, &inode, &dataBlocks, &dataBlocksSize);
    //             // for (DWORD i = 0; i < dataBlocksSize; i++) {
    //             //     printf("%lu\n", dataBlocks[i]); 
    //             // }
    //         // }
    //         // printf("links = %d\n", inode.i_links_count);
    //         buf += de.rec_len;  
    //     }
    //     // ext2_inode myInode;
    //     // GetInodeByNumber(hDisk, 12, &myInode);
    //     // printf("from 11:\n");
    //     // printf("%d\n", myInode.i_blocks);
    //     // printf("%d\n", myInode.i_block[12]);
    //     // printf("%d\n", myInode.i_block[13]);
    //     // printf("%d\n", myInode.i_block[14]);

    //     printf("status %d\n", ResolvePath(hDisk, "/abc/a.txt", &inode));
    //     printf("OOOOOOOOOOOOOO\n");

    //     printf("BLOCKS:\n");
    //     for (DWORD i = 0; i < 15; printf("%d\n", inode.i_block[i++]));
    //     PDWORD dataBlocks = NULL;
    //     ULONGLONG dataBlocksSize;
    //     printf("---- %d\n", GetDataBlocks(hDisk, &inode, &dataBlocks, &dataBlocksSize));
    //     for (DWORD i = 0; i < dataBlocksSize; printf("   > %d\n", dataBlocks[i++]));
    //     HANDLE hWinFile = CreateFileA("C:\\Users\\User\\course_work\\testfile.txt", GENERIC_WRITE, 
    //                            FILE_SHARE_READ | FILE_SHARE_WRITE,
    //                            NULL, CREATE_ALWAYS, 0, NULL);
    //     if (hDisk == INVALID_HANDLE_VALUE) {
    //         fprintf(stderr, "Error: Could not open disk.\n");
    //         return 1;
    //     }
    //     if(!ReadData(hDisk, hWinFile, &inode)) printf("FUCK\n");

    // } else {
    //     fprintf(stderr, "Failed to read inode.\n");
    // }
    // ext2_dir_entry* des = NULL;
    // ULONGLONG desSize;
    // ResolvePath(hDisk, "/", &inode);
    // // printf("%d\n", SeekInodeByFileName(hDisk, "aboba", &inode, &newInode));
    // GetChilds(hDisk, &inode, &des, &desSize);
    // for (DWORD i = 0; i < desSize; printf("%s\n", des[i++].name));
    // ResolvePath(hDisk, "/abc/", &inode);
    // GetChilds(hDisk, &inode, &des, &desSize);
    // for (DWORD i = 0; i < desSize; printf("%s\n", des[i++].name));
    // CopyFileToWindows(hDisk, "/abc/a.txt", "C:\\Users\\User\\course_work\\testfile.txt");
    // CopyFileToWindows(hDisk, "/abc/b.txt", "C:\\Users\\User\\course_work\\testfile2.txt");
    // CloseHandle(hDisk);
    // return 0;
}