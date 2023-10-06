#include "sound.h"
#include "tedmem.h"

#define PRECISION 4
#define OSCRELOADVAL (0x3FF << PRECISION)

static int             Volume;
static int             channelStatus[2];
static int             SndNoiseStatus;
static int             DAStatus;
static unsigned short  Freq[2];
static int             NoiseCounter;
static int             FlipFlop;
static int             oscCount[2];
static int             OscReload[2];
static int             oscStep;
static unsigned char    noise[256]; // 0-8
static unsigned int		MixingFreq;
static unsigned int		originalFreq;
static int				volumeTable[64];
static int				cachedDigiSample;
static int				cachedSoundSample[2];

void TED::tedSoundInit(unsigned int mixingFreq)
{
	originalFreq = TED_SOUND_CLOCK / 8;
	MixingFreq = mixingFreq;
	setClockStep(originalFreq, MixingFreq);
	FlipFlop = 0;
	oscCount[0] = oscCount[1] = 0;
	NoiseCounter = 0;
	Freq[0] = Freq[1] = 0;
	DAStatus = cachedDigiSample = 0;
	cachedSoundSample[0] = cachedSoundSample[1] = 0;

	unsigned char im = 0xff;
	for (int i=0; i < 256; i++) {
		im = (im << 1)|(((im >> 7) ^ (im >> 5) ^ (im >> 4) ^ (im >> 1)) & 1);
		noise[i] = (im & 1) << 5;
	}
	for (int i = 0; i < 64; i++) {
		int chdbl = (i & 0x30) == 0x30;
		int vol = ((i & 0x0F) < 9 ? (i & 0x0F) : 8);
		int nonl = ((chdbl && vol > 1) ? vol * vol * 54 - 173 * vol + 162 : 0);
		volumeTable[i] = (vol && i & 0x30) ? 
			(nonl + (586 + (vol - 1) * 1024) << (chdbl ? 1 : 0)) : 0;
	}
}

void TED::setClockStep(unsigned int originalFreq, unsigned int samplingFreq)
{
	oscStep = (int)((originalFreq * (double)(1 << PRECISION)) / (double)(samplingFreq) + 0.5);
}

void TED::setFrequency(unsigned int frequency)
{
	originalFreq = frequency;
	setClockStep(frequency, MixingFreq);
}

void TED::setSampleRate(unsigned int sampleRate)
{
	MixingFreq = sampleRate;
	setClockStep(originalFreq, sampleRate);
}

void TED::calcSamples(short *buffer, unsigned int nrsamples)
{
	// Rendering...
	// Calculate the buffer...
	if (DAStatus) {// digi?
		for (;nrsamples--;) {
			*buffer++ = cachedDigiSample;
		}
	} else {
		for (;nrsamples--;) {
			// Channel 1
			if (OscReload[0] != (0x3FF << PRECISION)) {
				if ((oscCount[0] += oscStep) >= OSCRELOADVAL) {
					FlipFlop ^= 0x10;
					cachedSoundSample[0] = Volume | (FlipFlop & channelStatus[0]);
					oscCount[0] = OscReload[0] + (oscCount[0] - OSCRELOADVAL);
				}
			}
			// Channel 2
			if (OscReload[1] != (0x3FF << PRECISION)) {
				if ((oscCount[1] += oscStep) >= OSCRELOADVAL) {
					FlipFlop ^= 0x20;
					cachedSoundSample[1] = Volume | (FlipFlop & channelStatus[1]) | (noise[NoiseCounter] & SndNoiseStatus);
					if (++NoiseCounter == 255)
						NoiseCounter = 0;
					oscCount[1] = OscReload[1] + (oscCount[1] - OSCRELOADVAL);
				}
			}
			*buffer++ = volumeTable[cachedSoundSample[0] | cachedSoundSample[1]];
		}   // for
	}
}

inline void setFreq(unsigned int channel, int freq)
{
	if (freq == 0x3FE) {
		FlipFlop |= 0x10 << channel;
		if (!channel)
			cachedSoundSample[0] = Volume | (channelStatus[0]);
		else
			cachedSoundSample[1] = Volume | (channelStatus[1] | (SndNoiseStatus >> 1));
	}
	OscReload[channel] = ((freq + 1) & 0x3FF) << PRECISION;
}

void TED::writeSoundReg(ClockCycle cycle, unsigned int reg, unsigned char value)
{
	flushBuffer(cycle, TED_SOUND_CLOCK);

	switch (reg) {
		case 0:
			Freq[0] = (Freq[0] & 0x300) | value;
			setFreq(0, Freq[0]);
			break;
		case 1:
			Freq[1] = (Freq[1] & 0x300) | value;
			setFreq(1, Freq[1]);
			break;
		case 2:
			Freq[1] = (Freq[1] & 0xFF) | (value << 8);
			setFreq(1, Freq[1]);
			break;
		case 3:
			if ((DAStatus = (value & 0x80))) {
				FlipFlop = 0x30;
				oscCount[0] = OscReload[0];
				oscCount[1] = OscReload[1];
				NoiseCounter = 0;
				cachedDigiSample = volumeTable[value & 0x3F];
			}
			Volume = value & 0x0F;
			channelStatus[0] = value & 0x10;
			channelStatus[1] = value & 0x20;
			SndNoiseStatus = ((value & 0x40) >> 1) & (channelStatus[1] ^ 0x20);
			cachedSoundSample[0] = Volume | (FlipFlop & channelStatus[0]);
			cachedSoundSample[1] = Volume | (FlipFlop & channelStatus[1]) | (noise[NoiseCounter] & SndNoiseStatus);
			break;
		case 4:
			Freq[0] = (Freq[0] & 0xFF) | (value << 8);
			setFreq(0, Freq[0]);
			break;
	}
}

