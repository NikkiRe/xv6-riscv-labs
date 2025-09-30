#include "../kernel/types.h"
#include "user.h"
#include "../kernel/param.h"
#include "../kernel/spinlock.h"
#include "../kernel/riscv.h"
#include "../kernel/proc.h"

#define close_pipe(pipe_fd) do { close(pipe_fd[0]); close(pipe_fd[1]); } while(0)

enum error_codes {
    PIPE_FAILED = 0x1,
    FORK_FAILED = 0x2,
    READ_FAILED = 0x3,
    WRITE_FAILED = 0x4
};

int main() {
    int pipe_[2];
    if (pipe(pipe_) == -1) {
        exit(PIPE_FAILED);
    }
    
    int pid = fork();
    if (pid == -1) {
        close_pipe(pipe_);
        exit(FORK_FAILED);
    }
    
    char buf[8];
    const char *ping = "ping";
    const char *pong = "pong";
    int status;
    
    if (pid == 0) {
        // Дочерний процесс
        wait(0);
        int bytes_read = read(pipe_[0], buf, sizeof(buf) - 1);
        if (bytes_read <= 0) {
            close_pipe(pipe_);
            exit(READ_FAILED);
        }
        buf[bytes_read] = '\0';
        printf("<%d>: got <%s>\n", getpid(), buf);
        
        int bytes_written = write(pipe_[1], pong, strlen(pong) + 1);
        if (bytes_written == -1) {
            close_pipe(pipe_);
            exit(WRITE_FAILED);
        }
        close_pipe(pipe_);
        exit(0);
    } else {
        // Родительский процесс
        int bytes_written = write(pipe_[1], ping, strlen(ping) + 1);
        if (bytes_written == -1) {
            close_pipe(pipe_);
            wait(0);
            exit(WRITE_FAILED);
        }
        
        wait(0);
        int bytes_read = read(pipe_[0], buf, sizeof(buf) - 1);
        if (bytes_read <= 0) {
            close_pipe(pipe_);
            wait(0);
            exit(READ_FAILED);
        }
        buf[bytes_read] = '\0';
        printf("<%d>: got <%s>\n", getpid(), buf);
        close_pipe(pipe_);
        wait(&status);
        exit(status);
    }
}