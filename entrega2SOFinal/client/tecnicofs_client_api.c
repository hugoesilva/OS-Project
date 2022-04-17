#include "tecnicofs_client_api.h"
#include <sys/types.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>

int sessionID = -1;
char client_pipe_name[CLIENT_PIPE_LENGTH];
char server_pipe_name[SERVER_PIPE_LENGTH];
int requestNum = 0;

int tfs_mount(char const *client_pipe_path, char const *server_pipe_path) {
    strcpy(client_pipe_name, client_pipe_path);
    for (int i = (int) strlen(client_pipe_name); i < CLIENT_PIPE_LENGTH; i++) {
        client_pipe_name[i] = '\0';
    }
    strcpy(server_pipe_name, server_pipe_path);

    unlink(client_pipe_name);

    if (mkfifo(client_pipe_name, 0777) == -1) {
        return -1;
    }

    char message[CLIENT_PIPE_LENGTH + CODE_SIZE];
    message[0] = TFS_OP_CODE_MOUNT;
    memcpy(message + CODE_SIZE, client_pipe_name, CLIENT_PIPE_LENGTH);

    char buffer[SESSION_ID_LENGTH];
    if (writeToServer(message, sizeof(char) + CLIENT_PIPE_LENGTH) == -1) {
        return -1;
    }
    if (readFromServer(buffer, SESSION_ID_LENGTH) == -1) {
        return -1;
    }
    
    sessionID = stringToInt(buffer);
    
    if (sessionID == -1) {
        printf("max sessions reached; cannot add client\n");
        return -1;
    }

    printf("sessionID = %d\n", sessionID);

    return 0;
}

int tfs_unmount() {
    size_t messageSize = CODE_SIZE + SESSION_ID_LENGTH;
    char message[messageSize];
    message[0] = TFS_OP_CODE_UNMOUNT;

    char sessionIDstr[SESSION_ID_LENGTH];

    intToString(sessionIDstr, &sessionID, sizeof(int));

    if (sessionID == -1) {
        return -1;
    }

    memcpy(message + CODE_SIZE, sessionIDstr, SESSION_ID_LENGTH);
    if (writeToServer(message, messageSize) == -1) {
        return -1;
    }

    char success[sizeof(int)];

    if (readFromServer(success, sizeof(int)) == -1) {
        return -1;
    }

    int res = stringToInt(success);
    if (res != 0) {
        return -1;
    }

    unlink(client_pipe_name);
    return 0;
}

int tfs_open(char const *name, int flags) {
    size_t messageSize = FILE_NAME_SIZE + CODE_SIZE + FLAG_SIZE + SESSION_ID_LENGTH;
    char message[messageSize];
    message[0] = TFS_OP_CODE_OPEN;
    char sessionIDstr[SESSION_ID_LENGTH];
    char flagStr[FLAG_SIZE];
    char nameStr[FILE_NAME_SIZE];

    strcpy(nameStr, name);

    intToString(sessionIDstr, &sessionID, SESSION_ID_LENGTH);
    memcpy(message + CODE_SIZE, sessionIDstr, SESSION_ID_LENGTH);

    for (int i = (int) strlen(name); i < FILE_NAME_SIZE; i++) {
        nameStr[i] = '\0';
    }

    memcpy(message + CODE_SIZE + SESSION_ID_LENGTH, nameStr, FILE_NAME_SIZE);

    intToString(flagStr, &flags, FLAG_SIZE);
    memcpy(message + CODE_SIZE + SESSION_ID_LENGTH + FILE_NAME_SIZE, flagStr, FLAG_SIZE);

    if (writeToServer(message, messageSize) == -1) {
        return -1;
    }

    char res[sizeof(int)];

    if (readFromServer(res, sizeof(int)) == -1) {
        return -1;
    }

    int fh = stringToInt(res);
    if (fh == -1) {
        return -1;
    }

    return fh;
}

int tfs_close(int fhandle) {
    size_t messageSize = CODE_SIZE + SESSION_ID_LENGTH + FHANDLE_SIZE;
    char message[messageSize];
    char sessionIDstr[SESSION_ID_LENGTH];
    char fhandleStr[FHANDLE_SIZE];

    message[0] = TFS_OP_CODE_CLOSE;

    intToString(sessionIDstr, &sessionID, SESSION_ID_LENGTH);
    memcpy(message + CODE_SIZE, sessionIDstr, SESSION_ID_LENGTH);

    intToString(fhandleStr, &fhandle, FHANDLE_SIZE);
    memcpy(message + CODE_SIZE + SESSION_ID_LENGTH, fhandleStr, FHANDLE_SIZE);

    if (writeToServer(message, messageSize) == -1) {
        return -1;
    }

    char resStr[sizeof(int)];
    if (readFromServer(resStr, sizeof(int)) == -1) {
        return -1;
    }

    int res = stringToInt(resStr);

    return res;
}

