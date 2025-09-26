// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"


void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run { //作为物理页头八个字节，同时指向下一个页
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
  struct run *superpage_list;  //superpage的开头
} kmem;

#define SPSTART (PHYSTOP-32*SUPERPGSIZE)      //superpage开始的地址

void 
kinit()
{
  initlock(&kmem.lock, "kmem");
  kmem.freelist = 0;
  kmem.superpage_list = 0;  //由于它们是 全局静态变量，在 C 语言里会被放在 BSS 段，BSS 段默认会被清零。但此处还是显示初始化
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  if(pa_end > (void*)SPSTART) {
    for(p = (char*)SPSTART; p + SUPERPGSIZE <= (char*)pa_end; p += SUPERPGSIZE) 
      superfree((void*)p);
    pa_end = (void*)SPSTART;
  }
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

void superfree(void* pa) {
  struct run *r;
  if(((uint64)pa % SUPERPGSIZE) != 0 || pa < (void*)SPSTART || (uint64)pa >= PHYSTOP)
    panic("superfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, SUPERPGSIZE);

  r = (struct run*)pa;
  acquire(&kmem.lock);
  r->next = kmem.superpage_list;
  kmem.superpage_list = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

void* superalloc(void) {
  struct  run *r;
  acquire(&kmem.lock);
  r = kmem.superpage_list;
  if(r)
    kmem.superpage_list = r->next;
  release(&kmem.lock);
  if(r)
    memset((char*)r, 5, SUPERPGSIZE);
  return (void*)r;
}
