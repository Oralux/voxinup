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
#include <dlfcn.h>
#include "player.h"
#include "debug.h"

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

#define MAX_BUFFER_SIZE 4096

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

unsigned int debug = 0;
static void speakup_add_text (synth *s, unsigned char *text);
static void jupiter_add_text (synth *s, unsigned char *text);

typedef void (add_text_func_t) (synth *s, unsigned char *text);

add_text_func_t *add_text[2] = {speakup_add_text, jupiter_add_text};

static uint8_t *audio_buffer = NULL;

#define JUPITER_ESPEAKUP_MARK_FORMAT "<mark name=\"%d\"/>"
#define JUPITER_ESPEAKUP_MARK_MIN_LENGTH 16 // e.g. length of '<mark name="1"/>'
#define JUPITER_ESPEAKUP_MARK_MIN_VALUE 1
#define JUPITER_ESPEAKUP_MARK_MAX_VALUE 99

/* Functions pointers */
typedef int (*t_eciAddText)(ECIHand hEngine, ECIInputText pText);
typedef int (*t_eciCopyVoice)(ECIHand hEngine, int iVoiceFrom, int iVoiceTo);
typedef ECIHand (*t_eciDelete)(ECIHand hEngine);
typedef ECIDictHand (*t_eciDeleteDict)(ECIHand hEngine, ECIDictHand hDict);
typedef void (*t_eciErrorMessage)(ECIHand hEngine, void* buffer);
typedef int (*t_eciGetAvailableLanguages)(enum ECILanguageDialect *aLanguages, int *nLanguages);
typedef ECIDictHand (*t_eciGetDict)(ECIHand hEngine);
typedef int (*t_eciGetParam)(ECIHand hEngine, enum ECIParam Param);
typedef int (*t_eciGetVoiceParam)(ECIHand hEngine, int iVoice, enum ECIVoiceParam Param);
typedef int (*t_eciInsertIndex)(ECIHand hEngine, int iIndex);
typedef enum ECIDictError (*t_eciLoadDict)(ECIHand hEngine, ECIDictHand hDict, enum ECIDictVolume DictVol, ECIInputText pFilename);
typedef ECIHand (*t_eciNew)(void);
typedef ECIDictHand (*t_eciNewDict)(ECIHand hEngine);
typedef void (*t_eciRegisterCallback)(ECIHand hEngine, ECICallback Callback, void *pData);
typedef enum ECIDictError (*t_eciSetDict)(ECIHand hEngine, ECIDictHand hDict);
typedef int (*t_eciSetOutputBuffer)(ECIHand hEngine, int iSize, short *psBuffer);
typedef int (*t_eciSetParam)(ECIHand hEngine, enum ECIParam Param, int iValue);
typedef int (*t_eciSetVoiceParam)(ECIHand hEngine, int iVoice, enum ECIVoiceParam Param, int iValue);
typedef int (*t_eciSpeaking)(ECIHand hEngine);
typedef int (*t_eciStop)(ECIHand hEngine);
typedef int (*t_eciSynchronize)(ECIHand hEngine);
typedef int (*t_eciSynthesize)(ECIHand hEngine);
typedef void (*t_eciVersion)(char *pBuffer);

static t_eciAddText _eciAddText;
static t_eciCopyVoice _eciCopyVoice;
static t_eciDelete _eciDelete;
static t_eciDeleteDict _eciDeleteDict;
static t_eciErrorMessage _eciErrorMessage;
static t_eciGetAvailableLanguages _eciGetAvailableLanguages;
static t_eciGetDict _eciGetDict;
static t_eciGetParam _eciGetParam;
static t_eciGetVoiceParam _eciGetVoiceParam;
static t_eciInsertIndex _eciInsertIndex;
static t_eciLoadDict _eciLoadDict;
static t_eciNew _eciNew;
static t_eciNewDict _eciNewDict;
static t_eciRegisterCallback _eciRegisterCallback;
static t_eciSetDict _eciSetDict;
static t_eciSetOutputBuffer _eciSetOutputBuffer;
static t_eciSetParam _eciSetParam;
static t_eciSetVoiceParam _eciSetVoiceParam;
static t_eciStop _eciStop;
static t_eciSpeaking _eciSpeaking;
static t_eciSynchronize _eciSynchronize;
static t_eciSynthesize _eciSynthesize;
static t_eciVersion _eciVersion;

#define LIBVOXIN "libvoxin.so"
#define LIBECI "libibmeci.so"