ssize_t tfs_write(int fhandle, void const *buffer, size_t len) {
    size_t messageSize = CODE_SIZE + SESSION_ID_LENGTH + FHANDLE_SIZE + sizeof(size_t) + len * sizeof(char);
    char message[messageSize];
    char sessionIDstr[SESSION_ID_LENGTH];
    char fhandleStr[FHANDLE_SIZE];
    char lenStr[sizeof(size_t)];

    message[0] = TFS_OP_CODE_WRITE;

    intToString(sessionIDstr, &sessionID, SESSION_ID_LENGTH);
    memcpy(message + CODE_SIZE, sessionIDstr, SESSION_ID_LENGTH);

    intToString(fhandleStr, &fhandle, FHANDLE_SIZE);
    memcpy(message + CODE_SIZE + SESSION_ID_LENGTH, fhandleStr, FHANDLE_SIZE);

    intToString(lenStr, &len, sizeof(size_t));
    memcpy(message + CODE_SIZE + SESSION_ID_LENGTH + FHANDLE_SIZE, lenStr, sizeof(size_t));

    memcpy(message + CODE_SIZE + SESSION_ID_LENGTH + FHANDLE_SIZE + sizeof(size_t), buffer, len * sizeof(char));

    if (writeToServer(message, messageSize) == -1) {
        return -1;
    }

    char resStr[sizeof(int)];
    if (readFromServer(resStr, sizeof(int)) == -1) {
        return -1;
    }

    int res = stringToInt(resStr);

    return res;
}

ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    size_t messageSize = CODE_SIZE + SESSION_ID_LENGTH + FHANDLE_SIZE + sizeof(size_t);
    char message[messageSize];
    char sessionIDstr[SESSION_ID_LENGTH];
    char fhandleStr[FHANDLE_SIZE];
    char lenStr[sizeof(size_t)];

    message[0] = TFS_OP_CODE_READ;

    intToString(sessionIDstr, &sessionID, SESSION_ID_LENGTH);
    memcpy(message + CODE_SIZE, sessionIDstr, SESSION_ID_LENGTH);

    intToString(fhandleStr, &fhandle, FHANDLE_SIZE);
    memcpy(message + CODE_SIZE + SESSION_ID_LENGTH, fhandleStr, FHANDLE_SIZE);

    intToString(lenStr, &len, sizeof(size_t));
    memcpy(message + CODE_SIZE + SESSION_ID_LENGTH + FHANDLE_SIZE, lenStr, sizeof(size_t));

    if (writeToServer(message, messageSize) == -1) {
        return -1;
    }

    char res[sizeof(int) + len];

    char bytesReadStr[sizeof(int)];

    if (readFromServer(res, sizeof(int) + len) == -1) {
        return -1;
    }

    for (int i = 0; i < sizeof(int); i++) {
        bytesReadStr[i] = res[i];
    }

    int bytesRead = stringToInt(bytesReadStr);

    strcpy(buffer, res + sizeof(int));

    return bytesRead;
}

int tfs_shutdown_after_all_closed() {
    size_t messageSize = CODE_SIZE + SESSION_ID_LENGTH;
    char message[messageSize];
    char sessionIDstr[SESSION_ID_LENGTH];

    message[0] = TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED;

    intToString(sessionIDstr, &sessionID, SESSION_ID_LENGTH);
    memcpy(message + CODE_SIZE, sessionIDstr, SESSION_ID_LENGTH);

    if (writeToServer(message, messageSize) == -1) {
        return -1;
    }

    char res[sizeof(int)];

    if (readFromServer(res, sizeof(int)) == -1) {
        return -1;
    }

    return stringToInt(res);
}

int writeToServer(char* buffer, size_t bufferSize) {
    int fds;
    requestNum++;
    printf("client request num -> %d\n", requestNum);
    if ((fds = openAux(server_pipe_name, O_WRONLY)) == -1) {
        return -1;
    }

    ssize_t bytesWritten = write(fds, buffer, (size_t) bufferSize);
    while (bytesWritten < bufferSize && bytesWritten != -1) {
        size_t aux = (size_t) bytesWritten;
        bytesWritten += write(fds, buffer + bytesWritten, bufferSize - aux);
    }
    if (bytesWritten <= 0) {
        close(fds);
        return -1;
    }

    if (close(fds) == -1) {
        return -1;
    }

    return 0;
}

int readFromServer(char* buffer, size_t bufferSize) {
    int fdc;

    if ((fdc = openAux(client_pipe_name, O_RDONLY)) == -1) {
        return -1;
    }

    ssize_t bytesRead = read(fdc, buffer, bufferSize);
    while (bytesRead < bufferSize && bytesRead != -1) {
        size_t aux = (size_t) bytesRead;
        bytesRead += read(fdc, buffer + bytesRead, bufferSize - aux);
    }
    if (bytesRead <= 0) {
        close(fdc);
        return -1;
    }

    if (close(fdc) == -1) {
        return -1;
    }

    return 0;
}
