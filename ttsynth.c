#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include <eci.h>
#include <iconv.h>
#include <string.h>
#include "player.h"

enum {
  state_stopped,
  state_speaking,
  state_idle
};

typedef enum {
  SPEAKUP_MODE=0,
  JUPITER_ESPEAKUP_MODE
} ttsynth_mode_t;

#define MAX_INPUT 1024
#define MAX_OUTPUT (4*MAX_INPUT)

typedef struct {
  int fd;
  iconv_t ld;
  unsigned char outbuf[MAX_OUTPUT + 1];
  ttsynth_mode_t mode;
  ECIHand eci;
  int state;
  int text_pending;
  int pitch;
  int rate;
  player_handle player; 
} synth;

static void speakup_add_text (synth *s, unsigned char *text);
static void jupiter_add_text (synth *s, unsigned char *text);

typedef void (add_text_func_t) (synth *s, unsigned char *text);

add_text_func_t *add_text[2] = {speakup_add_text, jupiter_add_text};

static uint8_t *audio_buffer = NULL;

#define JUPITER_ESPEAKUP_MARK_FORMAT "<mark name=\"%d\"/>"
#define JUPITER_ESPEAKUP_MARK_MIN_LENGTH 16 // e.g. length of '<mark name="1"/>'
#define JUPITER_ESPEAKUP_MARK_MIN_VALUE 1
#define JUPITER_ESPEAKUP_MARK_MAX_VALUE 99

enum ECICallbackReturn
ttsynth_callback (ECIHand hEngine,
		  enum ECIMessage Msg,
		  long lParam,
		  void *pData)
{
  synth *s = (synth *) pData;
  enum ECICallbackReturn rv = eciDataNotProcessed;

  if (s->state == state_idle)
    return eciDataProcessed;

  switch (Msg) {
  case eciIndexReply:
    if ((s->mode == JUPITER_ESPEAKUP_MODE)
	&& (lParam >= JUPITER_ESPEAKUP_MARK_MIN_VALUE)
	&& (lParam <= JUPITER_ESPEAKUP_MARK_MAX_VALUE))
      write(STDOUT_FILENO, (char*)&lParam, 1);

    rv = eciDataProcessed;
    break;
  case eciWaveformBuffer:
    rv = (!player_write(s->player, audio_buffer, lParam*2)) ?
      eciDataProcessed : eciDataNotProcessed;
    break;
  default:
    break;
  }
  return rv;
}


static void add_utf8_text(synth *s, unsigned char *utf8_text)
{
  char *inbuf;
  char *outbuf;
  size_t inbytesleft;
  size_t outbytesleft = MAX_OUTPUT;
  if (!s || !utf8_text)
    return;
  inbuf = (char*)utf8_text;
  inbytesleft = strlen(inbuf);
  outbuf = (char*)s->outbuf;
  if (-1 != iconv(s->ld, &inbuf, &inbytesleft, &outbuf, &outbytesleft))
    {
      s->outbuf[MAX_OUTPUT - outbytesleft] = 0;
      eciAddText(s->eci, s->outbuf);
    }

}

/* jupiter_add_text parses the input buffer according to the Jupiter / espeakup format,
   and supplies texts and indexes to the ECI Engine.
*/
static void
jupiter_add_text (synth *s,
		  unsigned char *text)
{
  unsigned char* textMax;
  unsigned char* buf;

  if (!s || !text|| !*text)
    return;

  textMax = text + strlen((char*)text) - 1;

  for (buf = text; buf < textMax; buf++) {
    int i = 0;
    if ((*buf == '<')
	&& (sscanf((char*)buf, JUPITER_ESPEAKUP_MARK_FORMAT, &i) == 1)
	&& (i >= JUPITER_ESPEAKUP_MARK_MIN_VALUE)
	&& (i <= JUPITER_ESPEAKUP_MARK_MAX_VALUE)) {
      if (buf > text) {
	s->text_pending = 1;
	*buf = 0;
	add_utf8_text(s, text);
	*buf = '<';
      }
      s->text_pending = 1;
      eciInsertIndex(s->eci, i);

      for (buf += JUPITER_ESPEAKUP_MARK_MIN_LENGTH - 1; buf <= textMax; buf++) {
	if (*buf == '>') {
	  buf++;
	  text = buf;
	  break;
	}
      }
    }
  }
  if (text < textMax) {
    s->text_pending = 1;
    add_utf8_text(s, text);
  }
}


static synth *
synth_new ()
{
  synth *s;
  struct player_format format;
  uint32_t bufsize = 0;

  s = (synth *) calloc (1, sizeof(synth));
  if (!s)
    return NULL;

  /* Create the ECI handle */
  s->eci = eciNew ();
  format.bits = 16;
  format.is_signed = true;
  format.is_little_endian = true;
  format.rate = 11025;
  format.channels = 1;
  s->player = player_create(&format, &bufsize);
  if (!s->player || !bufsize)
    goto bail;

  audio_buffer = calloc(1, bufsize);
  if (!audio_buffer)
    goto bail;
  
  /* Setup the audio callback */
  eciRegisterCallback (s->eci, ttsynth_callback, s);
  eciSetOutputBuffer (s->eci, bufsize/2, (short int*)audio_buffer);
  eciSetParam (s->eci, eciSynthMode, 0);
  eciSetParam (s->eci, eciNumberMode, 1);
  eciSetParam (s->eci, eciTextMode, 0);
  eciSetParam (s->eci, eciSampleRate, 2);
  eciSetParam (s->eci, eciDictionary, 1);
  s->state = state_idle;
  return s;

 bail:
  if (s->eci) {
    eciDelete (s->eci);
    s->eci = NULL;
  }
  free (s);
  free (audio_buffer);
  audio_buffer = NULL;
  return NULL;
}


