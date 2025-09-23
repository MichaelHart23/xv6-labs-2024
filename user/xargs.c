#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

//每行中会包含空格吗？我假设即使包含空格，该空格也作为一整个参数的一部分
char* read_line() {
    static char buf[512];
    buf[511] = 0;
    int index  = 0;
    if(read(0, buf+index, 1) != 1) return 0; //读完了
    while(buf[index] != '\n') {
        //一般来言，结束都以换行符结尾，不太可能遇到没有换行符就结束的情况, 所以循环内不必检查文件是否读完
        index++;
        read(0, buf+index, 1); //此行与上面中的读操作只能存在一个
        if(index > 510) {
            fprintf(2,"xargs: this line is too long %s\n", buf);
            return 0;
        }
    }
    buf[index] = 0;
    return buf;
}
int main(int argc, char* argv[]){
    if(argc + 1 > MAXARG) {
        fprintf(2, "xargs: too many arguments\n");
        exit(1);
    }
    int pid, status;
    char* args[MAXARG];
    args[MAXARG - 1] = 0;
    args[argc] = 0;
    for(int i = 1; i < argc; i++) {
        args[i-1] = argv[i];
    }
    while((args[argc-1] = read_line()) != 0) {
        if((pid = fork()) == 0)
            exec(args[0], args);
        else if(pid > 0)
            continue;
        else {
            fprintf(2, "xargs: fork error\n");
            exit(1);
        }
    }
    while(wait(&status) != -1) ;
}