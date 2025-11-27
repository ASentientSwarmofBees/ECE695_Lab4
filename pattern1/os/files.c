#include "ostraps.h"
#include "dlxos.h"
#include "process.h"
#include "dfs.h"
#include "files.h"
#include "synch.h"

file_descriptor files[FILE_MAX_OPEN_FILES];

static lock_t fdLock;

// You have already been told about the most likely places where you should use locks. You may use 
// additional locks if it is really necessary.

// STUDENT: put your file-level functions here

/*
open the given filename with one of three possible modes: "r", "w", "a". If opening the file in "w" mode, and the 
file already exists, the inode should first be deleted and then reopened. Return FILE_FAIL on failure, and the 
handle of a file descriptor on success. Remember to use locks whenever you allocate a new file descriptor. If 
opening the file in "a" mode, position the write pointer at EOF and create the file if it does not exist. You can 
use dstrncmp function (misc.c) to compare strings.
*/
int FileOpen(char *filename, char *mode) {
    int existingFileHandle = -1;
    int i, inode;
    int handle = -1;

    dbprintf('z', "FileOpen\n");

    for (i = 0; i < FILE_MAX_OPEN_FILES; i++) {
        if (dstrncmp(files[i].fileName, filename, FILE_MAX_FILENAME_LENGTH) == 0) {
            //File found and already exists
            if (existingFileHandle != -1) {
                dbprintf('z', "FileOpen (%d): ERROR. Somehow, filename '%s' was found opened by multiple files with handles %d and %d.\n", GetCurrentPid(), filename, existingFileHandle, i);
                return FILE_FAIL;
            }
            existingFileHandle = i;
            if (files[i].isOpen == 1) {
                dbprintf('z', "FileOpen (%d): ERROR. File '%s' is already open by another process with PID %d.\n", GetCurrentPid(), filename, files[i].processID);
                return FILE_FAIL;
            }
        }
    }
    if (existingFileHandle == -1) {
        if ((inode = DfsInodeFilenameExists(filename)) != -1) {
            dbprintf('z', "FileOpen (%d): File '%s' not found in files, but found at inode %d. Creating file.\n", GetCurrentPid(), filename, inode);
            //Find unallocated handle
            handle = -1;
            for (i = 0; i < FILE_MAX_OPEN_FILES; i++) {
                if (files[i].inUse == 0) {
                    handle = i;
                    break;
                }
            }
            if (handle == -1) {
                dbprintf('z', "FileOpen (%d): ERROR. No available file descriptors found. Too many files are in use.\n", GetCurrentPid());
                return FILE_FAIL;
            }
            dbprintf('z', "FileOpen (%d): Opening new file '%s' at handle %d.\n", GetCurrentPid(), filename, handle);
            LockHandleAcquire(fdLock);
            files[handle].inUse = 1;
            LockHandleRelease(fdLock);
            files[handle].isOpen = 1;
            dstrncpy(files[handle].fileName, filename, FILE_MAX_FILENAME_LENGTH);
            files[handle].inode = inode;
            files[handle].eof = 0;
            files[handle].mode = mode[0];
            files[handle].currentPosition = 0;
            files[handle].processID = GetCurrentPid();
            dbprintf('z', "FileOpen (%d): Done.\n", GetCurrentPid());
            return handle;
        }
    }
    switch(mode[0]) {
        case 'r':
            //Read
            if (existingFileHandle == -1) {
                dbprintf('z', "FileOpen (%d): ERROR. Tried to open file '%s' for read, but it does not exist.\n", GetCurrentPid(), filename);
                return FILE_FAIL;
            }
            files[existingFileHandle].isOpen = 1;

            files[existingFileHandle].mode = mode[0];
            files[existingFileHandle].processID = GetCurrentPid();
            dbprintf('z', "FileOpen (%d): Opening file '%s' at handle %d for read.\n", GetCurrentPid(), files[existingFileHandle].fileName, existingFileHandle);
            return existingFileHandle;
        case 'w':
            //Write (Overwrite)
            if (existingFileHandle != -1 || DfsInodeFilenameExists(filename) != -1) {
                //File exists. Delete and reallocate inode.
                dbprintf('z', "FileOpen (%d): Opening file '%s' at handle %d for write.\n", GetCurrentPid(), files[existingFileHandle].fileName, existingFileHandle);
                DfsInodeDelete(files[existingFileHandle].inode);
                files[existingFileHandle].inode = DfsInodeOpen(files[existingFileHandle].fileName);
                dbprintf('z', "FileOpen (%d): Done.\n", GetCurrentPid());
                return existingFileHandle;
            }
            //If the file doesn't exist, this has exactly the same behavior as 'a', so fall-through.
        case 'a':
            //Append
            if (existingFileHandle == -1) {
                //The file doesn't exist, so create it
                dbprintf('z', "FileOpen (%d): Creating new file '%s'.\n", GetCurrentPid(), filename);
                //Find unallocated handle
                for (i = 0; i < FILE_MAX_OPEN_FILES; i++) {
                    if (files[i].inUse == 0) {
                        handle = i;
                        break;
                    }
                }
                if (handle == -1) {
                    dbprintf('z', "FileOpen (%d): ERROR. No available file descriptors found. Too many files are in use.\n", GetCurrentPid());
                    return FILE_FAIL;
                }
                dbprintf('z', "FileOpen (%d): Opening new file '%s' at handle %d.\n", GetCurrentPid(), filename, handle);
                LockHandleAcquire(fdLock);
                files[handle].inUse = 1;
                LockHandleRelease(fdLock);
                files[handle].isOpen = 1;
                dstrncpy(files[handle].fileName, filename, FILE_MAX_FILENAME_LENGTH);
                files[handle].inode = DfsInodeOpen(files[handle].fileName);
                files[handle].eof = 0;
                files[handle].mode = mode[0];
                files[handle].currentPosition = 0;
                files[handle].processID = GetCurrentPid();
                dbprintf('z', "FileOpen (%d): Done.\n", GetCurrentPid());
                return handle;
            }
            files[existingFileHandle].currentPosition = DfsInodeFilesize(files[existingFileHandle].inode);
            files[existingFileHandle].mode = mode[0];
            files[existingFileHandle].processID = GetCurrentPid();
            dbprintf('z', "FileOpen (%d): File '%s' opened in append mode.\n", GetCurrentPid(), files[existingFileHandle].fileName);
            return existingFileHandle;
        default:
            dbprintf('z', "FileOpen: ERROR. Unsupported mode char (should be r, w, or a. Was '%c'.)\n", mode[0]);
            return FILE_FAIL;
    }
}

