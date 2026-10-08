# C overrides

Each `.c` file here can replace individual instructions (usually the first instruction of a routine) with hand-written C:

```c
#include "smk.h"
SMK_OVERRIDE(0x81F108, short_races_lap_init)
void short_races_lap_init(Cpu* c) { ... c->pc = 0xF10B; }
```

Run `python tools/recomp.py smk.sfc trace/out recomp/gen` and rebuild. Files whose name starts with `_` are ignored by the build: rename one to turn a mod on. Overrides change timing, so `verify_recomp.py` will (correctly) report differences while any are active.
