#pragma once

#include "types.h"

class Via
{
public:
	Via() : checkIrqCallback(dummyChkIrqCallback) {
		reset();
	}
	enum {
		IRQM_CTRL = 0x80,
		IRQM_T1 = 0x40,
		IRQM_T2 = 0x20,
		IRQM_CB1 = 0x10,
		IRQM_CB2 = 8,
		IRQM_SR = 4,
		IRQM_CA1 = 2,
		IRQM_CA2 = 1
	};
	enum viaPins {
		INPUTA = 0, INPUTB, INPUTCA1, INPUTCA2, INPUTCB1, INPUTCB2
	};
	unsigned char pra, ddra, prb, ddrb;
	unsigned short t1c, t1l, t2c, t2l, t1t, t2t, t1r, t2r;
	unsigned char sr, acr, pcr, ifr, ier;
	unsigned char pb7, pb6;
	bool pb7trigger;
	unsigned char reg[16];
	void* callBackParam;
	void write(unsigned int r, unsigned char value);
	unsigned char read(unsigned int r);
	CallBackFunctorInt8 checkIrqCallback;
	void reset();
	void countTimers();
	void setCheckIrqCallback(void* p, CallBackFunctorInt8 irqCallback_) {
		checkIrqCallback = irqCallback_;
		callBackParam = p;
	}
	static void dummyChkIrqCallback(void*, unsigned char) {
		return;
	}
};

inline void Via::countTimers()
{
	if (t1r) {
		t1c = t1l;
		t1r = 0;
	} else {
		t1c--;
		if (t1c == 0xFFFF) {
			// always reload from latch
			t1r = 1;
			// free-run mode?
			bool cont = !!(acr & 0x40);
			if (cont || !t1t) {
				t1t = cont ? (t1t ^ 1) : 1;
				ifr |= IRQM_T1;
				checkIrqCallback(callBackParam, ifr & ier);
			}
			if ((acr & 0x80) && ((acr & 0x40) || !pb7trigger)) {
				pb7 ^= 0x80;
				pb7trigger = true;
			}
		}
	}
	// not counting falling edge of PB6?
	if (!(acr & 0x20)) {
		if (t2r) {
			t2r = 0;
		}
		else {
			t2c--;
			if (t2c == 0xFFFF) {
				// always one-shot
				if (!t2t) {
					t2t = 1;
					ifr |= IRQM_T2;
					checkIrqCallback(callBackParam, ifr & ier);
				}
			}
		}
	}
}
