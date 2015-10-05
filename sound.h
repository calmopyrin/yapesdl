#ifndef _SOUND_H
#define _SOUND_H

#include "types.h"

#define SAMPLE_FREQ 48000 //48000 192000

// derive from this class for sound sources
class SoundSource : public LinkedList<SoundSource> {
public:
    SoundSource() {
        add(this);
    }
    ~SoundSource() {
        remove(this);
    }
    static void bufferFill(unsigned int nrsamples, short *buffer);
    virtual void calcSamples(short *buffer, unsigned int nrsamples) = 0;
private:
    char name[16];
};

extern void init_audio(unsigned int sampleFrq = SAMPLE_FREQ);
extern void close_audio();
extern void sound_pause();
extern void sound_resume();
extern void updateAudio(unsigned int nrsamples);

extern void flushBuffer(ClockCycle cycle, unsigned int frq);
extern void writeSoundReg(ClockCycle cycle, unsigned int reg, unsigned char value);
extern void ted_sound_init(unsigned int mixingFreq);

#endif
