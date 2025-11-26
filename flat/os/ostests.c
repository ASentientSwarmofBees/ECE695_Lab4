#include "ostraps.h"
#include "dlxos.h"
#include "traps.h"
#include "disk.h"
#include "dfs.h"

#define TESTS 2
#define NUMBYTES 1024

void RunOSTests() {
  // STUDENT: run any os-level tests here
  int i;
  uint32 array[10];
  dfs_block block;

  uint32 inode;
  int a;
  char big[NUMBYTES];
  char big2[NUMBYTES];

  disk_block diskBlock;

  printf("OS Tests Start (TESTS = %d)\n", TESTS);

  if (TESTS == 1) {
    //Test DFS functions
    printf("Testing DFS Allocate and Free Block.\n");
    printf("   Checking allocation of 0-9.\n");
    for (i = 0; i < 10; i++) {
      printf("   Block %d status: %d.\n", i, CheckIfBlockAllocatedInFBV(i));
    }
    printf("   Attempting to allocate 10 blocks.\n");
    for (i = 0; i < 10; i++) {
      array[i] = DfsAllocateBlock();
      printf("   Allocated block %d of 10. Block # %d.\n", i, array[i]);
    }
    printf("   Checking allocation of those 10 blocks.\n");
    for (i = 0; i < 10; i++) {
      printf("   Block %d status: %d.\n", array[i], CheckIfBlockAllocatedInFBV(array[i]));
    }
    printf("   Attempting to free 5 previously allocated blocks.\n");
    for (i = 0; i < 5; i++) {
      DfsFreeBlock(array[i]);
      printf("   Freed block %d of 10. Block # %d.\n", i, array[i]);
    }
    printf("   Checking allocation of those 10 blocks.\n");
    for (i = 0; i < 10; i++) {
      printf("   Block %d status: %d.\n", array[i], CheckIfBlockAllocatedInFBV(array[i]));
    }
    printf("   Reallocating 5 blocks.\n");
    for (i = 0; i < 5; i++) {
      array[i] = DfsAllocateBlock();
      printf("   Allocated block %d of 5. Block # %d.\n", i, array[i]);
    }

    printf("Testing DFS Write and Read Block.\n");
    for (i = 0; i < 10; i++) {
      DfsReadBlock(array[i], &block);
      printf("   Block %d byte %d value: 0x%x\n", array[i], i, block.data[i]);
      block.data[i] = 0xff;
      DfsWriteBlock(array[i], &block);
      printf("   Wrote 0xff to byte %d of block %d, then reading back.\n", i, array[i]);
      DfsReadBlock(array[i], &block);
      printf("   Block %d byte %d value: 0x%x\n", array[i], i, block.data[i]);
    }
  }
  if (TESTS == 2) {
    //ostests example code, testingInodes

    inode = DfsInodeOpen("ece695-file-1");
    printf("runostests: inode after open is %d\n", inode);
    for(a=0; a<1024; a++) {
      big[a] = a;
    }
    for(a=0; a<15; a++) {
      DfsInodeWriteBytes(inode, big, a*NUMBYTES, NUMBYTES);
    }
    for (a = 0; a < 15; a++) {
      DfsInodeReadBytes(inode, big2, a*NUMBYTES, NUMBYTES);
      printf("runostests: checking data at start byte %d.\n", a*NUMBYTES);
      for (i = 0; i < NUMBYTES; i++) {
        if (big[i] != big2[i]) {
          printf("runostests: FAIL: index big[%d] != big2[%d] (%d != %d)\n", a, a, big[a], big2[a]);
          break;
        }
      }
    }
    printf("runostests: ece595-file-1 ops worked!\n");
    DfsInodeDelete(inode);
    inode = DfsInodeOpen("ece595-file-2");
    printf("runostests: ece595-file-2 open, inode = %d\n", inode);
  }
}