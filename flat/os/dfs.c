#include "ostraps.h"
#include "dlxos.h"
#include "traps.h"
#include "queue.h"
#include "disk.h"
#include "dfs.h"
#include "synch.h"

#define DFS_INODE_MAX_NUM 256
#define DFS_FBV_MAX_NUM_WORDS 2048

static dfs_inode inodes[DFS_INODE_MAX_NUM]; // all inodes
static dfs_superblock sb; // superblock
static uint32 fbv[DFS_FBV_MAX_NUM_WORDS]; // Free block vector. fbv size = file system size / file system block size / 32 bits
//DFS_MAX_FILESYSTEM_SIZE / DFS_BLOCKSIZE = 0x4000000 / 1024 / 32 = 2048, 65536 bits so one bit per file system block

static uint32 negativeone = 0xFFFFFFFF;
static inline uint32 invert(uint32 n) { return n ^ negativeone; }

static lock_t fbvLock;

// You have already been told about the most likely places where you should use locks. You may use 
// additional locks if it is really necessary.

// STUDENT: put your file system level functions below.
// Some skeletons are provided. You can implement additional functions.

//TODO update the superblock copy whenever you change the superblock? does that include all of these changes to superblock valid?
//yes it does.

///////////////////////////////////////////////////////////////////
// Non-inode functions first
///////////////////////////////////////////////////////////////////

//declarations to avoid implicit declarations later
void DfsInvalidate();
int DfsOpenFileSystem();
int CheckIfBlockAllocatedInFBV(uint32 blocknum);
void PrintSBTest();

//-----------------------------------------------------------------
// DfsModuleInit is called at boot time to initialize things and
// open the file system for use.
//-----------------------------------------------------------------

void DfsModuleInit() {
// You essentially set the file system as invalid and then open 
// using DfsOpenFileSystem().
    printf("DfdModuleInit\n");
    DfsInvalidate();
    DfsOpenFileSystem();
    fbvLock = LockCreate();
}

//-----------------------------------------------------------------
// DfsInvalidate marks the current version of the filesystem in
// memory as invalid.  This is really only useful when formatting
// the disk, to prevent the current memory version from overwriting
// what you already have on the disk when the OS exits.
//-----------------------------------------------------------------

void DfsInvalidate() {
// This is just a one-line function which sets the valid bit of the 
// superblock to 0.
    sb.fileSystemValid = 0;
}

//-------------------------------------------------------------------
// DfsOpenFileSystem loads the file system metadata from the disk
// into memory.  Returns DFS_SUCCESS on success, and DFS_FAIL on 
// failure.
//-------------------------------------------------------------------

int DfsOpenFileSystem() {
    disk_block blockArray[4];
    disk_block *block;
    int i;

    printf("DfsOpenFileSystem\n");

    printf("SBtest pre loading (should be all 0's)\n");
    PrintSBTest();

//Basic steps:
// Check that filesystem is not already open
    if(sb.fileSystemValid == 1) {
        return DFS_FAIL;
    }
// Read superblock from disk.  Note this is using the disk read rather 
// than the DFS read function because the DFS read requires a valid 
// filesystem in memory already, and the filesystem cannot be valid 
// until we read the superblock. Also, we don't know the block size 
// until we read the superblock, either.
    DiskReadBlock(4, &blockArray[0]); //Superblock is always at physical block #4
    DiskReadBlock(5, &blockArray[1]);
    DiskReadBlock(6, &blockArray[2]);
    DiskReadBlock(7, &blockArray[3]);

// Copy the data from the block we just read into the superblock in memory
    bcopy((char*)blockArray, (char*)&sb, DFS_BLOCKSIZE); //todo: should these be passed with or without &?

    printf("Sbtest AFTer loading\n");
    PrintSBTest();
    
// All other blocks are sized by virtual block size:
// Read inodes
// Blocks are 256 bytes, each inode is 128 bytes
    for (i = 0; i < sb.numberInodes/2; i++) {
        DiskReadBlock(sb.inodesStartingBlockNumber+i, block); //Blocks 2 to 33, inclusive: Inode array.
        bcopy(&block->data[0], (char*)&inodes[2*i], 128);
        bcopy(&block->data[128], (char*)&inodes[2*i+1], 128);
    }
// Read free block vector
    for (i = 0; i < sb.numFBVBlocks; i++) {
        DiskReadBlock((sb.freeBlockVectorStartingBlockNumber+i)*4, &blockArray[0]);
        DiskReadBlock((sb.freeBlockVectorStartingBlockNumber+i)*4+1, &blockArray[1]);
        DiskReadBlock((sb.freeBlockVectorStartingBlockNumber+i)*4+2, &blockArray[2]);
        DiskReadBlock((sb.freeBlockVectorStartingBlockNumber+i)*4+3, &blockArray[3]);
        //each file system block is 1024 bytes, so 256 uint32s
        bcopy((char*)blockArray, (char*)&fbv[i*256], sb.fileSystemBlockSize); //TODO INCONSISTENCY IN WHETHER OR NOT IM ADDRESSING AAAAAAAAA
    }

// Change superblock to be invalid, write back to disk, then change 
// it back to be valid in memory
    DfsInvalidate();
    bcopy((char*)&sb, (char*)blockArray, sb.fileSystemBlockSize);
    DiskWriteBlock(4, &blockArray[0]);
    DiskWriteBlock(5, &blockArray[1]);
    DiskWriteBlock(6, &blockArray[2]);
    DiskWriteBlock(7, &blockArray[3]);
    // Whenever we write the superblock, we must also write the backup
    DiskWriteBlock(262140, &blockArray[0]);
    DiskWriteBlock(262141, &blockArray[1]);
    DiskWriteBlock(262142, &blockArray[2]);
    DiskWriteBlock(262143, &blockArray[3]);

    sb.fileSystemValid = 1;
    return DFS_SUCCESS;
}


