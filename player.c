#include <stdint.h>
#include <alsa/asoundlib.h>
#include "player.h"

struct player {
  snd_pcm_t *device;
  snd_pcm_format_t format;
  int bits;
  int rate;
  int channels;
  uint8_t *buffer;
};

player_handle player_create(struct player_format *format, uint32_t *buffer_size)
{
  struct player *p = NULL;
  snd_pcm_hw_params_t *hw_params = 0;
  int err = 0;

  if (!format || !buffer_size
      || (format->bits != 16)
      || !format->is_little_endian
      || !format->is_signed ) 
    return NULL;
  
  p = calloc(1, sizeof(struct player));
  if (!p)
    return NULL;
  
  // limited use case atm
  p->format = SND_PCM_FORMAT_S16_LE;
  p->rate = format->rate;
  p->bits = format->bits;
  p->channels = format->channels;
  
  if ((err = snd_pcm_open (&p->device, "default", SND_PCM_STREAM_PLAYBACK, 0)) < 0)
    goto bail;
  
  if ((err = snd_pcm_hw_params_malloc (&hw_params)) < 0)
    goto bail;

  if ((err = snd_pcm_hw_params_any (p->device, hw_params)) < 0)
    goto bail;

  if ((err = snd_pcm_hw_params_set_access (p->device, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
    goto bail;

  if ((err = snd_pcm_hw_params_set_format (p->device, hw_params, SND_PCM_FORMAT_S16_LE)) < 0)
    goto bail;

  if ((err = snd_pcm_hw_params_set_rate (p->device, hw_params, 11025, 0)) < 0)
    goto bail;

  if ((err = snd_pcm_hw_params_set_channels (p->device, hw_params, 1)) < 0)
    goto bail;

  if ((err = snd_pcm_hw_params (p->device, hw_params)) < 0)
    goto bail;

  snd_pcm_hw_params_free (hw_params);

  // figure out a buffer size
  *buffer_size = snd_pcm_avail_update (p->device)*2; // 2 = 2 bytes per sample (16 bits/mono)
  /* tmp = snd_pcm_avail_update (p)/4; */
  /* if (tmp > MAX_AUDIO_BUFFER_SIZE) */
  /*   tmp = MAX_AUDIO_BUFFER_SIZE; */
  
 bail:
  if (err) {
    free(p);
    p = NULL;
  }
  return p;
}

enum player_status player_write(player_handle handle, const uint8_t *buf, uint32_t size)
{
  enum player_status status = PLAYER_OK;
  struct player *p = (struct player *)handle;
  snd_pcm_sframes_t available = snd_pcm_avail_update (p->device);
  if (available < size/2) { // 2 = 2 bytes per sample (16 bits/mono)
    status = PLAYER_DATA_NOT_WRITTEN;  
  } else {
    snd_pcm_writei (p->device, buf, p->bits/8);
  }
  return status;
}

enum player_status player_stop(player_handle handle)
{
  struct player *p = (struct player *)handle;
  snd_pcm_drop (p->device);
  return PLAYER_OK;
}

enum player_status player_delete(player_handle handle)
{
  struct player *p = (struct player *)handle;
  snd_pcm_close (p->device);
  p->device = NULL;
  free(p);
  return PLAYER_OK;
}

enum player_status player_is_running(player_handle handle, bool *is_running)
{
  struct player *p = (struct player *)handle;
  snd_pcm_status_t *status;
  snd_pcm_state_t state;
  
  snd_pcm_status_malloc (&status);
  snd_pcm_status (p->device, status);
  state = snd_pcm_status_get_state (status);
  if (state != SND_PCM_STATE_RUNNING) {
    *is_running = false;
    snd_pcm_prepare (p->device);
  } else {
    *is_running = true;
  }
  snd_pcm_status_free (status);

  return PLAYER_OK;
}


