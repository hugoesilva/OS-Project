#include "operations.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <syscall.h>
#include <signal.h>

client clients[MAX_SESSIONS];
char freeSessions[MAX_SESSIONS];
pthread_t tid[MAX_SESSIONS];

int terminateThreadProccess = 0;
int requests = 0;
pthread_cond_t cond;
pthread_mutex_t sessionsLock;


int main(int argc, char **argv) {

    if (argc < 2) {
        printf("Please specify the pathname of the server's pipe.\n");
        return 1;
    }

    char *pipename = argv[1];
    printf("Starting TecnicoFS server with pipe called %s\n", pipename);

    unlink(pipename);

    if (tfs_init() == -1) {
        return -1;
    }

    if (mkfifo(pipename, 0777) < 0) {
        exit(1);
    }

    if (inicializeLocksAndConditions() == -1) {
        return -1;
    }

    if (inicializeFreeSessions() == -1) {
        return -1;
    }

    if (inicializeThreads() == -1) {
        return -1;
    }

    signal(SIGPIPE, SIG_IGN);

    int fds;
    if ((fds = openAux(pipename, O_RDONLY)) < 0) {
        return -1;
    }

    char code;
    ssize_t readAmount;


    while (1) {
        readAmount = READ_CONTROL;
        code = -1;
        readAmount = readFromClient(fds, &code, CODE_SIZE);
        if (terminateThreadProccess) {
            break;
        }
        if (readAmount == 0) {
            if (close(fds) == -1) {
                return -1;
            }
            if ((fds = openAux(pipename, O_RDONLY)) < 0) {
                return -1;
            }
            continue;
        }
        else if (readAmount == -1) {
            return -1;
        }
        else if (readAmount == READ_CONTROL) {
            break;
        }

        switch (code) {

            case TFS_OP_CODE_MOUNT:

                if (execMount(fds) == -1) {
                    close(fds);
                    return -1;
                }

                break;

            case TFS_OP_CODE_UNMOUNT:

                if (execUnmount(fds, code) == -1) {
                    close(fds);
                    return -1;
                }

                break;

            case TFS_OP_CODE_OPEN:
                if (execOpen(fds, code) == -1) {
                    close(fds);
                    return -1;
                }
                break;

            case TFS_OP_CODE_CLOSE:
                if (execClose(fds, code) == -1) {
                    close(fds);
                    return -1; 
                }
                break;
            
            case TFS_OP_CODE_WRITE:
                if (execWrite(fds, code) == -1) {
                    close(fds);
                    return -1;
                }
                break; 

            case TFS_OP_CODE_READ:
                if (execRead(fds, code) == -1) {
                    close(fds);
                    return -1;
                }
                break;

            case TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED:
                if (execShutdownAfterAllClosed(fds, code) == -1) {
                    close(fds);
                    return -1;
                }
                break;

            default:
                printf("********Read wrong code********  %c\n", code);
                close(fds);
                return -1;
        }
    }

    if (terminateThreadProccess) {
        pthread_cond_broadcast(&cond);
    }

    void* arg;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        pthread_join(tid[i], &arg);
        if (((*(int*)arg)) == -1) {
            printf("error ocurred\n");
        }
    }

    if (destroyClientMutexes() != 0) {
        return -1;
    }

    if (pthread_cond_destroy(&cond) != 0) {
        return -1;
    }
    if (pthread_mutex_destroy(&sessionsLock) != 0) {
        return -1;
    }
    unlink(pipename);

    return 0;
}

int inicializeFreeSessions() {
    if (pthread_mutex_lock(&sessionsLock) != 0) {
        return -1;
    }
    for (int i = 0; i < MAX_SESSIONS; i++) {
        clients[i].sessionID = -1;
        freeSessions[i] = FREE;
        if (pthread_mutex_init(&clients[i].requestLock, NULL) != 0) {
            return -1;
        }
    }
    if (pthread_mutex_unlock(&sessionsLock) != 0) {
        return -1;
    }
    return 0;
}

int tasks[MAX_SESSIONS];

