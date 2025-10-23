#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "net.h"

// xv6's ethernet and IP addresses
static uint8 local_mac[ETHADDR_LEN] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static uint32 local_ip = MAKE_IP_ADDR(10, 0, 2, 15);

// qemu host's ethernet address.
static uint8 host_mac[ETHADDR_LEN] = { 0x52, 0x55, 0x0a, 0x00, 0x02, 0x02 };

static struct spinlock netlock;

struct bind_rep {
  int pid;
  uint16 port;
};

#define PACKETS_QUEUE_SIZE 16

struct packets_queue {
  char* packets[PACKETS_QUEUE_SIZE];
  int packets_len[PACKETS_QUEUE_SIZE];
  uint8 nread;
  uint8 nwrite;
};

//两个数组中的元素一一对应，即两个数组中相同索引的元素相互对应
static struct bind_rep bind_reps[NPROC];  // 6 * 64
static struct packets_queue packets_queues[NPROC];  //196 * 64

void
netinit(void)
{
  initlock(&netlock, "netlock");
  for(int i = 0; i < NPROC; i++){
    bind_reps[i].pid = 0;
    bind_reps[i].port = 0;

    packets_queues[i].nread = 0;
    packets_queues[i].nwrite = 0;
    for(int j = 0; j < PACKETS_QUEUE_SIZE; j++) {
      packets_queues[i].packets[j] = 0;
      packets_queues[i].packets_len[j] = 0;
    }
  }
}


//
// bind(int port)
// prepare to receive UDP packets address to the port,
// i.e. allocate any queues &c needed.
//

//父进程绑定了某端口，那么该父进程fork出的子进程依旧绑定该端口
//其他进程不能绑定该端口
//很遗憾，子进程继承端口功能并没有在此处实现
uint64
sys_bind(void)
{
  acquire(&netlock);
  int dport, i;
  struct proc *p = myproc();
  argint(0, &dport);
  for(i = 0; i < NPROC; i++) { //防止多个进程同时绑定同一端口
    if(bind_reps[i].pid != 0 && bind_reps[i].port == (uint16)dport) {
      release(&netlock);
      return -1;
    }
  }
  for(i = 0; i < NPROC; i++) {
    if(bind_reps[i].pid == 0) {
      bind_reps[i].pid = p->pid;
      bind_reps[i].port = (uint16)dport;
      release(&netlock);
      printf("sys_bind: bind successfully for pid: %d  port: %d  index: %d \n", p->pid, dport, i);
      return 0;
    }
  }
  release(&netlock);
  return -1;
}

//
// unbind(int port)
// release any resources previously created by bind(port);
// from now on UDP packets addressed to port should be dropped.
//
uint64
sys_unbind(void)
{
  acquire(&netlock);
  int dport, i;
  struct proc *p = myproc();
  argint(0, &dport);
  for(i = 0; i < NPROC; i++) {
    if(bind_reps[i].pid == p->pid) {
      bind_reps[i].pid = 0;
      bind_reps[i].port = 0;

      packets_queues[i].nread = 0;
      packets_queues[i].nwrite = 0;
      for(int j = 0; j < PACKETS_QUEUE_SIZE; j++) {
        packets_queues[i].packets[j] = 0;
        packets_queues[i].packets_len[j] = 0;
      }
      release(&netlock);
      return 0;
    }
  }
  release(&netlock);
  return -1;
}

