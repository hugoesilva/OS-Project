#ifndef COMMON_H
#define COMMON_H

#include <sys/types.h>

/* tfs_open flags */
enum {
    TFS_O_CREAT = 0b001,
    TFS_O_TRUNC = 0b010,
    TFS_O_APPEND = 0b100,
};

/* operation codes (for client-server requests) */
enum {
    TFS_OP_CODE_MOUNT = 1,
    TFS_OP_CODE_UNMOUNT = 2,
    TFS_OP_CODE_OPEN = 3,
    TFS_OP_CODE_CLOSE = 4,
    TFS_OP_CODE_WRITE = 5,
    TFS_OP_CODE_READ = 6,
    TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED = 7
};

#define CLIENT_PIPE_LENGTH 40
#define SESSION_ID_LENGTH sizeof(int)
#define MAX_SESSIONS 20
#define CODE_SIZE sizeof(char)
#define FILE_NAME_SIZE 40
#define FLAG_SIZE sizeof(int)
#define FHANDLE_SIZE sizeof(int)
#define READ_CONTROL -1000
#define SERVER_PIPE_LENGTH 40


int openAux(char* path, int flags);

void intToString(char* buffer, void* num, size_t size);

int stringToInt(char* buffer);



#endif /* COMMON_H */