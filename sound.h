#ifndef _SOUND_H
#define _SOUND_H

#include "types.h"

#define SAMPLE_FREQ 48000

extern void init_audio(unsigned int soundFreq, unsigned int sampleFrq = SAMPLE_FREQ);
extern void close_audio();
extern void sound_pause();
extern void sound_resume();
extern void updateAudio(unsigned int nrsamples);
extern void render_audio(unsigned int nrsamples, short *buffer);

extern void flushBuffer(ClockCycle cycle, unsigned int frq);
extern void writeSoundReg(ClockCycle cycle, unsigned int reg, unsigned char value);
extern void ted_sound_init(unsigned int mixingFreq);

#endif
