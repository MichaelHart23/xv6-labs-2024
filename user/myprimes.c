#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

/*
我的迭代实现:
子进程负责写，且该子进程拥有某个管道的唯一写端，该管道的读端可以被父进程与下一个子进程读取
父进程若能读到"第一个素数"就新创建一个管道和一个子进程
在这个新创建的子进程中，关闭新管道的读端，保留该新管道唯一 一个写端(父进程会关闭该新管道的写端)，并从上一个子进程中读，利用新管道的唯一写端向外写
父进程会保留该新管道的读端，以此来决定是否进行下一次循环

以此往复



经测试，该程序可以在xv6操作系统中生成10000以下的所有素数且不崩溃，我TM太牛逼了
*/

int main() {
    int p[2];
    pipe(p);
    int pid = fork();
    if(pid == 0) {
        close(p[0]);
        for(int i = 2; i <= 10000; i++) {
            write(p[1], &i, 4);
        }
        close(p[1]);
        exit(0);
    }
    close(p[1]);

    int fd = p[0];
    int first_prime, other;

    while(read(fd, &first_prime, 4)) {
        printf("prime %d\n", first_prime);
        int new_p[2];
        pipe(new_p);
        pid = fork();
        if (pid == 0) {
            close(new_p[0]);
            while(read(fd, &other, 4)) {
                if(other%first_prime != 0) {
                    write(new_p[1], &other, 4);
                }
            }
            close(new_p[1]);
            exit(0);
        }
        close(new_p[1]);
        fd = new_p[0];
    }
}