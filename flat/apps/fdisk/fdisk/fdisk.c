#include "usertraps.h"
#include "misc.h"

#include "fdisk.h"

//TODO: look at dfs.c where i defined these?
dfs_superblock sb;
//dfs_inode inodes[DFS_INODE_MAX_NUM];
//uint32 fbv[DFS_FBV_MAX_NUM_WORDS];

int diskblocksize = 0; // These are global in order to speed things up
int disksize = 0;      // (i.e. fewer traps to OS to get the same number)

int FdiskWriteBlock(uint32 blocknum, dfs_block *b); //You can use your own function. This function 
//calls disk_write_block() to write physical blocks to disk

void main (int argc, char *argv[])
{
	// STUDENT: put your code here. Follow the guidelines below. They are just the main steps. 
	// You need to think of the finer details. You can use bzero() to zero out bytes in memory

  //Initializations and argc check

  // Need to invalidate filesystem before writing to it to make sure that the OS
  // doesn't wipe out what we do here with the old version in memory
  // You can use dfs_invalidate(); but it will be implemented in Problem 2. You can just do 
  printf("Test.");
  sb.fileSystemValid = 0;
  disksize = DiskSize(); //0x4000000; //64MB
  diskblocksize = DiskBytesPerBlock(); //256
  sb.fileSystemBlockSize = 1024;
  sb.numberFileSystemBlocks = 0x10000;  // 65536 blocks (64MB / 1024)

  // Make sure the disk exists before doing anything else
  DiskCreate();

  // Write all inodes as not in use and empty (all zeros)
  sb.inodesStartingBlockNumber = FDISK_INODE_BLOCK_START;
  sb.numberInodes = 256;
  //inode array is file system blocks 2-33, physical blocks 8-135
  for (int i = 0; i < sb.numberInodes; i++) {
    bzero((FDISK_INODE_BLOCK_START+i)*4, diskblocksize);
    bzero((FDISK_INODE_BLOCK_START+i)*4+1, diskblocksize);
    bzero((FDISK_INODE_BLOCK_START+i)*4+2, diskblocksize);
    bzero((FDISK_INODE_BLOCK_START+i)*4+3, diskblocksize);
  }
  // Next, setup free block vector (fbv) and write free block vector to the disk
  sb.freeBlockVectorStartingBlockNumber = FDISK_FBV_BLOCK_START;
  sb.numFBVBlocks = 8;
  //free block vector is file system blocks 34-41, physical blocks 136-167
  for (int i = 0; i < sb.numFBVBlocks; i++) {
    bzero((FDISK_FBV_BLOCK_START+i)*4, diskblocksize);
    bzero((FDISK_FBV_BLOCK_START+i)*4+1, diskblocksize);
    bzero((FDISK_FBV_BLOCK_START+i)*4+2, diskblocksize);
    bzero((FDISK_FBV_BLOCK_START+i)*4+3, diskblocksize);
  }
  // Finally, setup superblock as valid filesystem and write superblock and boot record to disk: 
  sb.fileSystemValid = 1;
  // boot record is all zeros in the first physical block, and superblock structure goes into the second physical block
  // Uh, shouldn't boot record be in first FILE system block (physical blocks 0-3) and superblock in second FILE system block (physical blocks 4-7)
  bzero(0, sb.fileSystemBlockSize);
  bcopy(4, &sb, sb.fileSystemBlockSize); //physical block 4 is the first block of the superblock
  bcopy(262140, &sb, sb.fileSystemBlockSize); //copy the superblock also to file system block 65535, where the copy of the superblock goes
  Printf("fdisk (%d): Formatted DFS disk for %d bytes.\n", getpid(), disksize);
}

int FdiskWriteBlock(uint32 blocknum, dfs_block *b) {
  // STUDENT: put your code here
  disk_write_block(blocknum, b);
}
