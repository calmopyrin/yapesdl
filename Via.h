#pragma once

#include "types.h"
#include <cstdio>

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
	unsigned short t1c, t1l, t2c, t2l;
	unsigned int t1t, t2t, t1r, t2r;
	unsigned char sr, acr, pcr, ifr, ier;
	unsigned char pb67;
	unsigned char reg[16];
	bool t1latch;
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
		if (!t1c--) {
			// always run, trigger reload from latch
			t1r = 1;
			// free-run mode or first timeout?
			bool cont = !!(acr & 0x40);
			if (cont || !t1t) {
				// IRQ only if hi order latch was updated or enabled? (fixme: bandits on vic20 fails)
				if (t1latch) //  && t1latch (ier & IRQM_T1)
				{
					ifr |= IRQM_T1;
					checkIrqCallback(callBackParam, ifr & ier);
				}
				// toggle PB7 only if above a threshold of 1 cycle
				if (acr & 0x80 && t1l > 1) {
					pb67 ^= 0x80;
				}
			}
			t1t = 1;
		}
	}
	// not counting falling edge of PB6?
	if (!(acr & 0x20)) {
		if (t2r) {
			t2r = 0;
		}
		else {
			if (!t2c--) {
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
