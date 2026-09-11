#pragma once

#include "sound.h"
#include "ym2149/emu2149.h"

class YM2149 :  public SoundSource, public Resettable
{
public:
	YM2149(unsigned int sampleRate_);
	virtual ~YM2149();
	virtual void reset(bool hard = false);
	virtual void setSampleRate(unsigned int sampleRate_);
	virtual void setFrequency(unsigned int frequency);
	virtual void calcSamples(short* buf, unsigned int count);
	void write(unsigned int reg, unsigned char value);
	unsigned char read(unsigned int reg);
protected:
	unsigned int ym2149RegSelect;
	PSG* ym2149 = NULL;
	unsigned int sampleRate;
};

