#pragma once

#include "types.h"

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
	void *callBackParam;
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
	static unsigned int tod2frames(TOD &todin);
	static void frames2tod(unsigned int frames, TOD &todout, unsigned int frq);
	unsigned int todCount, alarmCount;
	static unsigned int refCount;
	void setIrqCallback(CallBackFunctor irqCallback_, void *param) {
		irqCallback = irqCallback_;
		callBackParam = param;
	}

	friend class Vic2mem;
};
