#pragma once

#include "types.h"

#define WL_CIA_CORE

#ifndef WL_CIA_CORE

class Cia
{
private:
	unsigned char pra;
	unsigned char prb;
	unsigned char ddra;
	unsigned char ddrb;
	unsigned char prbTimerMode;
	unsigned char prbTimerToggle;
	unsigned char prbTimerOut;
	int ta;
	int tb;
	int latcha;
	int latchb;
	int taFeed;
	int tbFeed;
	int taReload;
	int tbReload;
	unsigned char cra;
	unsigned char crb;
	unsigned char sdr;
	unsigned int sdrShiftCnt;
	unsigned char icr;
	unsigned char irq_mask;
	bool icrRead;
	struct TOD {
		unsigned int sec;
		unsigned int min;
		unsigned int tenths;
		unsigned int halt;
		unsigned int hr;
		unsigned int ampm;
		bool latched;
	} alm, tod, todLatch;
	unsigned int todIn;
	bool pendingIrq;
	CallBackFunctor irqCallback;
	void* callBackParam;
	bool isNewCia;

public:
	Cia() { refCount++; irqCallback = 0; }
	~Cia() { refCount--; }
	unsigned char reg[16];
	void reset();
	void write(unsigned int addr, unsigned char value);
	unsigned char read(unsigned int addr);
	void checkTimerAUnderflow();
	void checkTimerBUnderflow(int cascaded);
	virtual void setIRQflag(unsigned int mask);
	void countTimers();
	void countTimerB(const int cascaded);
	void setTimerMode(const unsigned int flag, const unsigned int tv, unsigned int cr);
	void todUpdate();
	static unsigned int bcd2hex(unsigned int bcd);
	static unsigned int hex2bcd(unsigned int hex);
	static unsigned int tod2frames(TOD& todin);
	static void frames2tod(unsigned int frames, TOD& todout, unsigned int frq);
	unsigned int todCount, alarmCount;
	static unsigned int refCount;
	void setIrqCallback(CallBackFunctor irqCallback_, void* param) {
		irqCallback = irqCallback_;
		callBackParam = param;
	}

	friend class Vic2mem;
};

#else

class Cia {
public:
	enum {
		CountA0 = 0x00000001,
		CountA1 = 0x00000002,
		CountA2 = 0x00000004,
		CountA3 = 0x00000008,
		CountB0 = 0x00000010,
		CountB1 = 0x00000020,
		CountB2 = 0x00000040,
		CountB3 = 0x00000080,
		LoadA0 = 0x00000100,
		LoadA1 = 0x00000200,
		LoadB0 = 0x00000400,
		LoadB1 = 0x00000800,
		PB6Low0 = 0x00001000,
		PB6Low1 = 0x00002000,
		PB7Low0 = 0x00004000,
		PB7Low1 = 0x00008000,
		Interrupt0 = 0x00010000,
		Interrupt1 = 0x00020000,
		OneShotA0 = 0x00040000,
		OneShotB0 = 0x00080000
	};

	static const unsigned int DelayMask = ~(0x00100000 | CountA0 | CountB0 | LoadA0 | LoadB0 | PB6Low0 | PB7Low0 | Interrupt0 | OneShotA0 | OneShotB0);

	unsigned char reg[16];
	unsigned char pra, prb, ddra, ddrb;
	unsigned char icr, irq_mask;
	unsigned short ta, tb, latcha, latchb;
	unsigned char cra, crb;
	unsigned char sdr;

	unsigned int dwDelay;
	unsigned int dwFeed;

	unsigned char prbTimerOut, prbTimerMode, prbTimerToggle;
	unsigned char sdrShiftCnt;

	unsigned int todCount, alarmCount, todIn;
	bool pendingIrq, isNewCia;

	struct TOD {
		unsigned char hr, min, sec, tenths, ampm, latched;
		unsigned char halt;
	} tod, todLatch, alm;

	void* callBackParam = nullptr;
	void (*irqCallback)(void*) = nullCallback;
	static void nullCallback(void* nil) {};

	void setIrqCallback(void (*cb)(void*), void* param) {
		irqCallback = cb;
		callBackParam = param;
	}

	void setIRQflag(unsigned char flag) {
		icr |= flag;
		if (irq_mask & flag) {
			dwDelay |= Interrupt0;
		}
	}