//-------------------------------------------------------------------
// DfsCloseFileSystem writes the current memory version of the
// filesystem metadata to the disk, and invalidates the memory's 
// version.
//-------------------------------------------------------------------

int DfsCloseFileSystem() {
    disk_block blockArray[4];
    disk_block *block;
    int i; //loop var

    printf("DfsCloseFileSystem\n");

    if (sb.fileSystemValid != 1) {
        return DFS_FAIL;
    }

    //Write inodes
    //8 inodes fit in 1 fs block
    printf("DfsCloseFileSystem: Writing inodes.\n");
    for (i = 0; i < DFS_INODE_MAX_NUM; i += 8) {
        bcopy((char*)&inodes[i], (char*)blockArray, sb.fileSystemBlockSize);
        DiskWriteBlock(sb.inodesStartingBlockNumber+i*4, &blockArray[0]);
        DiskWriteBlock(sb.inodesStartingBlockNumber+i*4+1, &blockArray[1]);
        DiskWriteBlock(sb.inodesStartingBlockNumber+i*4+2, &blockArray[2]);
        DiskWriteBlock(sb.inodesStartingBlockNumber+i*4+3, &blockArray[3]);
    }

    //Write free block vector
    printf("DfsCloseFileSystem: Writing fbv.\n");
    for (i = 0; i < sb.numFBVBlocks*4; i++) {
        //64 uint32s fit in one 256 byte physical block
        bcopy((char*)&fbv[i*64], (char*)block, DISK_BLOCKSIZE);
        DiskWriteBlock(sb.freeBlockVectorStartingBlockNumber*4+i, block);
    }

    printf("DfsCloseFileSystem: Writing superblock.\n");
    //Write superblock
    bcopy((char*)&sb, (char*)blockArray, sb.fileSystemBlockSize);
    DiskWriteBlock(4, &blockArray[0]);
    DiskWriteBlock(5, &blockArray[1]);
    DiskWriteBlock(6, &blockArray[2]);
    DiskWriteBlock(7, &blockArray[3]);

    // Whenever we write the superblock, we must also write the backup
    DiskWriteBlock(262140, &blockArray[0]);
    DiskWriteBlock(262141, &blockArray[1]);
    DiskWriteBlock(262142, &blockArray[2]);
    DiskWriteBlock(262143, &blockArray[3]);

    DfsInvalidate();
    return DFS_SUCCESS;
}


//-----------------------------------------------------------------
// DfsAllocateBlock allocates a DFS block for use. Remember to use 
// locks where necessary.
//-----------------------------------------------------------------

