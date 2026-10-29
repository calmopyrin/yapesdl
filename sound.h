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
	static void bufferFill(unsigned int nrsamples, short *buffer, bool ahead = false);
	static void setSamplingRate(unsigned int sampleRate_) {
		sampleRate = sampleRate_;
	}
	virtual void calcSamples(short *buffer, unsigned int nrsamples) = 0;
	// Samples the output needs before the machine has got that far, to keep it
	// from running dry. A source whose state must stay at the machine's time
	// renders them without advancing that state.
	virtual void calcSamplesAhead(short *buffer, unsigned int nrsamples) { calcSamples(buffer, nrsamples); }
	// Time passes without audio being produced and the state follows the machine.
	virtual void skipSamples(unsigned int nrsamples) {}
	// Samples are being dropped by the output; a source that runs ahead of its
	// samples discards that lead, while its state keeps following the machine.
	virtual void holdSamples() {}
	// The machine cycle (in units of frq) that the samples about to be
	// rendered reach; sources must not run their state past it.
	virtual void setRenderLimit(ClockCycle cycle, unsigned int frq) {}
	static void skipAll(unsigned int nrsamples);
	static void setRenderLimitAll(ClockCycle cycle, unsigned int frq);
	static void holdAll();
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
extern void sound_reset_clock();
extern void sound_change_freq(unsigned int &newFreq);
extern void flushBuffer(ClockCycle cycle, unsigned int frq);
extern rvar_t soundSettings[];

#endif