	void reset() {
		for (int i = 0; i < 16; i++) reg[i] = 0;
		pra = prb = ddra = ddrb = 0;
		icr = irq_mask = 0;
		ta = tb = 0;
		latcha = latchb = 0xFFFF;
		cra = crb = 0;
		dwDelay = 0;
		dwFeed = 0;
		prbTimerOut = 0;
		prbTimerMode = 0;
		prbTimerToggle = 0;
		sdrShiftCnt = 0;
		todCount = 60 * 60 * 50;
		alarmCount = -1;
		tod.latched = false;
		tod.halt = 1;
		todIn = 60;
		tod.ampm = 0;
		pendingIrq = false;
		isNewCia = true;
	}

	unsigned int bcd2hex(unsigned int bcd) { return (((bcd & 0xf0) >> 4) * 10) + (bcd & 0xF); }
	unsigned int hex2bcd(unsigned int hex) { return ((hex / 10) << 4) + (hex % 10); }

	unsigned int tod2frames(TOD& todin) {
		return bcd2hex(todin.hr) * 180000 + bcd2hex(todin.min) * 3000 + bcd2hex(todin.sec) * 50 + bcd2hex(todin.tenths) * 5;
	}

	void frames2tod(unsigned int frames, TOD& todout, unsigned int frq) {
		unsigned int hours = frames * frq / 180000 / 50;
		frames -= hours * 180000;
		unsigned int minutes = frames / 3000;
		frames -= minutes * 3000;
		unsigned int seconds = frames / 50;
		frames -= seconds * 50;
		unsigned int tenths = frames / 5;
		todout.hr = hex2bcd(hours);
		todout.min = hex2bcd(minutes);
		todout.sec = hex2bcd(seconds);
		todout.tenths = hex2bcd(tenths);
	}

	void todUpdate() {
		if (!tod.halt) {
			todCount++;
			if (alarmCount == todCount) {
				setIRQflag(0x04);
			}
			if (todCount == 12 * 60 * 60 * 50) {
				tod.ampm ^= 0x80;
				todCount = 0;
			}
		}
	}

	void write(unsigned int addr, unsigned char value) {
		addr &= 0x0F;
		reg[addr] = value;

		switch (addr) {
		case 0x00: pra = value; break;
		case 0x01: prb = value; break;
		case 0x02: ddra = value; break;
		case 0x03: ddrb = value; break;
		case 0x04: latcha = (latcha & 0xFF00) | value; break;
		case 0x05:
			latcha = (latcha & 0x00FF) | (value << 8);
			if ((cra & 0x01) == 0) {
				dwDelay |= LoadA0;
			}
			break;
		case 0x06: latchb = (latchb & 0xFF00) | value; break;
		case 0x07:
			latchb = (latchb & 0x00FF) | (value << 8);
			if ((crb & 0x01) == 0) {
				dwDelay |= LoadB0;
			}
			break;
		case 0x08:
		case 0x09:
		case 0x0A:
		case 0x0B:
			tod.halt = (addr == 0x0B) ? 1 : 0;
			break;
		case 0x0C: sdr = value; sdrShiftCnt = 8; break;
		case 0x0D:
			if (value & 0x80) irq_mask |= (value & 0x1F);
			else irq_mask &= ~(value & 0x1F);
			if ((irq_mask & icr) != 0) {
				dwDelay |= Interrupt0;
			}
			break;
		case 0x0E: {
			unsigned char old_cra = cra;
			if ((value & 0x21) == 0x01) {
				dwDelay |= CountA1 | CountA0;
				dwFeed |= CountA0;
			}
			else {
				dwDelay &= ~(CountA1 | CountA0);
				dwFeed &= ~CountA0;
			}

			if (value & 0x08) dwFeed |= OneShotA0;
			else dwFeed &= ~OneShotA0;

			if (value & 0x10) {
				dwDelay |= LoadA0;
			}

			if ((value & 0x01) && !(old_cra & 0x01)) {
				prbTimerToggle |= 0x40;
			}

			if ((value & 0x02) == 0) {
				prbTimerMode &= ~0x40;
			}
			else {
				prbTimerMode |= 0x40;
				if ((value & 0x04) == 0) {
					if (dwDelay & PB6Low1) prbTimerOut |= 0x40;
					else prbTimerOut &= ~0x40;
				}
				else {
					prbTimerOut = (prbTimerOut & ~0x40) | (prbTimerToggle & 0x40);
				}
			}
			cra = value & 0xEF;
			todIn = (value & 0x80) ? 50 : 60;
			break;
		}
		case 0x0F: {
			unsigned char old_crb = crb;
			if ((value & 0x61) == 0x01) {
				dwDelay |= CountB1 | CountB0;
				dwFeed |= CountB0;
			}
			else {
				dwDelay &= ~(CountB1 | CountB0);
				dwFeed &= ~CountB0;
			}

			if (value & 0x08) dwFeed |= OneShotB0;
			else dwFeed &= ~OneShotB0;

			if (value & 0x10) {
				dwDelay |= LoadB0;
			}

			if ((value & 0x01) && !(old_crb & 0x01)) {
				prbTimerToggle |= 0x80;
			}

			if ((value & 0x02) == 0) {
				prbTimerMode &= ~0x80;
			}
			else {
				prbTimerMode |= 0x80;
				if ((value & 0x04) == 0) {
					if (dwDelay & PB7Low1) prbTimerOut |= 0x80;
					else prbTimerOut &= ~0x80;
				}
				else {
					prbTimerOut = (prbTimerOut & ~0x80) | (prbTimerToggle & 0x80);
				}
			}
			crb = value & 0xEF;
			break;
		}
		}
	}

