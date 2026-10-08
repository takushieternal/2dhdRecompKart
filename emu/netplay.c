// netplay.c: see netplay.h
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "netplay.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET sock_t;
#define CLOSESOCK closesocket
static uint64_t nowMs(void) { return GetTickCount64(); }
static void sleepMs(int ms) { Sleep(ms); }
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
typedef int sock_t;
#define INVALID_SOCKET (-1)
#define CLOSESOCK close
static uint64_t nowMs(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000; }
static void sleepMs(int ms) { usleep(ms * 1000); }
#endif

#define MAGIC 0x4E4B4D53u   // "SMKN"
#define VERSION 1
enum { P_HELLO = 1, P_WELCOME = 2, P_INPUT = 3, P_BYE = 4 };
#define RING 512             // frames of input history
#define REDUNDANCY 12        // each packet repeats the last N local inputs
#define TIMEOUT_MS 8000

struct Netplay {
  sock_t s;
  struct sockaddr_storage peer;
  socklen_t peerLen;
  bool host;
  int delay;
  uint32_t frame;            // next frame to run
  bool submitted;            // local input for frame+delay already queued
  uint16_t local[RING], remote[RING];
  uint32_t remoteFrame[RING];   // which frame each slot holds (UINT32_MAX = empty)
  uint32_t localTop;         // highest frame with a local input
  uint64_t lastSend, lastRecv;
  // desync detection
  uint32_t myHashFrame, peerHashFrame;
  uint64_t myHash, peerHash;
  bool desynced;
  uint8_t welcome[16 + 0x800];   // host: resent if the client's HELLO keeps coming
  int welcomeLen;
};

