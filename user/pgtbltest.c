#include "kernel/param.h"
#include "kernel/fcntl.h"
#include "kernel/types.h"
#include "kernel/riscv.h"
#include "user/user.h"

#define N (8 * (1 << 20))

void print_pgtbl();
void print_kpgtbl();
void ugetpid_test();
void superpg_test();
void sbrkmuch(char*);
int
main(int argc, char *argv[])
{
  print_pgtbl();
  ugetpid_test();
  print_kpgtbl();
  superpg_test();
  //sbrkmuch("sbrkmuch");

  printf("pgtbltest: all tests succeeded\n");
  exit(0);
}

char *testname = "???";

void
err(char *why)
{
  printf("pgtbltest: %s failed: %s, pid=%d\n", testname, why, getpid());
  exit(1);
}

void
print_pte(uint64 va)
{
    pte_t pte = (pte_t) pgpte((void *) va);
    printf("va 0x%lx pte 0x%lx pa 0x%lx perm 0x%lx\n", va, pte, PTE2PA(pte), PTE_FLAGS(pte));
}

void
print_pgtbl()
{
  printf("print_pgtbl starting\n");
  for (uint64 i = 0; i < 10; i++) {
    print_pte(i * PGSIZE);
  }
  uint64 top = MAXVA/PGSIZE;
  for (uint64 i = top-10; i < top; i++) {
    print_pte(i * PGSIZE);
  }
  printf("print_pgtbl: OK\n");
}

void
ugetpid_test()
{
  int i;

  printf("ugetpid_test starting\n");
  testname = "ugetpid_test";

  for (i = 0; i < 64; i++) {
    int ret = fork();
    if (ret != 0) {
      wait(&ret);
      if (ret != 0)
        exit(1);
      continue;
    }
    if (getpid() != ugetpid())
      err("missmatched PID");
    exit(0);
  }
  printf("ugetpid_test: OK\n");
}

void
print_kpgtbl()
{
  printf("print_kpgtbl starting\n");
  kpgtbl();
  printf("print_kpgtbl: OK\n");
}


void
supercheck(uint64 s)
{
  pte_t last_pte = 0;

  /*
  *  对所有L1页表项的对应的虚拟地址空间，检查这个地址空间里的所有页，看他们是否都能对应到同一个superpage的 L1 pte上
  */
  for (uint64 p = s;  p < s + 512 * PGSIZE; p += PGSIZE) {
    pte_t pte = (pte_t) pgpte((void *) p); 
    //printf("error after pgpte and loop num: %ld\n", (p-s)/PGSIZE + 1);
    if(pte == 0)
      err("no pte");
    if ((uint64) last_pte != 0 && pte != last_pte) {
        err("pte different");
    }
    if((pte & PTE_V) == 0 || (pte & PTE_R) == 0 || (pte & PTE_W) == 0){
      err("pte wrong");
    }
    last_pte = pte;
  }
  //printf("loop end\n");
  //修正：i < 512 -> i < 512 * PGSIZE
  for(int i = 0; i < 512; i += PGSIZE){
    //printf("in write loop\n");
    *(int*)(s+i) = i;
  }

  for(int i = 0; i < 512; i += PGSIZE){
    //printf("in read loop\n");
    if(*(int*)(s+i) != i)
      err("wrong value");
  }
  //printf("super check end\n");
}

void
superpg_test()
{
  int pid;
  
  printf("superpg_test starting\n");
  testname = "superpg_test";
  
  char *end = sbrk(N);
  if (end == 0 || end == (char*)0xffffffffffffffff)
    err("sbrk failed");
  
  //printf("end: %p\n", end);
  
  uint64 s = SUPERPGROUNDUP((uint64) end);

  //printf("s: %p\n", (void*)s);
  
  supercheck(s);

  //printf("control after first supercheck and before fork\n");

  if((pid = fork()) < 0) {
    err("fork");
  } else if(pid == 0) {
    
    supercheck(s);
    exit(0);
  } else {
    int status;
    wait(&status);
    if (status != 0) {
      exit(0);
    }
  }
  printf("superpg_test: OK\n");  
}

void
sbrkmuch(char *s)
{
  enum { BIG=100*1024*1024 };
  char *c, *oldbrk, *a, *lastaddr, *p;
  uint64 amt;

  oldbrk = sbrk(0);

  // can one grow address space to something big?
  a = sbrk(0);
  amt = BIG - (uint64)a;
  p = sbrk(amt);
  if (p != a) {
    printf("%s: sbrk test failed to grow big address space; enough phys mem?\n", s);
    exit(1);
  }

  // touch each page to make sure it exists.
  char *eee = sbrk(0);
  for(char *pp = a; pp < eee; pp += 4096)
    *pp = 1;

  lastaddr = (char*) (BIG-1);
  *lastaddr = 99;

  // can one de-allocate?
  a = sbrk(0);
  c = sbrk(-PGSIZE);
  if(c == (char*)0xffffffffffffffffL){
    printf("%s: sbrk could not deallocate\n", s);
    exit(1);
  }
  c = sbrk(0);
  if(c != a - PGSIZE){
    printf("%s: sbrk deallocation produced wrong address, a %p c %p\n", s, a, c);
    exit(1);
  }

  // can one re-allocate that page?
  a = sbrk(0);
  c = sbrk(PGSIZE);
  if(c != a || sbrk(0) != a + PGSIZE){
    printf("%s: sbrk re-allocation failed, a %p c %p\n", s, a, c);
    exit(1);
  }
  if(*lastaddr == 99){
    // should be zero
    printf("%s: sbrk de-allocation didn't really deallocate\n", s);
    exit(1);
  }

  a = sbrk(0);
  c = sbrk(-(sbrk(0) - oldbrk));
  if(c != a){
    printf("%s: sbrk downsize failed, a %p c %p\n", s, a, c);
    exit(1);
  }
}