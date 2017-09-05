#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include "player.h"
#include "eci.h"
#include "debug.h"
#define MAX_SAMPLES 1024
static uint8_t *audio_buf;
static uint32_t buffer_size = 0;

unsigned int debug = LV_DEBUG_LEVEL;

const char* vh_quote = "So long as there shall exist, by virtue of law and custom, decrees of "
  "damnation pronounced by society, artificially creating hells amid the "
  "civilization of earth, and adding the element of human fate to divine "
  "destiny; so long as the three great problems of the century--the "
  "degradation of man through pauperism, the corruption of woman through "
  "hunger, the crippling of children through lack of light--are unsolved; "
  "so long as social asphyxia is possible in any part of the world;--in "
  "other words, and with a still wider significance, so long as ignorance "
  "and poverty exist on earth, books of the nature of Les Miserables cannot "
  "fail to be of use."
  " "
  "HAUTEVILLE HOUSE, 1862.";


  /* "PREMIÈRE PROMENADE. " */
  /* "Me voici donc seul sur la terre, n'ayant plus de frère, de prochain, d'ami," */
  /* "de société que moi-même Le plus sociable et le plus aimant des humains en " */
  /* "a été proscrit. Par un accord unanime ils ont cherché dans les raffinements" */
  /* "de leur haine quel tourment pouvait être le plus cruel à mon âme sensible, " */
  /* "et ils ont brisé violemment tous les liens qui m'attachaient à eux. J'aurais" */
  /* "aimé les hommes en dépit d'eux-mêmes. Ils n'ont pu qu'en cessant de l'être " */
  /* "se dérober à mon affection. Les voilà donc étrangers, inconnus, nuls enfin " */
  /* "pour moi puisqu'ils l'ont voulu. Mais moi, détaché d'eux et de tout, que " */
  /* "suis-je moi-même ? Voilà ce qui me reste à chercher.", */



enum ECICallbackReturn my_client_callback(ECIHand hEngine, enum ECIMessage Msg, long lParam, void *pData)
{
  player_handle h = pData;

  if (Msg == eciWaveformBuffer) {
    //    player_write(h, audio_buf, buffer_size);  
    player_write(h, audio_buf, lParam*2);  
  }
  return eciDataProcessed;
}


static int init_eci(ECIHand eci, player_handle h) {
  eciRegisterCallback(eci, my_client_callback, h);
  
  if (eciSetOutputBuffer(eci, buffer_size/sizeof(short int), (short int *)audio_buf) == ECIFalse)
    return __LINE__;

  //  eciSetVoiceParam (s->eci, 0, eciPitchBaseline, s->pitch*11);
  
  /* //  if (eciSetVoiceParam(eci, 0, eciSpeed, 50) < 0) */
  /* if (eciSetVoiceParam(eci, 0, eciSpeed, 100) < 0) */
  /*   return __LINE__; */
    if (eciSetVoiceParam(eci, 0, eciSpeed, 100) < 0)
      return __LINE__;

    return 0;
}

int main() {
  struct player_format format = {bits:16, is_signed:true, is_little_endian:true, rate:11025, channels:1};
  player_handle h;
  ECIHand eci;
  int i;
  
  h = player_create(&format, &buffer_size); 
  audio_buf = malloc(buffer_size);
  
  eci = eciNew();
  if (!eci)
    return __LINE__;

  if (init_eci(eci, h))
    return __LINE__;
  
  for (i=0; i<20; i++) {
    fprintf(stderr, "--> %d\n", i);
  
    if (eciAddText(eci, vh_quote) == ECIFalse)
      return __LINE__;

    if (eciSynthesize(eci) == ECIFalse)
      return __LINE__;

#define ONE_MILLISECOND_IN_NANOSECOND 1000000 
    struct timespec req;
    req.tv_sec=0;
    req.tv_nsec=ONE_MILLISECOND_IN_NANOSECOND;
  
    while(eciSpeaking(eci) == ECIFalse)
      nanosleep(&req, NULL);

    int i=0;
    while((eciSpeaking(eci) == ECITrue) && (i<50)) {    
      nanosleep(&req, NULL);
      i++;
    }

    if (eciStop(eci) == ECIFalse) {
      char error[256];
      eciErrorMessage(eci, error);
      fprintf(stderr, "eciStop error=%s, status=0x%x\n", error, eciProgStatus(eci));
      eciClearErrors(eci);
      eciErrorMessage(eci, error);
      eciReset(eci);
      if (init_eci(eci, h))
	return __LINE__;
      //      return __LINE__;     
    }

    player_stop(h);

    usleep(250000);
    
    /* while(eciSpeaking(eci) == ECITrue) {     */
    /*   nanosleep(&req, NULL); */
    /* } */
  }

  if (eciDelete(eci) != NULL)
    return __LINE__;

  sleep(2);
  player_delete(h);
  free(audio_buf);
}
