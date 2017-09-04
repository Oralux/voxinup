#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include "player.h"

#define WAV_FILE "Front_Center.wav"
#define HEADER_SIZE 44

void main() {
  struct stat buf;
  uint8_t *audio_buf;
  FILE *fd;
  enum player_status ret;
  struct player_format format = {bits:16, is_signed:1, is_little_endian:true, rate:11025, channels:1};
  uint32_t buffer_size = 0;
  player_handle h;

  if (stat(WAV_FILE, &buf))
    return;

  audio_buf = malloc(buf.st_size);

  fd = fopen(WAV_FILE, "r");
  if (!fd)
    return;
  fread(audio_buf, buf.st_size, 1, fd);
  fclose(fd);
    
  h = player_create(&format, &buffer_size); 
  ret = player_write(h, audio_buf+HEADER_SIZE, buf.st_size-HEADER_SIZE);  
  ret = player_delete(h);
  free(audio_buf);
}
