// netplay: delay-based lockstep netplay for two players over UDP.
//
// Both sides run the same deterministic emulation. Every frame each side sends its controller
// state for frame f+delay; frame f only runs once both inputs for f are known. The host is
// player 1, the client player 2. On connect the host sends its SRAM and both machines power-cycle,
// so they start from identical state. A WRAM hash is exchanged every 60 frames to detect desyncs.
#ifndef NETPLAY_H
#define NETPLAY_H
#include <stdint.h>
#include <stdbool.h>

typedef struct Netplay Netplay;

// host: listen on port. client: connect to host:port. Blocks until connected (or timeout_ms).
// sram/sramLen: host sends its SRAM, client receives into it. Returns NULL on failure (err set).
Netplay* net_host(int port, int delay, uint32_t romCrc, uint8_t* sram, int sramLen, int timeoutMs, char* err, int errLen);
Netplay* net_join(const char* host, int port, uint32_t romCrc, uint8_t* sram, int sramLen, int timeoutMs, char* err, int errLen);

// Submit the local pad for the current frame and fetch both pads for the frame to run.
// Returns 1 when inputs are ready (p1/p2 set), 0 if still waiting (call again), -1 if disconnected.
int net_frame(Netplay* n, uint16_t localPad, uint16_t* p1, uint16_t* p2);
// Report this frame's state hash (call after running the frame). Returns false on desync.
bool net_checkHash(Netplay* n, uint64_t hash);
int net_delay(Netplay* n);
bool net_isHost(Netplay* n);
uint32_t net_frameNo(Netplay* n);
void net_close(Netplay* n);

#endif
