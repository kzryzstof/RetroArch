#include <file/file_path.h>
#include <string/stdstring.h>
#include <streams/interface_stream.h>
#include <streams/file_stream.h>
#include <net/net_http.h>
#include <libretro.h>
#include <lrc_hash.h>
#include <limits.h>

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include "include/webhooks.h"
#include "include/webhooks_client.h"
#include "include/webhooks_memory_dump.h"
#include "include/webhooks_progress_downloader.h"
#include "include/webhooks_progress_parser.h"
#include "include/webhooks_progress_tracker.h"
#include "include/webhooks_oauth.h"

#include "../deps/rcheevos/include/rc_api_runtime.h"
#include "../deps/rcheevos/include/rc_runtime.h"
#include "../deps/rcheevos/include/rc_runtime_types.h"
#include "../deps/rcheevos/include/rc_hash.h"
#include "../deps/rcheevos/src/rcheevos/rc_internal.h"

#include "../cheevos/cheevos_locals.h"

#include "../runloop.h"

struct wb_identify_game_data_t
{
  rc_hash_iterator_t iterator;
  char* path;
  uint8_t* datacopy;
  char hash[HASH_LENGTH];
};

//  Contains all the shared state for all the webhook modules.
wb_locals_t locals;

bool is_game_loaded = false;
bool is_access_token_valid = false;
retro_time_t last_update_time;
unsigned long frame_counter = 0;

wc_game_event_t queued_game_event;
wc_game_event_t queued_achievement_game_event;
wc_achievement_event_t queued_achievement;

const unsigned short NONE = 0;
const unsigned short LOADED = 1;
const unsigned short STARTED = 2;
const unsigned short ACHIEVEMENT = 3;
const unsigned short KEEP_ALIVE = 4;
const unsigned short UNLOADED = USHRT_MAX;
const unsigned long PROGRESS_UPDATE_FRAME_FREQUENCY = 30;

void clean_queued_game_event
(
  void
)
{
  queued_game_event.console_id = 0;
  queued_game_event.rom_hash = "";
  queued_game_event.game_event_id = NONE;
  queued_game_event.frame_number = 0;
  queued_game_event.time = 0;
}

void clean_queued_achievement_game_event
(
  void
)
{
  queued_achievement_game_event.console_id = 0;
  queued_achievement_game_event.rom_hash = "";
  queued_achievement_game_event.game_event_id = NONE;
  queued_achievement_game_event.frame_number = 0;
  queued_achievement_game_event.time = 0;
}
//  ---------------------------------------------------------------------------
//
//  ---------------------------------------------------------------------------
unsigned wb_peek
(
  unsigned address,
  unsigned num_bytes,
  void* ud
)
{
  unsigned avail;
  uint8_t* data = rc_libretro_memory_find_avail(&locals.memory, address, &avail);

  if (data && avail >= num_bytes)
  {
    switch (num_bytes)
    {
      case 4:
        return (data[3] << 24) | (data[2] << 16) | (data[1] <<  8) | (data[0]);
      case 3:
        return (data[2] << 16) | (data[1] << 8) | (data[0]);
      case 2:
        return (data[1] << 8)  | (data[0]);
      case 1:
        return data[0];
    }
  }

  return 0;
}

static void* wb_on_hash_handle_file_open
(
  const char* path
)
{
  return intfstream_open_file(path, RETRO_VFS_FILE_ACCESS_READ, RETRO_VFS_FILE_ACCESS_HINT_NONE);
}

static void wb_on_hash_handle_file_seek
(
  void* file_handle,
  int64_t offset,
  int origin
)
{
  intfstream_seek((intfstream_t*)file_handle, offset, origin);
}

static int64_t wb_on_hash_handle_file_tell
(
  void* file_handle
)
{
  return intfstream_tell((intfstream_t*)file_handle);
}

static size_t wb_on_hash_handle_file_read
(
  void* file_handle,
  void* buffer,
  size_t requested_bytes
)
{
  return intfstream_read((intfstream_t*)file_handle, buffer, requested_bytes);
}

static void wb_on_hash_handle_file_close(void* file_handle)
{
  intfstream_close((intfstream_t*)file_handle);
  CHEEVOS_FREE(file_handle);
}

