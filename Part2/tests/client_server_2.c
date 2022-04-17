#include "client/tecnicofs_client_api.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

/*  This test is similar to test1.c from the 1st exercise.
    The main difference is that this one explores the
    client-server architecture of the 2nd exercise. */

int main(int argc, char **argv) {

    char *str = "AAA!";
    char *path = "/f1";
    char buffer[40];

    int f;
    ssize_t r;

    if (argc < 5) {
        printf("You must provide the following arguments: client_pipe_path1, client_pipe_path2, client_pipe_path3 server_pipe_path\n");
        return 1;
    }

    int pid = fork();

    if (pid == 0) {
        // son
        int pid2 = fork();
        if (pid2 == 0) {
            // son
            assert(tfs_mount(argv[1], argv[4]) == 0);
        }

        else if (pid2 != -1) {
            // father
            assert(tfs_mount(argv[2], argv[4]) == 0);
        }
        else {
            printf("fork error\n");
            return -1;
        }
    }

    else if (pid != -1) {
        // father
        assert(tfs_mount(argv[3], argv[4]) == 0);
    }
    else {
        printf("fork error\n");
        return -1;
    }

    f = tfs_open(path, TFS_O_CREAT);
    assert(f != -1);
    
    r = tfs_write(f, str, strlen(str));
    assert(r == strlen(str));

    assert(tfs_close(f) != -1);

    f = tfs_open(path, 0);
    assert(f != -1);

    r = tfs_read(f, buffer, sizeof(buffer) - 1);
    assert(r == strlen(str));

    buffer[r] = '\0';
    assert(strcmp(buffer, str) == 0);

    assert(tfs_close(f) != -1);

    assert(tfs_unmount() == 0);

    /* assert(tfs_shutdown_after_all_closed() == 0); */

    printf("Successful test.\n");

    return 0;
}
