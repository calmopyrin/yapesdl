#pragma once

#include "keyboard.h"

class KEYSVIC :
	public KEYS
{
public:
	unsigned char feedkey(unsigned char latch);
	unsigned char feedKeyColumn(unsigned char column);
	unsigned char feedjoy();
	unsigned char getJoyState(unsigned int j);

protected:
	unsigned char keyReadMatrixRow(unsigned int r);
	virtual unsigned int fireButtonIndex(unsigned int a) const { return 4; };
};