static void put32(uint8_t* p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static uint32_t get32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void put64(uint8_t* p, uint64_t v) { put32(p, (uint32_t)v); put32(p + 4, (uint32_t)(v >> 32)); }
static uint64_t get64(const uint8_t* p) { return get32(p) | (uint64_t)get32(p + 4) << 32; }

static bool sockInit(void) {
#ifdef _WIN32
  static bool done = false;
  if(!done) { WSADATA w; if(WSAStartup(MAKEWORD(2, 2), &w)) return false; done = true; }
#endif
  return true;
}

static void setNonBlocking(sock_t s) {
#ifdef _WIN32
  u_long one = 1; ioctlsocket(s, FIONBIO, &one);
#else
  fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
}

static int recvPkt(Netplay* n, uint8_t* buf, int cap, struct sockaddr_storage* from, socklen_t* fromLen) {
  *fromLen = sizeof(*from);
  int r = recvfrom(n->s, (char*)buf, cap, 0, (struct sockaddr*)from, fromLen);
  if(r < 9 || get32(buf) != MAGIC) return -1;
  return r;
}

static void sendTo(Netplay* n, const uint8_t* buf, int len) {
  sendto(n->s, (const char*)buf, len, 0, (struct sockaddr*)&n->peer, n->peerLen);
}

static Netplay* alloc(sock_t s, bool host, int delay) {
  Netplay* n = calloc(1, sizeof(Netplay));
  n->s = s; n->host = host; n->delay = delay < 1 ? 1 : delay > 30 ? 30 : delay;
  // frames before the first real input use neutral pads
  for(int f = 0; f < RING; f++) n->remoteFrame[f] = 0xffffffff;
  for(int f = 0; f < n->delay; f++) { n->local[f] = 0; n->remote[f] = 0; n->remoteFrame[f] = f; }
  n->localTop = n->delay - 1;
  n->myHashFrame = n->peerHashFrame = 0xffffffff;
  n->lastRecv = nowMs();
  return n;
}

Netplay* net_host(int port, int delay, uint32_t romCrc, uint8_t* sram, int sramLen, int timeoutMs, char* err, int errLen) {
  if(!sockInit()) { snprintf(err, errLen, "socket init failed"); return NULL; }
  sock_t s = socket(AF_INET, SOCK_DGRAM, 0);
  struct sockaddr_in a = {0};
  a.sin_family = AF_INET; a.sin_port = htons(port); a.sin_addr.s_addr = htonl(INADDR_ANY);
  if(s == INVALID_SOCKET || bind(s, (struct sockaddr*)&a, sizeof a) != 0) {
    snprintf(err, errLen, "cannot listen on UDP port %d", port); if(s != INVALID_SOCKET) CLOSESOCK(s); return NULL;
  }
  setNonBlocking(s);
  Netplay* n = alloc(s, true, delay);
  printf("netplay: waiting for player 2 on UDP port %d...\n", port);
  uint64_t start = nowMs();
  uint8_t buf[4096];
  while(timeoutMs <= 0 || nowMs() - start < (uint64_t)timeoutMs) {
    struct sockaddr_storage from; socklen_t fl;
    int r = recvPkt(n, buf, sizeof buf, &from, &fl);
    if(r >= 13 && buf[4] == P_HELLO) {
      if(buf[5] != VERSION || get32(buf + 9) != romCrc) {
        uint8_t bye[9]; put32(bye, MAGIC); bye[4] = P_BYE; memcpy(&n->peer, &from, fl); n->peerLen = fl;
        sendTo(n, bye, 9);
        printf("netplay: rejected a client (different version or ROM)\n");
        continue;
      }
      memcpy(&n->peer, &from, fl); n->peerLen = fl;
      // WELCOME: delay + SRAM
      uint8_t w[16 + 0x800];
      put32(w, MAGIC); w[4] = P_WELCOME; w[5] = VERSION; w[6] = n->delay; w[7] = 0; put32(w + 8, sramLen);
      int sl = sramLen > 0x800 ? 0x800 : sramLen;
      memcpy(w + 12, sram, sl);
      sendTo(n, w, 12 + sl);
      memcpy(n->welcome, w, 12 + sl); n->welcomeLen = 12 + sl;
      printf("netplay: player 2 connected\n");
      n->lastRecv = nowMs();
      return n;
    }
    sleepMs(5);
  }
  snprintf(err, errLen, "nobody joined");
  net_close(n);
  return NULL;
}

Netplay* net_join(const char* host, int port, uint32_t romCrc, uint8_t* sram, int sramLen, int timeoutMs, char* err, int errLen) {
  if(!sockInit()) { snprintf(err, errLen, "socket init failed"); return NULL; }
  struct addrinfo hints = {0}, *res = NULL;
  hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM;
  char ps[16]; snprintf(ps, sizeof ps, "%d", port);
  if(getaddrinfo(host, ps, &hints, &res) != 0 || !res) { snprintf(err, errLen, "cannot resolve %s", host); return NULL; }
  sock_t s = socket(AF_INET, SOCK_DGRAM, 0);
  if(s == INVALID_SOCKET) { freeaddrinfo(res); snprintf(err, errLen, "socket failed"); return NULL; }
  setNonBlocking(s);
  Netplay* n = alloc(s, false, 1);
  memcpy(&n->peer, res->ai_addr, res->ai_addrlen); n->peerLen = (socklen_t)res->ai_addrlen;
  freeaddrinfo(res);
  printf("netplay: connecting to %s:%d...\n", host, port);
  uint64_t start = nowMs(), last = 0;
  uint8_t buf[4096];
  while(timeoutMs <= 0 || nowMs() - start < (uint64_t)timeoutMs) {
    if(nowMs() - last > 200) {
      uint8_t h[13]; put32(h, MAGIC); h[4] = P_HELLO; h[5] = VERSION; h[6] = h[7] = h[8] = 0; put32(h + 9, romCrc);
      sendTo(n, h, 13); last = nowMs();
    }
    struct sockaddr_storage from; socklen_t fl;
    int r = recvPkt(n, buf, sizeof buf, &from, &fl);
    if(r >= 5 && buf[4] == P_BYE) { snprintf(err, errLen, "host refused: different ROM or version"); net_close(n); return NULL; }
    if(r >= 12 && buf[4] == P_WELCOME) {
      int delay = buf[6];
      Netplay* m = alloc(n->s, false, delay);
      memcpy(&m->peer, &n->peer, n->peerLen); m->peerLen = n->peerLen;
      free(n);
      int sl = get32(buf + 8);
      if(sl > 0 && sl <= sramLen && r >= 12 + sl) memcpy(sram, buf + 12, sl);
      printf("netplay: connected (input delay %d frames)\n", delay);
      return m;
    }
    sleepMs(5);
  }
  snprintf(err, errLen, "no answer from %s:%d", host, port);
  net_close(n);
  return NULL;
}

static void sendInputs(Netplay* n) {
  uint8_t p[64];
  uint32_t top = n->localTop;
  uint32_t first = top + 1 >= REDUNDANCY ? top + 1 - REDUNDANCY : 0;
  int cnt = top - first + 1;
  put32(p, MAGIC); p[4] = P_INPUT; put32(p + 5, first); p[9] = cnt;
  for(int i = 0; i < cnt; i++) { p[10 + i * 2] = n->local[(first + i) % RING]; p[11 + i * 2] = n->local[(first + i) % RING] >> 8; }
  int o = 10 + cnt * 2;
  put32(p + o, n->myHashFrame); put64(p + o + 4, n->myHash);
  sendTo(n, p, o + 12);
  n->lastSend = nowMs();
}

static void poll(Netplay* n) {
  uint8_t buf[4096];
  for(;;) {
    struct sockaddr_storage from; socklen_t fl;
    int r = recvPkt(n, buf, sizeof buf, &from, &fl);
    if(r < 0) break;
    n->lastRecv = nowMs();
    if(buf[4] == P_HELLO && n->host) {
      if(n->welcomeLen) sendTo(n, n->welcome, n->welcomeLen);   // our WELCOME got lost
      continue;
    }
    if(buf[4] == P_BYE) { n->lastRecv = 0; break; }
    if(buf[4] != P_INPUT || r < 10) continue;
    uint32_t first = get32(buf + 5); int cnt = buf[9];
    if(r < 10 + cnt * 2) continue;
    for(int i = 0; i < cnt; i++) {
      uint32_t f = first + i;
      if(f < n->frame) continue;                      // already ran
      if(f >= n->frame + RING / 2) continue;          // too far ahead (shouldn't happen)
      n->remote[f % RING] = buf[10 + i * 2] | buf[11 + i * 2] << 8;
      n->remoteFrame[f % RING] = f;
    }
    int o = 10 + cnt * 2;
    if(r >= o + 12) {
      uint32_t hf = get32(buf + o);
      if(hf != 0xffffffff) { n->peerHashFrame = hf; n->peerHash = get64(buf + o + 4); }
    }
  }
  if(n->myHashFrame != 0xffffffff && n->myHashFrame == n->peerHashFrame && n->myHash != n->peerHash) n->desynced = true;
}

int net_frame(Netplay* n, uint16_t localPad, uint16_t* p1, uint16_t* p2) {
  if(!n->submitted) {
    uint32_t f = n->frame + n->delay;
    n->local[f % RING] = localPad;
    n->localTop = f;
    n->submitted = true;
    sendInputs(n);
  }
  poll(n);
  if(nowMs() - n->lastSend > 15) sendInputs(n);
  if(nowMs() - n->lastRecv > TIMEOUT_MS) return -1;
  uint32_t f = n->frame;
  if(n->remoteFrame[f % RING] != f) return 0;
  uint16_t me = n->local[f % RING], them = n->remote[f % RING];
  *p1 = n->host ? me : them;
  *p2 = n->host ? them : me;
  n->frame++;
  n->submitted = false;
  return 1;
}

bool net_checkHash(Netplay* n, uint64_t hash) {
  uint32_t f = n->frame - 1;
  if(f % 60 == 0) { n->myHashFrame = f; n->myHash = hash; }
  if(n->myHashFrame == n->peerHashFrame && n->myHash != n->peerHash) n->desynced = true;
  return !n->desynced;
}

int net_delay(Netplay* n) { return n->delay; }
bool net_isHost(Netplay* n) { return n->host; }
uint32_t net_frameNo(Netplay* n) { return n->frame; }

void net_close(Netplay* n) {
  if(!n) return;
  uint8_t bye[9]; put32(bye, MAGIC); bye[4] = P_BYE;
  if(n->peerLen) sendTo(n, bye, 9);
  CLOSESOCK(n->s);
  free(n);
}