//  ---------------------------------------------------------------------------
//  Computes the hash of the loaded ROM.
//  ---------------------------------------------------------------------------
static void wh_compute_hash
(
  const struct retro_game_info* info
)
{
  struct wb_identify_game_data_t* data;
  struct rc_hash_filereader file_reader;
  
  data = (struct wb_identify_game_data_t*) malloc(sizeof(struct wb_identify_game_data_t));

  if (!data)
  {
    WEBHOOKS_LOG(WEBHOOKS_TAG "Unable to compute hash of the ROM: allocation failed\n");
    return;
  }

  //  Provides hooks for reading files
  memset(&file_reader, 0, sizeof(file_reader));
  file_reader.open = wb_on_hash_handle_file_open;
  file_reader.seek = wb_on_hash_handle_file_seek;
  file_reader.tell = wb_on_hash_handle_file_tell;
  file_reader.read = wb_on_hash_handle_file_read;
  file_reader.close = wb_on_hash_handle_file_close;
  rc_hash_init_custom_filereader(&file_reader);

  //rc_hash_init_error_message_callback(rcheevos_handle_log_message);

  //  TODO: Ignored for now: obj-unix/release/webhooks/webhooks.o:webhooks.c:(.text+0x27f): undefined reference to `rc_hash_reset_cdreader_hooks'
  //  rc_hash_reset_cdreader_hooks();

  //  Fetch the first hash
  rc_hash_initialize_iterator(&data->iterator, info->path, (uint8_t*)info->data, info->size);

  if (!rc_hash_iterate(data->hash, &data->iterator))
  {
    WEBHOOKS_LOG(WEBHOOKS_TAG "No hashes found\n");
    rc_hash_destroy_iterator(&data->iterator);
    free(data);
    return;
  }

  memcpy(locals.hash, data->hash, HASH_LENGTH);
  locals.console_id = data->iterator.consoles[0];

  WEBHOOKS_LOG(WEBHOOKS_TAG "Current game's hash is '%s'\n", locals.hash);
}

//  ---------------------------------------------------------------------------
//
//  ---------------------------------------------------------------------------
static void wh_get_core_memory_info
(
  unsigned id,
  rc_libretro_core_memory_info_t* info
)
{
  retro_ctx_memory_info_t ctx_info;

  if (!info)
    return;

  ctx_info.id = id;
  if (core_get_memory(&ctx_info))
  {
    info->data = (unsigned char*)ctx_info.data;
    info->size = ctx_info.size;
  }
  else
  {
    info->data = NULL;
    info->size = 0;
  }
}

//  ---------------------------------------------------------------------------
//  Initialize wb_locals_t's memory field.
//  ---------------------------------------------------------------------------
static int wh_init_memory
(
  wb_locals_t* locals
)
{
  unsigned i;
  int result;
  struct retro_memory_map mmap;
  rarch_system_info_t *sys_info               = &runloop_state_get_ptr()->system;
  rarch_memory_map_t *mmaps                   = &sys_info->mmaps;
  struct retro_memory_descriptor *descriptors = (struct retro_memory_descriptor*)malloc(mmaps->num_descriptors * sizeof(*descriptors));

  if (!descriptors)
    return 0;

  mmap.descriptors = &descriptors[0];
  mmap.num_descriptors = mmaps->num_descriptors;

  for (i = 0; i < mmap.num_descriptors; ++i)
    memcpy(&descriptors[i], &mmaps->descriptors[i].core, sizeof(descriptors[0]));

  result = rc_libretro_memory_init
  (
    &locals->memory,
    &mmap,
    wh_get_core_memory_info,
    locals->console_id
  );

  free(descriptors);
  return result;
}

