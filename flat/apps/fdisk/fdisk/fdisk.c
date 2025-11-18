#include "usertraps.h"
#include "misc.h"

#include "fdisk.h"

//TODO: look at dfs.c where i defined these?
dfs_superblock sb;
//dfs_inode inodes[DFS_INODE_MAX_NUM];
//uint32 fbv[DFS_FBV_MAX_NUM_WORDS];

int diskblocksize = 0; // These are global in order to speed things up
int disksize = 0;      // (i.e. fewer traps to OS to get the same number)

int FdiskWriteFileSystemBlock(uint32 blocknum, dfs_block *b); //You can use your own function. This function 
//calls disk_write_block() to write physical blocks to disk
int FdiskWriteZerosToFileSystemBlock(uint32 blocknum);
void PrintPhysicalBlock(char* b);

void main (int argc, char *argv[])
{
  int i; //loop var
  dfs_block *block; //used to write FBV
  dfs_block *sb_dfsblock; //used to write superblock
	// STUDENT: put your code here. Follow the guidelines below. They are just the main steps. 
	// You need to think of the finer details. You can use bzero() to zero out bytes in memory
  int blksz = disk_blocksize();
  char testBlock[256];
  for (i = 0; i < blksz; i++) {
    Printf((char)testBlock[i]);
    Printf('a');
  }
  Printf("\n");
  PrintPhysicalBlock(testBlock);








  //Initializations and argc check

  // Need to invalidate filesystem before writing to it to make sure that the OS
  // doesn't wipe out what we do here with the old version in memory
  // You can use dfs_invalidate(); but it will be implemented in Problem 2. You can just do 
  Printf("fdisk (%d): Beginning disk initialization.\n", getpid(), disksize);
  sb.fileSystemValid = 0;
  disksize = disk_size(); //0x4000000; //64MB
  diskblocksize = disk_blocksize(); //256
  Printf("fdisk (%d): Disk size: 0x%x bytes, Disk block size: %d bytes.\n", getpid(), disksize, diskblocksize);
  sb.fileSystemBlockSize = 1024;
  sb.numberFileSystemBlocks = 0x10000;  // 65536 blocks (64MB / 1024)

  // Make sure the disk exists before doing anything else
  Printf("fdisk (%d): Creating disk.\n", getpid());
  
  disk_create();

  // Write all inodes as not in use and empty (all zeros)
  Printf("fdisk (%d): Zeroing inodes.\n", getpid());
  sb.inodesStartingBlockNumber = FDISK_INODE_BLOCK_START;
  sb.numberInodes = FDISK_NUM_INODES;
  //inode array is file system blocks 2-33, physical blocks 8-135
  for (i = 0; i < 32; i++) { //32 pages of inodes
    FdiskWriteZerosToFileSystemBlock(FDISK_INODE_BLOCK_START+i);
  }

  // Next, setup free block vector (fbv) and write free block vector to the disk
  Printf("fdisk (%d): Writing FBV to disk.\n", getpid());
  //free block vector is file system blocks 34-41, physical blocks 136-167
  sb.freeBlockVectorStartingBlockNumber = FDISK_FBV_BLOCK_START;
  sb.numFBVBlocks = 8;
  //Setting up FBV 0, which should mark that fs blocks 0-41 are in use
  for (i = 0; i < 1024; i++) {
    if (i <= 4) { //fs blocks 0-8, 9-15, 16-23, 24-31, 32-39
      block->data[i] = 0xFF;
    }
    else if (i == 5) { //fs blocks 40-41
      block->data[i] = 0x02;
    }
    else { //fs blocks 42-1023
      block->data[i] = 0x0;
    }
  }
  //Write FBV block 0
  FdiskWriteFileSystemBlock(FDISK_FBV_BLOCK_START, block);
  //Write FBV blocks 1-6
  for (i = 1; i < sb.numFBVBlocks-1; i++) {
    //mark all 65000 or so other blocks as empty 
    FdiskWriteZerosToFileSystemBlock(FDISK_FBV_BLOCK_START+i);
  }
  //Prepare FBV block 7, marking that fs block 65535 is in use (superblock backup)
  for (i = 0; i < 1024; i++) {
    if (i == 1023) {
      block->data[i] = 0x80;
    }
    else {
      block->data[i] = 0x0;
    }
  }
  //Write FBV block 7
  FdiskWriteFileSystemBlock(FDISK_FBV_BLOCK_START+7, block);

  // Finally, setup superblock as valid filesystem and write superblock and boot record to disk: 
  sb.fileSystemValid = 1;
  // boot record is all zeros in the first FILE system block (physical blocks 0-3), and superblock structure goes into the second FILE system block (physical blocks 4-7)
  Printf("fdisk (%d): Writing boot record and superblock to disk.\n", getpid());
  FdiskWriteZerosToFileSystemBlock(0);
  bcopy(&sb, sb_dfsblock, sb.fileSystemBlockSize);
  FdiskWriteFileSystemBlock(1, sb_dfsblock); //fs block 1 is the superblock
  FdiskWriteFileSystemBlock(65535, sb_dfsblock); //copy the superblock also to file system block 65535, where the copy of the superblock goes (physical blocks 262140-262143)
  Printf("fdisk (%d): Formatted DFS disk for 0x%x bytes.\n", getpid(), disksize);
}

int FdiskWriteFileSystemBlock(uint32 fsblocknum, dfs_block *b) {
  // STUDENT: put your code here
  int val;
  int total = 0;
  char *physicalBlock;
  int i = 0;
  int j = 0;
  //This should run 4 times, at data indices 0, 256, 512, and 768, to break the fs block into four physical blocks
  //meanwhile, j is used to count 0, 1, 2, 3.
  for (i = 0; i < sb.fileSystemBlockSize; i += diskblocksize) {
    bcopy((char*)b->data[i], physicalBlock, diskblocksize);
    PrintPhysicalBlock(physicalBlock);
    if ((val = disk_write_block(fsblocknum*4+j, physicalBlock)) != diskblocksize) {
      total += val;
      Printf("fdisk(%d): ERROR FdiskWriteFileSystemBlock: Tried to write %d bytes, instead wrote %d bytes. Returning with total bytes read %d.\n", getpid(), diskblocksize, val, total);
      return total;
    }
    total += val;
    j++;
  }
  Printf("fdisk(%d): FdiskWriteFileSystemBlock: Successfully wrote %d bytes to fs block %d (phys blocks %d-%d).\n", getpid(), total, fsblocknum, fsblocknum*4, fsblocknum*4+3);
  return total;
}

int FdiskWriteZerosToFileSystemBlock(uint32 fsblocknum) {
  dfs_block *zeroedBlock;
  bzero((char*)zeroedBlock, sb.fileSystemBlockSize);

  return FdiskWriteFileSystemBlock(fsblocknum, zeroedBlock);
}

void PrintPhysicalBlock(char* b) {
  int i;
  Printf("Printing block: ");
  for (i = 0; i < disk_blocksize; i++) {
    Printf(b[i]);
  }
  Printf("\n");
}