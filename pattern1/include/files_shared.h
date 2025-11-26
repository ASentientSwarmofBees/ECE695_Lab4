#ifndef __FILES_SHARED__
#define __FILES_SHARED__

struct dfs_inode; //forward declared so we don't get circular include errors

#define FILE_SEEK_SET 1
#define FILE_SEEK_END 2
#define FILE_SEEK_CUR 3

#define FILE_MAX_FILENAME_LENGTH 72

#define FILE_MAX_READWRITE_BYTES 4096

typedef struct file_descriptor {
  // STUDENT: put file descriptor info here
  uint32 inUse; //an in use indicator to tell if the descriptor is in use, as in it has been opened before.
  uint32 isOpen; //indicator to tell if the file is currently opened or not.
  char fileName[FILE_MAX_FILENAME_LENGTH]; //the filename, which is just a string.
  uint32 inode; //handle of the inode which this file-descriptor corresponds to.
  uint32 eof; //eof: Indicator if the End-of-file is reached; useful for read operations. BOOLEAN.
  char mode; //mode: set while opening the file. Possible values: "r", "w" 
  uint32 currentPosition; //current position: Current position in the file. block num or something?
  int processID; //Process id: Process that opened the file. No other process can do any operations
} file_descriptor;

#define FILE_FAIL -1
#define FILE_EOF -1
#define FILE_SUCCESS 1

#endif