//  ---------------------------------------------------------------------------
//
//  ---------------------------------------------------------------------------
static void wb_check_progress
(
  unsigned long frame_counter,
  retro_time_t time
)
{
  int result = wpt_process_frame(&locals.runtime);

  if (result != PROGRESS_UNCHANGED) {

    wc_game_event_t progress_game_event;
    progress_game_event.console_id = locals.console_id;
    progress_game_event.rom_hash = locals.hash;
    progress_game_event.game_event_id = 0;
    progress_game_event.frame_number = frame_counter;
    progress_game_event.time = time;
    
    wc_update_progress
    (
      locals.access_token,
      progress_game_event,
      wpt_get_last_progress()
    );
  }
  else {

    if (is_game_loaded && is_access_token_valid) {

      retro_time_t current_time = cpu_features_get_time_usec();

      long long elapsed_time = current_time - last_update_time;

      if (elapsed_time >= 60000000) {

        wc_game_event_t keep_alive_event;
        keep_alive_event.console_id = locals.console_id;
        keep_alive_event.rom_hash = locals.hash;
        keep_alive_event.game_event_id = KEEP_ALIVE;
        keep_alive_event.frame_number = frame_counter;
        keep_alive_event.time = time;
        
        wc_send_keep_alive_event
        (
          locals.access_token,
          keep_alive_event
        );

        last_update_time = current_time;
      }
    }
  }
}

//  ---------------------------------------------------------------------------
//
//  ---------------------------------------------------------------------------
static void wb_check_game_events
(
  unsigned long frame_counter,
  retro_time_t time
)
{
  rc_runtime_trigger_t* triggers = locals.runtime.triggers;

  for (uint32_t trigger_num = 0; trigger_num < locals.runtime.trigger_count; ++trigger_num, ++triggers) {
    rc_trigger_t* trigger = triggers->trigger;
    int result = rc_evaluate_trigger(trigger, &wb_peek, NULL, 0);

    if (result == RC_TRIGGER_STATE_TRIGGERED) {
      int event_id = locals.runtime.triggers[trigger_num].id;
      
      wc_game_event_t game_event;
      game_event.console_id = locals.console_id;
      game_event.rom_hash = locals.hash;
      game_event.game_event_id = event_id;
      game_event.frame_number = frame_counter;
      game_event.time = time;
      
      wc_send_game_event
      (
        locals.access_token,
        game_event,
        NULL
     );
    }
  }
}

//  ---------------------------------------------------------------------------
//
//  ---------------------------------------------------------------------------
static void wb_reset_game_events
(
 void
)
{
  rc_runtime_trigger_t* triggers = locals.runtime.triggers;
  for (uint32_t trigger_num = 0; trigger_num < locals.runtime.trigger_count; ++trigger_num, ++triggers) {
    rc_trigger_t* trigger = triggers->trigger;
    rc_reset_trigger(trigger);
  }
}



//  ---------------------------------------------------------------------------
//  Called when the game's progress & events have been received from the backend server.
//  ---------------------------------------------------------------------------
static void wh_on_game_progress_downloaded
(
  wb_locals_t* locals,
  const char* game_progress,
  size_t length
)
{
  wpp_parse_game_progress
  (
    game_progress,
    &locals->runtime
  );
}

//  ---------------------------------------------------------------------------
//  Called when the ACHIEVEMENT game event has been sent
//  ---------------------------------------------------------------------------
void webhooks_on_achievement_game_event_sent
(
  void
)
{
  clean_queued_achievement_game_event();
}

//  ---------------------------------------------------------------------------
//  Called when the LOADED game event has been successfully sent.
//  ---------------------------------------------------------------------------
void webhooks_on_loaded_game_event_sent
(
  void
)
{
  clean_queued_game_event();

  if (queued_achievement_game_event.game_event_id == ACHIEVEMENT)
  {
    //  Immediately sends the achievement even if
    //  the progress macro have not been downloaded yet.
    wc_send_achievement_event
    (
      locals.access_token,
      queued_achievement_game_event,
      queued_achievement,
      &webhooks_on_achievement_game_event_sent
    );
  }

  wpd_download_game_progress
  (
    &locals,
    &wh_on_game_progress_downloaded
  );

  wmd_on_game_loaded
  (
    locals.hash
  );

  is_game_loaded = true;
  
  //  TODO Send task to notify user everything is ok with Play Lab server
}

