#include "fs/operations.h"
#include <assert.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

/*  Simple test to check whether the implementation of
    tfs_destroy_after_all_closed is correct.
    Note: This test uses TecnicoFS as a library, not
    as a standalone server.
    We recommend to try out a similar test once the
    client-server version is ready; further, we suggest
    trying more elaborate tests of tfs_destroy_after_all_closed
*/
int f;
int closed_file = 0;

void *fn_thread(void *arg) {
    (void)
        arg; /* Since arg is not used, this line prevents a compiler warning */

    assert(f != -1);

    sleep(10);

    /* set *before* closing the file, so that it is set before
       tfs_destroy_after_all_close returns in the main thread
    */
    closed_file = 1;

    assert(tfs_close(f) != -1);

    return NULL;
}

int main() {

    assert(tfs_init() != -1);

    pthread_t t;
    f = tfs_open("/f1", TFS_O_CREAT);
    assert(pthread_create(&t, NULL, fn_thread, NULL) == 0);
    assert(tfs_destroy_after_all_closed() != -1);
    assert(closed_file == 1);

    // No need to join thread
    // just here to prevent thread leak error when compiled with sanitizer
    pthread_join(t, NULL);
    printf("Successful test.\n");

    return 0;
}
