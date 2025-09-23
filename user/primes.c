#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"



/*
本代码在xv6中运行时，在最后一次递归（即277的递归中）会崩溃，但b已经是277了，一用printf读b，就崩溃，不理解
可是用官方提供的测试工具却显示通过，真让人摸不着头脑
另外
全局变量p可以用函数传参的方式代替
全局数组arr作为临时存储一个轮次中产生的需要往下传的数，可以用读一个传一个的方式来代替
但上述两种优化反而使得能够生成的素数更少了

究其原因，应该是xv6系统的资源较少导致的，将递归改为迭代应该可以解决该问题
https://github.com/ahmedanwar123/xv6-labs-2024/blob/lab1part1/user/primes.c
上述网址提供的迭代解法确实可以完美生成全部素数
经测试，该代码最多能生成到283这个素数
*/


int p[2];
int arr[138];


void sieve() __attribute__((noreturn));

void sieve() {
    int b,c;
    
    close(p[1]);//关闭写端，使得所有写端全部关闭，以使得read能返回0
    if(!read(p[0], &b, 4)) exit(0);


    // if(b == 277 ) {
    //     printf("b is 277\n");
    //     printf("address is %p\n", &b);
    //     printf("b is %d\n", b);
    // }


    printf("prime %d\n", b);

    int i = 0;

    while(read(p[0], &c, 4)) {
        if(c % b != 0) arr[i++] = c;
    }
    close(p[0]); //关闭老的读端
    pipe(p); //创建新的管道


    int pid = fork();
    if(pid > 0) {
        int status;
        close(p[0]);
        for (int j = 0; j < i; j++) {
            write(p[1], &arr[j], 4);
        }
        close(p[1]);
        wait(&status);
        exit(0);
    }
    else if(pid == 0) {
        sieve();
        exit(0);
    }
    else{
        fprintf(2, "fork error\n");
        exit(1);
    }
}

int main() {
    int status;

    pipe(p);
    int pid = fork();
    if (pid == 0) {
        sieve();
        exit(0);
    }
    else if(pid > 0) {
        close(p[0]);
        for(int i = 2; i < 281; i++) {
            write(p[1], &i, 4);
        }
        close(p[1]);
        wait(&status);
    }
    else{
        fprintf(2, "fork error\n");
        exit(1);
    }
    exit(0);
}