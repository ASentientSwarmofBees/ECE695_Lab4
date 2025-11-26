PART 1
Files edited: fdisk.h, dfs_shared.h, files_shared.h, disk.h, dfs.c, dfs.h, files.c, files.h

PART 2
Files edited: dfs.c, dfs.h, ostests.c
Sources referenced: https://www.geeksforgeeks.org/c/sizeof-operator-c/

PART 3
Files edited: dfs.c, dfs_shared.h, ostests.c

PART 4
Notes: ostests.c has a global #define-d called "TESTS" that runs different tests.
TESTS = 1 -> tests disk functions (DfsAllocateBlock, DfsFreeBlock, DfsReadBlock, DfsWriteBlock, etc.)
TESTS = 2 -> tests inode functions (DfsInodeOpen, DfsInodeWriteBytes, DfsInodeReadBytes, DfsInodeDelete, etc.)
TESTS = 3 -> Same as test 2, but does not delete the inode afterwards.
TESTS = 4 -> To be used in conjunction with Test 3. Tries to read the data which should still be present after Test 3.
TESTS = 5 -> Same as test 2, but with a massive array size. *THIS CURRENTLY BREAKS THE FILE SYSTEM.* Need to re-execute fdisk.c and reset the disk after running this test to restore funcitonality.
TESTS = 6 -> Same as test 2, but with non block aligned values.
Files edited: ostests.c, files.c, fdisk.c
Sources referenced: https://stackoverflow.com/questions/348170/how-do-i-undo-git-add-before-commit
https://stackoverflow.com/questions/31281679/how-to-undo-local-changes-to-a-specific-file
https://stackoverflow.com/questions/6794110/git-revert-back-to-certain-commit

PART 5
Files edited: files.c, files.h, files_shared.h, dfs.c
Sources referenced: https://www.geeksforgeeks.org/cpp/opening-modes-in-standard-i-o-in-c-c-with-examples/

PART 6
Notes: I'm co-opting the ostests user application to test my file systems. The app ostests.c file now has a constant which detemrines whether OS tests or user app file tests will be ran.
if RUN_OS_TESTS = 0, user application file tests will run.
if RUN_OS_TESTS = 1, OS tests will run.
My user application file tests run two tests, both which open a file, write bytes, close the file, and then try to read the same data back. The two tests are the same, but the second test tests a much larger amount of data.
If an error happens where files are not correctly deleted, I was getting an error that said FileRename couldn't find a file with handle -1. When this happened, I reran fdisk and it fixed it. This bug shouldn't appear anymore now that I've fixed what was causing it, but just FYI if that error message shows up again, run fdisk.
Files edited: apps/ostests/ostests/ostests.c, file.c

PART 7
Files edited: dfs.c

PART 8

PART 9
