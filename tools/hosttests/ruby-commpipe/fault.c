/* LD_PRELOAD: after FAULT_AT_MS, pipe fds created before that behave like aborted sockets.
 * FAULT_MODE=err : read -> -1/ECONNABORTED, poll reports them ready (POLLERR|POLLHUP)
 * FAULT_MODE=eof : read -> 0,               poll reports them ready (POLLHUP)
 * FAULT_MODE=block: read -> -1/ECONNABORTED, poll never reports them (writes fail)
 * Writes to dead fds -> -1/EPIPE. Also counts 1-byte '!' writes to non-pipe fds (misdirected). */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
static int kind[1024];          /* 0 none, 1 comm pipe (pre-fault), 2 comm pipe (post-fault) */
static long t0ms = -1, faultAt = -1, period = 0; static int mode = 0;
static int nPipe, nDeadRead, nDeadWrite, nMisdirected, nPollDead, nPollDeadLate;
static long nowms(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return ts.tv_sec*1000+ts.tv_nsec/1000000; }
static void init(void){ if (t0ms>=0) return; t0ms=nowms(); const char*a=getenv("FAULT_AT_MS"); faultAt=a?atol(a):-1; const char*m=getenv("FAULT_MODE"); mode=!m?0:!strcmp(m,"eof")?1:!strcmp(m,"block")?2:0; const char*pp=getenv("FAULT_PERIOD_MS"); period=pp?atol(pp):0; }
static int gen(void){ init(); if (faultAt<0) return 0; long t=nowms()-t0ms-faultAt; if (t<0) return 0; { int g = period>0 ? (int)(t/period)+1 : 1; const char*c=getenv("FAULT_COUNT"); if (c && g>atoi(c)) g=atoi(c); return g; } }
static int faulted(void){ return gen()>0; }
static int dead(int fd){ return fd>=0 && fd<1024 && kind[fd]>0 && kind[fd]<=gen(); }
__attribute__((destructor)) static void fini(void){ fprintf(stderr,"FAULT_STATS pipes=%d dead_reads=%d dead_writes=%d dead_polls=%d misdirected_wakeups=%d dead_polls_after_1200ms=%d\n",nPipe,nDeadRead,nDeadWrite,nPollDead,nMisdirected,nPollDeadLate); }
int pipe2(int p[2], int flags){ static int (*r)(int*,int); if(!r) r=dlsym(RTLD_NEXT,"pipe2"); int x=r(p,flags); if(!x){ int k=gen()+1; if(p[0]<1024)kind[p[0]]=k; if(p[1]<1024)kind[p[1]]=k; __sync_fetch_and_add(&nPipe,1);} return x; }
int close(int fd){ static int (*r)(int); if(!r) r=dlsym(RTLD_NEXT,"close"); if(fd>=0&&fd<1024) kind[fd]=0; return r(fd); }
ssize_t read(int fd, void*b, size_t n){ static ssize_t (*r)(int,void*,size_t); if(!r) r=dlsym(RTLD_NEXT,"read"); if(dead(fd)){ __sync_fetch_and_add(&nDeadRead,1); if(mode==1) return 0; errno=ECONNABORTED; return -1;} return r(fd,b,n); }
ssize_t write(int fd, const void*b, size_t n){ static ssize_t (*r)(int,const void*,size_t); if(!r) r=dlsym(RTLD_NEXT,"write"); if(dead(fd)){ __sync_fetch_and_add(&nDeadWrite,1); errno=EPIPE; return -1;} if(n==1 && ((const char*)b)[0]=='!' && fd>2 && fd<1024 && kind[fd]==0){ struct stat st; if(fstat(fd,&st)==0 && !S_ISFIFO(st.st_mode)) __sync_fetch_and_add(&nMisdirected,1);} return r(fd,b,n); }
static int deadset(struct pollfd*f, nfds_t n){ int d=0; for(nfds_t i=0;i<n;i++) if(dead(f[i].fd)) d++; return d; }
int poll(struct pollfd*f, nfds_t n, int to){ static int (*r)(struct pollfd*,nfds_t,int); if(!r) r=dlsym(RTLD_NEXT,"poll");
  if(deadset(f,n)){ __sync_fetch_and_add(&nPollDead,1); if(nowms()-t0ms>1200) __sync_fetch_and_add(&nPollDeadLate,1);
    if(mode!=2){ int c=0; for(nfds_t i=0;i<n;i++){ f[i].revents=0; if(dead(f[i].fd)){ f[i].revents=POLLHUP|(mode==0?POLLERR:0)|POLLIN; c++; } } return c; }
    /* block: poll only the live fds (dead ones never ready) */
    struct pollfd g[16]; nfds_t m=0; for(nfds_t i=0;i<n&&m<16;i++){ g[m]=f[i]; if(dead(f[i].fd)) g[m].fd=-1; m++; } int x=r(g,m,to); for(nfds_t i=0;i<n;i++) f[i].revents=g[i].revents; return x; }
  return r(f,n,to); }
int ppoll(struct pollfd*f, nfds_t n, const struct timespec*ts, const sigset_t*ss){ static int (*r)(struct pollfd*,nfds_t,const struct timespec*,const sigset_t*); if(!r) r=dlsym(RTLD_NEXT,"ppoll");
  if(deadset(f,n) && mode!=2){ __sync_fetch_and_add(&nPollDead,1); int c=0; for(nfds_t i=0;i<n;i++){ f[i].revents=0; if(dead(f[i].fd)){ f[i].revents=POLLHUP|(mode==0?POLLERR:0)|POLLIN; c++; } } return c; }
  if(deadset(f,n)){ __sync_fetch_and_add(&nPollDead,1); struct pollfd g[16]; nfds_t m=0; for(nfds_t i=0;i<n&&m<16;i++){ g[m]=f[i]; if(dead(f[i].fd)) g[m].fd=-1; m++; } int x=r(g,m,ts,ss); for(nfds_t i=0;i<n;i++) f[i].revents=g[i].revents; return x; }
  return r(f,n,ts,ss); }
void rb_vita_comm_pipe_event(const char *what, int fd, int err, int nr, int nw)
{ fprintf(stderr, "REPAIR %ldms %s fd=%d err=%d kind=%d -> %d,%d\n", nowms()-t0ms, what, fd, err, fd>=0&&fd<1024?kind[fd]:-1, nr, nw); }