	unsigned char read(unsigned int addr) {
		addr &= 0x0F;
		switch (addr) {
		case 0x00: return (pra & ddra) | ~ddra;
		case 0x01: return (((prb & ddrb) | ~ddrb) & ~prbTimerMode) | (prbTimerOut & prbTimerMode);
		case 0x02: return ddra;
		case 0x03: return ddrb;
		case 0x04: return ta & 0xFF;
		case 0x05: return ta >> 8;
		case 0x06: return tb & 0xFF;
		case 0x07: return tb >> 8;
		case 0x0D: {
			unsigned char retval = icr;
			if (icr & irq_mask & 0x1F) {
				retval |= 0x80;
			}
			dwDelay &= ~(Interrupt0 | Interrupt1);
			icr = 0;
			return retval;
		}
		case 0x0E: return cra;
		case 0x0F: return crb;
		}
		return reg[addr];
	}

	void countTimers() {
		if (dwDelay & CountA3) {
			ta--;
		}

		if (ta == 0 && (dwDelay & CountA2)) {
			setIRQflag(0x01);

			prbTimerToggle ^= 0x40;

			if (cra & 0x02) {
				if ((cra & 0x04) == 0) {
					prbTimerOut |= 0x40;
					dwDelay |= PB6Low0;
					dwDelay &= ~PB6Low1;
				}
				else {
					prbTimerOut ^= 0x40;
				}
			}

			if ((dwDelay | dwFeed) & OneShotA0) {
				cra &= ~0x01;
				dwDelay &= ~(CountA2 | CountA1 | CountA0);
				dwFeed &= ~CountA0;
			}

			if ((crb & 0x61) == 0x41 || (crb & 0x61) == 0x61) {
				dwDelay |= CountB1;
			}

			dwDelay |= LoadA1;
		}

		if (dwDelay & LoadA1) {
			ta = latcha;
			dwDelay &= ~CountA2;
		}

		if (dwDelay & CountB3) {
			tb--;
		}

		if (tb == 0 && (dwDelay & CountB2)) {
			setIRQflag(0x02);

			prbTimerToggle ^= 0x80;

			if (crb & 0x02) {
				if ((crb & 0x04) == 0) {
					prbTimerOut |= 0x80;
					dwDelay |= PB7Low0;
					dwDelay &= ~PB7Low1;
				}
				else {
					prbTimerOut ^= 0x80;
				}
			}

			if ((dwDelay | dwFeed) & OneShotB0) {
				crb &= ~0x01;
				dwDelay &= ~(CountB2 | CountB1 | CountB0);
				dwFeed &= ~CountB0;
			}

			dwDelay |= LoadB1;
		}

		if (dwDelay & LoadB1) {
			tb = latchb;
			dwDelay &= ~CountB2;
		}

		if (dwDelay & (PB6Low1 | PB7Low1)) {
			if (dwDelay & PB6Low1) prbTimerOut &= ~0x40;
			if (dwDelay & PB7Low1) prbTimerOut &= ~0x80;
		}

		if (dwDelay & Interrupt1) {
			icr |= 0x80;
			irqCallback(callBackParam);
		}

		dwDelay = ((dwDelay << 1) & DelayMask) | dwFeed;
	}
	friend class Vic2mem;
};

#endif
