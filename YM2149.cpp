#include "YM2149.h"

YM2149::YM2149(unsigned int sampleRate_)
{
	ym2149 = PSG_new(sampleRate_, SAMPLE_FREQ);
	//PSG_setClockDivider(ym2149, 2);
	PSG_setQuality(ym2149, 1);
	PSG_setVolumeMode(ym2149, 2); // AY style
	sampleRate = sampleRate_;
	reset();
}

YM2149::~YM2149()
{
	free(ym2149);
	ym2149 = NULL;
}

void YM2149::reset(bool hard)
{
	PSG_reset(ym2149);
}

void YM2149::setFrequency(unsigned int frequency)
{
	PSG_setClock(ym2149, frequency);
}

void YM2149::setSampleRate(unsigned int sampleRate_)
{
	sampleRate = sampleRate_;
	PSG_setRate(ym2149, sampleRate_);
}

void YM2149::calcSamples(short* buf, unsigned int count)
{
	for (unsigned int i = 0; i < count; i++)
		buf[i] = -PSG_calc(ym2149);
}

void YM2149::write(unsigned int reg, unsigned char value)
{
	switch (reg) {
	default:
	case 1:
		break;
	case 2:
		PSG_writeReg(ym2149, ym2149RegSelect, value);
		break;
	case 3:
		ym2149RegSelect = value & 0x0F;
		break;
	}
}

unsigned char YM2149::read(unsigned int reg)
{
	return PSG_readReg(ym2149, ym2149RegSelect);
}