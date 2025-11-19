#include "ostraps.h"
#include "dlxos.h"
#include "traps.h"
#include "disk.h"
#include "dfs.h"

void RunOSTests() {
  // STUDENT: run any os-level tests here
  int i;
  uint32 array[10];
  dfs_block block;

  printf("OS Tests Start\n");

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

