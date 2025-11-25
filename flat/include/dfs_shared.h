#ifndef __DFS_SHARED__
#define __DFS_SHARED__

#include "files_shared.h"

typedef struct dfs_superblock {
  // STUDENT: put superblock internals here
  uint32 fileSystemValid; //a valid indicator for the file system. Boolean.
  uint32 fileSystemBlockSize; //the file system block size
  uint32 numberFileSystemBlocks; //the total number of file system blocks
  uint32 inodesStartingBlockNumber; //the starting file system block number for the array of inodes
  uint32 numberInodes; //the number of inodes in the inodes array
  uint32 freeBlockVectorStartingBlockNumber; //the starting file system block number for the free block vector.
  uint32 numFBVBlocks; //Used to store number of FBV blocks (8)
 } dfs_superblock;

#define DFS_BLOCKSIZE 1024  // Must be an integer multiple of the disk blocksize

typedef struct dfs_block {
  char data[DFS_BLOCKSIZE];
} dfs_block;

#define DIRECT_ADDRESS_TRANSLATIONS_TABLE_SIZE 10

typedef struct dfs_inode {
  // STUDENT: put inode structure internals here
  // IMPORTANT: sizeof(dfs_inode) MUST return 128 in order to fit in enough
  // inodes in the filesystem (and to make your life easier).  To do this, 
  // adjust the maximum length of the filename until the size of the overall inode 
  // is 128 bytes.
  uint32 inUse; //boolean. an in use indicator to tell if an inode is free or in use
  uint32 fileSize; //the size of the file this inode represents (i.e. the maximum byte that has been written to this file)
  char fileName[FILE_MAX_FILENAME_LENGTH]; //the filename, which is just a string. size of 72 to make the whole inode 128 bytes 
  uint32 directAddressTranslations[DIRECT_ADDRESS_TRANSLATIONS_TABLE_SIZE]; //a table of direct address translations for the first 10 virtual blocks
  uint32 indirectAddressTableBlockNumber; //a block number of a file system block on the disk which holds a table of indirect address translations for the virtual blocks beyond the first 10.
  uint32 doubleIndirectAddressTableBlockNumber; //a block number of a file system block on the disk which holds a table of double-indirect address translations for the virtual blocks beyond the first 10 and blocks under single-indirect table.
} dfs_inode;

#define DFS_MAX_FILESYSTEM_SIZE 0x4000000  // 64MB

#define DFS_FAIL -1
#define DFS_SUCCESS 1



#endif
