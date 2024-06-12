#ifndef __WEBHOOKS_CLIENT_H
#define __WEBHOOKS_CLIENT_H

#include "webhooks.h"

RETRO_BEGIN_DECLS

typedef struct wc_game_event_t
{
  unsigned int console_id;
  const char* rom_hash;
  unsigned short game_event_id;
  unsigned long frame_number;
  retro_time_t time;
  long awarded_achievements[MAX_ACHIEVEMENTS];
  unsigned int total_achievements;
} wc_game_event_t;

void wc_send_game_event
(
  const char* access_token,
  wc_game_event_t game_event,
  void* on_game_event_sent_callback
);

void wc_send_achievement_event
(
  const char* access_token,
  wc_game_event_t game_event,
  void* on_game_event_sent_callback
);

void wc_send_keep_alive_event
(
  const char* access_token,
  wc_game_event_t game_event
);

void wc_update_progress
(
  const char* access_token,
  wc_game_event_t game_event,
  const char* progress
);

RETRO_END_DECLS

#endif /* __WEBHOOKS_CLIENT_H */