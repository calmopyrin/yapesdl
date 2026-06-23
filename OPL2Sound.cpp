#include "OPL2Sound.h"

OPL2Sound::OPL2Sound(unsigned int sampleRate_)
{
	chip = new opl3_chip;
	sampleRate = sampleRate_;
	reset();
}

OPL2Sound::~OPL2Sound()
{
	delete chip;
	chip = NULL;
}

void OPL2Sound::reset()
{
	OPL3_Reset(chip, sampleRate);
}

void OPL2Sound::setFrequency(unsigned int frequency)
{
	sampleRate = frequency;
	OPL3_Reset(chip, sampleRate);
}

void OPL2Sound::setSampleRate(unsigned int sampleRate_)
{
	sampleRate = sampleRate_;
	OPL3_Reset(chip, sampleRate);
}

void OPL2Sound::calcSamples(short* buf, unsigned int count)
{
	OPL3_GenerateStreamMono(chip, buf, count);
}

void OPL2Sound::write(unsigned int reg, unsigned char value)
{
	if (reg >= (isOPL3model | 0xFF))
		return;

	if (reg & 1)
		OPL3_WriteRegBuffered(chip, oplRegSelect, value);
	else
		oplRegSelect = value;
}

unsigned char OPL2Sound::read(unsigned int reg)
{
	if (!(reg & 1) || isOPL3model)
		return OPL3_ReadReg(chip, reg);
	return 0;
}