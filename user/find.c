#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

/*
递归结构：
给一个目录以及要找的名字，利用read和dirent遍历这个目录的所有条目的名字，一样的打印
对于file，不管了
对于directory，递归
*/
void scan_dir(char* path, char* name) {
    char buf[512];
    char* p;
    struct stat st;
    int fd;
    struct dirent de;
    if((fd = open(path, O_RDONLY)) < 0) {
        fprintf(2, "find: cannot open %s.\n", path);
        return;
    }

    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
        fprintf(2, "The path: %s is too long.\n", path);
        close(fd);
        return;
    }

    strcpy(buf, path);
    p = buf + strlen(path);
    *p++ = '/';
    p[DIRSIZ] = 0;

    while(read(fd, &de, sizeof(de)) == sizeof(de)) {
        if(de.inum == 0) continue;
        if(strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) continue; //不要递归进自己和父亲
        memmove(p, de.name, DIRSIZ);
        if(strcmp(p, name) == 0) { //dirent结构体的name字段的未用到的字节应该是用0填充的, 但也要考虑14个字节全被填满，没有终止符的情况
            printf("%s/%s\n",path, name);
        }
        if(stat(buf, &st) < 0) {
            fprintf(2, "find: cannot stat %s.\n", buf);
            break;
        }
        if(st.type == T_DIR) scan_dir(buf, name);
    }
    close(fd);
}

void find(char* path, char* name) {
    int fd;
    struct stat st;
    if((fd = open(path, O_RDONLY)) < 0){
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }
    if(fstat(fd, &st) < 0) {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }
    if(st.type != T_DIR) {
        fprintf(2, "find: The second argument must be a directory\n");
        close(fd);
        return;
    }
    close(fd);
    scan_dir(path, name);
}

int main(int argc, char* argv[]) {
    if(argc != 3) {
        fprintf(2,"Usage: find /directory name\n");
        exit(1);
    }

    find(argv[1], argv[2]);
}