#pragma once

#include "sound.h"
#include "opl3/opl3.h"

class OPL2Sound :  public SoundSource
{
public:
	OPL2Sound(unsigned int sampleRate_);
	virtual ~OPL2Sound();
	virtual void reset();
	virtual void setSampleRate(unsigned int sampleRate_);
	virtual void setFrequency(unsigned int frequency);
	virtual void calcSamples(short* buf, unsigned int count);
	void write(unsigned int reg, unsigned char value);
	unsigned char read(unsigned int reg);
protected:
	unsigned int oplRegSelect;
	unsigned int isOPL3model = 0;
	opl3_chip* chip = NULL;
	unsigned int sampleRate;
};

