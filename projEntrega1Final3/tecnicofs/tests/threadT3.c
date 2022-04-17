#include "fs/operations.h"
#include <assert.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>

#define LOOPS 5


typedef struct fich {
    char* path;
    char* write;
    char read[12288];
    int fileHandle;
} file;

void* fnRead(void* arg) {
    file* f = (file*) arg;
    assert(tfs_read(f->fileHandle, f->read, sizeof(f->read)) % 4 == 0);
    return NULL;
}

void* fnWrite(void* arg) {
    file* f = (file*) arg;
    assert(tfs_write(f->fileHandle, f->write,  strlen(f->write)) % 4 == 0);
    return NULL;
}

void* fnOpen(void* arg, int flag) {
    file* f = (file*) arg;
    f->fileHandle = tfs_open(f->path, flag);
    return NULL;
}

void* fnClose(void* arg) {
    file* f = (file*) arg;
    tfs_close(f->fileHandle);
    return NULL;
}

void* readT(void* arg) {
    for (int i = 0; i < LOOPS; i++) {
        fnOpen(arg, TFS_O_CREAT);
        fnRead(arg);
        fnClose(arg);
    }
    return NULL;
}

void* writeT(void* arg) {
    for (int i = 0; i < LOOPS; i++) {
        fnWrite(arg);
    }
    return NULL;
}

void* writeA(void* arg) {
    for (int i = 0; i < LOOPS; i++) {
        fnOpen(arg, TFS_O_CREAT);
        writeT(arg);
        fnClose(arg);
    }
    return NULL;
}

int main() {
    pthread_t tid[6];
    file files[6];
    for (int i = 0; i < 6; i++) {
        files[i].path = "/f1";
        files[i].write = "AAAA";
    }

    assert(tfs_init() != -1);

    pthread_create(&tid[0], NULL, writeA, (void*) &files[0]);
    pthread_create(&tid[1], NULL, readT, (void*) &files[1]);
    pthread_create(&tid[2], NULL, readT, (void*) &files[2]);
    pthread_create(&tid[3], NULL, writeA, (void*) &files[3]);
    pthread_create(&tid[4], NULL, readT, (void*) &files[4]);
    pthread_create(&tid[5], NULL, writeA, (void*) &files[5]);

    for (int i = 0; i < 6; i++) {
        pthread_join(tid[i], NULL);
    }

    files[1].read[100] = '\0';

    printf("read: %s\n", files[1].read);
    printf("Successful test.\n");

    return 0;
}