static void
synth_close (synth *s)
{
  assert (s);
  assert (s->player);
  assert (s->eci != NULL_ECI_HAND);

  player_delete(s->player);
  eciDelete (s->eci);
  free (s);
}


static void
speakup_add_text (synth *s,
		  unsigned char *text)
{
  assert (s);

  eciAddText (s->eci, text);
  s->text_pending = 1;
}


static void
synth_speak (synth *s)
{
  bool is_running = false;
  assert (s);

  if (!s->text_pending)
    return;
  eciSynthesize (s->eci);
  s->state = state_speaking;
  s->text_pending = 0;
  player_is_running(s->player, &is_running);
}


static void
synth_stop (synth *s)
{
  assert (s);

  s->state = state_idle;
  eciSpeaking (s->eci);
  eciStop (s->eci);
  s->text_pending = 0;
  player_stop(s->player);
}


static void
synth_update_pitch (synth *s)
{
  eciSetVoiceParam (s->eci, 0, eciPitchBaseline, s->pitch*11);
}


static void
synth_update_rate (synth *s)
{
  eciSetVoiceParam (s->eci, 0, eciSpeed, s->rate*15);
}


static int
synth_process_command (synth *s,
		       unsigned char *buf,
		       int start,
		       int l)
{
  unsigned char param;
  unsigned char value;

  switch (buf[start]) {
  case 1:
    if (buf[start+1] == '+' || buf[start+1] == '-') {
      value = buf[start+2]-'0';
      param = buf[start+3];
      if (buf[start+1] == '-')
	value = -value;
      switch (param) {
      case 'p':
	s->pitch += value;
	synth_update_pitch (s);
	break;
      case 's':
	s->rate += value;
	synth_update_rate (s);
	break;
      }
      return 4;
    }
    else if (buf[start+1] >= '0' && buf[start+1] <= '9') {
      value = buf[start+1]-'0';
      param = buf[start+2];
      switch (param) {
      case 'p':
	s->pitch = value;
	synth_update_pitch (s);
	break;
      case 's':
	s->rate = value;
	synth_update_rate (s);
	break;
      }
    }
    return 3;
  case 24:
    synth_stop (s);
    return 1;
  }
  return 1;
}


static void
synth_process_data (synth *s)
{
  unsigned char buf[MAX_INPUT+1];
  int l;
  unsigned char tmp_buf[MAX_INPUT+1];
  int start, end;

  l = read (s->fd, buf, MAX_INPUT);
  start = end = 0;
  if ((l==0) && (s->mode == JUPITER_ESPEAKUP_MODE) && (getppid() == 1)) {
    exit(0);
  }
  while (start < l) {
    while (buf[end] >= 32 && end < l)
      end++;
    if (end != start) {
      strncpy ((char*)tmp_buf, (char*)&buf[start], end-start);
      tmp_buf[end-start] = 0;
      add_text[s->mode] (s, tmp_buf);
    }
    if (end < l)
      start = end = end+synth_process_command (s, buf, end, l);
    else
      start = l;
  }
  synth_speak (s);
}


static void
synth_main_loop (synth *s)
{
  fd_set set;
  struct timeval tv, timeout = {0, 20000};

  while (1) {
    int i;
    tv = timeout;

    FD_ZERO (&set);
    FD_SET (s->fd, &set);
    i = select (s->fd+1, &set, NULL, NULL, &tv);
    if (i == 0) {
      if (s->state == state_speaking) {
	if (!eciSpeaking (s->eci))
	  s->state = state_idle;
      }
    }
    else if (i > 0)
      synth_process_data (s);
  }
}


static void
usage()
{
  printf("Usage: voxinup [-j]\n"
	 "-j: jupiter/espeakup mode\n");
}


int
main (int argc, char** argv)
{
  synth *s;
  int fd = -1;
  iconv_t ld = (iconv_t)-1;
  ttsynth_mode_t mode = SPEAKUP_MODE;

  if (argc == 2) {
    if (strcmp("-j", argv[1]) != 0) {
      usage();
      return -1;
    }
    mode = JUPITER_ESPEAKUP_MODE;
    fd = STDIN_FILENO;
    /* only iso-8859-1 languages are currently supported */
    ld = iconv_open("ISO-8859-1//TRANSLIT", "UTF-8");
    if (ld == (iconv_t)-1) {
      return -1;
    }
  } else {
    mode = SPEAKUP_MODE;
    fd = open ("/dev/softsynth", O_RDONLY);
    if (fd < 0) {
      return -1;
    }
    fcntl (fd, F_SETFL,
	   fcntl (fd, F_GETFL) | O_NONBLOCK);
  }

  if (mode == SPEAKUP_MODE)
    daemon (0, 1);

  {
    FILE *f = fopen("/run/voxinup.pid", "w");
    if (f) {
      fprintf(f, "%d\n", getpid());
      fclose(f);
    }
  }

  s = synth_new ();
  if (!s) {
    return -1;
  }

  s->fd = fd;
  s->ld = ld;
  s->mode = mode;

  /* Setup initial voice parameters */

  s->pitch = 5;
  s->rate = 5;
  synth_update_pitch (s);
  synth_update_rate (s);

  synth_main_loop (s);
  synth_close (s);
  return 0;
}