/*
close the given file descriptor handle. Return FILE_FAIL on failure, and FILE_SUCCESS on success.
*/
int FileClose(int handle) {

    dbprintf('z', "FileClose\n");

    if (files[handle].isOpen == 1) {
        dbprintf('z', "FileClose (%d): Closing file.\n", GetCurrentPid());
        files[handle].isOpen = 0;
        files[handle].processID = -1;
        files[handle].mode = 'n';
        return FILE_SUCCESS;
    }
    else {
        dbprintf('z', "FileClose (%d): ERROR. File %d is not currently open.\n", GetCurrentPid(), handle);
        return FILE_FAIL;
    }
}

/*
read num_bytes from the open file descriptor identified by handle. Return FILE_FAIL on failure or if the end-of-file 
flag is already set, and the number of bytes read on success. If end of file is reached, the end-of-file flag in the 
file descriptor should be set.
*/
int FileRead(int handle, void *mem, int num_bytes) {
    int bytesRead;
    dbprintf('z', "FileRead\n");
    // the maximum number of bytes that can be read or written at any time by the file functions is 4096 bytes. 
    // All of these functions should only allow the process that opened a given file to use the open file descriptor.
    // Error if file not opened in r mode.
    if (files[handle].inUse != 1) {
        dbprintf('z', "FileRead (%d): ERROR. Tried to read from file '%s', which is not in use.\n", GetCurrentPid(), files[handle].fileName);
        return FILE_FAIL;
    }
    if (files[handle].isOpen != 1) {
        dbprintf('z', "FileRead (%d): ERROR. Tried to read from file '%s', which is not open.\n", GetCurrentPid(), files[handle].fileName);
        return FILE_FAIL;
    }
    if (files[handle].processID != GetCurrentPid()) {
        dbprintf('z', "FileRead (%d): ERROR. Tried to read from file '%s', which is in use by process %d.\n", GetCurrentPid(), files[handle].fileName, files[handle].processID);
        return FILE_FAIL;
    }
    if (num_bytes > FILE_MAX_READWRITE_BYTES) {
        dbprintf('z', "FileRead (%d): ERROR. Tried to read %d bytes from file '%s', which is greater than the max allowed read/write of %d bytes.\n", GetCurrentPid(), num_bytes, files[handle].fileName, FILE_MAX_READWRITE_BYTES);
        return FILE_FAIL;
    }
    if (files[handle].eof == 1) {
        dbprintf('z', "FileRead (%d): ERROR. EOF flag is set in file '%s'.\n", GetCurrentPid(), files[handle].fileName);
        return FILE_FAIL;
    }
    if (files[handle].mode != 'r') {
        dbprintf('z', "FileRead (%d): ERROR. File '%s' is open in '%c' mode, not 'r' mode.\n", GetCurrentPid(), files[handle].fileName, files[handle].mode);
        return FILE_FAIL;
    }

    if (files[handle].currentPosition + num_bytes >= DfsInodeFilesize(files[handle].inode)) {
        dbprintf('z', "FileRead (%d): Reading %d bytes from file '%s' will trigger EOF. (curr position: %d, eof: %d) num_bytes has been throttled to %d bytes.\n", GetCurrentPid(), num_bytes, files[handle].fileName, files[handle].currentPosition, DfsInodeFilesize(files[handle].inode), DfsInodeFilesize(files[handle].inode) - files[handle].currentPosition);
        num_bytes = DfsInodeFilesize(files[handle].inode) - files[handle].currentPosition;
        files[handle].eof = 1;
    }

    if ((bytesRead = DfsInodeReadBytes(files[handle].inode, mem, files[handle].currentPosition, num_bytes)) == num_bytes) {
        dbprintf('z', "FileRead (%d): Successfully read %d bytes from file '%s'.\n", GetCurrentPid(), bytesRead, files[handle].fileName);
    }
    else {
        dbprintf('z', "FileRead (%d): Attempted to read %d bytes from file '%s', but read %d bytes instead.\n", GetCurrentPid(), num_bytes, files[handle].fileName, bytesRead);
    }
    files[handle].currentPosition += bytesRead;
    return bytesRead;
}

