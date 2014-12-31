#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include <alsa/asoundlib.h>
#include <eci.h>


enum {
	state_stoped,
	state_speaking,
	state_idle
};


typedef struct {
	int fd;
	snd_pcm_t *device;
	ECIHand handle;
	int state;
	int text_pending;
	int pitch;
	int rate;
} synth;


#define MAX_AUDIO_BUFFER_SIZE 8192
static short audio_buffer[MAX_AUDIO_BUFFER_SIZE];


enum ECICallbackReturn
ttsynth_callback (ECIHand hEngine,
		   enum ECIMessage Msg,
		   long lParam,
		   void *pData)
{
	synth *s = (synth *) pData;
	enum ECICallbackReturn rv = eciDataNotProcessed;
	int available;
	
	if (s->state == state_idle)
		return eciDataProcessed;

	switch (Msg) {
	case eciIndexReply:
		rv = eciDataProcessed;
		break;
	case eciWaveformBuffer:
		available = snd_pcm_avail_update (s->device);
		if (available < lParam)
			rv = eciDataNotProcessed;
		else {
			snd_pcm_writei (s->device, audio_buffer, lParam);
			rv = eciDataProcessed;
		}
		break;
	}
	return rv;
}


static synth *
synth_new ()
{
	synth *s;
	snd_pcm_hw_params_t *hw_params = 0;
	unsigned int tmp;
	int dir;
	int err;
	
	s = (synth *) malloc (sizeof(synth));
	if (!s)
		return NULL;

	if (err = snd_pcm_open (&s->device, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0)
		goto bail;

	if (err = snd_pcm_hw_params_malloc (&hw_params) < 0)
		goto bail;
	
	if (err = snd_pcm_hw_params_any (s->device, hw_params) < 0)
		goto bail;

	if (err = snd_pcm_hw_params_set_access (s->device, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED) < 0)
		goto bail;

	if (err = snd_pcm_hw_params_set_format (s->device, hw_params, SND_PCM_FORMAT_S16_LE) < 0)
		goto bail;

	if (err = snd_pcm_hw_params_set_rate (s->device, hw_params, 22050, 0) < 0)
		goto bail;

	if (err = snd_pcm_hw_params_set_channels (s->device, hw_params, 1) < 0)
		goto bail;

	if (err = snd_pcm_hw_params (s->device, hw_params) < 0)
		goto bail;

	snd_pcm_hw_params_free (hw_params);

	// figure out a buffer size

	tmp = snd_pcm_avail_update (s->device)/4;
	if (tmp > MAX_AUDIO_BUFFER_SIZE)
		tmp = MAX_AUDIO_BUFFER_SIZE;

	// Open the soft synth handle

	s->fd = open ("/dev/softsynth", O_RDONLY);
	if (s->fd < 0)
		goto bail;
	fcntl (s->fd, F_SETFL, 
	       fcntl (s->fd, F_GETFL) | O_NONBLOCK);
	
	/* Create the ECI handle */

	s->handle = eciNew ();
	
	/* Setup the audio callback */

	eciRegisterCallback (s->handle, ttsynth_callback, s);
	eciSetOutputBuffer (s->handle, tmp, audio_buffer);
	eciSetParam (s->handle, eciSynthMode, 0);
	eciSetParam (s->handle, eciNumberMode, 1);
        eciSetParam (s->handle, eciTextMode, 0);
	eciSetParam (s->handle, eciSampleRate, 2);
	eciSetParam (s->handle, eciDictionary, 1);
	s->state = state_idle;
	return s;

 bail:

	if (s->device)
		snd_pcm_close (s->device);
	if (s->handle)
		eciDelete (s->handle);
	free (s);
	return NULL;
}


static void
synth_close (synth *s)
{
	assert (s);
	assert (s->device);
	assert (s->handle != NULL_ECI_HAND);

	snd_pcm_close (s->device);
	eciDelete (s->handle);
	free (s);
}


static void
synth_add_text (synth *s,
		const char *text)
{
	assert (s);

	eciAddText (s->handle, text);
	s->text_pending = 1;
}


static void
synth_speak (synth *s)
{
	snd_pcm_status_t *status;
	snd_pcm_state_t state;


	assert (s);

	if (!s->text_pending)
		return;
	eciSynthesize (s->handle);
	s->state = state_speaking;
	s->text_pending = 0;
	snd_pcm_status_malloc (&status);
	snd_pcm_status (s->device, status);
	state = snd_pcm_status_get_state (status);
	if (state != SND_PCM_STATE_RUNNING)
		snd_pcm_prepare (s->device);
	snd_pcm_status_free (status);
}


static void
synth_stop (synth *s)
{
	assert (s);

	s->state = state_idle;
	eciSpeaking (s->handle);
	eciStop (s->handle);
	s->text_pending = 0;
	snd_pcm_drop (s->device);
}


static void
synth_update_pitch (synth *s)
{
	eciSetVoiceParam (s->handle, 0, eciPitchBaseline, s->pitch*11);
}


static void
synth_update_rate (synth *s)
{
	eciSetVoiceParam (s->handle, 0, eciSpeed, s->rate*15);
}


static int
synth_process_command (synth *s,
		       char *buf,
		       int start,
		       int l)
{
	char param;
	char value;
	
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
	unsigned char buf[1025];
	int l;
	unsigned char tmp_buf[1025];
	int start, end;
	int text_pending = 0;

	l = read (s->fd, buf, 1024);
	start = end = 0;
	while (start < l) {
		while (buf[end] >= 32 && end < l)
			end++;
		if (end != start) {
			strncpy (tmp_buf, &buf[start], end-start);
			tmp_buf[end-start] = 0;
			synth_add_text (s, tmp_buf);
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
				if (!eciSpeaking (s->handle))
					s->state = state_idle;
			}
		}
		else if (i > 0)
			synth_process_data (s);
	}
}


int
main ()
{
	synth *s;

	daemon (0, 1);
	s = synth_new ();
	if (!s) {
		return -1;
	}

	/* Setup initial voice parameters */

	s->pitch = 5;
	s->rate = 5;
	synth_update_pitch (s);
	synth_update_rate (s);

	synth_main_loop (s);
	synth_close (s);
	return 0;
}