int inicializeThreads() {
    
    for (int i = 0; i < MAX_SESSIONS; i++) {
        tasks[i] = i;
        if (pthread_create(&tid[i], NULL, processRequests, (void*) &tasks[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

int inicializeLocksAndConditions() {
    if (pthread_mutex_init(&sessionsLock, NULL) != 0) {
        return -1;
    }
    if (pthread_cond_init(&cond, NULL) != 0) {
        return -1;
    }
    return 0;
}

int destroyClientMutexes() {
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (pthread_mutex_destroy(&clients[i].requestLock) != 0) {
            return -1;
        }
    }
    return 0;
}

int execMount(int fds) {

    printf("------------entrou mount------------\n");

    char client_pipe[CLIENT_PIPE_LENGTH];
    int i;
    char sessionIDstr[SESSION_ID_LENGTH];

    if (readFromClient(fds, client_pipe, CLIENT_PIPE_LENGTH) == -1) {
        return -1;
    }

    if (pthread_mutex_lock(&sessionsLock) != 0) {
        return -1;
    }

    for (i = 0; i < MAX_SESSIONS; i++) {
        if (freeSessions[i] == FREE) {
            break;
        }
    }

    if (i == MAX_SESSIONS) {
        printf("max sessions reached\n");
        int error = -1;
        int fdc;
        if ((fdc = openAux(client_pipe, O_WRONLY)) == -1) {
            pthread_mutex_unlock(&sessionsLock);
            return -1;
        }
        ssize_t bytesWritten = write(fdc, &error, sizeof(int));
        if (bytesWritten <= 0) {
            close(fdc);
            pthread_mutex_unlock(&sessionsLock);
            return -1;
        }

        if (close(fdc) == -1) {
            pthread_mutex_unlock(&sessionsLock);
            return -1;
        }
        return 0;
    }

    if (pthread_mutex_lock(&clients[i].requestLock) != 0) {
        return -1;
    }

    clients[i].sessionID = i;
    strcpy(clients[i].client_pipe_path, client_pipe);

    if (pthread_mutex_unlock(&clients[i].requestLock) != 0) {
        return -1;
    }

    freeSessions[i] = TAKEN;

    if (pthread_mutex_unlock(&sessionsLock) != 0) {
        return -1;
    }



    intToString(sessionIDstr, &i, sizeof(int));
    if (writeToClient(sessionIDstr, sizeof(int), i) == -1) {
        return -1;
    }

    return 0;
}

int execUnmount(int fds, char code) {

    printf("------------entrou unmount------------\n");

    char buffer[SESSION_ID_LENGTH];

    if (readFromClient(fds, buffer, SESSION_ID_LENGTH) == -1) {
        return -1;
    }

    int sessionID = stringToInt(buffer);

    if (pthread_mutex_lock(&clients[sessionID].requestLock) != 0) {
        return -1;
    }

    int clientResquestNum = clients[sessionID].messageCounter;
    if (clientResquestNum == MAX_REQUESTS) {
        return -1;
    }
    clients[sessionID].clientRequests[clientResquestNum].opCode = code;
    clients[sessionID].messageCounter++;

    if (pthread_mutex_unlock(&clients[sessionID].requestLock) != 0) {
        return -1;
    }

    if (pthread_cond_broadcast(&cond) != 0) {
        return -1;
    }

    return 0;
}

int execOpen(int fds, char code) {


    printf("------------entrou open------------\n");

    char message[SESSION_ID_LENGTH + MAX_FILE_NAME + FLAG_SIZE];
    char sessionIDstr[SESSION_ID_LENGTH];
    char filePath[MAX_FILE_NAME];
    char flagStr[FLAG_SIZE];

    if (readFromClient(fds, message, SESSION_ID_LENGTH + MAX_FILE_NAME + FLAG_SIZE) == -1) {
        return -1;
    }

    memcpy(sessionIDstr, message, SESSION_ID_LENGTH);

    memcpy(filePath, message + SESSION_ID_LENGTH, MAX_FILE_NAME);

    memcpy(flagStr, message + SESSION_ID_LENGTH + MAX_FILE_NAME, FLAG_SIZE);

    int sessionID = stringToInt(sessionIDstr);
    int flags = stringToInt(flagStr);

    if (pthread_mutex_lock(&clients[sessionID].requestLock) != 0) {
        return -1;
    }

    int clientResquestNum = clients[sessionID].messageCounter;
    if (clientResquestNum == MAX_REQUESTS) {
        return -1;
    }
    clients[sessionID].clientRequests[clientResquestNum].opCode = code;
    clients[sessionID].clientRequests[clientResquestNum].flags = flags;
    strcpy(clients[sessionID].clientRequests[clientResquestNum].fileName, filePath);
    clients[sessionID].messageCounter++;

    if (pthread_mutex_unlock(&clients[sessionID].requestLock) != 0) {
        return -1;
    }

    if (pthread_cond_broadcast(&cond) != 0) {
        return -1;
    }


    // -----

    return 0;
}

int execClose(int fds, char code) {

    printf("------------entrou close------------\n");

    char message[SESSION_ID_LENGTH + FHANDLE_SIZE];
    char sessionIDstr[SESSION_ID_LENGTH];
    char fhStr[FHANDLE_SIZE];

    if (readFromClient(fds, message, SESSION_ID_LENGTH + FHANDLE_SIZE) == -1) {
        return -1;
    }

    memcpy(sessionIDstr, message, SESSION_ID_LENGTH);

    memcpy(fhStr, message + SESSION_ID_LENGTH, FHANDLE_SIZE);

    int sessionID = stringToInt(sessionIDstr);

    int fh = stringToInt(fhStr);

    if (pthread_mutex_lock(&clients[sessionID].requestLock) != 0) {
        return -1;
    }

    int clientResquestNum = clients[sessionID].messageCounter;
    if (clientResquestNum == MAX_REQUESTS) {
        return -1;
    }
    clients[sessionID].clientRequests[clientResquestNum].opCode = code;
    clients[sessionID].clientRequests[clientResquestNum].filehandle = fh;
    clients[sessionID].messageCounter++;

    if (pthread_mutex_unlock(&clients[sessionID].requestLock) != 0) {
        return -1;
    }

    if (pthread_cond_broadcast(&cond) != 0) {
        return -1;
    }

    return 0;
}

int execWrite(int fds, char code) {

    printf("------------entrou write------------\n");

    char message[SESSION_ID_LENGTH + FHANDLE_SIZE + sizeof(size_t)];
    char sessionIDstr[SESSION_ID_LENGTH];
    char fhStr[FHANDLE_SIZE];
    char lenStr[sizeof(size_t)];

    if (readFromClient(fds, message, SESSION_ID_LENGTH + FHANDLE_SIZE + sizeof(size_t)) == -1) {
        return -1;
    }

    memcpy(sessionIDstr, message, SESSION_ID_LENGTH);

    memcpy(fhStr, message + SESSION_ID_LENGTH, FHANDLE_SIZE);

    memcpy(lenStr, message + SESSION_ID_LENGTH + FHANDLE_SIZE, sizeof(size_t));

    int sessionID = stringToInt(sessionIDstr);

    int fh = stringToInt(fhStr);

    size_t len = (size_t) stringToInt(lenStr);

    char* toWrite = (char*) malloc(sizeof(char) * (len+1));

    if (toWrite == NULL) {
        return -1;
    }

    if (readFromClient(fds, toWrite, len) == -1) {
        return -1;
    }

    toWrite[len] = '\0';

    if (pthread_mutex_lock(&clients[sessionID].requestLock) != 0) {
        return -1;
    }

    int clientResquestNum = clients[sessionID].messageCounter;
    if (clientResquestNum == MAX_REQUESTS) {
        free(toWrite);
        return -1;
    }
    clients[sessionID].clientRequests[clientResquestNum].opCode = code;
    clients[sessionID].clientRequests[clientResquestNum].filehandle = fh;
    clients[sessionID].clientRequests[clientResquestNum].writeLen = len;
    clients[sessionID].clientRequests[clientResquestNum].toWrite = toWrite;
    clients[sessionID].messageCounter++;

    if (pthread_mutex_unlock(&clients[sessionID].requestLock) != 0) {
        return -1;
    }

    if (pthread_cond_broadcast(&cond) != 0) {
        return -1;
    }

    return 0;
}


int execRead(int fds, char code) {

    printf("------------entrou read------------\n");

    char message[SESSION_ID_LENGTH + FHANDLE_SIZE + sizeof(size_t)];
    char sessionIDstr[SESSION_ID_LENGTH];
    char fhStr[FHANDLE_SIZE];
    char lenStr[sizeof(size_t)];

    if (readFromClient(fds, message, SESSION_ID_LENGTH + FHANDLE_SIZE + sizeof(size_t)) == -1) {
        return -1;
    }


    memcpy(sessionIDstr, message, SESSION_ID_LENGTH);

    memcpy(fhStr, message + SESSION_ID_LENGTH, FHANDLE_SIZE);

    memcpy(lenStr, message + SESSION_ID_LENGTH + FHANDLE_SIZE, sizeof(size_t));

    int sessionID = stringToInt(sessionIDstr);

    int fh = stringToInt(fhStr);

    size_t len = (size_t) stringToInt(lenStr);

    if (pthread_mutex_lock(&clients[sessionID].requestLock) != 0) {
        return -1;
    }

    int clientResquestNum = clients[sessionID].messageCounter;
    if (clientResquestNum == MAX_REQUESTS) {
        return -1;
    }
    clients[sessionID].clientRequests[clientResquestNum].opCode = code;
    clients[sessionID].clientRequests[clientResquestNum].filehandle = fh;
    clients[sessionID].clientRequests[clientResquestNum].readLen = len;
    clients[sessionID].messageCounter++;

    if (pthread_mutex_unlock(&clients[sessionID].requestLock) != 0) {
        return -1;
    }

    if (pthread_cond_broadcast(&cond) != 0) {
        return -1;
    }

    return 0;
}

int execShutdownAfterAllClosed(int fds, char code) {
    printf("------------entrou shut down------------\n");
    char sessionIDstr[SESSION_ID_LENGTH];

    if (readFromClient(fds, sessionIDstr, SESSION_ID_LENGTH) == -1) {
        return -1;
    }

    int sessionID = stringToInt(sessionIDstr);

    if (pthread_mutex_lock(&clients[sessionID].requestLock) != 0) {
        return -1;
    }

    int clientResquestNum = clients[sessionID].messageCounter;
    if (clientResquestNum == MAX_REQUESTS) {
        return -1;
    }
    clients[sessionID].clientRequests[clientResquestNum].opCode = code;
    clients[sessionID].messageCounter++;

    if (pthread_mutex_unlock(&clients[sessionID].requestLock) != 0) {
        return -1;
    }

    if (pthread_cond_broadcast(&cond) != 0) {
        return -1;
    }


    return 0;
}

int writeToClient(char* buffer, size_t bufferSize, int sessionID) {
    int fdc;

    if (pthread_mutex_lock(&sessionsLock) != 0) {
        return -1;
    }

    if ((fdc = openAux(clients[sessionID].client_pipe_path, O_WRONLY)) == -1) {
        return -1;
    }

    if (pthread_mutex_unlock(&sessionsLock) != 0) {
        return -1;
    }

    ssize_t bytesWritten = write(fdc, buffer, bufferSize);
    while (bytesWritten < bufferSize && bytesWritten != -1) {
        size_t aux = (size_t) bytesWritten;
        bytesWritten += write(fdc, buffer + bytesWritten, bufferSize - aux);
    }
    if (bytesWritten <= 0) {
        close(fdc);
        return -1;
    }

    if (close(fdc) == -1) {
        return -1;
    }

    return 0;
}

ssize_t readFromClient(int fds, char* buffer, size_t bufferSize) {

    ssize_t bytesRead = read(fds, buffer, bufferSize);
    while (bytesRead < bufferSize && bytesRead != -1) {
        if (terminateThreadProccess) {
            return 0;
        }
        size_t aux = (size_t) bytesRead;
        bytesRead += read(fds, buffer + bytesRead, bufferSize - aux);
    }
    if (bytesRead < 0) {
        return -1;
    }

    return bytesRead;
}


void* processRequests(void* arg) {
    int res;

    int threadID = (*(int*) arg);

    res = processRequestsAux(threadID);

    pthread_exit(&res);
}

int processRequestsAux(int threadID) {
    printf("threadID %d\n", threadID);

    if (pthread_mutex_lock(&sessionsLock) != 0) {
        return -1;
    }

    client* c = &clients[threadID];

    if (pthread_mutex_unlock(&sessionsLock) != 0) {
        return -1;
    }

    while (1) {

        if (pthread_mutex_lock(&c->requestLock) != 0) {
            return -1;
        }

        while ((c->messageCounter == 0 || c->sessionID == -1) && !terminateThreadProccess) {

            pthread_cond_wait(&cond, &c->requestLock);

        }

        if (terminateThreadProccess) {
            if (pthread_mutex_unlock(&c->requestLock) != 0) {
                return -1;
            }
            break;
        }

        client cAux = (*c);
        int requestCounter = c->messageCounter;
        request requestsCopy[MAX_REQUESTS];
        for (int i = 0; i < MAX_REQUESTS; i++) {
            requestsCopy[i] = c->clientRequests[i];
        }

        c->messageCounter = 0;

        if (pthread_mutex_unlock(&c->requestLock) != 0) {
            return -1;
        }

        for (int i = 0; i < requestCounter; i++) {
            switch (requestsCopy[i].opCode) {

                case TFS_OP_CODE_UNMOUNT:

                    if (unmountT(cAux) == -1) {
                        printf("unmount error\n");
                    }
                    break;

                case TFS_OP_CODE_OPEN:
                    if (openT(cAux) == -1) {
                        printf("open error\n");
                    }
                    break;

                case TFS_OP_CODE_CLOSE:
                    if (closeT(cAux) == -1) {
                        printf("close error\n"); 
                    }
                    break;
                
                case TFS_OP_CODE_WRITE:
                    if (writeT(cAux) == -1) {
                        printf("write error\n");
                    }
                    break; 

                case TFS_OP_CODE_READ:
                    if (readT(cAux) == -1) {
                        printf("read error\n");
                    }
                    break;

                case TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED:
                    if (shutdownAfterAllClosedT(cAux) == -1) {
                        printf("shutdown error\n");
                    }
                    terminateThreadProccess = 1;
                    break;

                default:
                    printf("wrong code read\n");
            }
        }
    }
    return 0;
}

int unmountT(client c) {

    int sessionID = c.sessionID;

    if (pthread_mutex_lock(&sessionsLock) != 0) {
        return -1;
    }

    if (freeSessions[sessionID] == TAKEN) {
        freeSessions[sessionID] = FREE;
        if (pthread_mutex_unlock(&sessionsLock) != 0) {
            return -1;
        }
        char message[sizeof(int)];
        int var = 0;
        intToString(message, &var, sizeof(int));

        if (writeToClient(message, sizeof(int), sessionID) == -1) {
            return -1;
        }
    }
    else {
        pthread_mutex_unlock(&sessionsLock);
        return -1;
    }

    return 0;
}

int openT(client c) {

    int sessionID = c.sessionID;

    char resStr[sizeof(int)];

    int res = tfs_open(c.clientRequests[0].fileName, c.clientRequests[0].flags);

    intToString(resStr, &res, (int) sizeof(int));

    if (writeToClient(resStr, sizeof(int), sessionID) == -1) {
        return -1;
    }

    return 0;
}

int closeT(client c) {

    int sessionID = c.sessionID;
    int fh = c.clientRequests[0].filehandle;

    int res = tfs_close(fh);
    char resStr[sizeof(int)];

    intToString(resStr, &res, sizeof(int));

    if (writeToClient(resStr, sizeof(int), sessionID) == -1) {
        return -1;
    }

    return 0;
}

int writeT(client c) {

    int sessionID = c.sessionID;
    size_t len = c.clientRequests[0].writeLen;
    int fh = c.clientRequests[0].filehandle;

    int bytesWritten = (int) tfs_write(fh, c.clientRequests[0].toWrite, len);

    free(c.clientRequests->toWrite);

    char bytesWrittenStr[sizeof(int)];
    
    intToString(bytesWrittenStr, &bytesWritten, sizeof(int));

    if (writeToClient(bytesWrittenStr, sizeof(int), sessionID) == -1) {
        return -1;
    }

    return 0;
}

int readT(client c) {

    size_t len = c.clientRequests[0].readLen;
    int fh = c.clientRequests[0].filehandle;
    int sessionID = c.sessionID;

    char toRead[len];

    for (int i = 0; i < len; i++) {
        toRead[i] = '\0';
    }

    ssize_t bytesRead = tfs_read(fh, toRead, len);

    size_t bytesReadAux;

    if (bytesRead == -1) {
        bytesReadAux = 1;
    }
    else {
        bytesReadAux = (size_t) bytesRead;
    }

    char toClient[sizeof(int) + len];

    char bytesReadStr[sizeof(ssize_t)];
    
    intToString(bytesReadStr, &bytesRead, sizeof(int));

    memcpy(toClient, bytesReadStr, sizeof(int));

    memcpy(toClient + sizeof(int), toRead, (size_t) bytesReadAux);

    if (writeToClient(toClient, sizeof(int) + len, sessionID) == -1) {
        return -1;
    }

    return 0;
}

int shutdownAfterAllClosedT(client c) {

    int sessionID = c.sessionID;

    int res = tfs_destroy_after_all_closed();

    char resStr[sizeof(int)];

    intToString(resStr, &res, sizeof(int));

    if (writeToClient(resStr, sizeof(int), sessionID) == -1) {
        return -1;
    }

    return 0;
}