uint32 DfsAllocateBlock() {
// Check that file system has been validly loaded into memory
// Find the first free block using the free block vector (FBV), mark it in use
// Return handle to block
    int blockFound = 0;
    int blockNum;
    int i = 0;

    printf("DfsAllocateBlock\n");

    if (sb.fileSystemValid != 1) {
        return DFS_FAIL;
    }

    LockHandleAcquire(fbvLock);

    do {
        printf("i/32 = %d, i mod 32 = %d, fbv[%d] = 0x%x, 0x1 << (imod2) = 0x%x, & = 0x%x\n", i/32, i%32, fbv[i/32], 0x1 << (i%32), fbv[i / 32] & (0x1 << (i % 32)));
        if (CheckIfBlockAllocatedInFBV(i) == 1) {
            i++;
        }
        else {
            fbv[i / 32] |= (0x1 << (i % 32));
            blockFound = 1;
            blockNum = i;
            printf("DfsAllocateBlock: Allocating fs block %d.\n", blockNum);
        }
    } while(blockFound == 0);

    LockHandleRelease(fbvLock);

    return blockNum;
}


//-----------------------------------------------------------------
// DfsFreeBlock deallocates a DFS block.
//-----------------------------------------------------------------

int DfsFreeBlock(uint32 blocknum) {

    if (sb.fileSystemValid != 1) {
        return DFS_FAIL;
    }

    LockHandleAcquire(fbvLock);

    if (CheckIfBlockAllocatedInFBV(blocknum) == 1) {
        fbv[blocknum / 32] &= ~(0x1 << (blocknum % 32));
        printf("DfsFreeBlock: Deallocated fs block %d.\n", blocknum);
        LockHandleRelease(fbvLock);
        return DFS_SUCCESS;
    }
    else {
        printf("DfsFreeBlock: Tried to free fs block %d, but was not in use.\n", blocknum);
        LockHandleRelease(fbvLock);
        return DFS_FAIL;
    }
}


//-----------------------------------------------------------------
// DfsReadBlock reads an allocated DFS block from the disk
// (which could span multiple physical disk blocks).  The block
// must be allocated in order to read from it.  Returns DFS_FAIL
// on failure, and the number of bytes read on success.  
//-----------------------------------------------------------------

int DfsReadBlock(uint32 blocknum, dfs_block *b) {
    disk_block blockArray[4];

    if (sb.fileSystemValid != 1) {
        return DFS_FAIL;
    }

    if (CheckIfBlockAllocatedInFBV(blocknum) == 0) {
        printf("DfsReadBlock: Tried to read fs block %d, but it is not allocated.\n", blocknum);
        return DFS_FAIL;
    }

    DiskReadBlock(blocknum*4, &blockArray[0]);
    DiskReadBlock(blocknum*4+1, &blockArray[1]);
    DiskReadBlock(blocknum*4+2, &blockArray[2]);
    DiskReadBlock(blocknum*4+3, &blockArray[3]);

    bcopy((char*)blockArray, (char*)b, sb.fileSystemBlockSize);
    printf("DfsReadBlock: Successfully read %d bytes from fs block %d.\n", sb.fileSystemBlockSize, blocknum);

    return sb.fileSystemBlockSize;
}


//-----------------------------------------------------------------
// DfsWriteBlock writes to an allocated DFS block on the disk
// (which could span multiple physical disk blocks).  The block
// must be allocated in order to write to it.  Returns DFS_FAIL
// on failure, and the number of bytes written on success.  
//-----------------------------------------------------------------

int DfsWriteBlock(uint32 blocknum, dfs_block *b) {
    disk_block blockArray[4];
    int bytesWritten = 0;
    
    if (sb.fileSystemValid != 1) {
        return DFS_FAIL;
    }

    if (CheckIfBlockAllocatedInFBV(blocknum) == 0) {
        printf("DfsWriteBlock: Tried to write to fs block %d, but it is not allocated.\n", blocknum);
        return DFS_FAIL;
    }

    bcopy((char*)b, (char*)blockArray, sb.fileSystemBlockSize);
    bytesWritten += DiskWriteBlock(blocknum*4, &blockArray[0]);
    bytesWritten += DiskWriteBlock(blocknum*4+1, &blockArray[1]);
    bytesWritten += DiskWriteBlock(blocknum*4+2, &blockArray[2]);
    bytesWritten += DiskWriteBlock(blocknum*4+3, &blockArray[3]);

    printf("DfsWriteBlock: Successfully wrote %d bytes to fs block %d.\n", bytesWritten, blocknum);

    return bytesWritten;
    //todo implement
}


////////////////////////////////////////////////////////////////////////////////
// Inode-based functions
////////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------
// DfsInodeFilenameExists looks through all the inuse inodes for 
// the given filename. If the filename is found, return the handle 
// of the inode. If it is not found, return DFS_FAIL.
//-----------------------------------------------------------------