//  ---------------------------------------------------------------------------
//  Called when the UNLOADED game event has been sent
//  ---------------------------------------------------------------------------
void webhooks_on_unloaded_game_event_sent
(
  void
)
{
  clean_queued_game_event();
  
  wmd_on_game_unloaded();

  is_game_loaded = false;
  
  wpd_download_game_progress
  (
    &locals,
    &wh_on_game_progress_downloaded
  );
}

//  ---------------------------------------------------------------------------
//  Called when the LOADED game event has been successfully sent.
//  ---------------------------------------------------------------------------
void webhooks_on_access_token_received
(
  const char* access_token
)
{
  locals.access_token = access_token;
  is_access_token_valid = true;
  
  if (queued_game_event.game_event_id == LOADED)
  {
    wc_send_game_event
    (
      locals.access_token,
      queued_game_event,
      &webhooks_on_loaded_game_event_sent
    );
  }
  else if (queued_game_event.game_event_id == UNLOADED)
  {
    wc_send_game_event
    (
      locals.access_token,
      queued_game_event,
      &webhooks_on_unloaded_game_event_sent
    );
  }
}

//  ---------------------------------------------------------------------------
//  Initializes the webhooks subsystem.
//  ---------------------------------------------------------------------------
void webhooks_initialize
(
  void
)
{
  if (locals.initialized) {
    return;
  }

  WEBHOOKS_LOG(WEBHOOKS_TAG "Initializing\n");

  rc_runtime_init(&locals.runtime);

  clean_queued_game_event();
  
  woauth_load_accesstoken
  (
    (access_token_callback_t)&webhooks_on_access_token_received
  );

  locals.initialized = true;
  
  WEBHOOKS_LOG(WEBHOOKS_TAG "Initialized\n");
}

//  ---------------------------------------------------------------------------
//  Called when a new game is loaded in the emulator.
//  ---------------------------------------------------------------------------
void webhooks_load_game
(
  const struct retro_game_info* info
)
{
  WEBHOOKS_LOG(WEBHOOKS_TAG "New game loaded: %s\n", info->path);

  frame_counter = 0;

  const retro_time_t time = cpu_features_get_time_usec();

  wh_compute_hash(info);

  wh_init_memory(&locals);

  wpt_clear_progress();

  if (strlen(locals.hash) > 0) {
    
    queued_game_event.console_id = locals.console_id;
    queued_game_event.rom_hash = locals.hash;
    queued_game_event.game_event_id = LOADED;
    queued_game_event.frame_number = frame_counter;
    queued_game_event.time = time;
    
    if (!is_access_token_valid) {
      WEBHOOKS_LOG(WEBHOOKS_TAG "Access token is not available at this point. No LOADED game event sent.\n");
      return;
    }
    
    wc_send_game_event
    (
      locals.access_token,
      queued_game_event,
      &webhooks_on_loaded_game_event_sent
    );
  }
}

//  ---------------------------------------------------------------------------
//  Called when a game is being unloaded.
//  ---------------------------------------------------------------------------
void webhooks_unload_game
(
 void
)
{
  WEBHOOKS_LOG(WEBHOOKS_TAG "Current game has been unloaded\n");
  
  const retro_time_t time = cpu_features_get_time_usec();
  
  if (strlen(locals.hash) > 0) {
    
    queued_game_event.console_id = locals.console_id;
    queued_game_event.rom_hash = locals.hash;
    queued_game_event.game_event_id = UNLOADED;
    queued_game_event.frame_number = frame_counter;
    queued_game_event.time = time;
    
    if (!is_access_token_valid) {
      WEBHOOKS_LOG(WEBHOOKS_TAG "Access token is not available at this point. No UNLOADED game event sent.\n");
      return;
    }
    
    wc_send_game_event
    (
      locals.access_token,
      queued_game_event,
      &webhooks_on_unloaded_game_event_sent
    );
  }
}

