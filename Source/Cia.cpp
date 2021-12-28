#include "Cia.h"

unsigned int Cia::refCount = 0;

void Cia::reset()
{
	pra = prb = 0;
	ddra = ddrb = 0;
	icr = 0;
	irq_mask = 0;
	ta = tb = taFeed = tbFeed = 0;
	latcha = latchb = 0xFFFF;
	tbReload = 0;
	cra = crb = 0;
	prbTimerOut = 0;
	prbTimerMode = 0;
	prbTimerToggle = 0x80;
	sdrShiftCnt = 0;
	// ToD
	todCount = 60 * 60 * 50; // set to 1hr at reset
	alarmCount = -1;
	tod.latched = false;
	tod.halt = 1;
	todIn = 60;
	tod.ampm = 0;
	pendingIrq = false;
}

void Cia::setIRQflag(unsigned int mask)
{
	if (mask) {
		if (!(icr & 0x80)) {
#if 0
			pendingIrq = true;
#else
			icr |= 0x80;
			irqCallback(callBackParam);
#endif
		}
	}
}

unsigned int Cia::bcd2hex(unsigned int bcd)
{
	return (((bcd & 0xf0) >> 4) * 10) + (bcd & 0xf);
}

unsigned int Cia::hex2bcd(unsigned int hex)
{
	return ((hex / 10) << 4) + (hex % 10);
}

// called after each new frame
void Cia::todUpdate()
{
	if (!tod.halt) {
		todCount += 1;
		if (alarmCount == todCount) {
			// set alarm IRQ
			icr |= 4;
			setIRQflag(irq_mask & icr);
		}
		if (todCount == 12 * 60 * 60 * 50) {// 12 AM/PM
			tod.ampm ^= 0x80;
			todCount = 0;
		}
#if 0
		TOD time;
		frames2tod(todCount, time, todIn);
		// if (!(todCount % 2000))
		fprintf(stderr, "Count:%09u Time: %02xh:%02Xm:%02Xs:%02Xths.\n", todCount, time.hr, time.min, time.sec, time.tenths);
#endif
	}
}

unsigned int Cia::tod2frames(TOD &todin)
{
	unsigned int newmsec =
		bcd2hex(todin.hr) * 180000 +
		bcd2hex(todin.min) * 3000 +
		bcd2hex(todin.sec) * 50 +
		bcd2hex(todin.tenths) * 5;
	return newmsec;
}

void Cia::frames2tod(unsigned int frames, TOD &todout, unsigned int frq)
{
	unsigned int hours = frames * frq / 180000 / 50;
	frames = frames - hours * 180000;
	unsigned int minutes = frames / 3000;
	frames = frames - minutes * 3000;
	unsigned int seconds = frames / 50;
	frames = frames - seconds * 50;
	unsigned int tenths = frames / 5;

	todout.hr = hex2bcd(hours);
	todout.min = hex2bcd(minutes);
	todout.sec = hex2bcd(seconds);
	todout.tenths = hex2bcd(tenths);
}

inline void Cia::setTimerMode(const unsigned int flag, const unsigned int tv, unsigned int cr)
{
	if (cr & 2) {
		prbTimerMode |= flag; // PB6 shows timer underflow state
		if (cr & 4) { // On a timer overflow, PBx is inverted?
			prbTimerOut = (prbTimerOut & ~flag) | (prbTimerToggle & flag);
		}
		else {
			if (!tv) {
				prbTimerOut |= flag;
			}
			else {
				prbTimerOut &= ~flag;
			}
		}
	}
	else
		prbTimerMode &= ~flag;
}