uint32 DfsInodeFilenameExists(char *filename) {
    return 0;
    //todo implement
}


//-----------------------------------------------------------------
// DfsInodeOpen: search the list of all inuse inodes for the 
// specified filename. If the filename exists, return the handle 
// of the inode. If it does not, allocate a new inode for this 
// filename and return its handle. Return DFS_FAIL on failure. 
// Remember to use locks whenever you allocate a new inode.
//-----------------------------------------------------------------

uint32 DfsInodeOpen(char *filename) {
    return 0;
    //todo implement
}


//-----------------------------------------------------------------
// DfsInodeDelete de-allocates any data blocks used by this inode, 
// including the indirect addressing block if necessary, then mark 
// the inode as no longer in use. Use locks when modifying the 
// "inuse" flag in an inode.Return DFS_FAIL on failure, and 
// DFS_SUCCESS on success.
//-----------------------------------------------------------------

int DfsInodeDelete(uint32 handle) {
    return 0;
    //todo implement
}


//-----------------------------------------------------------------
// DfsInodeReadBytes reads num_bytes from the file represented by 
// the inode handle, starting at virtual byte start_byte, copying 
// the data to the address pointed to by mem. Return DFS_FAIL on 
// failure, and the number of bytes read on success.
//-----------------------------------------------------------------

int DfsInodeReadBytes(uint32 handle, void *mem, int start_byte, int num_bytes) {
    return 0;
    //todo implement
}


//-----------------------------------------------------------------
// DfsInodeWriteBytes writes num_bytes from the memory pointed to 
// by mem to the file represented by the inode handle, starting at 
// virtual byte start_byte. Note that if you are only writing part 
// of a given file system block, you'll need to read that block 
// from the disk first. Return DFS_FAIL on failure and the number 
// of bytes written on success.
//-----------------------------------------------------------------

int DfsInodeWriteBytes(uint32 handle, void *mem, int start_byte, int num_bytes) {
    return 0;
    //todo implement
}


//-----------------------------------------------------------------
// DfsInodeFilesize simply returns the size of an inode's file. 
// This is defined as the maximum virtual byte number that has 
// been written to the inode thus far. Return DFS_FAIL on failure.
//-----------------------------------------------------------------

uint32 DfsInodeFilesize(uint32 handle) {
    return 0;
    //todo implement
}


//-----------------------------------------------------------------
// DfsInodeAllocateVirtualBlock allocates a new filesystem block 
// for the given inode, storing its blocknumber at index 
// virtual_blocknumber in the translation table. If the 
// virtual_blocknumber resides in the indirect address space, and 
// there is not an allocated indirect addressing table, allocate it. 
// Return DFS_FAIL on failure, and the newly allocated file system 
// block number on success.
//-----------------------------------------------------------------

uint32 DfsInodeAllocateVirtualBlock(uint32 handle, uint32 virtual_blocknum) {
    return 0;
    //todo implement
}



//-----------------------------------------------------------------
// DfsInodeTranslateVirtualToFilesys translates the 
// virtual_blocknum to the corresponding file system block using 
// the inode identified by handle. Return DFS_FAIL on failure.
//-----------------------------------------------------------------

uint32 DfsInodeTranslateVirtualToFilesys(uint32 handle, uint32 virtual_blocknum) {
    return 0;
    //todo implement
}

int CheckIfBlockAllocatedInFBV(uint32 blocknum) {
    if (fbv[blocknum / 32] & (0x1 << (blocknum % 32))) {
        return 1;
    }
    else {
        return 0;
    }
}

void PrintSBTest() {
    printf("   ___PrintSBTest___\n");
    printf("   sb.fileSystemValid: %d\n", sb.fileSystemValid);
    printf("   sb.fileSystemBlockSize: %d\n", sb.fileSystemBlockSize);
    printf("   sb.numberFileSystemBlocks: %d\n", sb.numberFileSystemBlocks);
    printf("   sb.inodesStartingBlockNumber: %d\n", sb.inodesStartingBlockNumber);
    printf("   sb.numberInodes: %d\n", sb.numberInodes);
    printf("   sb.freeBlockVectorStartingBlockNumber: %d\n", sb.freeBlockVectorStartingBlockNumber);
    printf("   sb.numFBVBlocks: %d\n", sb.numFBVBlocks);
}