struct file {
  enum { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE } type;
  int ref; // reference count 文件表项引用计数，比如被多个进程同时打开，比如一个进程dup了
  char readable;
  char writable;
  struct pipe *pipe; // FD_PIPE
  struct inode *ip;  // FD_INODE and FD_DEVICE
  uint off;          // FD_INODE  仅当类型为INODE，即普通文件时有效
  short major;       // FD_DEVICE 设备号主编号，查设备驱动表用
};

//设备表示为一个 32 bit 整数：
//高 16 位: major 设备号  (选择设备驱动)
//低 16 位: minor 设备号  (区分同驱动下不同设备实例)
#define major(dev)  ((dev) >> 16 & 0xFFFF)
#define minor(dev)  ((dev) & 0xFFFF)
#define	mkdev(m,n)  ((uint)((m)<<16| (n)))

// in-memory copy of an inode
struct inode {
  uint dev;           // Device number
  uint inum;          // Inode number 仅用于唯一标识in_memory的inode，实际上，在磁盘中的inode根据其位置就能得出编号
  int ref;            // Reference count  即指向该inode的指针的数量，iget增加该字段，iput减少该字段
  struct sleeplock lock; // protects everything below here
  int valid;          // inode has been read from disk? 标识该 inode 的内容是否已从磁盘加载到内存。

  short type;         // copy of disk inode
  short major;
  short minor;
  short nlink;
  uint size;
  uint addrs[NDIRECT+2];
};

// map major device number to device functions.
// 统一device的读写接口
struct devsw {
  int (*read)(int, uint64, int);
  int (*write)(int, uint64, int);
};

extern struct devsw devsw[];

#define CONSOLE 1
