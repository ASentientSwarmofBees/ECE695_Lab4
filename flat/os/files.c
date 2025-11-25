#include "ostraps.h"
#include "dlxos.h"
#include "process.h"
#include "dfs.h"
#include "files.h"
#include "synch.h"

// You have already been told about the most likely places where you should use locks. You may use 
// additional locks if it is really necessary.

// STUDENT: put your file-level functions here

int FileOpen(char *filename, char *mode) {
    return 0;
    //todo implement
}

int FileClose(int handle) {
    return 0;
    //todo implement
}

int FileRead(int handle, void *mem, int num_bytes) {
    return 0;
    //todo implement
}

int FileWrite(int handle, void *mem, int num_bytes) {
    return 0;
    //todo implement
}

int FileSeek(int handle, int num_bytes, int from_where) {
    return 0;
    //todo implement
}

int FileDelete(char *filename) {
    return 0;
    //todo implement
}

int FileRename(char *oldname, char *newname) {
    return 0;
    //todo implement
}
