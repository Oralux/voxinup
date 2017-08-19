#ifndef PLAYER_H
#define PLAYER_H

#include <stdint.h>
#include <stdbool.h>

enum player_status {
  PLAYER_OK=0,
  PLAYER_ARGS_ERROR, 
  PLAYER_WRITE_ERROR, 
};

struct player_format {
  int bits;
  bool is_signed;
  bool is_little_endian;
  int rate;
  int channels;
};

typedef void* player_handle;

player_handle player_create(struct player_format *format, uint32_t *buffer_size);
enum player_status player_write(player_handle handle, const uint8_t *buf, uint32_t size);
enum player_status player_stop(player_handle handle);
enum player_status player_delete(player_handle handle);

#endif
