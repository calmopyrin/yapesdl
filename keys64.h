#pragma once
#include "keyboard.h"

class KEYS64 :
	public KEYS
{
public:
	KEYS64(void);
	~KEYS64(void);
	unsigned char feedkey(unsigned char latch);
	unsigned char feedKeyColumn(unsigned char column);
	unsigned char feedjoy();
	unsigned char getJoyState(unsigned int j);

protected:
	unsigned char keyReadMatrixRow(unsigned int r);
	unsigned char latchedJoy;
};
