#include "fs/operations.h"
#include <assert.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>

typedef struct fich {
    char* path;
    char* write;
    char read[40];
    int fileHandle;
} file;


void* fnRead(void* arg) {
    file* f = (file*) arg;
    tfs_read(f->fileHandle, f->read, sizeof(f->read));
    return NULL;
}

void* fnWrite(void* arg) {
    file* f = (file*) arg;
    tfs_write(f->fileHandle, f->write,  strlen(f->write));
    return NULL;
}

void* fnOpen(void* arg) {
    file* f = (file*) arg;
    f->fileHandle = tfs_open(f->path, TFS_O_CREAT);
    return NULL;
}

void* fnClose(void* arg) {
    file* f = (file*) arg;
    tfs_close(f->fileHandle);
    return NULL;
}

void* fnLookup(void* arg) {
    file* f = (file*) arg;
    tfs_lookup(f->path);
    return NULL;
}

int main() {
    file files[12];
    pthread_t tid[12];
    for (int i = 0; i < 12; i++) {
        files[i].path = "/f1";
    }
    
    assert(tfs_init() != -1);

    pthread_create(&tid[0], NULL, fnOpen, (void*) &files[0]);
    pthread_create(&tid[1], NULL, fnOpen, (void*) &files[1]);
    pthread_create(&tid[2], NULL, fnOpen, (void*) &files[2]);
    pthread_create(&tid[3], NULL, fnOpen, (void*) &files[3]);

    /* for (int i = 0; i < 4; i++) {
        pthread_join(tid[i], NULL);
    } */


    pthread_create(&tid[4], NULL, fnLookup, (void*) &files[4]);
    pthread_create(&tid[5], NULL, fnLookup, (void*) &files[5]);
    pthread_create(&tid[6], NULL, fnLookup, (void*) &files[6]);
    pthread_create(&tid[7], NULL, fnLookup, (void*) &files[7]);

    /* for (int i = 4; i < 8; i++) {
        pthread_join(tid[i], NULL);
    } */

    pthread_create(&tid[8], NULL, fnClose, (void*) &files[8]);
    pthread_create(&tid[9], NULL, fnClose, (void*) &files[9]);
    pthread_create(&tid[10], NULL, fnClose, (void*) &files[10]);
    pthread_create(&tid[11], NULL, fnClose, (void*) &files[11]);

    for (int i = 0 /* 8 */; i < 12; i++) {
        pthread_join(tid[i], NULL);
    }

    printf("Successful test.\n");

    return 0;
}
