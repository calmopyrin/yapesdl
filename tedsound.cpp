#include "sound.h"
#include "tedmem.h"
#include "Sid.h"

#define PRECISION 4
#define OSCRELOADVAL (0x3FF << PRECISION)

static int             Volume;
static int             Snd1Status;
static int             Snd2Status;
static int             SndNoiseStatus;
static int             DAStatus;
static unsigned short  			  Freq1;
static unsigned short  			  Freq2;
static int             NoiseCounter;
static int             FlipFlop[2];
static int             oscCount1;
static int             oscCount2;
static int             OscReload[2];
static int             oscStep;
static unsigned char            noise[256]; // 0-8
static unsigned int		MixingFreq;

void ted_sound_init(unsigned int mixingFreq)
{
	//oscStep = (int) (( 2.0 * 110860.45 * (double) (1 << PRECISION)) / (double) (mixingFreq));
	oscStep = (int) (( TED_SOUND_CLOCK * (double) (1 << PRECISION)) / (double) (mixingFreq) + 0.5);
    FlipFlop[0] = 0;
    FlipFlop[1] = 0;
    oscCount1 = 0;
    oscCount2 = 0;
    NoiseCounter = 0;
	Freq1 = Freq2 = 0;
	DAStatus = 0;
	MixingFreq = mixingFreq;

	/* initialise im with 0xa8 */
	int im = 0xa8;
    for (int i=0; i<256; i++) {
		noise[i] = im & 1;
		im = (im<<1)+(1^((im>>7)&1)^((im>>5)&1)^((im>>4)&1)^((im>>1)&1));
    }
}

inline void render_ted_audio(Sint16 *buffer, unsigned int nrsamples)
{
    // Rendering...
	// Calculate the buffer...
	if (DAStatus) {// digi?
		short sample = 0;
		if (Snd1Status) sample += Volume;
		if (Snd2Status) sample += Volume;
		for (;nrsamples--;) {
			*buffer++ = sample;
		}
	} else {
		unsigned int result1, result2;
		for (;nrsamples--;) {
			// Channel 1
			if ((oscCount1 += oscStep) >= OSCRELOADVAL) {
				if (OscReload[0] != (0x3FF << PRECISION))
					FlipFlop[0] ^= 1;
				oscCount1 = OscReload[0] + (oscCount1 - OSCRELOADVAL);
			}
			// Channel 2
			if ((oscCount2 += oscStep) >= OSCRELOADVAL) {
				if (OscReload[1] != (0x3FF << PRECISION)) {
					FlipFlop[1] ^= 1;
					if (NoiseCounter++==256)
						NoiseCounter=0;
				}
				oscCount2 = OscReload[1] + (oscCount2 - OSCRELOADVAL);
			}
			result1 = (FlipFlop[0] && Snd1Status) ? Volume : 0;
			if (Snd2Status) {
				result2 = FlipFlop[1] ? Volume : 0;
			} else if (SndNoiseStatus) {
				result2 = noise[NoiseCounter] ? Volume : 0;
			} else {
				result2 = 0;
			}
			*buffer++ = result1 + result2;
		}   // for
	}
}

void render_audio(unsigned int nrsamples, short *buffer)
{
    render_ted_audio(buffer, nrsamples);
    SIDsound *sid = theTed->getSidCard();

    if (sid != NULL) {
        short temp[SAMPLE_FREQ];
        int i = nrsamples - 1;
        //sid->calcSamplesLQ(temp, nrsamples);
		sid->calcSamples(temp, nrsamples);
        do {
			buffer[i] += temp[i];
		} while(i--);
    }
}

inline void setFreq(unsigned int channel, int freq)
{
	if (freq == 0x3FF) {
		freq = -1;
	} else if (freq == 0x3FE) {
		FlipFlop[channel] = 1;
	}
	OscReload[channel] = ((freq + 1)&0x3FF) << PRECISION;
}

void writeSoundReg(ClockCycle cycle, unsigned int reg, unsigned char value)
{
	flushBuffer(cycle);

	switch (reg) {
		case 0:
			Freq1 = (Freq1 & 0x300) | value;
			setFreq(0, Freq1);
			break;
		case 1:
			Freq2 = (Freq2 & 0x300) | value;
			setFreq(1, Freq2);
			break;
		case 2:
			Freq2 = (Freq2 & 0xFF) | (value << 8);
			setFreq(1, Freq2);
			break;
		case 3:
			if ((DAStatus = (value & 0x80))) {
				FlipFlop[0] = 1;
				FlipFlop[1] = 1;
				oscCount1 = OscReload[0];
				oscCount2 = OscReload[1];
				NoiseCounter = 0xFF;
			}
			Volume = value & 0x0F;
			if (Volume > 8) Volume = 8;
			Volume <<= 10;
			Snd1Status = value & 0x10;
			Snd2Status = value & 0x20;
			SndNoiseStatus = value & 0x40;
			break;
		case 4:
			Freq1 = (Freq1 & 0xFF) | (value << 8);
			setFreq(0, Freq1);
			break;
	}
}
