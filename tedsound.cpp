#include "sound.h"
#include "tedmem.h"

#define PRECISION 4
#define OSCRELOADVAL (0x400 << PRECISION)

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

void TED::calcSamples(short* buffer, unsigned int nrsamples)
{
	if (DAStatus) {
		while (nrsamples--)
			*buffer++ = cachedDigiSample;
		return;
	}

	int osc0 = oscCount[0];
	int osc1 = oscCount[1];
	const int reload0 = OscReload[0];
	const int reload1 = OscReload[1];
	const bool ch0_active = reload0 != (0x3FF << PRECISION);
	const bool ch1_active = reload1 != (0x3FF << PRECISION);
	const int ch0_status = channelStatus[0];
	const int ch1_status = channelStatus[1];
	const int noise_status = SndNoiseStatus;

	int css0 = cachedSoundSample[0];
	int css1 = cachedSoundSample[1];
	int cssOred = css0 | css1;
	int ff = FlipFlop;

	const int step = oscStep;

	int nc = NoiseCounter;

	while (nrsamples--) {
		// Channel 1
		if (ch0_active) {
			osc0 += step;
			if (osc0 >= OSCRELOADVAL) {
				ff ^= 0x10;
				css0 = Volume | (ff & ch0_status);
				cssOred = css0 | css1;
				osc0 -= OSCRELOADVAL;
				osc0 += reload0;
			}
		}
		// Channel 2
		if (ch1_active) {
			osc1 += step;
			if (osc1 >= OSCRELOADVAL) {
				ff ^= 0x20;
				css1 = Volume | (ff & ch1_status) |
					(noise[nc] & noise_status);
				cssOred = css0 | css1;
				nc = (nc + 1) % 0xFF;
				osc1 -= OSCRELOADVAL;
				osc1 += reload1;
			}
		}
		*buffer++ = volumeTable[cssOred];
	}

	oscCount[0] = osc0;
	oscCount[1] = osc1;
	cachedSoundSample[0] = css0;
	cachedSoundSample[1] = css1;
	FlipFlop = ff;
	NoiseCounter = nc;
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

