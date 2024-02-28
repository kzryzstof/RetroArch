#ifndef __WEBHOOKS_CLIENT_H
#define __WEBHOOKS_CLIENT_H

RETRO_BEGIN_DECLS

typedef struct wc_game_event_t
{
  unsigned int console_id;
  const char* rom_hash;
  unsigned short game_event_id;
  unsigned long frame_number;
  retro_time_t time;
} wc_game_event_t;

typedef struct wc_achievement_event_t
{
  unsigned int active;
  unsigned int total;
  const char* badge;
  const char* title;
  unsigned int points;
} wc_achievement_event_t;

void wc_send_game_event
(
  wc_game_event_t game_event,
  void* on_game_event_sent_callback
);

void wc_send_achievement_event
(
  wc_game_event_t game_event,
  wc_achievement_event_t achievement_event
);

void wc_send_keep_alive_event
(
  wc_game_event_t game_event
);

void wc_update_progress
(
  wc_game_event_t game_event,
  const char* progress
);

RETRO_END_DECLS

#endif /* __WEBHOOKS_CLIENT_H */