/*
write num_bytes to the open file descriptor identified by handle. If the file is opened with mode="r", then return 
failure. Return FILE_FAIL on failure, and the number of bytes written on success.
*/
int FileWrite(int handle, void *mem, int num_bytes) {
    // the maximum number of bytes that can be read or written at any time by the file functions is 4096 bytes. 
    // All of these functions should only allow the process that opened a given file to use the open file descriptor.
    // Error if file not opened in w or a mode
    int bytesWritten;

    dbprintf('z', "FileWrite\n");

    if (files[handle].inUse != 1) {
        dbprintf('z', "FileWrite (%d): ERROR. Tried to write to file '%s', which is not in use.\n", GetCurrentPid(), files[handle].fileName);
        return FILE_FAIL;
    }
    if (files[handle].isOpen != 1) {
        dbprintf('z', "FileWrite (%d): ERROR. Tried to write to file '%s', which is not open.\n", GetCurrentPid(), files[handle].fileName);
        return FILE_FAIL;
    }
    if (files[handle].processID != GetCurrentPid()) {
        dbprintf('z', "FileWrite (%d): ERROR. Tried to write to file '%s', which is in use by process %d.\n", GetCurrentPid(), files[handle].fileName, files[handle].processID);
        return FILE_FAIL;
    }
    if (num_bytes > FILE_MAX_READWRITE_BYTES) {
        dbprintf('z', "FileWrite (%d): ERROR. Tried to write %d bytes to file '%s', which is greater than the max allowed read/write of %d bytes.\n", GetCurrentPid(), num_bytes, files[handle].fileName, FILE_MAX_READWRITE_BYTES);
        return FILE_FAIL;
    }
    //Here's where FileRead checked EOF, but write doesn't care about EOF
    if (files[handle].mode != 'w' && files[handle].mode != 'a') {
        dbprintf('z', "FileWrite (%d): ERROR. File '%s' is open in '%c' mode, not 'w' or 'a' mode.\n", GetCurrentPid(), files[handle].fileName, files[handle].mode);
        return FILE_FAIL;
    }

    //Here's where FileRead throttled num_bytes, but write doesn't care about EOF

    if ((bytesWritten = DfsInodeWriteBytes(files[handle].inode, mem, files[handle].currentPosition, num_bytes)) == num_bytes) {
        dbprintf('z', "FileWrite (%d): Successfully wrote %d bytes to file '%s'.\n", GetCurrentPid(), bytesWritten, files[handle].fileName);
    }
    else {
        dbprintf('z', "FileWrite (%d): Attempted to write %d bytes to file '%s', but wrote %d bytes instead.\n", GetCurrentPid(), num_bytes, files[handle].fileName, bytesWritten);
    }
    files[handle].currentPosition += bytesWritten;
    return bytesWritten;
}

