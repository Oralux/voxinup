/*
 * player.c based on aplay.c (from alsa-utils, GPL Version 2, June 1991)
 *
 */

#include <stdint.h>
#include <alsa/asoundlib.h>
#include "player.h"
#include <sys/time.h>
#include <errno.h>
#include "debug.h"

#define PLAYER_ID 0x504c4159

static FILE *fd=NULL;
#define WAV_FILE "/tmp/player.wav"

struct player {
  uint32_t id;
  snd_pcm_t *device;
  snd_pcm_format_t format;
  unsigned int bits;
  unsigned int rate;
  unsigned int channels;
  uint8_t *buffer;
  snd_pcm_uframes_t period_size;
  unsigned buffer_time;
  size_t chunk_bytes;
};

#define IS_PLAYER(p) (p && (p->id == PLAYER_ID))

/* static bool xrun(snd_pcm_t *device) */
/* { */
/*   ENTER(); */
/*   bool ret = true; */
/*   snd_pcm_status_t *status; */
/*   int res; */

/*   snd_pcm_status_alloca(&status); */

/*   if ((res = snd_pcm_status(device, status)) < 0) { */
/*     err("Error: snd_pcm_status (%s)", snd_strerror(res)); */
/*     return false; */
/*   } */

/*   if (snd_pcm_status_get_state(status) == SND_PCM_STATE_XRUN) { */
/*     struct timeval now, diff, tstamp; */
/*     gettimeofday(&now, 0); */
/*     snd_pcm_status_get_trigger_tstamp(status, &tstamp); */
/*     timersub(&now, &tstamp, &diff); */
/*     err("underrun!!! (at least %.3f ms long)",	 */
/* 	diff.tv_sec * 1000 + diff.tv_usec / 1000.0); */
    
/*     if ((res = snd_pcm_prepare(device)) < 0) { */
/*       err("Error: snd_pcm_prepare (%s)", snd_strerror(res)); */
/*       ret = false; */
/*     } */
/*   } */
/*   msg("read/write error, state = %s", snd_pcm_state_name(snd_pcm_status_get_state(status))); */
/*   return ret; */
/* } */

static bool suspend(snd_pcm_t *device)
{
  ENTER();
  int res;
  bool ret = true;

  while ((res = snd_pcm_resume(device)) == -EAGAIN)
    sleep(1);	/* wait until suspend flag is released */

  if (res < 0) {
    if ((res = snd_pcm_prepare(device)) < 0) {
      err("Error: snd_pcm_prepare (%s)", snd_strerror(res));
      ret = false;
    }
  }
  return ret;
}