enum ECICallbackReturn
ttsynth_callback (ECIHand hEngine,
		  enum ECIMessage Msg,
		  long lParam,
		  void *pData)
{
  ENTER();
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
    //    rv = (!player_write(s->player, audio_buffer, lParam*2)) ? 
    //  eciDataProcessed : eciDataNotProcessed; // lParam*2: 2 bytes, mono channel
    player_write(s->player, audio_buffer, lParam*2); // lParam*2: 2 bytes, mono channel
    rv = eciDataProcessed;
    break;
  default:
    break;
  }
  return rv;
}


static void add_utf8_text(synth *s, unsigned char *utf8_text)
{
  ENTER();
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
      _eciAddText(s->eci, s->outbuf);
    }

}

/* jupiter_add_text parses the input buffer according to the Jupiter / espeakup format,
   and supplies texts and indexes to the ECI Engine.
*/
static void
jupiter_add_text (synth *s,
		  unsigned char *text)
{
  ENTER();
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
      _eciInsertIndex(s->eci, i);

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
  ENTER();
  synth *s;
  struct player_format format = {bits:16, is_signed:true, is_little_endian:true, rate:22050, channels:1};
  uint32_t bufsize = 0;

  s = (synth *) calloc (1, sizeof(synth));
  if (!s) {
	dbg2("Error: alloc");
    return NULL;
  }

  /* Create the ECI handle */
  s->eci = _eciNew ();

  s->player = player_create(&format, &bufsize);
  if (!s->player || !bufsize)
    goto bail;

  //TODO  audio_buffer = calloc(1, bufsize);
  audio_buffer = calloc(1, MAX_BUFFER_SIZE);
  if (!audio_buffer)
    goto bail;
  
  /* Setup the audio callback */
  _eciRegisterCallback (s->eci, ttsynth_callback, s);
  //TODO  eciSetOutputBuffer (s->eci, bufsize/sizeof(short int), (short int*)audio_buffer);
  _eciSetOutputBuffer (s->eci, MAX_BUFFER_SIZE/sizeof(short int), (short int*)audio_buffer);
  _eciSetParam (s->eci, eciSynthMode, 0);
  _eciSetParam (s->eci, eciNumberMode, 1);
  _eciSetParam (s->eci, eciTextMode, 0);
  _eciSetParam (s->eci, eciSampleRate, 2);
  _eciSetParam (s->eci, eciDictionary, 1);
  s->state = state_idle;
  return s;

 bail:
  if (s->eci) {
    _eciDelete (s->eci);
    s->eci = NULL;
  }
  free (s);
  free (audio_buffer);
  audio_buffer = NULL;
  {
	//	const char *err = "Can't create eci handle!";
	dbg2("Can't create eci handle! ");
  }
  return NULL;
}


static void
synth_close (synth *s)
{
  ENTER();
  assert (s);
  assert (s->player);
  assert (s->eci != NULL_ECI_HAND);

  _eciDelete (s->eci);
  player_delete(s->player);
  free (s);
}


static void
speakup_add_text (synth *s,
		  unsigned char *text)
{
  assert (s);

  _eciAddText (s->eci, text);
  s->text_pending = 1;
}


static void
synth_speak (synth *s)
{
  ENTER();
  assert (s);

  if (!s->text_pending)
    return;
  _eciSynthesize (s->eci);
  s->state = state_speaking;
  s->text_pending = 0;
  LEAVE();
}


static void
synth_stop (synth *s)
{
  ENTER();
  assert (s);

  s->state = state_idle;
  //  eciSpeaking (s->eci);
  _eciStop (s->eci);
  s->text_pending = 0;
  player_stop(s->player);
  LEAVE();
}


static void
synth_update_pitch (synth *s)
{
  ENTER();  
  _eciSetVoiceParam (s->eci, 0, eciPitchBaseline, s->pitch*11);
}


static void
synth_update_rate (synth *s)
{
  ENTER();
  _eciSetVoiceParam (s->eci, 0, eciSpeed, s->rate*25);
}


static int
synth_process_command (synth *s,
		       unsigned char *buf,
		       int start,
		       int l)
{
  ENTER();
  unsigned char param;
  unsigned char value;

  dbg("buf: type=%d, +/-=%c, value=%c, param=%c", buf[start], buf[start+1], buf[start+2], buf[start+3]);
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
      LEAVE();
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
    LEAVE();
    return 3;
  case 24:
    synth_stop (s);
    LEAVE();
    return 1;
  }
  LEAVE();
  return 1;
}


static void
synth_process_data (synth *s)
{
  ENTER();
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
  LEAVE();
}


static void
synth_main_loop (synth *s)
{
  ENTER();
  fd_set set;
  struct timeval tv, timeout = {0, 20000};

  while (1) {
    int i;
    tv = timeout;

    FD_ZERO (&set);
    FD_SET (s->fd, &set);
    i = select (s->fd+1, &set, NULL, NULL, &tv);
	//    dbg("event received");
    if (i == 0) {
      if (s->state == state_speaking) {
		//		dbg("call eciSpeaking");
		if (!_eciSpeaking (s->eci))
		  s->state = state_idle;
		//		dbg("ret eciSpeaking");
      }
    }
    else if (i > 0)
      synth_process_data (s);
  }
}


