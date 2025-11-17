#ifndef __FILES_SHARED__
#define __FILES_SHARED__

#define FILE_SEEK_SET 1
#define FILE_SEEK_END 2
#define FILE_SEEK_CUR 3

#define FILE_MAX_FILENAME_LENGTH 72

#define FILE_MAX_READWRITE_BYTES 4096 //TODO: this was commented out. does it need to be changed?

typedef struct file_descriptor {
  // STUDENT: put file descriptor info here
  uint32 inUse; //an in use indicator to tell if the descriptor is free or in use. Boolean.
  dfs_inode inode; //inode, which this file-descriptor corresponds to. TODO: should this be a pointer?
  char fileName[FILE_MAX_FILENAME_LENGTH]; //the filename, which is just a string.
  uint32 eof; //eof: Indicator if the End-of-file is reached; useful for read operations. TODO: should be a boolean?
  char mode; //mode: set while opening the file. Possible values: "r", "w" 
  uint32 currentPosition; //current position: Current position in the file. block num or something?
  uint32 processID; //Process id: Process that opened the file. No other process can do any operations
} file_descriptor;

#define FILE_FAIL -1
#define FILE_EOF -1
#define FILE_SUCCESS 1

#endif