player_handle player_create(struct player_format *format, uint32_t *buffer_size)
{
  ENTER();
  struct player *p = NULL;
  snd_pcm_hw_params_t *hw_params = NULL;
  snd_pcm_sw_params_t *sw_params = NULL;
  int err = 0;
  snd_pcm_uframes_t bufsiz = 0;
  unsigned period_time = 0;
  snd_pcm_uframes_t period_frames = 0;
  snd_pcm_uframes_t buffer_frames = 0;

  #ifdef DEBUG
  msg("wav in %s", WAV_FILE);
  fd = fopen(WAV_FILE,"w");
  if (!fd) {
    err("%s not opened", WAV_FILE);
  }
  #endif
  
  // limited use case
  if (!format || !buffer_size
      || (format->bits != 16)
      || !format->is_little_endian
      || !format->is_signed ) {
    err("Unexpected args");
    return NULL;
  }

  p = calloc(1, sizeof(struct player));
  if (!p) {
    err("calloc (%d)", errno);
    return NULL;
  }

  p->format = SND_PCM_FORMAT_S16_LE;
  p->rate = format->rate;
  p->bits = format->bits;
  p->channels = format->channels;

  // Open the "default" pcm in blocking mode
  //TODO  if ((err = snd_pcm_open (&p->device, "default", SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
  if ((err = snd_pcm_open (&p->device, "sysdefault", SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
    err("Error: snd_pcm_open (%s)", snd_strerror(err));
    goto bail;
  }

  p->period_size = 1024;

  snd_pcm_hw_params_alloca(&hw_params);
  snd_pcm_sw_params_alloca(&sw_params);

  if ((err = snd_pcm_hw_params_any (p->device, hw_params)) < 0) {
    err("Error: snd_pcm_hw_params_any (%s)", snd_strerror(err));
    goto bail;
  }

  if ((err = snd_pcm_hw_params_set_access (p->device, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
    err("Error: snd_pcm_hw_params_set_access (%s)", snd_strerror(err));
    goto bail;
  }

  if ((err = snd_pcm_hw_params_set_format (p->device, hw_params, p->format)) < 0) {
    err("Error: snd_pcm_hw_set_params_set_format (%s)", snd_strerror(err));
    goto bail;
  }

  if ((err = snd_pcm_hw_params_set_rate (p->device, hw_params, p->rate, 0)) < 0) {
    err("Error: snd_pcm_hw_set_params_set_rate (%s)", snd_strerror(err));
    goto bail;
  }

  if ((err = snd_pcm_hw_params_set_channels (p->device, hw_params, p->channels)) < 0) {
    err("Error: snd_pcm_hw_set_params_set_channels (%s)", snd_strerror(err));
    goto bail;
  }

  if ((err = snd_pcm_hw_params_set_rate_near(p->device, hw_params, &p->rate, 0)) < 0) {
    err("Error: snd_pcm_hw_set_params_set_rate_near (%s)", snd_strerror(err));
    goto bail;
  }

  if ((err = snd_pcm_hw_params_get_buffer_time_max(hw_params, &p->buffer_time, 0)) < 0) {
    err("Error: snd_pcm_hw_get_buffer_time_max (%s)", snd_strerror(err));
    goto bail;
  }

  if (p->buffer_time > 500000)
    p->buffer_time = 500000;

  if (p->buffer_time > 0)
    period_time = p->buffer_time / 4;

  if (period_time > 0) {
    if ((err = snd_pcm_hw_params_set_period_time_near(p->device,
						      hw_params,
						      &period_time, 0)) < 0) {
      err("Error: snd_pcm_hw_set_period_time_near (%s)", snd_strerror(err));
      goto bail;
    }
  } else if ((err = snd_pcm_hw_params_set_period_size_near(p->device,
							   hw_params,
							   &period_frames,
							   0)) < 0) {
    err("Error: snd_pcm_hw_set_period_size_near (%s)", snd_strerror(err));
    goto bail;
  }

  if (p->buffer_time > 0) {
    if ((err = snd_pcm_hw_params_set_buffer_time_near(p->device,
						      hw_params,
						      &p->buffer_time,
						      0)) < 0) {
      err("Error: snd_pcm_hw_set_buffer_time_near (%s)", snd_strerror(err));
      goto bail;
    }
  } else if ((err = snd_pcm_hw_params_set_buffer_size_near(p->device,
							   hw_params,
							   &buffer_frames)) < 0) {
    err("Error: snd_pcm_hw_set_buffer_size_near (%s)", snd_strerror(err));
    goto bail;
  }

  if ((err = snd_pcm_hw_params(p->device, hw_params)) < 0) {
    err("Error: snd_pcm_hw_params (%s)", snd_strerror(err));
    goto bail;
  }

  snd_pcm_hw_params_get_period_size(hw_params, &p->period_size, 0);
  snd_pcm_hw_params_get_buffer_size(hw_params, &bufsiz);
  if (p->period_size == bufsiz) {
    err = -1;
    goto bail;
  }

  snd_pcm_sw_params_current(p->device, sw_params);
  snd_pcm_sw_params_set_avail_min(p->device, sw_params, p->period_size);

  // TODO
  /* if ((err = snd_pcm_sw_params_set_start_threshold(p->device, sw_params, bufsiz)) < 0) { */
  /*   err("Error: snd_pcm_sw_params_set_start_threshold (%s)", snd_strerror(err));     */
  /*   goto bail; */
  /* } */

  /* if ((err = snd_pcm_sw_params_set_stop_threshold(p->device, sw_params, bufsiz)) < 0) { */
  /*   err("Error: snd_pcm_sw_params_set_stop_threshold (%s)", snd_strerror(err));     */
  /*   goto bail; */
  /* } */

  if ((err = snd_pcm_sw_params(p->device, sw_params)) < 0) {
    err("Error: snd_pcm_sw_params (%s)", snd_strerror(err));
    err = -1;
    goto bail;
  }

  p->chunk_bytes = p->period_size * (p->bits * p->channels) / 8;
  *buffer_size = p->chunk_bytes;
  p->id = PLAYER_ID;
  msg("chunk_bytes=%lu, period_size=%lu, buffer_time=%d", p->chunk_bytes, p->period_size, p->buffer_time);
  
 bail:
  if (err) {
    free(p);
    p = NULL;
  }
  return p;
}

enum player_status player_write(player_handle handle, const uint8_t *buf, uint32_t size)
{
  ENTER();
  enum player_status status = PLAYER_OK;
  struct player *p = (struct player *)handle;
  snd_pcm_sframes_t nb_frames_written = 0;
  snd_pcm_sframes_t nb_frames_max;
  snd_pcm_sframes_t n;
  const uint8_t *b = buf;

  if (!IS_PLAYER(p) || !buf) {
    return PLAYER_ARGS_ERROR;
  }

#ifdef DEBUG
  if (fd) {
    size_t n = fwrite(buf, size, 1, fd);
    if (!n)
      err("error write dbg");
  }
#endif
    
  nb_frames_max = size/2; // 16 bits, mono

  while (nb_frames_written < nb_frames_max) {
    size_t remain = nb_frames_max - nb_frames_written;
    if (remain > p->chunk_bytes/2)
      remain = p->chunk_bytes/2;

    n = snd_pcm_writei (p->device, b, remain);  // 16 bits, mono
    if (n >= 0) {
      nb_frames_written += n;
      b += n*2; // 16 bits mono
      dbg("snd_pcm_writei: write %lu/%lu frames", nb_frames_written, nb_frames_max);
      if (nb_frames_written < nb_frames_max)
	snd_pcm_wait(p->device, 100);
    } else if (n == -EAGAIN) {
      dbg("snd_pcm_writei: EAGAIN");
      snd_pcm_wait(p->device, 100);
    } else if (n == -EPIPE) {
      /* if (!xrun(p->device)) { */
      status = PLAYER_WRITE_ERROR;
      break;
      /* } */
    } else if (n == -ESTRPIPE) {
      if (!suspend(p->device)) {
	status = PLAYER_WRITE_ERROR;
	break;
      }
    } else {
      int err;
      err("Error: snd_pcm_writei (%s)", snd_strerror(n));
      if ((err = snd_pcm_prepare(p->device)) < 0) {
	err("Error: snd_pcm_prepare (%s)", snd_strerror(err));
	status = PLAYER_WRITE_ERROR;
	break;
      }
    }
  }
  return status;
}

enum player_status player_stop(player_handle handle)
{
  ENTER();
  struct player *p = (struct player *)handle;
  enum player_status status = PLAYER_OK;
  int err;

  if (!IS_PLAYER(p)) {
    return PLAYER_ARGS_ERROR;
  }
    
  snd_pcm_drop (p->device);
  if ((err = snd_pcm_prepare(p->device)) < 0) {
    err("Error: snd_pcm_prepare (%s)", snd_strerror(err));
    status = PLAYER_WRITE_ERROR;
  }

  return status;
}

enum player_status player_delete(player_handle handle)
{
  ENTER();
  struct player *p = (struct player *)handle;
  if (!IS_PLAYER(p)) {
    return PLAYER_ARGS_ERROR;
  }
  snd_pcm_drain(p->device);
  snd_pcm_close (p->device);
  p->device = NULL;
  free(p);
#ifdef DEBUG
  if (fd)
    fclose(fd);
#endif  
  return PLAYER_OK;
}