static void
usage()
{
  dbg2("Usage: voxinup [OPTION]\n"
	   "  -j: jupiter/espeakup mode\n"
	   "  -d: debug\n"
	   "  -D: daemonize\n"
	   );
}


int
main (int argc, char** argv)
{
  synth *s;
  int fd = -1;
  iconv_t ld = (iconv_t)-1;
  ttsynth_mode_t mode = SPEAKUP_MODE;
  int opt;
  int daemonize = 0;
  void *libHandle;

  while ((opt = getopt(argc, argv, "jdD")) != -1) {
	switch (opt) {
	case 'd':
	  debug = LV_DEBUG_LEVEL;
	  ENTER();
	  break;
	case 'D':
	  daemonize = 1;
	  break;
	case 'j':
	  mode = JUPITER_ESPEAKUP_MODE;
	  break;
	default:
      usage();
      return -1;
	}
  }
  
  libHandle = dlopen(LIBVOXIN, RTLD_NOW);
  if (libHandle == NULL) {
	libHandle = dlopen(LIBECI, RTLD_NOW);
	if (libHandle == NULL) {
	  dbg2("Can't load %s or %s (%s)\n", LIBVOXIN, LIBECI, dlerror());
	  return -1;
	}
  }
	
  _eciAddText = (t_eciAddText)dlsym(libHandle, "eciAddText");
  _eciCopyVoice = (t_eciCopyVoice)dlsym(libHandle, "eciCopyVoice");
  _eciDelete = (t_eciDelete)dlsym(libHandle, "eciDelete");
  _eciDeleteDict = (t_eciDeleteDict)dlsym(libHandle, "eciDeleteDict");
  _eciErrorMessage = (t_eciErrorMessage)dlsym(libHandle, "eciErrorMessage");
  _eciGetAvailableLanguages = (t_eciGetAvailableLanguages)dlsym(libHandle, "eciGetAvailableLanguages");
  _eciGetDict = (t_eciGetDict)dlsym(libHandle, "eciGetDict");
  _eciGetParam = (t_eciGetParam)dlsym(libHandle, "eciGetParam");
  _eciGetVoiceParam = (t_eciGetVoiceParam)dlsym(libHandle, "eciGetVoiceParam");
  _eciInsertIndex = (t_eciInsertIndex)dlsym(libHandle, "eciInsertIndex");
  _eciLoadDict = (t_eciLoadDict)dlsym(libHandle, "eciLoadDict");
  _eciNew = (t_eciNew)dlsym(libHandle, "eciNew");
  _eciNewDict = (t_eciNewDict)dlsym(libHandle, "eciNewDict");
  _eciRegisterCallback = (t_eciRegisterCallback)dlsym(libHandle, "eciRegisterCallback");
  _eciSetDict = (t_eciSetDict)dlsym(libHandle, "eciSetDict");
  _eciSetOutputBuffer = (t_eciSetOutputBuffer)dlsym(libHandle, "eciSetOutputBuffer");
  _eciSetParam = (t_eciSetParam)dlsym(libHandle, "eciSetParam");
  _eciSetVoiceParam = (t_eciSetVoiceParam)dlsym(libHandle, "eciSetVoiceParam");
  _eciStop = (t_eciStop)dlsym(libHandle, "eciStop");
  _eciSpeaking = (t_eciSpeaking)dlsym(libHandle, "eciSpeaking");
  _eciSynchronize = (t_eciSynchronize)dlsym(libHandle, "eciSynchronize");
  _eciSynthesize = (t_eciSynthesize)dlsym(libHandle, "eciSynthesize");
  _eciVersion = (t_eciVersion)dlsym(libHandle, "eciVersion");
 	
  
  switch (mode) {
  case SPEAKUP_MODE:
    while((fd = open ("/dev/softsynth", O_RDONLY)) < 0) {
	  dbg2("Waiting for speakup (/dev/softsynth)");
	  usleep(100000);
	}
	fcntl (fd, F_SETFL,
		   fcntl (fd, F_GETFL) | O_NONBLOCK);
	break;

  default: // JUPITER_ESPEAKUP_MODE
	fd = STDIN_FILENO;
	/* only iso-8859-1 languages are currently supported */
	ld = iconv_open("ISO-8859-1//TRANSLIT", "UTF-8");
	if (ld == (iconv_t)-1) {
	  dbg2("Error: iconv");
	  return -1;
	}
	break;
  }

  if (daemonize)
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
