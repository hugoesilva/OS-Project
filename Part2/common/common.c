#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <syscall.h>
#include <signal.h>

int openAux(char* path, char flag) {
    errno = 0;
    int fh = open(path, flag);
    while (errno  == EINTR) {
        fh = open(path, flag);
    }

    return fh;
}

void intToString(char* buffer, void* num, size_t size) {
    memcpy(buffer, (char *) num, size);
}

int stringToInt(char* buffer) {
    int counter = 0;
    int res = 0;
    double multiplier = 0;

    while (buffer[counter] != '\0') {
        counter++;
    }

    counter--;

    while (counter >= 0) {
        int x = 0;
        int exp = 1;
        while (x < multiplier) {
            exp *= 10;
            x++;
        }
        int dig = buffer[counter];
        res += dig * (int) exp;
        counter--;
    }
    return res;
}
