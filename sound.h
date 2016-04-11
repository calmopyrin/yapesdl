#ifndef _SOUND_H
#define _SOUND_H

#include "types.h"

#ifdef __EMSCRIPTEN__
#define SAMPLE_FREQ 48000 // 44100
#else
#define SAMPLE_FREQ 48000 //48000 192000
#endif

//#define AUDIO_CALLBACK

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
	static void setSamplingRate(unsigned int sampleRate_) {
		sampleRate = sampleRate_;
	}
    virtual void calcSamples(short *buffer, unsigned int nrsamples) = 0;
	virtual void setFrequency(unsigned int frequency) = 0;
	virtual void setSampleRate(unsigned int sampleRate) = 0;
private:
    char name[16];
protected:
	static unsigned int sampleRate;
};

extern void init_audio(unsigned int sampleFrq = SAMPLE_FREQ);
extern void close_audio();
extern void sound_pause();
extern void sound_resume();
extern void sound_reset();
extern void sound_change_freq(unsigned int &newFreq);
extern void flushBuffer(ClockCycle cycle, unsigned int frq);
extern rvar_t soundSettings[];

#endif
