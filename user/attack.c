#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"

int
main(int argc, char *argv[])
{
  // your code here.  you should write the secret to fd 2 using write
  // (e.g., write(2, secret, 8)

  // char* e = sbrk(17 * PGSIZE);
  // e = e + 16 * PGSIZE + 32;
  // write(2, e, 8);
  // exit(1);
  char sentence[] = {"my very very very secret pw is:"};
  char s[32];
  s[31] = 0;
  char* end;
  int n = 0;
  while((end = sbrk(PGSIZE))) {
    n++;
    if(n == 17) {
      //memmove(s, end, 31);
      //printf("%s\n", s);
      char* secret = end + 32;
      write(2, secret, 8);
      break;
    }
    memmove(s, end, 31);
    if(strcmp(s, sentence) == 0) {
      printf("%s\n", s);
      char* secret = end + 32;
      write(2, secret, 8);
      break;
    }
  }
  exit(1);
}
