#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


int main(int argc, char*  argv[]) {
    if(argc != 1) {
        printf("Usage: pingpong\n");
        exit(1);
    }
    int p1[2], p2[2];
    pipe(p1); //child->parent
    pipe(p2); //parent->child
    int pid = fork();
    if(pid == 0) {
        char c;
        read(p2[0], &c, 1);
        printf("%d: received ping\n", getpid());
        write(p1[1], "b", 1);
        exit(0);
    }
    else if(pid > 0) {
        write(p2[1],"b", 1);
        char c;
        read(p1[0], &c, 1);
        printf("%d: received pong\n", getpid());
        exit(0);
    }
    else {
        fprintf(2, "fork error\n");
        exit(1);
    }
}