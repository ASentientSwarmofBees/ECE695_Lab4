PART 1
Notes: To execute, run "mainframer.sh 'cd apps/fdisk && make run'" from lab4/flat/
Files edited: fdisk.h, dfs_shared.h, files_shared.h, disk.h, dfs.c, dfs.h, files.c, files.h

PART 2
Files edited: dfs.c, dfs.h, ostests.c
Sources referenced: https://www.geeksforgeeks.org/c/sizeof-operator-c/

PART 3
Files edited: dfs.c, dfs_shared.h, ostests.c

PART 4
Notes: To execute, run "mainframer.sh 'cd apps/ostests && make run'" from lab4/flat/
To execute, RUN_OS_TESTS in apps/ostests/ostests/ostests.c must be set to 0.
os/ostests.c has a global #define-d called "TESTS" that runs different tests.
TESTS = 1 -> tests disk functions (DfsAllocateBlock, DfsFreeBlock, DfsReadBlock, DfsWriteBlock, etc.)
TESTS = 2 -> tests inode functions (DfsInodeOpen, DfsInodeWriteBytes, DfsInodeReadBytes, DfsInodeDelete, etc.)
TESTS = 3 -> Same as test 2, but does not delete the inode afterwards.
TESTS = 4 -> To be used in conjunction with Test 3. Tries to read the data which should still be present after Test 3.
TESTS = 5 -> Same as test 2, but with a massive array size. (Note: this doesn't seem to work when running in cached mode, but it does work if you switch DfsReadBlock and DfsWriteBlock to their Uncached versions. I cannot for the life of me figure out why this is, because caching works in Parts 7-9 just fine.)
TESTS = 6 -> Same as test 2, but with non block aligned values.
Files edited: ostests.c, files.c, fdisk.c
Sources referenced: https://stackoverflow.com/questions/348170/how-do-i-undo-git-add-before-commit
https://stackoverflow.com/questions/31281679/how-to-undo-local-changes-to-a-specific-file
https://stackoverflow.com/questions/6794110/git-revert-back-to-certain-commit

PART 5
Files edited: files.c, files.h, files_shared.h, dfs.c
Sources referenced: https://www.geeksforgeeks.org/cpp/opening-modes-in-standard-i-o-in-c-c-with-examples/

PART 6
Notes: To execute, run "mainframer.sh 'cd apps/ostests && make run'" from lab4/flat/
To execute, RUN_OS_TESTS in apps/ostests/ostests/ostests.c must be set to 1.
I'm co-opting the ostests user application to test my file systems. The app ostests.c file now has a constant which detemrines whether OS tests or user app file tests will be ran.
if RUN_OS_TESTS = 0, user application file tests will run.
if RUN_OS_TESTS = 1, OS tests will run.
My user application file tests run two tests, both which open a file, write bytes, close the file, and then try to read the same data back. The two tests are the same, but the second test uses a much larger amount of data.
If an error happens where files are not correctly deleted, I was getting an error that said FileRename couldn't find a file with handle -1. When this happened, I reran fdisk and it fixed it. This bug shouldn't appear anymore now that I've fixed what was causing it, but just FYI if that error message shows up again, run fdisk.
Files edited: apps/ostests/ostests/ostests.c, file.c

PART 7
Notes: To execute, run any past tests. My code when I submit it is the cached version. If you want to test the uncached version, you'll need change the names of DfsReadBlock and DfsWriteBlock with their alternate uncached versions.
I'm changing my code here to make sure that I meet the printing requirements for this project. I've changed all of my printfs to dbprintfs on the debug character 'z'.
By default, only the Cache Miss messages will print. To see all of my debug messages, change line 8 in apps/ostests/Makefile from:
	cd ../../bin; dlxsim -x os.dlx.obj -a -D F -u ostests.dlx.obj; ee469_fixterminal
to:
	cd ../../bin; dlxsim -x os.dlx.obj -a -D zF -u ostests.dlx.obj; ee469_fixterminal
The latency calculation in my "Cache Miss" print message does not work. I haven't gotten clock.c stuff to work.
The eviction policy that I used for this part was tracking which block hadn't been accessed for the longest amount of time.
I'm having some glitches that only seem to happen when the disk is in a broken state. So, if things aren't working, go ahead and run fdisk once and try again, please.
Files edited: dfs.c, files.c

PART 8
Notes: To execute, run "mainframer.sh 'cd apps/pattern && make run'" from either lab4/pattern1/, lab4/pattern2/, or lab4/pattern3/.
I don't know how long this part is intended to take, but I have some slightly concerning runtimes.
Pattern 1: 17 minutes
Pattern 2: 27 minutes
Pattern 3: 5 minutes
Files edited: all files.c, all pattern.c, all dfs.c

PART 9
Notes: To execute, run "mainframer.sh 'cd 