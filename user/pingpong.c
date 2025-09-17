#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

enum fails {
    FAIL_PIPE = 1,
    FAIL_FORK = 2,
    FAIL_READ = 3,
    FAIL_WRITE = 4,
    FAIL_WAIT = 5,
};

int main(int argc, char *argv[])
{
    int p[2];
    int pid;
    char buf[16];

    if (pipe(p) < 0) {
        fprintf(2, "pipe failed\n");
        exit(FAIL_PIPE);
    }

    pid = fork();
    if (pid < 0) {
        fprintf(2, "fork failed\n");
        exit(FAIL_FORK);
    }

    if (pid == 0) {
        close(p[1]); 

        if (read(p[0], buf, sizeof(buf)) <= 0) {
            fprintf(2, "child read failed\n");
            exit(FAIL_READ);
        }
        printf("%d: got ping\n", getpid());

        close(p[0]); 

        exit(0);
    } else { 
        close(p[0]); 

        if (write(p[1], "ping", 5) != 5) {
            fprintf(2, "parent write failed\n");
            exit(FAIL_WRITE);
        }
        close(p[1]);

        if (wait(0) < 0) {
            fprintf(2, "wait failed\n");
            exit(FAIL_WAIT);
        }
        printf("%d: got pong\n", getpid());
        exit(0);
    }
}