//  ---------------------------------------------------------------------------
//  Called when the game is being reset.
//  ---------------------------------------------------------------------------
void webhooks_reset_game
(
 void
)
{
  WEBHOOKS_LOG(WEBHOOKS_TAG "Current game has been reset\n");

  frame_counter = 0;

  const retro_time_t time = cpu_features_get_time_usec();

  wb_reset_game_events();

  wpt_clear_progress();

  if (strlen(locals.hash) > 0) {

    queued_game_event.console_id = locals.console_id;
    queued_game_event.rom_hash = locals.hash;
    queued_game_event.game_event_id = LOADED;
    queued_game_event.frame_number = frame_counter;
    queued_game_event.time = time;

    if (!is_access_token_valid) {
      WEBHOOKS_LOG(WEBHOOKS_TAG "Access token is not available at this point. No UNLOADED/LOADED game event sent.\n");
      return;
    }

    queued_game_event.game_event_id = UNLOADED;

    wc_send_game_event
    (
      locals.access_token,
      queued_game_event,
      &webhooks_on_unloaded_game_event_sent
    );

    queued_game_event.game_event_id = LOADED;

    wc_send_game_event
    (
      locals.access_token,
      queued_game_event,
      &webhooks_on_loaded_game_event_sent
    );
  }

  wmd_on_game_unloaded();

  wmd_on_game_loaded(locals.hash);
}


//  ---------------------------------------------------------------------------
//  Called for each frame.
//  ---------------------------------------------------------------------------
void webhooks_process_frame
 (
   void
 )
{
  if (!is_game_loaded)
    return;
  
   frame_counter++;

  if (!is_access_token_valid) {
    WEBHOOKS_LOG(WEBHOOKS_TAG "Access token is not available at this point. No PROGRESS game event sent.\n");
    return;
  }

   const retro_time_t time = cpu_features_get_time_usec();

   //  Gets the latest values from the memory.
   rc_update_memref_values(locals.runtime.memrefs, &wb_peek, NULL);
   rc_update_variables(locals.runtime.variables, &wb_peek, NULL, NULL);

   wb_check_game_events(frame_counter, time);

   wb_check_progress(frame_counter, time);

   wmd_dump(frame_counter, &locals);
}

//  ---------------------------------------------------------------------------
//  
//  ---------------------------------------------------------------------------
void webhooks_update_achievements
(
  const rcheevos_racheevo_t* cheevo,
  const char* achievement_id,
  const char* achievement_title,
  unsigned int achievement_points
)
{
  int number_of_active  = 0;
  int total_number      = 0;

  const retro_time_t time = cpu_features_get_time_usec();

  //  Only deals with supported & official achievements.
  const rcheevos_racheevo_t* current_achievement = locals.current_achievement;

  for (; current_achievement < locals.last_achievement; current_achievement++)
  {
    if (current_achievement->active & RCHEEVOS_ACTIVE_UNOFFICIAL)
      continue;

    if (current_achievement->active & RCHEEVOS_ACTIVE_UNSUPPORTED)
      continue;

    total_number++;

    if (current_achievement->active)
      number_of_active++;
  }

  queued_achievement_game_event.console_id = locals.console_id;
  queued_achievement_game_event.rom_hash = locals.hash;
  queued_achievement_game_event.game_event_id = ACHIEVEMENT;
  queued_achievement_game_event.frame_number = frame_counter;
  queued_achievement_game_event.time = time;
  
  queued_achievement.active = number_of_active;
  queued_achievement.total = total_number;
  queued_achievement.badge = achievement_id;
  queued_achievement.title = achievement_title;
  queued_achievement.points = achievement_points;
  
  if (!is_access_token_valid) {
    WEBHOOKS_LOG(WEBHOOKS_TAG "Access token is not available at this point. No ACHIEVEMENT game event sent.\n");
    return;
  }
  
  wc_send_achievement_event
  (
    locals.access_token,
    queued_achievement_game_event,
    queued_achievement,
    &webhooks_on_achievement_game_event_sent
  );
}

void webhooks_on_achievements_loaded
(
  const rcheevos_racheevo_t* achievements,
  const unsigned int achievements_count
)
{
   locals.current_achievement = achievements;
   locals.last_achievement    = achievements + achievements_count;

   webhooks_update_achievements
   (
     achievements,
     achievements->badge,
     achievements->title,
     achievements->points
   );
}

void webhooks_on_achievement_awarded
(
  const rcheevos_racheevo_t* cheevo
)
{
   webhooks_update_achievements
   (
     cheevo,
     cheevo->badge,
     cheevo->title,
     cheevo->points
   );
}