void Cia::write(unsigned int addr, unsigned char value)
{
	//fprintf(stderr, "$(%04X) CIA%i write : %02X @ PC=%04X\n", addr, refCount, value, theTed->cpuptr->getPC());
	addr &= 0xF;
	switch (addr) {
	case 0x00:
		pra = value;
		break;

	case 0x01:
		prb = value;
		break;

	case 0x02:
		ddra = value;
		break;

	case 0x03:
		ddrb = value;
		break;

	case 0x04:
		latcha = (latcha & 0xFF00) | value;
		break;

	case 0x05:
		latcha = (latcha & 0xFF) | (value << 8);
		// Reload timer A only if stopped
		if (!(cra & 1)) {
			taReload = 1;
		}
		break;

	case 0x06:
		latchb = (latchb & 0xFF00) | value;
		break;

	case 0x07:
		latchb = (latchb & 0xFF) | (value << 8);
		// Reload timer B only if stopped
		if (!(crb & 1)) {
			tbReload = 1;
		}
		break;

	case 0x08:
		if (crb & 0x80) {
			frames2tod(alarmCount, alm, todIn);
			alm.tenths = value & 0x0F;
			alarmCount = tod2frames(alm);
		}
		else {
			if (!tod.halt)
				frames2tod(todCount, tod, todIn);
			tod.tenths = value & 0x0F;
			if (!tod.halt)
				todCount = tod2frames(tod);
		}
		tod.halt = false;
		break;

	case 0x09:
		if (crb & 0x80) {
			frames2tod(alarmCount, alm, todIn);
			alm.sec = value & 0x7F;
			alarmCount = tod2frames(alm);
		}
		else {
			if (!tod.halt)
				frames2tod(todCount, tod, todIn);
			tod.sec = value & 0x7F;
			if (!tod.halt)
				todCount = tod2frames(tod);
		}
		break;

	case 0x0A:
		if (crb & 0x80) {
			frames2tod(alarmCount, alm, todIn);
			alm.min = value & 0x7F;
			alarmCount = tod2frames(alm);
		}
		else {
			if (!tod.halt)
				frames2tod(todCount, tod, todIn);
			tod.min = value & 0x7F;
			if (!tod.halt)
				todCount = tod2frames(tod);
		}
		break;

	case 0x0B:
		if (crb & 0x80) {
			frames2tod(alarmCount, alm, todIn);
			alm.hr = value & 0x9F;
			alarmCount = tod2frames(alm);
		}
		else {
			if (!tod.halt)
				frames2tod(todCount, tod, todIn);
			tod.hr = value & 0x9F;
			//todCount = tod2frames(tod);
		}
		tod.halt = true;
		break;

	case 0x0C:
		sdr = value;
		sdrShiftCnt = 8;
		break;

	case 0x0D:
		if (value & 0x80)
			irq_mask |= (value & 0x1F);
		else
			irq_mask &= ~(value & 0x1F);
		setIRQflag(icr & irq_mask);
		break;

	case 0x0E:
		// rising edge of CRA0 sets PB6 toggle
		if (!(cra & 1) && (value & 1))
			prbTimerToggle |= 0x40;
		cra = value & 0xEF;
		if (!(value & 1))
			taFeed = 0;
		// Forced reload
		if (value & 0x10) {
			taReload = 1;
			value &= ~0x10;
			taFeed &= ~1;
		}
		// set Timer A mode
		setTimerMode(0x40, ta, cra);
		// ToD clock rate
		todIn = value & 0x80 ? 50 : 60;
		break;

	case 0x0F:
		// rising edge of CRB0 sets PB7 toggle
		if (!(crb & 1) && (value & 1))
			prbTimerToggle |= 0x80;
		crb = value & 0xEF;
		if (!(crb & 1))
			tbFeed = 0;
		// Forced reload
		if (value & 0x10) {
			tbReload = 1;
			value &= ~0x10;
			tbFeed &= ~1;
		}
		// set Timer B mode
		setTimerMode(0x80, tb, crb);
		break;
	}
	reg[addr] = value;
}

unsigned char Cia::read(unsigned int addr)
{
	addr &= 0x0F;
	switch (addr) {
	case 0x00:
		return pra | ~ddra;
	case 0x01:
	{
		unsigned char retval;
		retval = ((prb | ~ddrb) & ~prbTimerMode) | (prbTimerOut & prbTimerMode);
		return retval;
	}
	case 0x02:
		return ddra;
	case 0x03:
		return ddrb;
	case 0x04:
		return ta & 0xFF;
	case 0x05:
		return ta >> 8;
	case 0x06:
		return tb & 0xFF;
	case 0x07:
		return tb >> 8;
	case 0x08:
		if (tod.latched) {
			tod.latched = false;
			return todLatch.sec;
		}
		else {
			frames2tod(todCount, tod, todIn);
			return tod.tenths;
		}
	case 0x09:
		if (tod.latched)
			return todLatch.sec;
		else {
			frames2tod(todCount, tod, todIn);
			return tod.sec;
		}
	case 0x0A:
		if (tod.latched)
			return todLatch.min;
		else {
			frames2tod(todCount, tod, todIn);
			return tod.min;
		}
	case 0x0B:
		if (!tod.halt) {
			frames2tod(todCount, tod, todIn);
		}
		tod.latched = true;
		todLatch = tod;
		return todLatch.hr | tod.ampm;
	case 0x0C:
		return sdr;
	case 0x0D:
	{
		unsigned char retval = icr & 0x9F;
		icr = 0;
		pendingIrq = false;
		return retval;
	}
	case 0x0E:
		return cra;
	case 0x0F:
		return crb;
	}
	return reg[addr];
}

