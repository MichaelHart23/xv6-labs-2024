/*
https://github.com/ahmedanwar123/xv6-labs-2024/blob/lab1part1/user/primes.c
该代码是primes题目的迭代解法
*/

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define MAX 1000
#define FIRST_PRIME 2

int generate_natural();
int prime_filter(int in_fd, int prime);

int main(int argc, char *argv[])
{
    int prime;
    int in = generate_natural();
    int pid; // child process ID

    while (read(in, &prime, sizeof(int)))
    {
        printf("prime %d\n", prime);
        in = prime_filter(in, prime); //更新in，更新后的in将被写入新一轮待筛选的数
    }

    // Wait for the child processes to finish to prevent zombie processes
    while ((pid = wait(0)) > 0)
    {
    }

    exit(0);
}

int generate_natural()
{
    int out_pipe[2];
    pipe(out_pipe);

    if (!fork())
    { //子进程
        for (int i = FIRST_PRIME; i < MAX; i++) //最开始的把要求的所有的数依次写入
        {
            write(out_pipe[1], &i, sizeof(int));
        }
        close(out_pipe[1]);
        exit(0);
    }
    
    //父进程
    close(out_pipe[1]);
    return out_pipe[0];//管道对象与作用与无关，此处不会被销毁
}

// Prime filtering function
int prime_filter(int in_fd, int prime)
{
    int num;
    int out_pipe[2];
    pipe(out_pipe);

    if (!fork())
    { //子进程，在子进程中向管道写入新一轮待筛选的数
        while (read(in_fd, &num, sizeof(int)))
        {
            if (num % prime)
            {
                write(out_pipe[1], &num, sizeof(int));
            }
        }
        close(in_fd);
        close(out_pipe[1]);
        exit(0);
    }
    
    //父进程
    close(in_fd);
    close(out_pipe[1]);
    return out_pipe[0];
}