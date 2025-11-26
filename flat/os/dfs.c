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
static lock_t inodeLock;

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
    inodeLock = LockCreate();
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
    bcopy((char*)blockArray, (char*)&sb, (int)sizeof(sb)); //changed from DFS_BLOCKSIZE to (int)sizeof(sb)

    PrintSBTest();
    //Set this flag to 0 if it isn't already. The local version in memory should never have this set to 1, only the disk version.
    sb.fdiskFlag = 0;
    
// All other blocks are sized by virtual block size:
// Read inodes
// Blocks are 256 bytes, each inode is 128 bytes
    for (i = 0; i < sb.numberInodes/2; i++) {
        DiskReadBlock(sb.inodesStartingBlockNumber*4+i, block); //Blocks 2 to 33, inclusive: Inode array.
        bcopy(&block->data[0], (char*)&inodes[2*i], (int)sizeof(dfs_inode)); //changed from 128 to (int)sizeof(dfs_inode)
        bcopy(&block->data[128], (char*)&inodes[2*i+1], (int)sizeof(dfs_inode)); //changed from 128 to (int)sizeof(dfs_inode)
    }

    // SANITY CHECK: 
    // CHECK IF ANY INODES ARE IN USE
    printf("Checking if any inode are in use.\n");
    for (i = 0; i < sb.numberInodes; i++) {
        if (inodes[i].inUse != 0) {
            printf("inode %d in use = %d. filesize %d, filename '%s', direct addresses %d, %d, %d... indirect tables %d, %d.\n", i, inodes[i].inUse, inodes[i].fileSize, inodes[i].fileName, inodes[i].directAddressTranslations[0], inodes[i].directAddressTranslations[1], inodes[i].directAddressTranslations[2], inodes[i].indirectAddressTableBlockNumber, inodes[i].doubleIndirectAddressTableBlockNumber);
        }
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
    bzero((char*)blockArray, sb.fileSystemBlockSize);
    bcopy((char*)&sb, (char*)blockArray, (int)sizeof(sb)); //changed from sb.fileSystemBlockSize to (int)sizeof(sb)
    DiskWriteBlock(4, &blockArray[0]);
    DiskWriteBlock(5, &blockArray[1]);
    DiskWriteBlock(6, &blockArray[2]);
    DiskWriteBlock(7, &blockArray[3]);
    // Whenever we write the superblock, we must also write the backup
    DiskWriteBlock((DFS_MAX_FILESYSTEM_SIZE/DFS_BLOCKSIZE-1)*4, &blockArray[0]); //physical block 262140
    DiskWriteBlock((DFS_MAX_FILESYSTEM_SIZE/DFS_BLOCKSIZE-1)*4+1, &blockArray[1]);
    DiskWriteBlock((DFS_MAX_FILESYSTEM_SIZE/DFS_BLOCKSIZE-1)*4+2, &blockArray[2]);
    DiskWriteBlock((DFS_MAX_FILESYSTEM_SIZE/DFS_BLOCKSIZE-1)*4+3, &blockArray[3]);

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
    dfs_superblock sb_flagCheck;

    if (sb.fileSystemValid != 1) {
        printf("DfsCloseFileSystem: File sytem is invalid. Not writing to disk.\n");
        return DFS_FAIL;
    }

    //CHECK IF FDISKFLAG ON DISK IS 1. IF IT IS, DON'T WRITE ANYTHING TO MEMORY.
    DiskReadBlock(4, &blockArray[0]); //Superblock is always at physical block #4
    DiskReadBlock(5, &blockArray[1]);
    DiskReadBlock(6, &blockArray[2]);
    DiskReadBlock(7, &blockArray[3]);
    bcopy((char*)blockArray, (char*)&sb_flagCheck, (int)sizeof(sb_flagCheck)); //changed from DFS_BLOCKSIZE to (int)sizeof(sb)
    if (sb_flagCheck.fdiskFlag == 1) {
        printf("DfsCloseFileSystem: Fdisk flag is set to 1. File system will not be overwritten.\n");
        return DFS_FAIL;
    }

    //Write inodes
    //8 inodes fit in 1 fs block
    //2 inodes fit in 1 disk block
    printf("DfsCloseFileSystem: Writing inodes from block %d.\n", sb.inodesStartingBlockNumber);
    for (i = 0; i < sb.numberInodes/2; i++) {
        bcopy((char*)&inodes[i*2], (char*)block, DISK_BLOCKSIZE);
        //printf("Sanity check: inodes[%d] 0x%x, blockArray 0x%x, size %d bytes\n", i, &inodes[i], blockArray, sb.fileSystemBlockSize);
        //printf("Sanity Check: saving inodes[%d-%d]. Saving at physical block %d.\n", i*2, i*2+1, sb.inodesStartingBlockNumber*4+i);
        //printf("DfsCloseFileSystem: Writing inodes to block %d.\n", sb.inodesStartingBlockNumber*4+i);
        DiskWriteBlock(sb.inodesStartingBlockNumber*4+i, block);
    }

    //Write free block vector
    printf("DfsCloseFileSystem: Writing fbv from block %d.\n", sb.freeBlockVectorStartingBlockNumber);
    for (i = 0; i < sb.numFBVBlocks*4; i++) {
        //64 uint32s fit in one 256 byte physical block
        bcopy((char*)&fbv[i*64], (char*)block, DISK_BLOCKSIZE);
        DiskWriteBlock(sb.freeBlockVectorStartingBlockNumber*4+i, block);
    }

    printf("DfsCloseFileSystem: Writing superblock.\n");
    //Write superblock
    bzero((char*)blockArray, sb.fileSystemBlockSize);
    bcopy((char*)&sb, (char*)blockArray, (int)sizeof(sb)); //changed from sb.fileSystemBlockSize to (int)sizeof(sb)
    DiskWriteBlock(4, &blockArray[0]);
    DiskWriteBlock(5, &blockArray[1]);
    DiskWriteBlock(6, &blockArray[2]);
    DiskWriteBlock(7, &blockArray[3]);

    // Whenever we write the superblock, we must also write the backup
    DiskWriteBlock((DFS_MAX_FILESYSTEM_SIZE/DFS_BLOCKSIZE-1)*4, &blockArray[0]); //physical block 262140
    DiskWriteBlock((DFS_MAX_FILESYSTEM_SIZE/DFS_BLOCKSIZE-1)*4+1, &blockArray[1]); //physical block 262141
    DiskWriteBlock((DFS_MAX_FILESYSTEM_SIZE/DFS_BLOCKSIZE-1)*4+2, &blockArray[2]); //physical block 262142
    DiskWriteBlock((DFS_MAX_FILESYSTEM_SIZE/DFS_BLOCKSIZE-1)*4+3, &blockArray[3]); //physical block 262143

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

    if (sb.fileSystemValid != 1) {
        return DFS_FAIL;
    }

    LockHandleAcquire(fbvLock);

    do {
        //printf("i = %d, i/32 = %d, i mod 32 = %d, fbv[%d] = 0x%x, 0x1 << (imod32) = 0x%x, & = 0x%x\n", i, i/32, i%32, i/32, fbv[i/32], 0x1 << (i%32), fbv[i / 32] & (0x1 << (i % 32)));
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

    printf("DfsFreeBlock: %d\n", blocknum);

    LockHandleAcquire(fbvLock);

    printf("DfsFreeBlock: Acquired fbvLock\n");

    if (CheckIfBlockAllocatedInFBV(blocknum) == 1) {
        printf("DfsFreeBlock: fs block %d is '1' in FBV.\n", blocknum);
        fbv[blocknum / 32] &= invert(0x1 << (blocknum % 32));
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
    int val = 0;

    if (sb.fileSystemValid != 1) {
        return DFS_FAIL;
    }

    if (CheckIfBlockAllocatedInFBV(blocknum) == 0) {
        printf("DfsReadBlock: Tried to read fs block %d, but it is not allocated.\n", blocknum);
        return DFS_FAIL;
    }

    val += DiskReadBlock(blocknum*4, &blockArray[0]);
    val += DiskReadBlock(blocknum*4+1, &blockArray[1]);
    val += DiskReadBlock(blocknum*4+2, &blockArray[2]);
    val += DiskReadBlock(blocknum*4+3, &blockArray[3]);

    bcopy((char*)blockArray, (char*)b, sb.fileSystemBlockSize);

    if (val != sb.fileSystemBlockSize) {
        printf("DfsReadBlock: Tried to read %d bytes from fs block %d,, but only read %d.\n", sb.fileSystemBlockSize, blocknum, val);
    }
    else {
        printf("DfsReadBlock: Successfully read %d bytes from fs block %d.\n", sb.fileSystemBlockSize, blocknum);
    }

    return val;
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
    int i;

    if (sb.fileSystemValid != 1) {
        printf("DfsInodeFilenameExists: ERROR. File system not valid.\n");
        return DFS_FAIL;
    }

    for (i = 0; i < sb.numberInodes; i++) {
        if (inodes[i].inUse == 1 && dstrncmp(filename, inodes[i].fileName, FILE_MAX_FILENAME_LENGTH) == 0) {
            return (uint32)i;
        }
    }

    return DFS_FAIL;
}


//-----------------------------------------------------------------
// DfsInodeOpen: search the list of all inuse inodes for the 
// specified filename. If the filename exists, return the handle 
// of the inode. If it does not, allocate a new inode for this 
// filename and return its handle. Return DFS_FAIL on failure. 
// Remember to use locks whenever you allocate a new inode.
//-----------------------------------------------------------------

uint32 DfsInodeOpen(char *filename) {
    uint32 handle;
    int i;
    int handleFound = 0;

    if (sb.fileSystemValid != 1) {
        return DFS_FAIL;
    }

    if ((handle = DfsInodeFilenameExists(filename)) != -1) {
        //Filename exists
        printf("DfsInodeOpen: file '%s' already exists at handle %d.\n", filename, handle);
        return handle;
    }

    for (i = 0; i < sb.numberInodes; i++) {
        if (inodes[i].inUse == 0) {
            // Free inode found
            handle = i;
            handleFound = 1;
            break;
        }
    }
    if (handleFound == 0) {
        printf("DfsInodeOpen: ERROR. No free inodes left to allocate.\n");
        return DFS_FAIL;
    }
    //Copy over filename
    for (i = 0; i < FILE_MAX_FILENAME_LENGTH; i++) {
        inodes[handle].fileName[i] = filename[i];
    }

    LockHandleAcquire(inodeLock);
    inodes[handle].inUse = 1;
    LockHandleRelease(inodeLock);

    printf("DfsInodeOpen: Allocated inode %d.\n", handle);
    return handle;
}


//-----------------------------------------------------------------
// DfsInodeDelete de-allocates any data blocks used by this inode, 
// including the indirect addressing block if necessary, then mark 
// the inode as no longer in use. Use locks when modifying the 
// "inuse" flag in an inode. Return DFS_FAIL on failure, and 
// DFS_SUCCESS on success.
//-----------------------------------------------------------------

int DfsInodeDelete(uint32 handle) {
    int i, j;
    dfs_block singleIndirectBlock;
    dfs_block doubleIndirectBlock;
    uint32 singleIndirectTable[256];
    uint32 doubleIndirectTable[256];

    printf("DfsInodeDelete\n");

    if (sb.fileSystemValid != 1) {
        return DFS_FAIL;
    }

    if (inodes[handle].inUse == 0) {
        printf("DfsInodeDelete: ERROR. Tried to delete an inode that is not in use. (handle: %d)\n", handle);
        return DFS_FAIL;
    }
    printf("DfsInodeDelete: Deleting inode handle %d, inUse == %d\n", handle, inodes[handle].inUse);

    //de-allocate data blocks used by this inode
    for (i = 0; i < DIRECT_ADDRESS_TRANSLATIONS_TABLE_SIZE; i++) {
        printf("DfsInodeDelete: Checking if direct addr table[%d] is in use, fs block %d\n", i, inodes[handle].directAddressTranslations[i]);
        if (inodes[handle].directAddressTranslations[i] != 0 && CheckIfBlockAllocatedInFBV(inodes[handle].directAddressTranslations[i]) == 1) {
            printf("DfsINodeDelete: Freeing direct Addr table [%d], fs block %d\n", i, inodes[handle].directAddressTranslations[i]);
            DfsFreeBlock(inodes[handle].directAddressTranslations[i]);
        }
        inodes[handle].directAddressTranslations[i] = 0;
    }

    //de-allocate indirect addressing blocks if in use
    printf("DfsInodeDelete: Checking if indirect table is in use, fs block %d\n", inodes[handle].indirectAddressTableBlockNumber);
    if (inodes[handle].indirectAddressTableBlockNumber != 0 && CheckIfBlockAllocatedInFBV(inodes[handle].indirectAddressTableBlockNumber) == 1) {
        //de-allocate all blocks pointed to by indirect address table
        DfsReadBlock(inodes[handle].indirectAddressTableBlockNumber, &singleIndirectBlock);
        bcopy((char*)&singleIndirectBlock, (char*)singleIndirectTable, DFS_BLOCKSIZE);
        for (i = 0; i < 256; i++) {
            if(singleIndirectTable[i] != 0 && CheckIfBlockAllocatedInFBV(singleIndirectTable[i]) == 1) {
                printf("DfsInodeDelete: Freeing indirect addr table[%d], fs block %d.\n", i, singleIndirectTable[i]);
                DfsFreeBlock(singleIndirectTable[i]);
            }
            singleIndirectTable[i] = 0;
        }
        printf("DfsInodeDelete: Freeing indirect addr table, fs block %d.\n", inodes[handle].indirectAddressTableBlockNumber);
        DfsFreeBlock(inodes[handle].indirectAddressTableBlockNumber);
    }
    inodes[handle].indirectAddressTableBlockNumber = 0;
    printf("DfsInodeDelete: Checking if double indirect table is in use, fs block %d\n", inodes[handle].doubleIndirectAddressTableBlockNumber);
    if (inodes[handle].doubleIndirectAddressTableBlockNumber != 0 && CheckIfBlockAllocatedInFBV(inodes[handle].doubleIndirectAddressTableBlockNumber) == 1) {
        //de-allocate all blocks pointed to by tables pointed to within double address table
        DfsReadBlock(inodes[handle].doubleIndirectAddressTableBlockNumber, &doubleIndirectBlock);
        bcopy((char*)&doubleIndirectBlock, (char*)doubleIndirectTable, DFS_BLOCKSIZE);
        for (i = 0; i < 256; i++) {
            if (doubleIndirectTable[i] != 0 && CheckIfBlockAllocatedInFBV(doubleIndirectTable[i])) {
                DfsReadBlock(doubleIndirectTable[i], &singleIndirectBlock);
                bcopy((char*)&singleIndirectBlock, (char*)singleIndirectTable, DFS_BLOCKSIZE);
                for (j = 0; j < 256; j++) {
                    if (singleIndirectTable[j] != 0 && CheckIfBlockAllocatedInFBV(singleIndirectTable[j]) == 1) {
                        printf("DfsInodeDelete: Freeing double indirect addr table [%d][%d], fs block %d.\n", i, j, singleIndirectTable[j]);
                        DfsFreeBlock(singleIndirectTable[j]);
                    }
                    singleIndirectTable[j] = 0;
                }
                printf("DfsInodeDelete: Freeing indirect addr table[%d], fs block %d.\n", i, doubleIndirectTable[i]);
                DfsFreeBlock(doubleIndirectTable[i]);
            }
            doubleIndirectTable[i] = 0;
        }
        printf("DfsInodeDelete: Freeing indirect addr table, fs block %d.\n", inodes[handle].doubleIndirectAddressTableBlockNumber);
        DfsFreeBlock(inodes[handle].doubleIndirectAddressTableBlockNumber);
    }
    inodes[handle].doubleIndirectAddressTableBlockNumber = 0;

    //mark inode as not in use
    printf("DfsInodeDelete: Marking inode %d as not in use. ", handle);
    LockHandleAcquire(inodeLock);
    inodes[handle].inUse = 0;
    printf("inuse now = %d.\n", inodes[handle].inUse);
    LockHandleRelease(inodeLock);

    //Clear all other values in inode
    inodes[handle].fileSize = 0;
    for (i = 0; i < FILE_MAX_FILENAME_LENGTH; i++) {
        inodes[handle].fileName[i] = 0;
    }

    return DFS_SUCCESS;
}


//-----------------------------------------------------------------
// DfsInodeReadBytes reads num_bytes from the file represented by 
// the inode handle, starting at virtual byte start_byte, copying 
// the data to the address pointed to by mem. Return DFS_FAIL on 
// failure, and the number of bytes read on success.
//-----------------------------------------------------------------

int DfsInodeReadBytes(uint32 handle, void *mem, int start_byte, int num_bytes) {
    int virtualBlockNumber; //For the virtual block number the start_byte falls within
    int virtualByteOffset;  //For the offset within that virtual block where start_byte lies
    int bytesRead = 0; //Total number of bytes read so far
    int fileSysBlockNumber;
    dfs_block currDfsblock;

    if (sb.fileSystemValid != 1) {
        return DFS_FAIL;
    }

    if (inodes[handle].inUse != 1) {
        printf("DfsInodeReadBytes: ERROR. Tried to read bytes from an inode that is not in use.\n");
        return DFS_FAIL;
    }

    if (num_bytes < 0) {
        return DFS_FAIL;
    }

    if (num_bytes == 0) {
        return 0;
    }

    if (start_byte + num_bytes > inodes[handle].fileSize) {
        //If we're trying to read past the end of the file, throttle it down to stop at the end of the file
        printf("DfsInodeReadBytes: Trying to read past end of file (%d + %d > %d). Throttling num_bytes to %d.\n", start_byte, num_bytes, inodes[handle].fileSize, inodes[handle].fileSize - start_byte);
        num_bytes = inodes[handle].fileSize - start_byte;
    }

    while (num_bytes > 0) {

        virtualBlockNumber = start_byte / DFS_BLOCKSIZE;
        virtualByteOffset = start_byte % DFS_BLOCKSIZE;
        
        //First, get to the actual direct block we need to be reading at
        fileSysBlockNumber = DfsInodeTranslateVirtualToFilesys(handle, virtualBlockNumber);
        DfsReadBlock(fileSysBlockNumber, &currDfsblock);

        //Now, actually do the reading!
        if (virtualByteOffset + num_bytes > DFS_BLOCKSIZE) {
            //We are reading past the current block and will need to move on to the next block.
            bcopy(&currDfsblock.data[virtualByteOffset], (char*)(&mem + bytesRead), DFS_BLOCKSIZE - virtualByteOffset);
            bytesRead += DFS_BLOCKSIZE - virtualByteOffset;
            num_bytes -= DFS_BLOCKSIZE - virtualByteOffset;
            start_byte += DFS_BLOCKSIZE - virtualByteOffset;
        }
        else {
            //There is room in the current block to finish reading everything we need to.
            bcopy(&currDfsblock.data[virtualByteOffset], (char*)(&mem + bytesRead), num_bytes);
            bytesRead += virtualByteOffset;
            num_bytes -= virtualByteOffset;
            start_byte +=  virtualByteOffset;
            if (num_bytes != 0) {
                printf("DfsInodeReadBytes: Something probably went wrong here. This should be the end of our read, but num_bytes (remaining) is %d instead of 0.\n", num_bytes);
            }
        }
    }

    printf("DfsInodeReadBytes: Successfully read %d bytes.\n", bytesRead);
    return bytesRead;
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
    int virtualBlockNumber; //For the virtual block number the start_byte falls within
    int virtualByteOffset;  //For the offset within that virtual block where start_byte lies
    int bytesWritten = 0; //Total number of bytes written so far
    int fileSysBlockNumber;
    dfs_block currDfsBlock;

    if (sb.fileSystemValid != 1) {
        return DFS_FAIL;
    }

    if (inodes[handle].inUse != 1) {
        printf("DfsInodeWriteBytes: ERROR. Tried to write bytes to an inode that is not in use.\n");
        return DFS_FAIL;
    }

    if (num_bytes < 0) {
        return DFS_FAIL;
    }

    if (num_bytes == 0) {
        return 0;
    }

    printf("DfsInodeWriteBytes: Writing %d bytes from start byte %d in inode %d.\n", num_bytes, start_byte, handle);

    while (num_bytes > 0) {

        virtualBlockNumber = start_byte / DFS_BLOCKSIZE;
        virtualByteOffset = start_byte % DFS_BLOCKSIZE;
        printf("DfsInodeWriteBytes: While loop. start byte: %d, virtual block: %d, virtual offset: %d.\n", start_byte, virtualBlockNumber, virtualByteOffset);
        
        //First, get to the actual direct block we need to be reading at
        if (DfsInodeTranslateVirtualToFilesys(handle, virtualBlockNumber) == DFS_FAIL) {
            //Block needs to be allocated
            printf("DfsInodeWriteBytes: Allocating virtual block %d.\n", virtualBlockNumber);
            DfsInodeAllocateVirtualBlock(handle, virtualBlockNumber);
        }
        fileSysBlockNumber = DfsInodeTranslateVirtualToFilesys(handle, virtualBlockNumber);
        DfsReadBlock(fileSysBlockNumber, &currDfsBlock);

        //Now, actually do the writing!
        if (virtualByteOffset + num_bytes > DFS_BLOCKSIZE) {
            //We are reading past the current block and will need to move on to the next block.
            bcopy((char*)(&mem + bytesWritten), &currDfsBlock.data[virtualByteOffset], DFS_BLOCKSIZE - virtualByteOffset);
            DfsWriteBlock(fileSysBlockNumber, &currDfsBlock);
            bytesWritten += DFS_BLOCKSIZE - virtualByteOffset;
            num_bytes -= DFS_BLOCKSIZE - virtualByteOffset;
            start_byte += DFS_BLOCKSIZE - virtualByteOffset;
        }
        else {
            //There is room in the current block to finish writing everything we need to.
            bcopy((char*)(&mem + bytesWritten), &currDfsBlock.data[virtualByteOffset], num_bytes);
            DfsWriteBlock(fileSysBlockNumber, &currDfsBlock);
            bytesWritten += num_bytes;
            num_bytes -= num_bytes;
            start_byte +=  num_bytes;
        }
    }

    //update inode filesize to the maximum byte that has been written to in this file
    if (start_byte > inodes[handle].fileSize) {
        inodes[handle].fileSize = start_byte;
    }

    printf("DfsInodeWriteBytes: Successfully wrote %d bytes.\n", bytesWritten);
    return bytesWritten;
}


//-----------------------------------------------------------------
// DfsInodeFilesize simply returns the size of an inode's file. 
// This is defined as the maximum virtual byte number that has 
// been written to the inode thus far. Return DFS_FAIL on failure.
//-----------------------------------------------------------------

uint32 DfsInodeFilesize(uint32 handle) {

    if (sb.fileSystemValid != 1) {
        return DFS_FAIL;
    }

    if (inodes[handle].inUse == 0) {
        printf("DfsInodeFilesize: ERROR, inode %d not in use.\n", handle);
        return DFS_FAIL;
    }

    return inodes[handle].fileSize;
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
    dfs_block doubleIndirectAddrBlock;
    dfs_block singleIndirectAddrBlock;
    uint32 doubleIndirectAddrTable[256];
    uint32 singleIndirectAddrTable[256];
    uint32 indexWithinDoubleIndirectBlock;
    uint32 indexWithinSingleIndirectBlock;

    if (sb.fileSystemValid != 1) {
        return DFS_FAIL;
    }

    if (inodes[handle].inUse != 1) {
        printf("DfsInodeAllocateVirtualBlock: ERROR, inode %d not in use.\n", handle);
        return DFS_FAIL;
    }

    if (virtual_blocknum < DIRECT_ADDRESS_TRANSLATIONS_TABLE_SIZE) {
        //One of the 10 direct blocks
        if (inodes[handle].directAddressTranslations[virtual_blocknum] != 0 && CheckIfBlockAllocatedInFBV(inodes[handle].directAddressTranslations[virtual_blocknum] == 1)) {
            printf("DfsInodeAllocateVirtualBlock: ERROR, directAddress[%d] is already allocated (fs block %d).\n", virtual_blocknum, inodes[handle].directAddressTranslations[virtual_blocknum]);
            return DFS_FAIL;
        }
        inodes[handle].directAddressTranslations[virtual_blocknum] = DfsAllocateBlock();
        printf("DfsInodeAllocateVirtualBlock: allocated virtual block %d to fs block %d.\n", virtual_blocknum, inodes[handle].directAddressTranslations[virtual_blocknum]);
        return inodes[handle].directAddressTranslations[virtual_blocknum];
    }
    else if (virtual_blocknum <= (256+10-1)) { //265, DFS_BLOCKSIZE / sizeof(uint32) + DIRECT_ADDRESS_TRANSLATIONS_TABLE_SIZE - 1
        //In first indirect table
        //Check if first indirect table is allocated yet
        printf("DfsInodeAllocateVirtualBlock: Checking if indirect address table has been allocated. Virtual block num: %d, indirect address table block number: %d.\n", virtual_blocknum, inodes[handle].indirectAddressTableBlockNumber);
        if (inodes[handle].indirectAddressTableBlockNumber == 0 || CheckIfBlockAllocatedInFBV(inodes[handle].indirectAddressTableBlockNumber) != 1) {
            //If not allocated, need to allocate that first
            inodes[handle].indirectAddressTableBlockNumber = DfsAllocateBlock();
            printf("DfsInodeAllocateVirtualBlock: Allocated indirectAddressTable to fs block %d.\n", inodes[handle].indirectAddressTableBlockNumber);
        }
        DfsReadBlock(inodes[handle].indirectAddressTableBlockNumber, &singleIndirectAddrBlock);
        bcopy((char*)&singleIndirectAddrBlock, (char*)singleIndirectAddrTable, DFS_BLOCKSIZE);
        if (singleIndirectAddrTable[virtual_blocknum - 10] != 0 && CheckIfBlockAllocatedInFBV(singleIndirectAddrTable[virtual_blocknum - 10]) == 1) {
            printf("DfsInodeAllocateVirtualBlock: ERROR. Virtual block num %d is already allocated. IndirectAddrTable[%d] = fs block %d.\n", virtual_blocknum, virtual_blocknum - 10, singleIndirectAddrTable[virtual_blocknum - 10]);
            return DFS_FAIL;
        }
        singleIndirectAddrTable[virtual_blocknum - 10] = DfsAllocateBlock();
        printf("DfsInodeAllocateVirtualBlock: allocated virtual block %d to fs block %d.\n", virtual_blocknum, singleIndirectAddrTable[virtual_blocknum - 10]);
        bcopy((char*)singleIndirectAddrTable, (char*)&singleIndirectAddrBlock, DFS_BLOCKSIZE);
        DfsWriteBlock(inodes[handle].indirectAddressTableBlockNumber, &singleIndirectAddrBlock);
        return singleIndirectAddrTable[virtual_blocknum - 10];
    }
    else {
        //In second indirect table
        //Check if second indirect table is allocated yet
        if (inodes[handle].doubleIndirectAddressTableBlockNumber == 0) {
            //If not allocated, need to allocate that first
            inodes[handle].doubleIndirectAddressTableBlockNumber = DfsAllocateBlock();
            printf("DfsInodeAllocateVirtualBlock: Allocated doubleIndirectAddressTable to fs block %d.\n", inodes[handle].doubleIndirectAddressTableBlockNumber);
        }
        DfsReadBlock(inodes[handle].doubleIndirectAddressTableBlockNumber, &doubleIndirectAddrBlock);
        bcopy((char*)&doubleIndirectAddrBlock, (char*)doubleIndirectAddrTable, DFS_BLOCKSIZE);

        indexWithinDoubleIndirectBlock = (virtual_blocknum - (256+10)) / 256;
        indexWithinSingleIndirectBlock = (virtual_blocknum - (256+10)) % 256;

        //Check if second indirect table[] is allocated yet
        if (doubleIndirectAddrTable[indexWithinDoubleIndirectBlock] == 0) {
            //If not allocated, need to allocate that first
            doubleIndirectAddrTable[indexWithinDoubleIndirectBlock] = DfsAllocateBlock();
            printf("DfsInodeAllocateVirtualBlock: Allocated doubleIndirectAddressTable[%d] to fs block %d.\n", indexWithinDoubleIndirectBlock, doubleIndirectAddrTable[indexWithinDoubleIndirectBlock]);
            bcopy((char*)doubleIndirectAddrTable, (char*)&doubleIndirectAddrBlock, DFS_BLOCKSIZE);
            DfsWriteBlock(inodes[handle].doubleIndirectAddressTableBlockNumber, &doubleIndirectAddrBlock);
        }
        DfsReadBlock(doubleIndirectAddrTable[indexWithinDoubleIndirectBlock], &singleIndirectAddrBlock);
        bcopy((char*)&singleIndirectAddrBlock, (char*)singleIndirectAddrTable, DFS_BLOCKSIZE);

        //Check if second indirect table[][] is already allocated
        if (singleIndirectAddrTable[indexWithinSingleIndirectBlock] != 0 && CheckIfBlockAllocatedInFBV(singleIndirectAddrTable[indexWithinSingleIndirectBlock]) == 1) {
            printf("DfsInodeAllocateVirtualBlock: ERROR. Virtual block num %d is already allocated. IndirectAddrTable[%d][%d] = fs block %d.\n", virtual_blocknum, indexWithinDoubleIndirectBlock, indexWithinSingleIndirectBlock, singleIndirectAddrTable[indexWithinSingleIndirectBlock]);
            return DFS_FAIL;
        }
        singleIndirectAddrTable[indexWithinSingleIndirectBlock] = DfsAllocateBlock();
        printf("DfsInodeAllocateVirtualBlock: allocated virtual block %d to fs block %d.\n", virtual_blocknum, singleIndirectAddrTable[indexWithinSingleIndirectBlock]);
        bcopy((char*)singleIndirectAddrTable, (char*)&singleIndirectAddrBlock, DFS_BLOCKSIZE);
        DfsWriteBlock(doubleIndirectAddrTable[indexWithinDoubleIndirectBlock], &singleIndirectAddrBlock);
        return singleIndirectAddrTable[indexWithinSingleIndirectBlock];
    }
}


//-----------------------------------------------------------------
// DfsInodeTranslateVirtualToFilesys translates the 
// virtual_blocknum to the corresponding file system block using 
// the inode identified by handle. Return DFS_FAIL on failure.
//-----------------------------------------------------------------

uint32 DfsInodeTranslateVirtualToFilesys(uint32 handle, uint32 virtual_blocknum) {
    uint32 singleIndirectTable[256];
    uint32 doubleIndirectTable[256];
    dfs_block singleIndirectBlock;
    dfs_block doubleIndirectBlock;
    uint32 indexWithinDoubleIndirectBlock;
    uint32 indexWithinSingleIndirectBlock;

    if (sb.fileSystemValid != 1) {
        return DFS_FAIL;
    }

    if (inodes[handle].inUse == 0) {
        printf("DfsInodeTranslateVirtualToFilesys: ERROR, inode %d not in use.\n", handle);
        return DFS_FAIL;
    }

    if (virtual_blocknum < DIRECT_ADDRESS_TRANSLATIONS_TABLE_SIZE) {
        if (inodes[handle].directAddressTranslations[virtual_blocknum] == 0 || CheckIfBlockAllocatedInFBV(inodes[handle].directAddressTranslations[virtual_blocknum]) != 1) {
            printf("DfsInodeTranslateVirtualToFilesys: ERROR. direct address table [%d] not allocated. (virtual block num %d)\n", virtual_blocknum, virtual_blocknum);
            return DFS_FAIL;
        }
        return inodes[handle].directAddressTranslations[virtual_blocknum];
    }

    else if (virtual_blocknum <= (256+10-1)) {
        //indirect table
        // Check if allocated
        if (inodes[handle].indirectAddressTableBlockNumber == 0 || CheckIfBlockAllocatedInFBV(inodes[handle].indirectAddressTableBlockNumber != 1)) {
            printf("DfsInodeTranslateVirtualToFilesys: ERROR. Indirect address table not allocated. (virtual block num %d, indirect addr table %d, fbv came back as %d)\n", virtual_blocknum, inodes[handle].indirectAddressTableBlockNumber, CheckIfBlockAllocatedInFBV(inodes[handle].indirectAddressTableBlockNumber));
            return DFS_FAIL;
        }
        DfsReadBlock(inodes[handle].indirectAddressTableBlockNumber, &singleIndirectBlock);
        bcopy((char*)&singleIndirectBlock, (char*)singleIndirectTable, DFS_BLOCKSIZE);
        if (singleIndirectTable[virtual_blocknum - 10] == 0 || CheckIfBlockAllocatedInFBV(singleIndirectTable[virtual_blocknum - 10] != 1)) {
            printf("DfsInodeTranslateVirtualToFilesys: ERROR. Indirect address table[%d]. (virtual block num %d)\n", virtual_blocknum - 10, virtual_blocknum);
            return DFS_FAIL;
        }
        return singleIndirectTable[virtual_blocknum - 10];
    }

    else {
        //double indirect table
        if (inodes[handle].doubleIndirectAddressTableBlockNumber == 0 || CheckIfBlockAllocatedInFBV(inodes[handle].doubleIndirectAddressTableBlockNumber != 1)) {
            printf("DfsInodeTranslateVirtualToFilesys: ERROR. Double indirect address table not allocated. (virtual block num %d)\n", virtual_blocknum);
            return DFS_FAIL;
        }
        DfsReadBlock(inodes[handle].doubleIndirectAddressTableBlockNumber, &doubleIndirectBlock);
        bcopy((char*)&doubleIndirectBlock, (char*)doubleIndirectTable, DFS_BLOCKSIZE);

        indexWithinDoubleIndirectBlock = (virtual_blocknum - (256+10)) / 256;
        indexWithinSingleIndirectBlock = (virtual_blocknum - (256+10)) % 256;

        if (doubleIndirectTable[indexWithinDoubleIndirectBlock] == 0 || CheckIfBlockAllocatedInFBV(doubleIndirectTable[indexWithinDoubleIndirectBlock] != 1)) {
            printf("DfsInodeTranslateVirtualToFilesys: ERROR. Double indirect address table[%d] not allocated. (virtual block num %d)\n", indexWithinDoubleIndirectBlock, virtual_blocknum);
            return DFS_FAIL;
        }
        DfsReadBlock(doubleIndirectTable[indexWithinDoubleIndirectBlock], &singleIndirectBlock);
        bcopy((char*)&singleIndirectBlock, (char*)singleIndirectTable, DFS_BLOCKSIZE);

        if (singleIndirectTable[indexWithinSingleIndirectBlock] == 0 || CheckIfBlockAllocatedInFBV(singleIndirectTable[indexWithinSingleIndirectBlock] != 1)) {
            printf("DfsInodeTranslateVirtualToFilesys: ERROR. Double indirect address table[%d][%d] not allocated. (virtual block num %d)\n", indexWithinDoubleIndirectBlock, indexWithinSingleIndirectBlock, virtual_blocknum);
            return DFS_FAIL;
        }
        return singleIndirectTable[indexWithinSingleIndirectBlock];
    }
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