void Cia::checkTimerAUnderflow()
{
	if (!ta && (taFeed & 1)) {
		icr |= 0x01; // Set timer A IRQ flag
		setIRQflag(icr & irq_mask); // FIXME, 1 cycle delay
		if ((crb & 0x41) == 0x41) { // cascaded timer? CNT pin is high by default
			tbFeed |= 1;
			countTimerB(-1);
		}
		prbTimerToggle ^= 0x40; // PRA7 underflow count toggle
								// timer A output to PB6?
		if (cra & 2) {
			// set PRA6 high for one clock cycle
			if (cra & 4) {
				prbTimerOut ^= 0x40; // toggle PRB6 between 1 and 0
			}
			else {
				prbTimerOut |= 0x40; // set high for one clock
			}
		}
		//prbTimerOut = (prbTimerOut & ~0x40) | (prbTimerToggle & 0x40);
		if (cra & 8) { // One-shot?
			cra &= 0xFE; // Stop timer
			taFeed = 0;
		}
		taReload = 1;
	}
}

void Cia::checkTimerBUnderflow(const int cascaded)
{
	if (tb == cascaded && (tbFeed & 1)) {
		icr |= 0x02; // Set timer B IRQ flag
		setIRQflag(icr & irq_mask); // FIXME, 1 cycle delay on later CIA's
		prbTimerToggle ^= 0x80; // PRB7 underflow count toggle
								// timer A output to PB6?
		if (crb & 2) {
			// set PRB7 high for one clock cycle
			if (crb & 4) {
				prbTimerOut ^= 0x80; // toggle PRB7 between 1 and 0
			}
			else {
				prbTimerOut |= 0x80; // set high for one clock
			}
		}
		//prbTimerOut = (prbTimerOut & ~0x80) | (prbTimerToggle & 0x80);
		if (crb & 8) {// One-shot?
			crb &= 0xFE; // Stop timer
			tbFeed = 0;
		}
		// Reload from latch if not cascading
		tbReload = 1;
	}
}

void Cia::countTimerB(int cascaded)
{
	tb -= (tbFeed & 1);
	tbFeed = (tbFeed >> 1) | ((crb & 1) << 1);
	// Underflow?
	checkTimerBUnderflow(cascaded);
	if (tbReload) {
		tbReload = 0;
		// Reload from latch
		tb = latchb;
		// skip decrement in next cycle
		tbFeed &= ~1;
	}
}

void Cia::countTimers()
{
	if (pendingIrq) {
		icr |= 0x80;
		irqCallback(callBackParam);
		pendingIrq = false;
	}
	if ((cra & 0x40) && sdrShiftCnt) {
		sdrShiftCnt -= 1;
		if (!sdrShiftCnt) {
			icr |= 8;
			setIRQflag(icr & irq_mask);
		}
	}
	if (!(cra & 4)) {
		prbTimerOut &= ~0x40; // reset PRB6
	}
	if ((cra & 0x20) == 0x00) {
		ta -= (taFeed & 1);
		taFeed = (taFeed >> 1) | ((cra & 1) << 1);
		// Underflow?
		checkTimerAUnderflow();
		if (taReload) {
			taReload = 0;
			// Reload from latch
			ta = latcha;
			// skip decrement in next cycle
			taFeed &= ~1;
		}
	}
	if (!(crb & 4)) {
		prbTimerOut &= ~0x80; // reset PRB7
	}
	if ((crb & 0x60) == 0x00) { // TimerB counting phi clock cycles?
		countTimerB(0);
	}
}
