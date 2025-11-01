#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

// Simple logging that allows concurrent FS system calls.
//
// A log transaction contains the updates of multiple FS system
// calls. The logging system only commits when there are
// no FS system calls active. Thus there is never
// any reasoning required about whether a commit might
// write an uncommitted system call's updates to disk.
//
// A system call should call begin_op()/end_op() to mark
// its start and end. Usually begin_op() just increments
// the count of in-progress FS system calls and returns.
// But if it thinks the log is close to running out, it
// sleeps until the last outstanding end_op() commits.
//
// The log is a physical re-do log containing disk blocks.
// The on-disk log format:
//   header block, containing block #s for block A, B, C, ...
//   block A
//   block B
//   block C
//   ...
// Log appends are synchronous.

// Contents of the header block, used for both the on-disk header block
// and to keep track in memory of logged block# before commit.
struct logheader {
  int n;               //几个block被修改了
  int block[LOGSIZE];  //被修改的块的编号，即blockno
};

struct log {
  struct spinlock lock;
  int start;    //日志起始位置的block编号——blockno
  int size;     //日志区域大小(块数)
  int outstanding; // how many FS sys calls are executing.有几个系统调用**正在**写, 该字段因并发而存在
  // 此字段即表示有几个正在向log层写，且没写完的syscall
  // 另外，一个系统调用可能会写多个block，log系统认为每个系统调用都写了MAXOPBLOCKS个block
  // 于是log层给正在向log层写，且没写完的syscall预留的空间就是outstanding * MAXOPBLOCKS

  int committing;  // in commit(), please wait.
  int dev;
  struct logheader lh;
};
struct log log;

static void recover_from_log(void);
static void commit();

void
initlog(int dev, struct superblock *sb)
{
  if (sizeof(struct logheader) >= BSIZE)
    panic("initlog: too big logheader");

  initlock(&log.lock, "log");
  log.start = sb->logstart;
  log.size = sb->nlog;
  log.dev = dev;
  recover_from_log();
}

// Copy committed blocks from log to their home location
// 把数据从磁盘的log区真正复制到磁盘中存放该数据的位置
static void
install_trans(int recovering)
{
  int tail;

  for (tail = 0; tail < log.lh.n; tail++) {
    struct buf *lbuf = bread(log.dev, log.start+tail+1); // read log block
    struct buf *dbuf = bread(log.dev, log.lh.block[tail]); // read dst
    memmove(dbuf->data, lbuf->data, BSIZE);  // copy block to dst
    bwrite(dbuf);  // write dst to disk
    if(recovering == 0)
      bunpin(dbuf);  //在log_write中pin的，而若是正在recovering，则对一个block的写操作并没有在buffer cache中记录报错，也就没有pin
    brelse(lbuf);
    brelse(dbuf);
  }
}

// Read the log header from disk into the in-memory log header
static void
read_head(void)
{
  struct buf *buf = bread(log.dev, log.start);
  struct logheader *lh = (struct logheader *) (buf->data);
  int i;
  log.lh.n = lh->n;
  for (i = 0; i < log.lh.n; i++) {
    log.lh.block[i] = lh->block[i];
  }
  brelse(buf);
}

// Write in-memory log header to disk.
// This is the true point at which the
// current transaction commits.
// 将日志头写入磁盘的log区。 这是提交点（commit point），如果系统在这次写入后崩溃，
// 那么恢复（recovery）时会重新执行日志中的写入操作，从而replay整个事务。
static void
write_head(void)
{
  struct buf *buf = bread(log.dev, log.start); //磁盘log区的第一个block是存放log header的
  struct logheader *hb = (struct logheader *) (buf->data); //指针转换
  int i;
  //数据复制
  hb->n = log.lh.n;
  for (i = 0; i < log.lh.n; i++) {
    hb->block[i] = log.lh.block[i];
  }
  //写入磁盘
  bwrite(buf);
  brelse(buf);
}

static void
recover_from_log(void)
{
  read_head();
  install_trans(1); // if committed, copy from log to disk
  log.lh.n = 0;
  write_head(); // clear the log 即把n字段清零log header写回磁盘log区
}

// called at the start of each FS system call.
void
begin_op(void)
{
  acquire(&log.lock);
  while(1){
    if(log.committing){
      sleep(&log, &log.lock);
    } else if(log.lh.n + (log.outstanding+1)*MAXOPBLOCKS > LOGSIZE){
      // this op might exhaust log space; wait for commit.
      sleep(&log, &log.lock);
    } else {
      log.outstanding += 1; //正在向log层写的syscall数量加一
      release(&log.lock);
      break;
    }
  }
}

// called at the end of each FS system call.
// commits if this was the last outstanding operation.
void
end_op(void)
{
  int do_commit = 0;

  acquire(&log.lock);
  log.outstanding -= 1; //一个syscall向log层的写操作完成了
  if(log.committing)
    panic("log.committing");
  if(log.outstanding == 0){ //没有正在向log层写的syscall了，于是准备commit
    do_commit = 1;
    log.committing = 1;
  } else {
    // begin_op() may be waiting for log space,
    // and decrementing log.outstanding has decreased
    // the amount of reserved space.
    wakeup(&log);
  }
  release(&log.lock);

  if(do_commit){
    // call commit w/o holding locks, since not allowed
    // to sleep with locks.
    commit();
    acquire(&log.lock);
    log.committing = 0;
    wakeup(&log);
    release(&log.lock);
  }
}

// Copy modified blocks from cache to log.
// 将只在内存(Buffer Cache)修改的block复制到disk的log区
static void
write_log(void)
{
  int tail;

  for (tail = 0; tail < log.lh.n; tail++) {
    //磁盘log区的第一个blockno是存放log header的，所以此处有“+1”，并空出start对应的block
    struct buf *to = bread(log.dev, log.start+tail+1); // log block
    struct buf *from = bread(log.dev, log.lh.block[tail]); // cache block
    memmove(to->data, from->data, BSIZE);
    bwrite(to);  // write the log
    brelse(from);
    brelse(to);
  }
}

static void
commit()
{
  if (log.lh.n > 0) {
    write_log();     // Write modified blocks from cache to log
    write_head();    // Write header to disk -- the real commit
    install_trans(0); // Now install writes to home locations
    log.lh.n = 0;
    write_head();    // Erase the transaction from the log
  }
}

// Caller has modified b->data and is done with the buffer.
// Record the block number and pin in the cache by increasing refcnt.
// commit()/write_log() will do the disk write.
//
// log_write() replaces bwrite(); a typical use is:
//   bp = bread(...)
//   modify bp->data[]
//   log_write(bp)
//   brelse(bp)
void
log_write(struct buf *b)
{
  int i;

  acquire(&log.lock);
  if (log.lh.n >= LOGSIZE || log.lh.n >= log.size - 1)
    panic("too big a transaction");
  if (log.outstanding < 1)
    panic("log_write outside of trans");

  for (i = 0; i < log.lh.n; i++) {
    if (log.lh.block[i] == b->blockno)   // log absorption：合并写到同一个block的多个写操作
      break;                            //此处的逻辑是该block已经在要更新的block数组里了，就break
  }
  log.lh.block[i] = b->blockno;
  if (i == log.lh.n) {  // Add new block to log?  上个循环是正常结束还是break掉的
    bpin(b);            //增加该buf的引用计数，防止被逐出cache buffer，一是为了保证之后的向磁盘中写数据的操作，二是保证从该磁盘block读数据的一致性
    log.lh.n++;         // 为什么pin？因为该block已经被写了，在向磁盘中同步修改之前不能逐出cache buffer
  }
  release(&log.lock);
}

