#include "Via.h"
#ifdef _DEBUG
#include <cstdio>
#endif

void Via::write(unsigned int r, unsigned char value) 
{
	switch (r & 0xF) {
	case 0:
		prb = value;
		break;
	case 1:
	case 0xF: // same as #1, no handshake
		ifr &= ~IRQM_CA1;
		pra = value;
		break;
	case 2:
		ddrb = value;
		break;
	case 3:
		ddra = value;
		break;
	case 4:
	case 6:
		t1l = (t1l & 0xFF00) | value;
		break;
	case 5:
		// write T1 high order counter, read from low order latch
		t1l = (t1l & 0xFF) | (value << 8);
		// trigger a reload
		t1latch = true;
		t1r = 1;
		t1t = 0;
		// toggle PB7
		if (acr & 0x80 && t1l > 1)
			pb67 ^= 0x80;
		// Clear T1 IRQ
		ifr &= ~IRQM_T1;
		checkIrqCallback(callBackParam, ifr & ier);
		break;
	case 7:
		// write T1 high order latch
		t1l = (t1l & 0xFF) | (value << 8);
		// despite what official docs state, it does clear the interrupt flag
		ifr &= ~IRQM_T1;
		checkIrqCallback(callBackParam, ifr & ier);
		// if IRQ disabled, disallow it
		if (!(ier & IRQM_T1))
			t1latch = false;
		break;
	case 8:
		// write T2 low order latch
		t2l = (t2l & 0xFF00) | value;
		break;
	case 9:
		// write T2 high order counter, low order latch xfered to low count
		t2c = (t2l & 0xFF) | (value << 8);
		// skip one decrement
		t2r = 1;
		// Clear T2 IRQ
		ifr &= ~IRQM_T2;
		checkIrqCallback(callBackParam, ifr & ier);
		t2t = 0;
		break;
	case 0xA:
		sr = value;
		break;
	case 0xB:
		/* ACR bits:
		  7 = PB7 output enable square wave output if PB7 is programmed as a T1 output it will go low on the phi2 following the write operation
		  6 = free-run enable
		  5 = timer 2 control (0=timed interrupt,1=countdown with pulses)
		  2,3,4 = shift register control
		  1 = PB latching enabled
		  0 = PA latching enabled
		*/
		acr = value;
		// if one-shot, allow timeout again
		if (!(acr & 0x40))
			t1t = 0;
		// Clear PB7
		if (acr & 0x80)
			pb67 |= 0x80;
		break;
	case 0xC:
		pcr = value;
		break;
	case 0xD:
		ifr &= ~(value | 0x80);
		checkIrqCallback(callBackParam, ifr & ier);
		break;
	case 0xE:
		if (value & 0x80)
			ier |= value & 0x7F;
		else
			ier &= ~value;
		break;
	default:
		break;
	}
}

unsigned char Via::read(unsigned int r)
{
	switch (r & 0xF) {
	case 0:
		if ((pcr & 0xA0) != 0x20)
			ifr &= ~IRQM_CB2;
		// Clear CB1 IRQ flag
		ifr &= ~IRQM_CB1;
		checkIrqCallback(callBackParam, ifr & ier);
		return ((prb | ~ddrb) & ~(acr & 0x80)) | pb67;
	default:
	case 1:
	case 0xF:
		if ((pcr & 0x0A) != 0x02)
			ifr &= ~IRQM_CA2;
		// Clear CA1 IRQ flag
		ifr &= ~IRQM_CA1;
		checkIrqCallback(callBackParam, ifr & ier);
		return pra | ~ddra;
	case 2:
		return ddrb;
	case 3:
		return ddra;
	case 4:
		// Clear T1 IRQ flag
		ifr &= ~IRQM_T1;
		checkIrqCallback(callBackParam, ifr & ier);
		return t1c & 0xFF;
	case 5:
		return t1c >> 8;
	case 6:
		return t1l & 0xFF;
	case 7:
		return t1l >> 8;
	case 8:
		// Clear T2 IRQ flag
		ifr &= ~IRQM_T2;
		checkIrqCallback(callBackParam, ifr & ier);
		if ((acr & 0x20) && (t2c & 0xFF))
			return (t2c - 1) & 0xFF;
		return t2c & 0xFF;
	case 9:
		return t2c >> 8;
	case 0xA:
		// Shift register IRQ is cleared on read
		ifr &= ~IRQM_SR;
		checkIrqCallback(callBackParam, ifr & ier);
		return sr;
	case 0xB:
		return acr;
	case 0xC:
		return pcr;
	case 0xD:
		// bit #7 will be read as 1 if any IRQ is active
		return ifr | ((ifr & ier & 0x7F) ? 0x80 : 0);
	case 0xE:
		// reading bit #7 gives 1 always
		return ier | 0x80;
	}
}

void Via::reset()
{
	// clears all 6522 internal registers to logic 0
	// (except T1 and T2 latches and counters and the shift register)
	pra = ddra = prb = ddrb = 0;
	ifr = ier = 0;
	acr = pcr = 0;
	sr = 0; t1c = t2c = t1l = t2l = 0xFFFF;
	t1t = t2t = 0;
	pb67 = 0;
	t1latch = false;
}
