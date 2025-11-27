#include "ostraps.h"
#include "dlxos.h"
#include "traps.h"
#include "queue.h"
#include "disk.h"
#include "dfs.h"
#include "synch.h"
#include "files.h" //Added just so DfsModuleInit() can call my new function FileInitLock(), because I need some way to tell files.c to init the lock the first time it's used
#include "clock.h"

#define DFS_INODE_MAX_NUM 256
#define DFS_FBV_MAX_NUM_WORDS 2048
#define BUFFER_CACHE_SLOTS 128

static dfs_inode inodes[DFS_INODE_MAX_NUM]; // all inodes
static dfs_superblock sb; // superblock
static uint32 fbv[DFS_FBV_MAX_NUM_WORDS]; // Free block vector. fbv size = file system size / file system block size / 32 bits
//DFS_MAX_FILESYSTEM_SIZE / DFS_BLOCKSIZE = 0x4000000 / 1024 / 32 = 2048, 65536 bits so one bit per file system block

static uint32 negativeone = 0xFFFFFFFF;
static inline uint32 invert(uint32 n) { return n ^ negativeone; }

static lock_t fbvLock;
static lock_t inodeLock;
static lock_t bufferLock;

static dfs_block bufferCache[BUFFER_CACHE_SLOTS];
static int bufferCacheBlockNums[BUFFER_CACHE_SLOTS];
static int bufferCacheDirty[BUFFER_CACHE_SLOTS];
static int bufferCacheTimeSinceAccess[BUFFER_CACHE_SLOTS];
static uint32 cacheStatistics_NumHits = 0;
static uint32 cacheStatistics_NumMisses = 0;
static uint32 cacheStatistics_NumDiskReads = 0;
static uint32 cacheStatistics_NumDiskWrites = 0;
static double cacheStatistics_totalTimeSpentOnCacheMisses = 0.0;

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
int DfsCacheHit(int blocknum);
int DfsCacheAllocateSlot(int blocknum);
int DfsCacheFlush();
void SimulateDiskAccessTimeAndWait4MS();
void PrintCacheMissMessage();
void IncrementCacheTimeSinceAccess(int index);

//-----------------------------------------------------------------
// DfsModuleInit is called at boot time to initialize things and
// open the file system for use.
//-----------------------------------------------------------------