//
// recv(int dport, int *src, short *sport, char *buf, int maxlen)
// if there's a received UDP packet already queued that was
// addressed to dport, then return it.
// otherwise wait for such a packet.
//
// sets *src to the IP source address.
// sets *sport to the UDP source port.
// copies up to maxlen bytes of UDP payload to buf.
// returns the number of bytes copied,
// and -1 if there was an error.
//
// dport, *src, and *sport are host byte order.
// bind(dport) must previously have been called.
//
uint64
sys_recv(void)
{
  acquire(&netlock);
  int i, len;
  int dport;
  uint64 src;
  uint64 sport;
  uint64 buf;
  int maxlen;

  argint(0, &dport);
  argaddr(1, &src);
  argaddr(2, &sport);
  argaddr(3, &buf);
  argint(4, &maxlen);

  struct proc* p = myproc();
  for(i = 0; i < NPROC; i++) {
    //if(bind_reps[i].pid == p->pid && bind_reps[i].port == dport) {
    //父进程绑定了某端口，那么该父进程fork出的子进程依旧绑定该端口
    //其他进程不能绑定该端口
    //很遗憾，子进程继承端口功能并没有在此处实现
    //进而此处根本没有区分进程
    if(bind_reps[i].port == dport) {
      break;
    }
  }
  if(i == NPROC) {
    printf("sys_recv: Didn't find port %d, do you bind?\n", dport);
    release(&netlock);
    return -1;
  }

  //printf("sys_recv: find bind of port: %d  pid: %d  index in queues: %d\n", dport, p->pid, i);

  while(packets_queues[i].nwrite == packets_queues[i].nread) {  
    //printf("sys_recv: queue %d read position: %d\n", i, packets_queues[i].nread);
    //printf("sys_recv: queue %d write position: %d\n", i,  packets_queues[i].nwrite);
    //printf("sys_recv: sleep for queue %d of packets queues\n", i);
    sleep(&packets_queues[i], &netlock);
    //printf("sys_recv: sleep over for queue %d in packets queues\n", i);
    //printf("sys_recv: queue %d read position: %d\n", i, packets_queues[i].nread);
    //printf("sys_recv: queue %d write position: %d\n", i,  packets_queues[i].nwrite);
  }
    
  
  int packet_pos = packets_queues[i].nread % PACKETS_QUEUE_SIZE;
  char* packet = packets_queues[i].packets[packet_pos];
  
  //int packet_len = packets_queues[i].packets_len[packet_pos];
  packets_queues[i].nread++;

  struct eth* eth = (struct eth*)packet;
  struct ip* ip = (struct ip*)(eth + 1);
  uint32 ip_src = ntohl(ip->ip_src);
  //printf("sys_recv: ip src: %x\n", ip_src);
  if(copyout(p->pagetable, src, (char*)&ip_src, 4) == -1) {
    printf("sys_recv: copyout src failed\n");
    kfree((void*)packet);
    packets_queues[i].packets[packet_pos] = 0;
    release(&netlock);
    return -1;
  }

  struct udp* udp = (struct udp*)(ip + 1);
  uint16 udp_sport = ntohs(udp->sport);
  //printf("sys_recv: source port: %d\n", udp_sport);
  if(copyout(p->pagetable, sport, (char*)&udp_sport, 2) == -1) {
    printf("sys_recv: copyout sport failed\n");
    kfree((void*)packet);
    packets_queues[i].packets[packet_pos] = 0;
    release(&netlock);
    return -1;
  }


  char* payload = packet + sizeof(struct eth) + sizeof(struct ip) + sizeof(struct udp);
  uint16 ulen = ntohs(udp->ulen);
  int payload_len = ulen - sizeof(struct udp);
  len = maxlen > payload_len ? payload_len : maxlen;
  //printf("sys_recv: payload: %s\n", payload);
  //printf("sys_recv: payload len: %d\n", payload_len);
  if(copyout(p->pagetable, buf, payload, len) == -1) {
    printf("sys_recv: copyout buf failed\n");
    kfree((void*)packet);
    packets_queues[i].packets[packet_pos] = 0;
    release(&netlock);
    return -1;
  }

  //printf("after copyout buf\n");

  kfree((void*)packet);
  packets_queues[i].packets[packet_pos] = 0;
  release(&netlock);
  return len;
}

// This code is lifted from FreeBSD's ping.c, and is copyright by the Regents
// of the University of California.
static unsigned short
in_cksum(const unsigned char *addr, int len)
{
  int nleft = len;
  const unsigned short *w = (const unsigned short *)addr;
  unsigned int sum = 0;
  unsigned short answer = 0;

  /*
   * Our algorithm is simple, using a 32 bit accumulator (sum), we add
   * sequential 16 bit words to it, and at the end, fold back all the
   * carry bits from the top 16 bits into the lower 16 bits.
   */
  while (nleft > 1)  {
    sum += *w++;
    nleft -= 2;
  }

  /* mop up an odd byte, if necessary */
  if (nleft == 1) {
    *(unsigned char *)(&answer) = *(const unsigned char *)w;
    sum += answer;
  }

  /* add back carry outs from top 16 bits to low 16 bits */
  sum = (sum & 0xffff) + (sum >> 16);
  sum += (sum >> 16);
  /* guaranteed now that the lower 16 bits of sum are correct */

  answer = ~sum; /* truncate to 16 bits */
  return answer;
}

//
// send(int sport, int dst, int dport, char *buf, int len)
//
uint64
sys_send(void)
{
  struct proc *p = myproc();
  int sport;
  int dst;
  int dport;
  uint64 bufaddr;
  int len;

  argint(0, &sport);
  argint(1, &dst);
  argint(2, &dport);
  argaddr(3, &bufaddr);
  argint(4, &len);

  int total = len + sizeof(struct eth) + sizeof(struct ip) + sizeof(struct udp);
  if(total > PGSIZE)
    return -1;

  char *buf = kalloc();
  if(buf == 0){
    printf("sys_send: kalloc failed\n");
    return -1;
  }
  memset(buf, 0, PGSIZE);

  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, host_mac, ETHADDR_LEN);
  memmove(eth->shost, local_mac, ETHADDR_LEN);
  eth->type = htons(ETHTYPE_IP);

  struct ip *ip = (struct ip *)(eth + 1);
  ip->ip_vhl = 0x45; // version 4, header length 4*5
  ip->ip_tos = 0;
  ip->ip_len = htons(sizeof(struct ip) + sizeof(struct udp) + len);
  ip->ip_id = 0;
  ip->ip_off = 0;
  ip->ip_ttl = 100;
  ip->ip_p = IPPROTO_UDP;
  ip->ip_src = htonl(local_ip);
  ip->ip_dst = htonl(dst);
  ip->ip_sum = in_cksum((unsigned char *)ip, sizeof(*ip));

  struct udp *udp = (struct udp *)(ip + 1);
  udp->sport = htons(sport);
  udp->dport = htons(dport);
  udp->ulen = htons(len + sizeof(struct udp));

  char *payload = (char *)(udp + 1);
  if(copyin(p->pagetable, payload, bufaddr, len) < 0){
    kfree(buf);
    printf("send: copyin failed\n");
    return -1;
  }

  //printf("sys_send: total len: %d  payload len: %d\n", total, len);

  e1000_transmit(buf, total);

  return 0;
}

