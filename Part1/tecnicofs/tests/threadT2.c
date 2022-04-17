#include "fs/operations.h"
#include <assert.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>

#define SIZE 500

typedef struct fich {
    char* path;
    char write[SIZE];
    char read[1024];
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

void* processT(void* arg) {
    fnOpen(arg);
    fnWrite(arg);
    fnClose(arg);
    fnOpen(arg);
    fnRead(arg);
    fnClose(arg);
    return NULL;
}

int main() {
    pthread_t tid[4];
    file f1;
    file f2;
    memset(f1.write, 'A', SIZE);
    memset(f2.write, 'B', SIZE);
    f1.path = "/f1";
    f2.path = "/f2";

    assert(tfs_init() != -1);

    pthread_create(&tid[0], NULL, processT, (void*) &f1);
    pthread_create(&tid[1], NULL, processT, (void*) &f2);

    for (int i = 0; i < 2; i++) {
        pthread_join(tid[i], NULL);
    }

    f1.read[SIZE] = '\0';
    f2.read[SIZE] = '\0';
    

    printf("%s\n", f1.read);
    printf("%s\n", f2.read);

    assert(strlen(f1.read) == SIZE);
    assert(strlen(f2.read) == SIZE);

    printf("Successful test.\n");
    return 0;
}