void DfsModuleInit() {
    int i;
// You essentially set the file system as invalid and then open 
// using DfsOpenFileSystem().
    dbprintf('z', "_DfsModuleInit\n");
    DfsInvalidate();
    DfsOpenFileSystem();
    fbvLock = LockCreate();
    inodeLock = LockCreate();
    bufferLock = LockCreate();
    FileInitLock();
    //Init the cache
    LockHandleAcquire(bufferLock);
    for (i = 0; i < BUFFER_CACHE_SLOTS; i++) {
        bufferCacheBlockNums[i] = -1;
        bufferCacheDirty[i] = 0;
        bufferCacheTimeSinceAccess[i] = 0;
    }
    LockHandleRelease(bufferLock);
    ClkModuleInit();
    //ClkStart();
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

    dbprintf('z', "DfsOpenFileSystem\n");

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
    dbprintf('z', "Checking if any inode are in use.\n");
    for (i = 0; i < sb.numberInodes; i++) {
        if (inodes[i].inUse != 0) {
            dbprintf('z', "inode %d in use = %d. filesize %d, filename '%s', direct addresses %d, %d, %d... indirect tables %d, %d.\n", i, inodes[i].inUse, inodes[i].fileSize, inodes[i].fileName, inodes[i].directAddressTranslations[0], inodes[i].directAddressTranslations[1], inodes[i].directAddressTranslations[2], inodes[i].indirectAddressTableBlockNumber, inodes[i].doubleIndirectAddressTableBlockNumber);
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

    if (sb.fileSystemValid != 1) {
        dbprintf('z', "DfsCloseFileSystem: File sytem is invalid. Not writing to disk.\n");
        return DFS_FAIL;
    }

    dbprintf('z', "DfsCloseFileSystem: Caling DfsCacheFlush().\n");
    DfsCacheFlush();

    //Write inodes
    //8 inodes fit in 1 fs block
    //2 inodes fit in 1 disk block
    dbprintf('z', "DfsCloseFileSystem: Writing inodes from block %d.\n", sb.inodesStartingBlockNumber);
    for (i = 0; i < sb.numberInodes/2; i++) {
        bcopy((char*)&inodes[i*2], (char*)block, DISK_BLOCKSIZE);
        //dbprintf('z', "Sanity check: inodes[%d] 0x%x, blockArray 0x%x, size %d bytes\n", i, &inodes[i], blockArray, sb.fileSystemBlockSize);
        //dbprintf('z', "Sanity Check: saving inodes[%d-%d]. Saving at physical block %d.\n", i*2, i*2+1, sb.inodesStartingBlockNumber*4+i);
        //dbprintf('z', "DfsCloseFileSystem: Writing inodes to block %d.\n", sb.inodesStartingBlockNumber*4+i);
        DiskWriteBlock(sb.inodesStartingBlockNumber*4+i, block);
    }

    //Write free block vector
    dbprintf('z', "DfsCloseFileSystem: Writing fbv from block %d.\n", sb.freeBlockVectorStartingBlockNumber);
    for (i = 0; i < sb.numFBVBlocks*4; i++) {
        //64 uint32s fit in one 256 byte physical block
        bcopy((char*)&fbv[i*64], (char*)block, DISK_BLOCKSIZE);
        DiskWriteBlock(sb.freeBlockVectorStartingBlockNumber*4+i, block);
    }

    dbprintf('z', "DfsCloseFileSystem: Writing superblock.\n");
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

    dbprintf('z', "_DfsAllocateBlock\n");

    if (sb.fileSystemValid != 1) {
        return DFS_FAIL;
    }

    LockHandleAcquire(fbvLock);

    do {
        //dbprintf('z', "i = %d, i/32 = %d, i mod 32 = %d, fbv[%d] = 0x%x, 0x1 << (imod32) = 0x%x, & = 0x%x\n", i, i/32, i%32, i/32, fbv[i/32], 0x1 << (i%32), fbv[i / 32] & (0x1 << (i % 32)));
        if (CheckIfBlockAllocatedInFBV(i) == 1) {
            i++;
        }
        else {
            fbv[i / 32] |= (0x1 << (i % 32));
            blockFound = 1;
            blockNum = i;
            dbprintf('z', "DfsAllocateBlock: Allocating fs block %d.\n", blockNum);
        }
    } while(blockFound == 0);

    LockHandleRelease(fbvLock);

    return blockNum;
}


//-----------------------------------------------------------------
// DfsFreeBlock deallocates a DFS block.
//-----------------------------------------------------------------

int DfsFreeBlock(uint32 blocknum) {

    dbprintf('z', "_DfsFreeBlock: handle %d", blocknum);

    if (sb.fileSystemValid != 1) {
        return DFS_FAIL;
    }

    dbprintf('z', "DfsFreeBlock: %d\n", blocknum);

    LockHandleAcquire(fbvLock);

    if (CheckIfBlockAllocatedInFBV(blocknum) == 1) {
        dbprintf('z', "DfsFreeBlock: fs block %d is '1' in FBV.\n", blocknum);
        fbv[blocknum / 32] &= invert(0x1 << (blocknum % 32));
        dbprintf('z', "DfsFreeBlock: Deallocated fs block %d.\n", blocknum);
        LockHandleRelease(fbvLock);
        return DFS_SUCCESS;
    }
    else {
        dbprintf('z', "DfsFreeBlock: Tried to free fs block %d, but was not in use.\n", blocknum);
        LockHandleRelease(fbvLock);
        return DFS_FAIL;
    }
}

//-----------------------------------------------------------------
/*
DFS READ BLOCK - PART 7 ONWARD
checks the buffer cache for blocknum, and loads it from the disk into the buffer cache if it is not found. It then 
copies the bytes from the buffer copy into the dfs_block b. On a cache miss, you should also print the statistics 
for cumulative hit/miss rates and disk I/O counts. The format should be

Cache Miss: Hit Rate = XX.XXX%, Miss Rate = XX.XXX%, Disk Reads = N, Disk Writes = M, Miss Handling Latency = Xms
Where "Miss Handling Latency" is the average of all the latencies for handling cache misses so far (including this 
one). (Hint: the source code includes timer functions)
*/
//-----------------------------------------------------------------

int DfsReadBlockCached(uint32 blocknum, dfs_block *b) {
    int cacheIndex;
    int val = 0;
    disk_block blockArray[4];
    double timeOfCacheMissStart;

    dbprintf('z', "_DfsReadBlock: blocknum %d, *b\n", blocknum);

    if (sb.fileSystemValid != 1) {
        return DFS_FAIL;
    }

    if (CheckIfBlockAllocatedInFBV(blocknum) == 0) {
        dbprintf('z', "DfsReadBlock: Tried to read fs block %d, but it is not allocated.\n", blocknum);
        return DFS_FAIL;
    }

    if ((cacheIndex = DfsCacheHit(blocknum)) != DFS_FAIL) {
        dbprintf('z', "DfsReadBlock: Cache hit! Cache[%d], blocknum %d.\n", cacheIndex, blocknum);
        val = sb.fileSystemBlockSize;
    }
    else {
        timeOfCacheMissStart = ClkGetCurTime();
        //Block is not in cache and must be loaded in
        cacheIndex = DfsCacheAllocateSlot(blocknum);
        val += DiskReadBlock(blocknum*4, &blockArray[0]);
        val += DiskReadBlock(blocknum*4+1, &blockArray[1]);
        val += DiskReadBlock(blocknum*4+2, &blockArray[2]);
        val += DiskReadBlock(blocknum*4+3, &blockArray[3]);
        SimulateDiskAccessTimeAndWait4MS();
        cacheStatistics_NumDiskReads++;
        //dbprintf('z', "SANITY CHECK: DISKREADS INC: %d\n", cacheStatistics_NumDiskReads);
        bcopy((char*)blockArray, (char*)&bufferCache[cacheIndex], sb.fileSystemBlockSize);
        if (val != sb.fileSystemBlockSize) {
            dbprintf('z', "DfsReadBlock: Tried to read %d bytes from fs block %d into cache, but only read %d.\n", sb.fileSystemBlockSize, blocknum, val);
        }
        cacheStatistics_totalTimeSpentOnCacheMisses += ClkGetCurTime() - timeOfCacheMissStart;
        PrintCacheMissMessage(cacheStatistics_totalTimeSpentOnCacheMisses/cacheStatistics_NumMisses);
    }
    bcopy((char*)&bufferCache[cacheIndex], (char*)b, sb.fileSystemBlockSize);
    IncrementCacheTimeSinceAccess(cacheIndex);
    return val;
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

    dbprintf('z', "_DfsReadBlock: blocknum %d, *b\n", blocknum);

    if (sb.fileSystemValid != 1) {
        return DFS_FAIL;
    }

    if (CheckIfBlockAllocatedInFBV(blocknum) == 0) {
        dbprintf('z', "DfsReadBlock: Tried to read fs block %d, but it is not allocated.\n", blocknum);
        return DFS_FAIL;
    }

    val += DiskReadBlock(blocknum*4, &blockArray[0]);
    val += DiskReadBlock(blocknum*4+1, &blockArray[1]);
    val += DiskReadBlock(blocknum*4+2, &blockArray[2]);
    val += DiskReadBlock(blocknum*4+3, &blockArray[3]);

    bcopy((char*)blockArray, (char*)b, sb.fileSystemBlockSize);

    if (val != sb.fileSystemBlockSize) {
        dbprintf('z', "DfsReadBlock: Tried to read %d bytes from fs block %d,, but only read %d.\n", sb.fileSystemBlockSize, blocknum, val);
    }
    else {
        //dbprintf('z', "DfsReadBlock: Successfully read %d bytes from fs block %d.\n", sb.fileSystemBlockSize, blocknum);
    }

    return val;
}

//-----------------------------------------------------------------
/*
DFS WRITE BLOCK - PART 7 ONWARD
checks the buffer cache for blocknum, and reads it from the disk into the buffer cache if it is not found. It then 
writes to the buffer cache copy of the block, and marks the block as dirty. Also print the same cache miss print as 
above on a cache miss. 
*/ 
//-----------------------------------------------------------------

int DfsWriteBlockCached(uint32 blocknum, dfs_block *b) {
    disk_block blockArray[4];
    int val = 0;
    int cacheIndex;
    double timeOfCacheMissStart;

    dbprintf('z', "_DfsWriteBlock: blocknum %d, *b\n", blocknum);
    
    if (sb.fileSystemValid != 1) {
        return DFS_FAIL;
    }

    if (CheckIfBlockAllocatedInFBV(blocknum) == 0) {
        dbprintf('z', "DfsWriteBlock: Tried to write to fs block %d, but it is not allocated.\n", blocknum);
        return DFS_FAIL;
    }
    
    if ((cacheIndex = DfsCacheHit(blocknum)) != DFS_FAIL) {
        dbprintf('z', "DfsWriteBlock: Cache hit! Cache[%d], blocknum %d.\n", cacheIndex, blocknum);
        val = sb.fileSystemBlockSize;
    }
    else {
        timeOfCacheMissStart = ClkGetCurTime();
        //Block is not in cache and must written to
        cacheIndex = DfsCacheAllocateSlot(blocknum);
        val += DiskReadBlock(blocknum*4, &blockArray[0]);
        val += DiskReadBlock(blocknum*4+1, &blockArray[1]);
        val += DiskReadBlock(blocknum*4+2, &blockArray[2]);
        val += DiskReadBlock(blocknum*4+3, &blockArray[3]);
        cacheStatistics_NumDiskReads++;
        SimulateDiskAccessTimeAndWait4MS();
        //dbprintf('z', "SANITY CHECK: DISKREADS INC: %d\n", cacheStatistics_NumDiskReads);
        bcopy((char*)blockArray, (char*)&bufferCache[cacheIndex], sb.fileSystemBlockSize);
        if (val != sb.fileSystemBlockSize) {
            dbprintf('z', "DfsWriteBlock: Tried to read %d bytes from fs block %d into cache, but only read %d.\n", sb.fileSystemBlockSize, blocknum, val);
        }
        cacheStatistics_totalTimeSpentOnCacheMisses += ClkGetCurTime() - timeOfCacheMissStart;
        PrintCacheMissMessage(cacheStatistics_totalTimeSpentOnCacheMisses/cacheStatistics_NumMisses);
    }

    bcopy((char*)b, (char*)&bufferCache[cacheIndex], sb.fileSystemBlockSize);
    IncrementCacheTimeSinceAccess(cacheIndex);
    LockHandleAcquire(bufferLock);
    bufferCacheDirty[cacheIndex] = 1;
    LockHandleRelease(bufferLock);
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

    dbprintf('z', "_DfsWriteBlock: blocknum %d, *b\n", blocknum);
    
    if (sb.fileSystemValid != 1) {
        return DFS_FAIL;
    }

    if (CheckIfBlockAllocatedInFBV(blocknum) == 0) {
        dbprintf('z', "DfsWriteBlock: Tried to write to fs block %d, but it is not allocated.\n", blocknum);
        return DFS_FAIL;
    }

    bcopy((char*)b, (char*)blockArray, sb.fileSystemBlockSize);
    bytesWritten += DiskWriteBlock(blocknum*4, &blockArray[0]);
    bytesWritten += DiskWriteBlock(blocknum*4+1, &blockArray[1]);
    bytesWritten += DiskWriteBlock(blocknum*4+2, &blockArray[2]);
    bytesWritten += DiskWriteBlock(blocknum*4+3, &blockArray[3]);

    //dbprintf('z', "DfsWriteBlock: Successfully wrote %d bytes to fs block %d.\n", bytesWritten, blocknum);

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

    dbprintf('z', "_DfsInodeFilenameExists: filename %s", filename);

    if (sb.fileSystemValid != 1) {
        dbprintf('z', "DfsInodeFilenameExists: ERROR. File system not valid.\n");
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

    dbprintf('z', "_DfsInodeOpen: filename %s", filename);

    if (sb.fileSystemValid != 1) {
        return DFS_FAIL;
    }

    if ((handle = DfsInodeFilenameExists(filename)) != -1) {
        //Filename exists
        dbprintf('z', "DfsInodeOpen: file '%s' already exists at handle %d.\n", filename, handle);
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
        dbprintf('z', "DfsInodeOpen: ERROR. No free inodes left to allocate.\n");
        return DFS_FAIL;
    }
    //Copy over filename
    dstrncpy(inodes[handle].fileName, filename, FILE_MAX_FILENAME_LENGTH);

    //dbprintf('z', "DfsInodeOpen: Sanity Check. double table %d\n", inodes[handle].doubleIndirectAddressTableBlockNumber);

    LockHandleAcquire(inodeLock);
    inodes[handle].inUse = 1;
    LockHandleRelease(inodeLock);

    dbprintf('z', "DfsInodeOpen: Allocated inode %d.\n", handle);
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

    dbprintf('z', "_DfsInodeDelete: handle %d\n", handle);

    if (sb.fileSystemValid != 1) {
        return DFS_FAIL;
    }

    if (inodes[handle].inUse == 0) {
        dbprintf('z', "DfsInodeDelete: ERROR. Tried to delete an inode that is not in use. (handle: %d)\n", handle);
        return DFS_FAIL;
    }
    dbprintf('z', "DfsInodeDelete: Deleting inode handle %d, inUse == %d\n", handle, inodes[handle].inUse);

    //de-allocate data blocks used by this inode
    for (i = 0; i < DIRECT_ADDRESS_TRANSLATIONS_TABLE_SIZE; i++) {
        dbprintf('z', "DfsInodeDelete: Checking if direct addr table[%d] is in use, fs block %d\n", i, inodes[handle].directAddressTranslations[i]);
        if (inodes[handle].directAddressTranslations[i] != 0 && CheckIfBlockAllocatedInFBV(inodes[handle].directAddressTranslations[i]) == 1) {
            dbprintf('z', "DfsINodeDelete: Freeing direct Addr table [%d], fs block %d\n", i, inodes[handle].directAddressTranslations[i]);
            DfsFreeBlock(inodes[handle].directAddressTranslations[i]);
        }
        inodes[handle].directAddressTranslations[i] = 0;
    }

    //de-allocate indirect addressing blocks if in use
    dbprintf('z', "DfsInodeDelete: Checking if indirect table is in use, fs block %d\n", inodes[handle].indirectAddressTableBlockNumber);
    if (inodes[handle].indirectAddressTableBlockNumber != 0 && CheckIfBlockAllocatedInFBV(inodes[handle].indirectAddressTableBlockNumber) == 1) {
        //de-allocate all blocks pointed to by indirect address table
        dbprintf('z', "SANITY CHECK. DRB 11. block: %d\n", inodes[handle].indirectAddressTableBlockNumber);
        DfsReadBlock(inodes[handle].indirectAddressTableBlockNumber, &singleIndirectBlock);
        bcopy((char*)&singleIndirectBlock, (char*)singleIndirectTable, DFS_BLOCKSIZE);
        for (i = 0; i < 256; i++) {
            if(singleIndirectTable[i] != 0 && CheckIfBlockAllocatedInFBV(singleIndirectTable[i]) == 1) {
                dbprintf('z', "DfsInodeDelete: Freeing indirect addr table[%d], fs block %d.\n", i, singleIndirectTable[i]);
                DfsFreeBlock(singleIndirectTable[i]);
            }
            singleIndirectTable[i] = 0;
        }
        dbprintf('z', "DfsInodeDelete: Freeing indirect addr table, fs block %d.\n", inodes[handle].indirectAddressTableBlockNumber);
        DfsFreeBlock(inodes[handle].indirectAddressTableBlockNumber);
    }
    inodes[handle].indirectAddressTableBlockNumber = 0;
    dbprintf('z', "DfsInodeDelete: Checking if double indirect table is in use, fs block %d\n", inodes[handle].doubleIndirectAddressTableBlockNumber);
    if (inodes[handle].doubleIndirectAddressTableBlockNumber != 0 && CheckIfBlockAllocatedInFBV(inodes[handle].doubleIndirectAddressTableBlockNumber) == 1) {
        //de-allocate all blocks pointed to by tables pointed to within double address table
        dbprintf('z', "SANITY CHECK. DRB 10. block: %d\n", inodes[handle].doubleIndirectAddressTableBlockNumber);
        DfsReadBlock(inodes[handle].doubleIndirectAddressTableBlockNumber, &doubleIndirectBlock);
        bcopy((char*)&doubleIndirectBlock, (char*)doubleIndirectTable, DFS_BLOCKSIZE);
        for (i = 0; i < 256; i++) {
            if (doubleIndirectTable[i] != 0 && CheckIfBlockAllocatedInFBV(doubleIndirectTable[i])) {
                dbprintf('z', "SANITY CHECK. DRB 9. block: %d\n", doubleIndirectTable[i]);
                DfsReadBlock(doubleIndirectTable[i], &singleIndirectBlock);
                bcopy((char*)&singleIndirectBlock, (char*)singleIndirectTable, DFS_BLOCKSIZE);
                for (j = 0; j < 256; j++) {
                    if (singleIndirectTable[j] != 0 && CheckIfBlockAllocatedInFBV(singleIndirectTable[j]) == 1) {
                        dbprintf('z', "DfsInodeDelete: Freeing double indirect addr table [%d][%d], fs block %d.\n", i, j, singleIndirectTable[j]);
                        DfsFreeBlock(singleIndirectTable[j]);
                    }
                    singleIndirectTable[j] = 0;
                }
                dbprintf('z', "DfsInodeDelete: Freeing indirect addr table[%d], fs block %d.\n", i, doubleIndirectTable[i]);
                DfsFreeBlock(doubleIndirectTable[i]);
            }
            doubleIndirectTable[i] = 0;
        }
        dbprintf('z', "DfsInodeDelete: Freeing indirect addr table, fs block %d.\n", inodes[handle].doubleIndirectAddressTableBlockNumber);
        DfsFreeBlock(inodes[handle].doubleIndirectAddressTableBlockNumber);
    }
    inodes[handle].doubleIndirectAddressTableBlockNumber = 0;

    //mark inode as not in use
    dbprintf('z', "DfsInodeDelete: Marking inode %d as not in use. ", handle);
    LockHandleAcquire(inodeLock);
    inodes[handle].inUse = 0;
    dbprintf('z', "inuse now = %d.\n", inodes[handle].inUse);
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

    dbprintf('z', "_DfsInodeReadBytes: handle %d, *mem, start_byte %d, num_bytes %d\n", handle, start_byte, num_bytes);

    if (sb.fileSystemValid != 1) {
        return DFS_FAIL;
    }

    if (inodes[handle].inUse != 1) {
        dbprintf('z', "DfsInodeReadBytes: ERROR. Tried to read bytes from an inode that is not in use.\n");
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
        dbprintf('z', "DfsInodeReadBytes: Trying to read past end of file (%d + %d > %d). Throttling num_bytes to %d.\n", start_byte, num_bytes, inodes[handle].fileSize, inodes[handle].fileSize - start_byte);
        num_bytes = inodes[handle].fileSize - start_byte;
    }

    while (num_bytes > 0) {

        virtualBlockNumber = start_byte / DFS_BLOCKSIZE;
        virtualByteOffset = start_byte % DFS_BLOCKSIZE;

        //First, get to the actual direct block we need to be reading at
        if (DfsInodeTranslateVirtualToFilesys(handle, virtualBlockNumber) == DFS_FAIL) {
            //Block needs to be allocated
            dbprintf('z', "DfsInodeWriteBytes: Allocating virtual block %d.\n", virtualBlockNumber);
            DfsInodeAllocateVirtualBlock(handle, virtualBlockNumber);
        }
        dbprintf('z', "DfsInodeReadBytes: While loop. start byte: %d, num bytes: %d, virtual block: %d, virtual offset: %d, fs block: %d.\n", start_byte, num_bytes, virtualBlockNumber, virtualByteOffset, fileSysBlockNumber);
        dbprintf('z', "SANITY CHECK. DRB 8. block: %d\n", fileSysBlockNumber);
        DfsReadBlock(fileSysBlockNumber, &currDfsblock);

        //Now, actually do the reading!
        if (virtualByteOffset + num_bytes > DFS_BLOCKSIZE) {
            //We are reading past the current block and will need to move on to the next block.
            bcopy((char*)&currDfsblock.data[virtualByteOffset], (char*)(mem) + bytesRead, DFS_BLOCKSIZE - virtualByteOffset);
            bytesRead += DFS_BLOCKSIZE - virtualByteOffset;
            num_bytes -= DFS_BLOCKSIZE - virtualByteOffset;
            start_byte += DFS_BLOCKSIZE - virtualByteOffset;
        }
        else {
            //There is room in the current block to finish reading everything we need to.
            bcopy((char*)&currDfsblock.data[virtualByteOffset], (char*)(mem) + bytesRead, num_bytes);
            bytesRead += num_bytes;
            start_byte += num_bytes;
            num_bytes = 0;
        }
    }

    dbprintf('z', "DfsInodeReadBytes: Successfully read %d bytes.\n", bytesRead);
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

    dbprintf('z', "_DfsInodeWriteBytes: handle %d, *mem, start_byte %d, num_bytes %d\n", handle, start_byte, num_bytes);

    if (sb.fileSystemValid != 1) {
        return DFS_FAIL;
    }

    if (inodes[handle].inUse != 1) {
        dbprintf('z', "DfsInodeWriteBytes: ERROR. Tried to write bytes to an inode that is not in use.\n");
        return DFS_FAIL;
    }

    if (num_bytes < 0) {
        return DFS_FAIL;
    }

    if (num_bytes == 0) {
        return 0;
    }

    dbprintf('z', "DfsInodeWriteBytes: Writing %d bytes from start byte %d in inode %d.\n", num_bytes, start_byte, handle);

    while (num_bytes > 0) {

        virtualBlockNumber = start_byte / DFS_BLOCKSIZE;
        virtualByteOffset = start_byte % DFS_BLOCKSIZE;

        //First, get to the actual direct block we need to be reading at
        if (DfsInodeTranslateVirtualToFilesys(handle, virtualBlockNumber) == DFS_FAIL) {
            //Block needs to be allocated
            dbprintf('z', "DfsInodeWriteBytes: Allocating virtual block %d.\n", virtualBlockNumber);
            DfsInodeAllocateVirtualBlock(handle, virtualBlockNumber);
        }
        fileSysBlockNumber = DfsInodeTranslateVirtualToFilesys(handle, virtualBlockNumber);
        dbprintf('z', "DfsInodeWriteBytes: While loop. start byte: %d, num bytes: %d, virtual block: %d, virtual offset: %d, fs block: %d\n", start_byte, num_bytes, virtualBlockNumber, virtualByteOffset, fileSysBlockNumber);
        dbprintf('z', "SANITY CHECK. DRB 7. block: %d\n", fileSysBlockNumber);
        DfsReadBlock(fileSysBlockNumber, &currDfsBlock);

        //Now, actually do the writing!
        //dbprintf('z', "SANITY CHECK: mem 0x%x, mem + byteswritten 0x%x\n", mem, (char*)(mem) + bytesWritten);
        if (virtualByteOffset + num_bytes > DFS_BLOCKSIZE) {
            //We are reading past the current block and will need to move on to the next block.
            bcopy((char*)(mem) + bytesWritten, (char*)&currDfsBlock.data[virtualByteOffset], DFS_BLOCKSIZE - virtualByteOffset);
            DfsWriteBlock(fileSysBlockNumber, &currDfsBlock);
            bytesWritten += DFS_BLOCKSIZE - virtualByteOffset;
            num_bytes -= DFS_BLOCKSIZE - virtualByteOffset;
            start_byte += DFS_BLOCKSIZE - virtualByteOffset;
        }
        else {
            //There is room in the current block to finish writing everything we need to.
            bcopy((char*)(mem) + bytesWritten, (char*)&currDfsBlock.data[virtualByteOffset], num_bytes);
            DfsWriteBlock(fileSysBlockNumber, &currDfsBlock);
            bytesWritten += num_bytes;
            start_byte += num_bytes;
            num_bytes = 0;
        }
    }

    //update inode filesize to the maximum byte that has been written to in this file
    if (start_byte > inodes[handle].fileSize) {
        inodes[handle].fileSize = start_byte;
    }

    dbprintf('z', "DfsInodeWriteBytes: Successfully wrote %d bytes.\n", bytesWritten);
    return bytesWritten;
}


//-----------------------------------------------------------------
// DfsInodeFilesize simply returns the size of an inode's file. 
// This is defined as the maximum virtual byte number that has 
// been written to the inode thus far. Return DFS_FAIL on failure.
//-----------------------------------------------------------------

uint32 DfsInodeFilesize(uint32 handle) {

    dbprintf('z', "_DfsInodeFilesize: handle %d\n", handle);

    if (sb.fileSystemValid != 1) {
        return DFS_FAIL;
    }

    if (inodes[handle].inUse == 0) {
        dbprintf('z', "DfsInodeFilesize: ERROR, inode %d not in use.\n", handle);
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

uint32  DfsInodeAllocateVirtualBlock(uint32 handle, uint32 virtual_blocknum) {
    dfs_block doubleIndirectAddrBlock;
    dfs_block singleIndirectAddrBlock;
    uint32 doubleIndirectAddrTable[256];
    uint32 singleIndirectAddrTable[256];
    uint32 indexWithinDoubleIndirectBlock;
    uint32 indexWithinSingleIndirectBlock;

    dbprintf('z', "_DfsInodeAllocateVirtualBlock: handle %d, virtual_blocknum %d\n", handle, virtual_blocknum);

    if (sb.fileSystemValid != 1) {
        return DFS_FAIL;
    }

    if (inodes[handle].inUse != 1) {
        dbprintf('z', "DfsInodeAllocateVirtualBlock: ERROR, inode %d not in use.\n", handle);
        return DFS_FAIL;
    }

    if (virtual_blocknum < DIRECT_ADDRESS_TRANSLATIONS_TABLE_SIZE) {
        //One of the 10 direct blocks
        if (inodes[handle].directAddressTranslations[virtual_blocknum] != 0 && CheckIfBlockAllocatedInFBV(inodes[handle].directAddressTranslations[virtual_blocknum] == 1)) {
            dbprintf('z', "DfsInodeAllocateVirtualBlock: ERROR, directAddress[%d] is already allocated (fs block %d).\n", virtual_blocknum, inodes[handle].directAddressTranslations[virtual_blocknum]);
            return DFS_FAIL;
        }
        inodes[handle].directAddressTranslations[virtual_blocknum] = DfsAllocateBlock();
        dbprintf('z', "DfsInodeAllocateVirtualBlock: allocated virtual block %d to fs block %d.\n", virtual_blocknum, inodes[handle].directAddressTranslations[virtual_blocknum]);
        return inodes[handle].directAddressTranslations[virtual_blocknum];
    }
    else if (virtual_blocknum <= (256+10-1)) { //265, DFS_BLOCKSIZE / sizeof(uint32) + DIRECT_ADDRESS_TRANSLATIONS_TABLE_SIZE - 1
        //In first indirect table
        //Check if first indirect table is allocated yet
        dbprintf('z', "DfsInodeAllocateVirtualBlock: Checking if indirect address table has been allocated. Virtual block num: %d, indirect address table block number: %d.\n", virtual_blocknum, inodes[handle].indirectAddressTableBlockNumber);
        if (inodes[handle].indirectAddressTableBlockNumber == 0 || CheckIfBlockAllocatedInFBV(inodes[handle].indirectAddressTableBlockNumber) != 1) {
            //If not allocated, need to allocate that first
            inodes[handle].indirectAddressTableBlockNumber = DfsAllocateBlock();
            dbprintf('z', "DfsInodeAllocateVirtualBlock: Allocated indirectAddressTable to fs block %d.\n", inodes[handle].indirectAddressTableBlockNumber);
        }
        dbprintf('z', "SANITY CHECK. DRB 6. block: %d\n", inodes[handle].indirectAddressTableBlockNumber);
        DfsReadBlock(inodes[handle].indirectAddressTableBlockNumber, &singleIndirectAddrBlock);
        bcopy((char*)&singleIndirectAddrBlock, (char*)singleIndirectAddrTable, DFS_BLOCKSIZE);
        if (singleIndirectAddrTable[virtual_blocknum - 10] != 0 && CheckIfBlockAllocatedInFBV(singleIndirectAddrTable[virtual_blocknum - 10]) == 1) {
            dbprintf('z', "DfsInodeAllocateVirtualBlock: ERROR. Virtual block num %d is already allocated. IndirectAddrTable[%d] = fs block %d.\n", virtual_blocknum, virtual_blocknum - 10, singleIndirectAddrTable[virtual_blocknum - 10]);
            return DFS_FAIL;
        }
        singleIndirectAddrTable[virtual_blocknum - 10] = DfsAllocateBlock();
        dbprintf('z', "DfsInodeAllocateVirtualBlock: allocated virtual block %d to fs block %d.\n", virtual_blocknum, singleIndirectAddrTable[virtual_blocknum - 10]);
        bcopy((char*)singleIndirectAddrTable, (char*)&singleIndirectAddrBlock, DFS_BLOCKSIZE);
        DfsWriteBlock(inodes[handle].indirectAddressTableBlockNumber, &singleIndirectAddrBlock);
        return singleIndirectAddrTable[virtual_blocknum - 10];
    }
    else {
        //In second indirect table
        //Check if second indirect table is allocated yet
        dbprintf('y', "DfsInodeAllocateVirtualBlock: Allocating a block in double indirect table. doubleIndirectBlockNum: %d\n", inodes[handle].doubleIndirectAddressTableBlockNumber);
        //THIS IS NEVER EVER EVER EXECUTING. DOUBLEINDIRECTADDRESSTABLEBLOCKNUMBER IS JUST FRICKING 309 ALREADY. IT SHOULD START AS 0!!!!!!!!
        if (inodes[handle].doubleIndirectAddressTableBlockNumber == 0 || CheckIfBlockAllocatedInFBV(inodes[handle].doubleIndirectAddressTableBlockNumber) != 1) {
            //If not allocated, need to allocate that first
            inodes[handle].doubleIndirectAddressTableBlockNumber = DfsAllocateBlock();
            dbprintf('z', "DfsInodeAllocateVirtualBlock: Allocated doubleIndirectAddressTable to fs block %d.\n", inodes[handle].doubleIndirectAddressTableBlockNumber);
            //MAKE ABSOLUTELY SURE IT'S FRICKING EMPTY
            dbprintf('z', "DfsInodeAllocateVirtualBlock: Making ABSOLUTELY SURE DOUBLE INDIRECT TABLE IS EMPTY.\n");
            bzero(&doubleIndirectAddrBlock, sb.fileSystemBlockSize);
            DfsWriteBlock(inodes[handle].doubleIndirectAddressTableBlockNumber, &doubleIndirectAddrBlock);
        }
        dbprintf('z', "SANITY CHECK. DRB 5. block: %d\n", inodes[handle].doubleIndirectAddressTableBlockNumber);
        DfsReadBlock(inodes[handle].doubleIndirectAddressTableBlockNumber, &doubleIndirectAddrBlock);
        bcopy((char*)&doubleIndirectAddrBlock, (char*)doubleIndirectAddrTable, DFS_BLOCKSIZE);

        indexWithinDoubleIndirectBlock = (virtual_blocknum - (256+10)) / 256;
        indexWithinSingleIndirectBlock = (virtual_blocknum - (256+10)) % 256;
        dbprintf('z', "DfsInodeAllocateVirtualBlock: Calculated virtual blocknum to be in double table[%d][%d]\n", indexWithinDoubleIndirectBlock, indexWithinSingleIndirectBlock)

        //Check if second indirect table[] is allocated yet
        dbprintf('z', "DfsInodeAllocateVirtualBlock: Checking if double indirect table[%d] has been allocated yet. %d = 0?\n", indexWithinDoubleIndirectBlock, doubleIndirectAddrTable[indexWithinDoubleIndirectBlock])
        if (doubleIndirectAddrTable[indexWithinDoubleIndirectBlock] == 0) {
            //If not allocated, need to allocate that first
            doubleIndirectAddrTable[indexWithinDoubleIndirectBlock] = DfsAllocateBlock();
            dbprintf('z', "DfsInodeAllocateVirtualBlock: Allocated doubleIndirectAddressTable[%d] to fs block %d.\n", indexWithinDoubleIndirectBlock, doubleIndirectAddrTable[indexWithinDoubleIndirectBlock]);
            bcopy((char*)doubleIndirectAddrTable, (char*)&doubleIndirectAddrBlock, DFS_BLOCKSIZE);
            DfsWriteBlock(inodes[handle].doubleIndirectAddressTableBlockNumber, &doubleIndirectAddrBlock);
        }
        dbprintf('z', "SANITY CHECK. DRB 4. block: %d\n", doubleIndirectAddrTable[indexWithinDoubleIndirectBlock]);
        DfsReadBlock(doubleIndirectAddrTable[indexWithinDoubleIndirectBlock], &singleIndirectAddrBlock);
        bcopy((char*)&singleIndirectAddrBlock, (char*)singleIndirectAddrTable, DFS_BLOCKSIZE);

        //Check if second indirect table[][] is already allocated
        if (singleIndirectAddrTable[indexWithinSingleIndirectBlock] != 0 && CheckIfBlockAllocatedInFBV(singleIndirectAddrTable[indexWithinSingleIndirectBlock]) == 1) {
            dbprintf('z', "DfsInodeAllocateVirtualBlock: ERROR. Virtual block num %d is already allocated. IndirectAddrTable[%d][%d] = fs block %d.\n", virtual_blocknum, indexWithinDoubleIndirectBlock, indexWithinSingleIndirectBlock, singleIndirectAddrTable[indexWithinSingleIndirectBlock]);
            return DFS_FAIL;
        }
        singleIndirectAddrTable[indexWithinSingleIndirectBlock] = DfsAllocateBlock();
        dbprintf('z', "DfsInodeAllocateVirtualBlock: allocated virtual block %d to fs block %d.\n", virtual_blocknum, singleIndirectAddrTable[indexWithinSingleIndirectBlock]);
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

    dbprintf('z', "_DfsInodeTranslateVirtualToFilesys: handle %d, virtual_blocknum %d\n", handle, virtual_blocknum);

    if (sb.fileSystemValid != 1) {
        dbprintf('z', "DfsInodeTranslateVirtualToFilesys: File system invalid.\n");
        return DFS_FAIL;
    }

    if (inodes[handle].inUse == 0) {
        dbprintf('z', "DfsInodeTranslateVirtualToFilesys: ERROR, inode %d not in use.\n", handle);
        return DFS_FAIL;
    }

    if (virtual_blocknum < DIRECT_ADDRESS_TRANSLATIONS_TABLE_SIZE) {
        if (inodes[handle].directAddressTranslations[virtual_blocknum] == 0 || CheckIfBlockAllocatedInFBV(inodes[handle].directAddressTranslations[virtual_blocknum]) != 1) {
            dbprintf('z', "DfsInodeTranslateVirtualToFilesys: ERROR. direct address table [%d] not allocated. (virtual block num %d)\n", virtual_blocknum, virtual_blocknum);
            return DFS_FAIL;
        }
        return inodes[handle].directAddressTranslations[virtual_blocknum];
    }

    else if (virtual_blocknum <= (256+10-1)) {
        //indirect table
        // Check if allocated
        if (inodes[handle].indirectAddressTableBlockNumber == 0 || CheckIfBlockAllocatedInFBV(inodes[handle].indirectAddressTableBlockNumber) != 1) {
            dbprintf('z', "DfsInodeTranslateVirtualToFilesys: ERROR. Indirect address table not allocated. (virtual block num %d\n", virtual_blocknum);
            return DFS_FAIL;
        }
        dbprintf('z', "SANITY CHECK. DRB 3. block: %d\n", inodes[handle].indirectAddressTableBlockNumber);
        DfsReadBlock(inodes[handle].indirectAddressTableBlockNumber, &singleIndirectBlock);
        bcopy((char*)&singleIndirectBlock, (char*)singleIndirectTable, DFS_BLOCKSIZE);
        if (singleIndirectTable[virtual_blocknum - 10] == 0 || CheckIfBlockAllocatedInFBV(singleIndirectTable[virtual_blocknum - 10]) != 1) {
            dbprintf('z', "DfsInodeTranslateVirtualToFilesys: ERROR. Indirect address table[%d] not allocated. (virtual block num %d)\n", virtual_blocknum - 10, virtual_blocknum);
            return DFS_FAIL;
        }
        return singleIndirectTable[virtual_blocknum - 10];
    }

    else {
        //double indirect table
        if (inodes[handle].doubleIndirectAddressTableBlockNumber == 0 || CheckIfBlockAllocatedInFBV(inodes[handle].doubleIndirectAddressTableBlockNumber) != 1) {
            dbprintf('z', "DfsInodeTranslateVirtualToFilesys: ERROR. Double indirect address table not allocated. (virtual block num %d)\n", virtual_blocknum);
            return DFS_FAIL;
        }
        dbprintf('z', "SANITY CHECK. DRB 2. block: %d\n", inodes[handle].doubleIndirectAddressTableBlockNumber);
        DfsReadBlock(inodes[handle].doubleIndirectAddressTableBlockNumber, &doubleIndirectBlock);
        bcopy((char*)&doubleIndirectBlock, (char*)doubleIndirectTable, DFS_BLOCKSIZE);

        indexWithinDoubleIndirectBlock = (virtual_blocknum - (256+10)) / 256;
        indexWithinSingleIndirectBlock = (virtual_blocknum - (256+10)) % 256;

        if (doubleIndirectTable[indexWithinDoubleIndirectBlock] == 0 || CheckIfBlockAllocatedInFBV(doubleIndirectTable[indexWithinDoubleIndirectBlock]) != 1) {
            dbprintf('z', "DfsInodeTranslateVirtualToFilesys: ERROR. Double indirect address table[%d] not allocated. (virtual block num %d)\n", indexWithinDoubleIndirectBlock, virtual_blocknum);
            return DFS_FAIL;
        }
        dbprintf('z', "SANITY CHECK. DRB 1. block: %d\n", doubleIndirectTable[indexWithinDoubleIndirectBlock]);
        DfsReadBlock(doubleIndirectTable[indexWithinDoubleIndirectBlock], &singleIndirectBlock);
        bcopy((char*)&singleIndirectBlock, (char*)singleIndirectTable, DFS_BLOCKSIZE);

        if (singleIndirectTable[indexWithinSingleIndirectBlock] == 0 || CheckIfBlockAllocatedInFBV(singleIndirectTable[indexWithinSingleIndirectBlock]) != 1) {
            dbprintf('z', "DfsInodeTranslateVirtualToFilesys: ERROR. Double indirect address table[%d][%d] not allocated. (virtual block num %d)\n", indexWithinDoubleIndirectBlock, indexWithinSingleIndirectBlock, virtual_blocknum);
            return DFS_FAIL;
        }
        dbprintf('z', "TranslateVirtToFilesys: Sanity Check: returning %d.\n", singleIndirectTable[indexWithinSingleIndirectBlock]);
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
    dbprintf('z', "   ___PrintSBTest___\n");
    dbprintf('z', "   sb.fileSystemValid: %d\n", sb.fileSystemValid);
    dbprintf('z', "   sb.fileSystemBlockSize: %d\n", sb.fileSystemBlockSize);
    dbprintf('z', "   sb.numberFileSystemBlocks: %d\n", sb.numberFileSystemBlocks);
    dbprintf('z', "   sb.inodesStartingBlockNumber: %d\n", sb.inodesStartingBlockNumber);
    dbprintf('z', "   sb.numberInodes: %d\n", sb.numberInodes);
    dbprintf('z', "   sb.freeBlockVectorStartingBlockNumber: %d\n", sb.freeBlockVectorStartingBlockNumber);
    dbprintf('z', "   sb.numFBVBlocks: %d\n", sb.numFBVBlocks);
}

/*
checks the cache for the given blocknum. Returns DFS_FAIL if blocknum is not found, and the handle to the buffer 
cache slot on success. 
*/
int DfsCacheHit(int blocknum) {
    int i;
    int index = DFS_FAIL;

    dbprintf('z', "_DfsCacheHit: blocknum %d\n", blocknum);

    for (i = 0; i < BUFFER_CACHE_SLOTS; i++) {
        if (bufferCacheBlockNums[i] == blocknum) {
            index = i;
            cacheStatistics_NumHits++;
            break;
        }
    }
    cacheStatistics_NumMisses++;
    return index;
}

/*
allocate a buffer slot for the given filesystem block number. If an empty slot is not available, evict a block 
based on your replacement policy. If this evicted block is marked as dirty, then it must be written back to the 
disk.
*/
int DfsCacheAllocateSlot(int blocknum) {
    int i;
    int index = -1;
    int highest_found_time_since_access;
    disk_block blockArray[4];
    int bytesWritten = 0;

    dbprintf('z', "_DfsCacheAllocateSlot: blocknum %d\n", blocknum);

    if (blocknum > 65535) {
        dbprintf('z', "DfsCacheAllocateSlot: ERROR. Blocknum %d greater than max blocknum %d.\n", blocknum, 65535)
        return DFS_FAIL;
    }

    if ((index = DfsCacheHit(blocknum)) != DFS_FAIL) {
        dbprintf('z', "DfsCacheAllocateSlot: ERROR. Blocknum %d already in cache idx %d.\n", blocknum, index);
        return index;
    }

    for (i = 0; i < BUFFER_CACHE_SLOTS; i++) {
        if (bufferCacheBlockNums[i] == -1) {
            LockHandleAcquire(bufferLock);
            bufferCacheBlockNums[i] = blocknum;
            LockHandleRelease(bufferLock);
            dbprintf('z', "DfsCacheAllocateSlot: Allocating slot %d for blocknum %d.\n", i, blocknum);
            return i;
        }
    }

    //No free block is available. Remove a block according to eviction policy. Write it back to disk if it is dirty.
    //CURRENT EVICTION POLICY: BLOCKS WHICH HAVE BEEN USED THE LEAST NUMBER OF TIMES
    highest_found_time_since_access = bufferCacheTimeSinceAccess[0];
    index = 0;
    for (i = 0; i < BUFFER_CACHE_SLOTS; i++) {
        if (bufferCacheTimeSinceAccess[i] > highest_found_time_since_access) {
            highest_found_time_since_access = bufferCacheTimeSinceAccess[i];
            index = i;
        }
    }
    dbprintf('z', "DfsCacheAllocateSlot: Evicting cache[%d] with time %d since access (dirty = %d).\n", index, bufferCacheTimeSinceAccess[index], bufferCacheDirty[index]);
    if (bufferCacheDirty[index] == 1) {
        //Writeback!
        bcopy((char*)&bufferCache[index], (char*)blockArray, sb.fileSystemBlockSize);
        bytesWritten += DiskWriteBlock(blocknum*4, &blockArray[0]);
        bytesWritten += DiskWriteBlock(blocknum*4+1, &blockArray[1]);
        bytesWritten += DiskWriteBlock(blocknum*4+2, &blockArray[2]);
        bytesWritten += DiskWriteBlock(blocknum*4+3, &blockArray[3]);
        cacheStatistics_NumDiskWrites++;
        SimulateDiskAccessTimeAndWait4MS();
        if (bytesWritten != sb.fileSystemBlockSize) {
            dbprintf('z', "DfsCacheAllocateSlot: ERROR. Tried to write dirty cache to disk. Tried to write %d bytes, but only wrote %d. (this is a non-terminating error)\n", sb.fileSystemBlockSize, bytesWritten);
        }
    }
    LockHandleAcquire(bufferLock);
    bufferCacheBlockNums[index] = -1;
    bufferCacheDirty[index] = 0;
    bufferCacheTimeSinceAccess[index] = 0;
    LockHandleRelease(bufferLock);
    return index;
}

/*
move through all the full slots in the buffer, writing any dirty blocks to the disk and then adding the buffer slot 
back to the list of available empty slots. Returns DSF_FAIL on failure and DFS_SUCCESS on success. This function is 
primarily used when the operating system exits. You will need to call it from your DfsFileSystemClose function.
*/
int DfsCacheFlush() {
    int i;
    int bytesWritten;
    disk_block blockArray[4];

    dbprintf('z', "_DfsCacheFlush\n");

    for (i = 0; i < BUFFER_CACHE_SLOTS; i++) {
        if (bufferCacheDirty[i] == 1) {
            bytesWritten = 0;
            dbprintf('z', "DfsCacheFlush: Flushing dirty cache[%d].\n", i);
            bcopy((char*)&bufferCache[i], (char*)blockArray, sb.fileSystemBlockSize);
            bytesWritten += DiskWriteBlock(bufferCacheBlockNums[i]*4, &blockArray[0]);
            bytesWritten += DiskWriteBlock(bufferCacheBlockNums[i]*4+1, &blockArray[1]);
            bytesWritten += DiskWriteBlock(bufferCacheBlockNums[i]*4+2, &blockArray[2]);
            bytesWritten += DiskWriteBlock(bufferCacheBlockNums[i]*4+3, &blockArray[3]);
            cacheStatistics_NumDiskWrites++;
            if(bytesWritten != sb.fileSystemBlockSize) {
                dbprintf('z', "DfsCacheFlush: ERROR. Tried to write %d bytes, but only wrote %d.\n", sb.fileSystemBlockSize, bytesWritten);
            }
        }
    }
    return DFS_SUCCESS;
}

void PrintCacheMissMessage(double latencyInSeconds) {
    double hit_rate, miss_rate;
    hit_rate = ((double)(cacheStatistics_NumHits))/(cacheStatistics_NumHits + cacheStatistics_NumMisses)*100;
    miss_rate = ((double)(cacheStatistics_NumMisses))/(cacheStatistics_NumHits + cacheStatistics_NumMisses)*100;
    dbprintf('z', "####%c", '#');
    printf("Cache Miss: Hit Rate = %.3f%%, Miss Rate = %.3f%%, ", hit_rate, miss_rate);
    printf("Disk Reads = %d, Disk Writes = %d, Miss Handling Latency = %.3fms\n", cacheStatistics_NumDiskReads, cacheStatistics_NumDiskWrites, latencyInSeconds/1000);
}

void SimulateDiskAccessTimeAndWait4MS() {
    /*
    double startTime = ClkGetCurTime();
    ClkStart();
    dbprintf('z', "SimulateDiskAccessTimeAndWait%dMS\n", 4);
    while ((ClkGetCurTime() - startTime) < 0.004) {
        printf("%f - %f = %f\n", ClkGetCurTime(), startTime, ClkGetCurTime() - startTime);
    }
    */
}

void IncrementCacheTimeSinceAccess(int index) {
    int i;
    LockHandleAcquire(bufferLock);
    for (i = 0; i < BUFFER_CACHE_SLOTS; i++) {
        if (i != index) {
            bufferCacheTimeSinceAccess[i]++;
        }
    }
    LockHandleRelease(bufferLock);
}