void
ip_rx(char *buf, int len)
{
  // don't delete this printf; make grade depends on it.
  static int seen_ip = 0;
  if(seen_ip == 0)
    printf("ip_rx: received an IP packet\n");
  seen_ip = 1;

  acquire(&netlock);
  int i;
  struct eth* eth = (struct eth*)buf;
  struct ip* ip = (struct ip*)(eth + 1);
  if(ip->ip_p != IPPROTO_UDP) {
    kfree(buf);
    release(&netlock);
    return;
  }
  struct udp* udp = (struct udp*)(ip + 1);
  for(i = 0; i < NPROC; i++) {
    if(bind_reps[i].pid != 0 && bind_reps[i].port == ntohs(udp->dport)) {
      break;
    }
  }
  if(i == NPROC) {
    kfree(buf);
    release(&netlock);
    return;
  }
  if(packets_queues[i].nwrite - packets_queues[i].nread >= PACKETS_QUEUE_SIZE) {
    kfree(buf);
    release(&netlock);
    return;
  }
  //printf("ip_rx: nread: %d\n", packets_queues[i].nread);
  //printf("ip_rx: nwrite: %d\n",  packets_queues[i].nwrite);
  int packet_pos = packets_queues[i].nwrite % PACKETS_QUEUE_SIZE;
  packets_queues[i].packets[packet_pos] = buf;
  packets_queues[i].packets_len[packet_pos] = len;
  packets_queues[i].nwrite++;
  wakeup(&packets_queues[i]);
  //printf("send wakeup signal of queue %d in packets queues\n", i);
  release(&netlock);
}

//
// send an ARP reply packet to tell qemu to map
// xv6's ip address to its ethernet address.
// this is the bare minimum needed to persuade
// qemu to send IP packets to xv6; the real ARP
// protocol is more complex.
//
// 用于建立arp缓存表，即ip地址和mac地址的映射关系
void
arp_rx(char *inbuf)
{
  static int seen_arp = 0;

  if(seen_arp){
    kfree(inbuf);
    return;
  }
  printf("arp_rx: received an ARP packet\n");
  seen_arp = 1;

  struct eth *ineth = (struct eth *) inbuf;
  struct arp *inarp = (struct arp *) (ineth + 1);

  char *buf = kalloc();
  if(buf == 0)
    panic("send_arp_reply");
  
  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, ineth->shost, ETHADDR_LEN); // ethernet destination = query source
  memmove(eth->shost, local_mac, ETHADDR_LEN); // ethernet source = xv6's ethernet address
  eth->type = htons(ETHTYPE_ARP);

  struct arp *arp = (struct arp *)(eth + 1);
  arp->hrd = htons(ARP_HRD_ETHER);
  arp->pro = htons(ETHTYPE_IP);
  arp->hln = ETHADDR_LEN;
  arp->pln = sizeof(uint32);
  arp->op = htons(ARP_OP_REPLY);

  memmove(arp->sha, local_mac, ETHADDR_LEN);
  arp->sip = htonl(local_ip);
  memmove(arp->tha, ineth->shost, ETHADDR_LEN);
  arp->tip = inarp->sip;

  e1000_transmit(buf, sizeof(*eth) + sizeof(*arp));

  kfree(inbuf);
}

void
net_rx(char *buf, int len)
{
  struct eth *eth = (struct eth *) buf;

  if(len >= sizeof(struct eth) + sizeof(struct arp) &&
     ntohs(eth->type) == ETHTYPE_ARP){
    arp_rx(buf);
  } else if(len >= sizeof(struct eth) + sizeof(struct ip) &&
     ntohs(eth->type) == ETHTYPE_IP){
    ip_rx(buf, len);
  } else {
    kfree(buf);
  }
}

void sys_memory_check() {
  int i;
  for(i = 0; i < NPROC; i++) {
    if(bind_reps[i].pid != 0) {
      printf("pakcets queue for port: %d\n", bind_reps[i].port);
      printf("number of read: %d\n", packets_queues[i].nread);
      printf("number of write: %d\n", packets_queues[i].nwrite);
      int j = 0;
      while(j < PACKETS_QUEUE_SIZE) {
        printf("  packet %d : %p\n", j, packets_queues[i].packets[j]);
        j++;
      }
    }
  }
}
