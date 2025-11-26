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
TESTS = 5 -> Same as test 2, but with a massive array size.
Files edited: ostests.c, files.c, fdisk.c
Sources referenced: https://stackoverflow.com/questions/348170/how-do-i-undo-git-add-before-commit
https://stackoverflow.com/questions/31281679/how-to-undo-local-changes-to-a-specific-file
https://stackoverflow.com/questions/6794110/git-revert-back-to-certain-commit

PART 5

PART 6

PART 7

PART 8

PART 9
