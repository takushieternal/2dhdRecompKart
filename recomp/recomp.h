#ifndef RECOMP_H
#define RECOMP_H
#include <stdint.h>
// Installs the recompiled-code hook. Each Cpu opts in with cpu->useRecomp = true.
void recomp_install(void);
extern uint64_t recomp_calls;
#include <stdio.h>
extern FILE* recomp_ilog;
#endif
