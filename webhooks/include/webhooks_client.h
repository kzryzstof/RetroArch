#ifndef __WEBHOOKS_CLIENT_H
#define __WEBHOOKS_CLIENT_H

RETRO_BEGIN_DECLS

void wc_send_game_event
(
  unsigned int console_id,
  const char* rom_hash,
  unsigned short game_event,
  unsigned long frame_number,
  retro_time_t time,
  void* on_game_event_sent_callback
);

void wc_send_achievement_event
(
  unsigned int console_id,
  const char* rom_hash,
  unsigned short game_event,
  unsigned int active_achievements,
  unsigned int total_achievements,
  const char* achievement_badge,
  const char* achievement_title,
  unsigned int achievement_points,
  unsigned long frame_number,
  retro_time_t time
);

void wc_send_keep_alive_event
(
  unsigned int console_id,
  const char* rom_hash,
  unsigned short game_event,
  unsigned long frame_number,
  retro_time_t time
);

void wc_update_progress
(
  unsigned int console_id,
  const char* rom_hash,
  const char* progress,
  unsigned long frame_number,
  retro_time_t time
);

RETRO_END_DECLS

#endif /* __WEBHOOKS_CLIENT_H */