/*
seek num_bytes within the file descriptor identified by handle, from the location specified by from_where. There are 
three possible values for from_where: FILE_SEEK_CUR (seek relative to the current position), FILE_SEEK_SET (seek 
relative to the beginning of the file), and FILE_SEEK_END (seek relative to the end of the file). Any seek operation 
will clear the eof flag.
*/
int FileSeek(int handle, int num_bytes, int from_where) {
    // Error if file not open

    dbprintf('z', "FileSeek\n");

    if (files[handle].isOpen != 1) {
        dbprintf('z', "FileSeek (%d): ERROR. File with handle %d is not open.\n", GetCurrentPid(), handle);
        return FILE_FAIL;
    }
    if (num_bytes < 0) {
        dbprintf('z', "FileSeek (%d): ERROR. Can only seek positive number of bytes.\n", GetCurrentPid());
        return FILE_FAIL;
    }
    switch(from_where) {
        case FILE_SEEK_CUR:
            dbprintf('z', "FileSeek (%d): Seeking from %d -> %d.\n", GetCurrentPid(), files[handle].currentPosition, files[handle].currentPosition + num_bytes);
            files[handle].currentPosition += num_bytes;
            break;
        case FILE_SEEK_SET:
            dbprintf('z', "FileSeek (%d): Seeking from %d -> %d.\n", GetCurrentPid(), files[handle].currentPosition, num_bytes);
            files[handle].currentPosition = num_bytes;
            break;
        case FILE_SEEK_END:
            dbprintf('z', "FileSeek (%d): Seeking from %d -> %d.\n", GetCurrentPid(), files[handle].currentPosition, DfsInodeFilesize(files[handle].inode) + num_bytes);
            files[handle].currentPosition = DfsInodeFilesize(files[handle].inode) + num_bytes;
            break;
        default:
            dbprintf('z', "FileSeek (%d): ERROR. Invalid 'from_where' value.\n", GetCurrentPid());
            return FILE_FAIL;
    }
    //Clear eof flag
    files[handle].eof = 0;
    return FILE_SUCCESS;
}

/*
delete the file specified by filename. Return FILE_FAIL on failure, and FILE_SUCCESS on success.
*/
int FileDelete(char *filename) {
    int i;
    int handle = -1;

    dbprintf('z', "FileDelete\n");

    for (i = 0; i < FILE_MAX_OPEN_FILES; i++) {
        if (dstrncmp(files[i].fileName, filename, FILE_MAX_FILENAME_LENGTH) == 0) {
            if (handle != -1) {
                dbprintf('z', "FileDelete (%d): ERROR. More than one file found with name '%s'. File system is probably in a broken state.\n", GetCurrentPid(), filename);
                return FILE_FAIL;
            }
            handle = i;
        }
    }
    if (handle == -1) {
        dbprintf('z', "FileDelete (%d): ERROR. File with name '%s' not found.\n", GetCurrentPid(), filename);
        return FILE_FAIL;
    }
    //File has been found.
    //Should this check if process id matches?
    if (files[handle].isOpen == 1) {
        dbprintf('z', "FileDelete (%d), ERROR. File '%s' is currently open. Close it first.\n", GetCurrentPid(), filename);
        return FILE_FAIL;
    }
    dbprintf('z', "FileDelete (%d): Deleting file '%s' at handle %d.\n", GetCurrentPid(), files[handle].fileName, handle);
    LockHandleAcquire(fdLock);
    files[handle].inUse = 0;
    LockHandleRelease(fdLock);
    files[handle].isOpen = 0;
    for (i = 0; i < FILE_MAX_FILENAME_LENGTH; i++) {
        files[handle].fileName[i] = 0;
    }
    DfsInodeDelete(files[handle].inode);
    files[handle].inode = 0;
    files[handle].eof = 0;
    files[handle].mode = 'n';
    files[handle].currentPosition = 0;
    files[handle].processID = 0;
    dbprintf('z', "FileDelete (%d): File successfully deleted.\n", GetCurrentPid());
    return FILE_SUCCESS;
}

/*
rename a file; return fail if newname already exists.
*/
int FileRename(char *oldname, char *newname) {
    int i;
    int handle = -1;

    dbprintf('z', "FileRename\n");
    
    //Check if file with newname already exists.
    for (i = 0; i < FILE_MAX_OPEN_FILES; i++) {
        if (dstrncmp(files[i].fileName, newname, FILE_MAX_FILENAME_LENGTH) == 0) {
            dbprintf('z', "FileRename: ERROR. File already exists with name '%s' at handle %d.\n", newname, i);
            return FILE_FAIL;
        }
    }
    //Find handle of file with oldname.
    for (i = 0; i < FILE_MAX_OPEN_FILES; i++) {
        if (dstrncmp(files[i].fileName, oldname, FILE_MAX_FILENAME_LENGTH) == 0) {
            handle = i;
            break;
        }
    }
    if (handle == -1) {
        dbprintf('z', "FileRename: ERROR. File matching oldname not found.\n");
        return FILE_FAIL;
    }
    //perform rename
    dstrncpy(files[handle].fileName, newname, FILE_MAX_FILENAME_LENGTH);
    dbprintf('z', "FileRename: Renamed '%s' to '%s'.\n", oldname, newname);
    return FILE_SUCCESS;
}

void FileInitLock() {
    fdLock = LockCreate();
}
