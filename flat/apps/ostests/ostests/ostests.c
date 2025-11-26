#include "usertraps.h"

#define RUN_OS_TESTS 0

#define NUM_INTS 300

void main (int argc, char *argv[])
{
  int dataArray1[NUM_INTS];
  int dataArray2[NUM_INTS];
  int i, handle1, handle2;

  if (RUN_OS_TESTS == 1) {
    run_os_tests();
    return;
  }
  Printf("-----Running File Tests!\n");
  Printf("-----File Tests: Test 1: Testing opening a file, writing data to it, and reading back the same data.\n");
  for (i = 0; i < NUM_INTS; i++) {
    dataArray1[i] = i;
  }
  handle1 = file_open("file_test_1", "w");
  file_write(handle1, dataArray1, sizeof(int)*NUM_INTS);
  file_close(handle1);
  handle1 = file_open("file_test_1", "r");
  file_read(handle1, dataArray2, sizeof(int)*NUM_INTS);
  for (i = 0; i < NUM_INTS; i++) {
    if (dataArray1[i] != dataArray2[i]) {
      Printf("-----File Tests: Error! d1[%d] != d2[%d] (%d != %d)\n", i, i, dataArray1[i], dataArray2[i]);
      return;
    }
  }
  file_close(handle1);
  file_delete("file_test_1");
  Printf("-----File Tests: Passed Test 1!\n");
}
