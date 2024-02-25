#ifndef __WEBHOOKS_MEMORY_DUMP_H
#define __WEBHOOKS_MEMORY_DUMP_H

#include "include/webhooks.h"

RETRO_BEGIN_DECLS

#include "rc_runtime_types.h"

void wmp_on_game_loaded
(
    const char* rom_hash
);

void wmp_on_game_unloaded
(
  void
);

void wmp_dump
(
    unsigned long frame_counter,
    wb_locals_t* locals
);

RETRO_END_DECLS

#endif /* __WEBHOOKS_MEMORY_DUMP_H */
