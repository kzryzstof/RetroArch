#include "include/webhooks_memory_dump.h"
#include "include/webhooks.h"

#include <stdio.h>
#include <errno.h>

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include "../paths.h"
#include "../input/input_driver.h"
#include "../libretro-common/include/libretro.h"

//    TODO These parameters are specific to NES ROMS.
const uint32_t memory_start = 0x000000;
const uint32_t memory_size = 0x0007FF;
char dump_file_name[] = "dump.bin";

uint8_t *memory_content = NULL;
FILE *file;

#define DUMP_ACTIVATED false

void wmd_on_game_loaded
(
    const char* rom_hash
)
{
  if(!DUMP_ACTIVATED)
    return;

  const char *save_file_dir = dir_get_ptr(RARCH_DIR_SAVEFILE);
  char *dump_file = (char*)malloc(strlen(save_file_dir) + strlen(dump_file_name) + 2);

  strcat(dump_file, save_file_dir);
  strcat(dump_file, "/");
  strcat(dump_file, dump_file_name);

  file = fopen(dump_file, "w+");

  unsigned long rom_hash_length = strlen(rom_hash);

  fwrite(&rom_hash_length, sizeof(unsigned long), 1, file);
  fwrite(rom_hash, sizeof(char), rom_hash_length, file);

  if (memory_content == NULL)
      memory_content = (uint8_t*)malloc(sizeof(uint8_t) * memory_size);
}

void wmd_on_game_unloaded
(
  void
)
{
  if(!DUMP_ACTIVATED)
    return;

  if (file == NULL)
   return;

  fclose(file);
  file = NULL;
}

void wmd_dump
(
    unsigned long frame_counter,
    wb_locals_t* locals
)
{
  if(!DUMP_ACTIVATED)
    return;

  //  ---------------------------------------------------------------------------
  //  NES Memory dump.
  uint8_t *current_memory_content = memory_content;

  for (uint32_t address = memory_start; address < memory_size; address++) {
      uint8_t* data = rc_libretro_memory_find_avail(&locals->memory, address, NULL);

      if (data == NULL)
          break;

      *current_memory_content = *data;
      current_memory_content++;
  }

  if (current_memory_content == memory_content)
    return;
  
    bool is_enter_pressed = input_key_pressed(RETRO_DEVICE_ID_JOYPAD_START, false);
    fwrite(&frame_counter, sizeof(unsigned long), 1, file);
    fwrite(&is_enter_pressed, sizeof(bool), 1, file);
    fwrite(&memory_size, sizeof(uint32_t), 1, file);
    fwrite(memory_content, sizeof(uint8_t), memory_size, file);
  //  ---------------------------------------------------------------------------
}
