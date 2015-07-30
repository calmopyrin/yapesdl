#include <stdio.h>
#include <memory.h>
#include "vic2mem.h"
#include "c64rom.h"
#include "Sid.h"
#include "Clockable.h"
#include "video.h"
#include "keys64.h"
#include "sound.h"
#include "tape.h"

#define PIXELS_PER_ROW 504
#define FAST_BOOT 1
#define BEAMY2RASTER(X) (X < 265 ? X + 47 : X - 265)
#define RASTER2BEAMY(X) (X < 47 ? 265 + X : X - 47)

#define SET_BITS(REG, VAL) { \
		unsigned int i = 7; \
		do { \
			REG = ((VAL) & (1 << i)) == (1 << i); \
		} while(i--); \
	}

static unsigned char cycleLookup[][128] = {
// SCREEN:             |===========0102030405060708091011121314151617181920212223242526272829303132333435363738391111=========
//     coordinate:                                                                                    111111111111111111111111111111
//0000000000111111111122222222223333333333444444444455555555556666666666777777777788888888889999999999000000000011111111112222222222
//0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789
//     beamX:
//11111111111111111111111111
//000000000011111111112222220000000000111111111122222222223333333333444444444455555555556666666666777777777788888888889999999999
//012345678901234567890123450123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789
// NO SPRITES NO BADLINE
{"3 i 4 i 5 i 6 i 7 i r r r r r g g g g g g g g g g g g g g g g g g g g g g g g g g g g g g g g g g g g g g g g i i 0 i 1 i 2 i "},
// no sprites, bad line
{"3 i 4 i 5 i 6 i 7 i r r*r*r*rcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcg i i 0 i 1 i 2 i "}
};

Vic2mem::Vic2mem()
{
    enableSidCard(true, 0);
	sidCard->setFrequency(1);
	sidCard->setModel(SID6581R1);
	actram = Ram;
    loadroms();
	chrbuf = DMAbuf;
	clrbuf = DMAbuf + 64;
	tmpClrbuf = DMAbuf + 128;
	// setting screen memory pointer
	scrptr = screen;
	// pointer of the end of the screen memory
	endptr = scrptr + PIXELS_PER_ROW;
	framecol = 0;
	crsrblinkon = false;
	vicBase = Ram;
	charrombank = charRomC64;
	tap = new TAP;
	keys64 = new KEYS64;
	Reset(true);
}

Vic2mem::~Vic2mem()
{
	delete keys64;
	delete tap;
}

void Vic2mem::Reset(bool clearmem)
{
	if (clearmem) {
		for (int i=0;i<RAMSIZE;Ram[i] = (i>>1)<<1==i ? 0 : 0xFF, i++);
		loadroms();
	}
    soundReset();
	cia[0].reset();
	cia[1].reset();
	vicReg[0x19] = 0;
	prp = 7;
	prddr = 0;
}

void Vic2mem::loadroms()
{
    memcpy(RomLo[0], basicRomC64, basicRomC64_size);
    memcpy(RomHi[0], kernalRomC64, kernalRomC64_size);
	mem_8000_bfff = RomLo[0];
	mem_c000_ffff = RomHi[0];
#if FAST_BOOT
    //if (memcmp()) // TODO
	unsigned char patch[] = { 0xA0, 0xA0, 0xA2, 0x00, 0x84, 0xC1, 0x86, 0xC2 };
	memset(mem_c000_ffff + 0x1D68, 0xEA, 0x24);
	memcpy(mem_c000_ffff + 0x1D68, patch, sizeof(patch));
#endif
}

void Vic2mem::copyToKbBuffer(const char *text, unsigned int length)
{
	if (!length)
		length = strlen(text);
	Write(0xc6, length);
	while (length--)
		Write(0x0277 + length, text[length]);
}

Color Vic2mem::getColor(unsigned int ix)
{
	const double bsat = 45.0;
	Color color[16] = {
		{ 0, 0, 0 }, { 0, 5.0, 0 }, { 112.5, 2.9375, bsat }, { 292.5, 3.875, bsat },
		{ 45, 3.125, bsat }, { 225, 3.5, bsat }, { 0, 2.75, bsat }, { 180, 4.25, bsat},
		{ 135, 3.125, bsat }, { 157.5, 2.75, bsat }, { 112.5, 3.5, bsat }, { 0, 2.9375, 0 },
		{ 0, 3.41, 0 }, { 225, 4.25, bsat }, { 0, 3.41, bsat }, { 0, 3.875, 0 }
	};
	return color[ix & 0xF];
}

void Vic2mem::soundReset()
{
    sidCard->reset();
}

void Vic2mem::CIA::reset()
{
	pra = prb = 0;
	ddra = ddrb = 0;
	icr = 0;
	irq_mask = 0;
	ta = tb = latcha = latchb = 0;
	cra = crb = 0;
	// ToD
	todCount = 60 * 60 * 50; // set to 1hr at reset
	alarmCount = -1;
	tod.latched = false;
	tod.halt = 1;
	todIn = 60;
	tod.ampm = 0;
}

void Vic2mem::CIA::setIRQflag(unsigned int mask)
{
	if (mask & 0x1F) {
		icr |= 0x80;
	} else {
		icr &= 0x7F;
	}
}

unsigned int Vic2mem::CIA::bcd2hex(unsigned int bcd)
{
# if 0
	unsigned int d = 0;

	while (bcd) {
		d <<= 4;
		d |= bcd % 10;
		bcd /= 10;
	} while (bcd);
	return d;
#else
    return (((bcd & 0xf0) >> 4) * 10) + (bcd & 0xf);
#endif
}

unsigned int Vic2mem::CIA::hex2bcd(unsigned int hex)
{
    return ((hex / 10) << 4) + (hex % 10);
}

// called after each new frame
void Vic2mem::CIA::todUpdate()
{
    if (!tod.halt) {
        todCount += 1;
        if (alarmCount == todCount) {
            // set alarm IRQ
            icr |= 4;
            setIRQflag(irq_mask & icr);
        }
		if (todCount == 12 * 60 * 60 * 50) // 12 AM/PM
			tod.ampm ^= 0x80;
#if 0
        TOD time;
        frames2tod(todCount, time, todIn);
       // if (!(todCount % 2000))
            fprintf(stderr, "Count:%09u Time: %02xh:%02Xm:%02Xs:%02Xths.\n", todCount, time.hr, time.min, time.sec, time.tenths);
#endif
    }
}

unsigned int Vic2mem::CIA::tod2frames(TOD &todin)
{
    unsigned int newmsec =
        bcd2hex(todin.hr) * 180000 +
        bcd2hex(todin.min) * 3000 +
        bcd2hex(todin.sec) * 50 +
        bcd2hex(todin.tenths) * 5;
    return newmsec;
}

void Vic2mem::CIA::frames2tod(unsigned int frames, TOD &todout, unsigned int frq)
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

void Vic2mem::CIA::write(unsigned int addr, unsigned char value)
{
    //fprintf(stderr, "$(%04X) CIA write : %02X @ PC=%04X\n", addr, value /* cpuptr->getPC()*/);
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
			// Reload timer A if stopped
			if (!(cra & 1))
				ta = latcha;
			break;

		case 0x06:
			latchb = (latchb & 0xFF00) | value;
			break;

		case 0x07:
			latchb = (latchb & 0xFF) | (value << 8);
			// Reload timer B if stopped
			if (!(crb & 1))
				tb = latchb;
			break;

		case 0x08:
			if (crb & 0x80) {
                frames2tod(alarmCount, alm, todIn);
				alm.tenths = value & 0x0F;
                alarmCount = tod2frames(alm);
			} else {
                frames2tod(todCount, tod, todIn);
				tod.tenths = value & 0x0F;
				todCount = tod2frames(tod);
			}
            tod.halt = false;
			break;

		case 0x09:
			if (crb & 0x80) {
                frames2tod(alarmCount, alm, todIn);
				alm.sec = value & 0x7F;
				alarmCount = tod2frames(alm);
			} else {
                frames2tod(todCount, tod, todIn);
				tod.sec = value & 0x7F;
                todCount = tod2frames(tod);
			}
			break;

		case 0x0A:
			if (crb & 0x80) {
                frames2tod(alarmCount, alm, todIn);
				alm.min = value & 0x7F;
                alarmCount = tod2frames(alm);
			} else {
                frames2tod(todCount, tod, todIn);
				tod.min = value & 0x7F;
                todCount = tod2frames(tod);
			}
			break;

		case 0x0B:
			if (crb & 0x80) {
                frames2tod(alarmCount, alm, todIn);
				alm.hr = value & 0x9F;
                alarmCount = tod2frames(alm);
			} else {
                frames2tod(todCount, tod, todIn);
				tod.hr = value & 0x9F;
				todCount = tod2frames(tod);
			}
            tod.halt = true;
			break;

		case 0x0C:
			sdr = value;
			sdrShiftCnt = 8;
			break;

		case 0x0D:
			if (value & 0x80)
				irq_mask |= value & 0x9F;
			else
				irq_mask &= ~value;
			setIRQflag(icr & irq_mask);
            break;

		case 0x0E:
			cra = value & 0xEF;
			// ToD clock rate
			todIn = value & 0x80 ? 50 : 60;
			if (value & 0x10) // Forced reload
				ta = latcha;
			break;

		case 0x0F:
			crb = value & 0xEF;
			if (value & 0x10) // Forced reload
				tb = latchb;
			break;
	}
	reg[addr] = value;
}

unsigned char Vic2mem::CIA::read(unsigned int addr)
{
	addr &= 0x0F;
	switch (addr) {
		case 0x00:
			//return (pra & ddra) | ( 0xff & ~ddra);
			return pra | ~ddra;
		case 0x01:
			{
				unsigned char retval;
				retval = (prb & ddrb)
					| (0xff & ~ddrb);
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
            } else {
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
            frames2tod(todCount, tod, todIn);
		    tod.latched = true;
			todLatch = tod;
			return todLatch.hr;
		case 0x0C:
			return sdr;
		case 0x0D:
			{
				unsigned char retval = icr;
				icr = 0;
				//setIRQflag(0);
				return retval & 0x9F;
			}
		case 0x0E:
			return cra;
		case 0x0F:
			return crb;
	}
	return reg[addr];
}

void Vic2mem::CIA::countTimers()
{
    if ((cra & 0x40) && sdrShiftCnt) {
        sdrShiftCnt -= 1;
        if (!sdrShiftCnt) {
            icr |= 8;
            setIRQflag(icr & irq_mask);
        }
    }
    if ((cra & 0x20) == 0x00 && cra & 1 ) {
		if (!ta--) {
		   	icr |= 0x01; // Set timer A IRQ flag
			setIRQflag(icr & irq_mask); // FIXME, 1 cycle delay
			prbTimerToggle ^= 0x40; // PRA7 underflow count toggle
			// timer A output to PB6?
			if (cra & 2) {
			    // set PRA6 high for one clock
			    if (cra & 4) {
                    prbTimerOut ^= 0x40; // toggle PRA6 between 1 and 0
			    } else {
			        prbTimerOut |= 0x40; // set high for one clock
			    }
			}
     	  	if (cra & 8) // One-shot?
				cra &= 0xFE; // Stop timer
			// Reload from latch
  	 	  	ta = latcha;
		}
	}
    if ((crb & 0x20) == 0x00 && crb & 1) {
		if (!tb--) {
		   	icr |= 0x02; // Set timer B IRQ flag
			setIRQflag(icr & irq_mask); // FIXME, 1 cycle delay
			prbTimerToggle ^= 0x80; // PRB7 underflow count toggle
			// timer A output to PRB6?
			if (crb & 2) {
			    // set PRB7 high for one clock
			    if (crb & 4) {
                    prbTimerOut ^= 0x80; // toggle PRB7 between 1 and 0
			    } else {
			        prbTimerOut |= 0x80; // set high for one clock
			    }
			}
     	  	if (crb & 8) // One-shot?
				crb &= 0xFE; // Stop timer
			// Reload from latch
			tb = latchb;
		}
	}
}

void Vic2mem::changeCharsetBank()
{
    const unsigned int vicBank = (((cia[1].pra | ~cia[1].ddra) ^ 0xFF) & 3) << 14;
	vicBase = Ram + vicBank;
	const unsigned int vmOffset = ((vicReg[0x18] & 0xF0) << 6);
    VideoBase = vicBase + vmOffset;
	const unsigned int cSetOffset = ((vicReg[0x18] & 0x0E) << 10);
	charrambank = vicBase + cSetOffset;

	cset = (!(vicBank & 0x4000) && ((cSetOffset & 0x3000) == 0x1000)) // 4 or 6
		? charrombank + (cSetOffset & 0x0800) : charrambank;
	grbank = vicBase + ((vicReg[0x18] & 8) << 10);
#if 0
	fprintf(stderr, "VIC bank: %04X, matrix:%04X(%u) in line:%03i pra:%02X ddra:%02X vic18:%02X\n", 
		vicBank, cSetOffset, cset != charrambank, beamy, cia[1].pra, cia[1].ddra, vicReg[0x18]);
#endif
}

void Vic2mem::checkIRQflag()
{
	irqFlag = (cia[0].icr | vicReg[0x19]) & 0x80;
}

// read memory through memory decoder
unsigned char Vic2mem::Read(unsigned int addr)
{
	switch (addr & 0xF000) {
		case 0x0000:
			switch (addr & 0xFFFF) {
				case 0:
					return prddr;
				case 1:
					return prp | ~prddr;
				default:
					return actram[addr & 0xFFFF];
			}
		default:
			return actram[addr & 0xFFFF];
		case 0xA000:
		case 0xB000:
			return mem_8000_bfff[addr & 0x1FFF];
		case 0xE000:
		case 0xF000:
			return mem_c000_ffff[addr & 0x1FFF];
		case 0xD000:
			if (!(prp & 3))
				return actram[addr & 0xFFFF];
			else if (charrom) {
				return charRomC64[addr & 0x0FFF];
			} else {
				switch ( addr >> 8 ) {
					case 0xD0: // VIC2
					case 0xD1:
					case 0xD2:
					case 0xD3:
						addr &= 0x3F;
						switch (addr) {
							case 0x12:
								return (BEAMY2RASTER(beamy)) & 0xFF;
							case 0x11:
								return (vicReg[0x11] & 0x7f) | (((BEAMY2RASTER(beamy)) & 0x100) >> 1);
							case 0x13: // LPX
								return beamx << 1;
								//return (beamx < 99 ? 26 + beamx : beamx - 99) << 2;
							case 0x16:
								return vicReg[0x16] | 0xC0;
							case 0x18:
								return vicReg[0x18] | 1;
							case 0x19:
								return vicReg[0x19] | 0x70;
							case 0x1A:
								return vicReg[0x1A] | 0xF0;
							case 0x20:
								return framecol & 0x0F;
							case 0x21:
							case 0x22:
							case 0x23:
							case 0x24:
								return ecol[(addr & 0x3F) - 0x21];
						}
						return vicReg[addr];
					case 0xD4: // SID
					case 0xD5:
					case 0xD6:
					case 0xD7:
						return sidCard->read(addr & 0x1F);
					case 0xD8: // Color RAM
					case 0xD9:
					case 0xDA:
					case 0xDB:
						return colorRAM[addr & 0x03FF];
					case 0xDC: // CIA1
						{
							unsigned char retval;
							switch (addr & 0x0F) {
								case 0x00:
									return keys64->getJoyState(1);
								case 0x01:
									retval = keys64->feedkey(cia[0].pra /*| ~cia[0].ddra*/);
									//fprintf(stderr, "$Kb(%02X) read: %02X\n", cia[0].pra, retval);
									break;
								case 0x0D:
									checkIRQflag();
								default:
									retval = cia[0].read(addr);
							}
							return retval;
						}
					case 0xDD: // CIA2
						switch (addr & 0x0F) {
							case 0:
								return (readBus() & 0xC0) | (cia[1].read(0) & 0x3F);
                            case 0xD:
                                {
                                    unsigned char retval = cia[1].read(0xD);
                                    cpuptr->clearNmi();
                                    return retval;
                                }
							default:
								;
						}
						return cia[1].read(addr);
					default: // open address space
						return beamy ^ beamx;//actram[addr & 0xFFFF];
				}
			}
	}
}

void Vic2mem::Write(unsigned int addr, unsigned char value)
{
	switch (addr & 0xF000) {
		case 0x0000:
			switch ( addr & 0xFFFF ) {
				case 0:
					prddr = value & 0xDF;
					return;
				case 1:
					prp = value;
					mem_8000_bfff = ((prp & 3) == 3) ? RomLo[0] : Ram + 0xa000; // a000..bfff
					mem_c000_ffff = ((prp & 2) == 2) ? RomHi[0] : Ram + 0xe000; // e000..ffff
					charrom = (!(prp & 4) && (prp & 3));
					return;
				default:
					actram[addr & 0xFFFF] = value;
			}
			return;
		default:
			actram[addr & 0xFFFF] = value;
			return;
		case 0xD000:
			if (!(prp & 3)) { // should be read(1)
				actram[addr & 0xFFFF] = value;
			} else if (!charrom) {
				//unsigned int i;
				switch ( addr >> 8 ) {
					case 0xD0: // VIC2
						addr &= 0x3F;
						switch (addr) {
							case 0x12:
								irqline = RASTER2BEAMY(((BEAMY2RASTER(irqline) & 0x100) | value));
								if (beamy == irqline) {
									vicReg[0x19] |= (vicReg[0x1A] & 1) ? 0x81 : 0x01;
									checkIRQflag();
								}
								break;
							case 0x11:
								// raster IRQ line
								irqline = RASTER2BEAMY(((BEAMY2RASTER(irqline) & 0xFF)
									| ((value & 0x80) << 1)));
								if (beamy == irqline) {
									vicReg[0x19] |= (vicReg[0x1A] & 1) ? 0x81 : 0x01;
									checkIRQflag();
								}
								// get vertical offset of screen when smooth scroll
								vshift = value&0x07;
								// check for flat screen (23 rows)
								fltscr = !(value&0x08);
								// check for extended mode
								ecmode = value & EXTCOLOR;
								// check for graphics mode (5th b14it)
								scrattr = (scrattr & ~(GRAPHMODE|EXTCOLOR))|(value & (GRAPHMODE|EXTCOLOR));
								// Check if screen is turned on
                                if (value & 0x10 && !beamy && !attribFetch) {
                                    attribFetch = ScreenOn = true;
                                    vertSubCount = 7;
                                } else if (attribFetch && ((fltscr && beamy == 8) || (!fltscr && beamy == 4))) {
                                    ScreenOn = true;
                                } else if ((beamy == 200 && fltscr) || (beamy == 204 && !fltscr)) {
                                    ScreenOn = false;
                                }
								break;
							case 0x16:
								// check for narrow screen (38 columns)
								nrwscr = value & 0x08;
								// get horizontal offset of screen when smooth scroll
								hshift = value & 0x07;
								scrattr = (scrattr & ~(MULTICOLOR)) | (value & (MULTICOLOR));
								break;
							case 0x18:
								vicReg[0x18] = value;
								changeCharsetBank();
								break;
							case 0x19:
								vicReg[0x19] &= (0x0F & ~value);
								// check if we have a pending IRQ
								if ((vicReg[0x1a]) & 0x0F & vicReg[0x19])
									vicReg[0x19] |= 0x80;
								else
									vicReg[0x19] &= 0x7F;
								checkIRQflag();
								return;
							case 0x1a:
								// check if we have a pending IRQ
								if ((vicReg[0x19]) & 0x0F & value)
									vicReg[0x19] |= 0x80;
								else
									vicReg[0x19] &= 0x7F;
								checkIRQflag();
								break;
							case 0x20:
								value &= 0x0F;
								framecol=(value<<24)|(value<<16)|(value<<8)|value;
								break;
							case 0x21:
								ecol[0]=bmmcol[0]=mcol[0]=value&0x0F;
								break;
							case 0x22:
								ecol[1]=bmmcol[3]=mcol[1]=value&0x0F;
								break;
							case 0x23:
								ecol[2]=mcol[2]=value&0x0F;
								break;
							case 0x24:
								ecol[3]=value&0x0F;
								break;
							// sprites
							case 0x00:
							case 0x02:
							case 0x04:
							case 0x06:
							case 0x08:
							case 0x0A:
							case 0x0C:
							case 0x0E:
								mob[addr >> 1].x = value;
								break;
							case 0x01:
							case 0x03:
							case 0x05:
							case 0x07:
							case 0x09:
							case 0x0B:
							case 0x0D:
							case 0x0F:
								mob[addr >> 1].y = (mob[addr >> 1].y & 0x100) | value;
								break;
							case 0x10:
								{
									unsigned int ix = addr >> 1;
									unsigned int i = 7;
									do {
										mob[ix].y = (mob[ix].y & 0xFF) | ((value << (i - 6)) & 0x100);
									} while (i--);
								}
								break;
							case 0x15:
								SET_BITS(mob[i].enabled, value);
								break;
							case 0x1B:
								SET_BITS(mob[i].priority, value);
								break;
							case 0x1C:
								SET_BITS(mob[i].multicolor, value);
								break;
							case 0x1D:
								SET_BITS(mob[i].expandX, value);
								break;
							case 0x25:
							case 0x26:
								mobExtCol[addr - 0x25] = value & 0x0F;
								break;
							case 0x27:
							case 0x28:
							case 0x29:
							case 0x2A:
							case 0x2B:
							case 0x2C:
							case 0x2D:
							case 0x2E:
								mob[addr - 0x27].color = value & 0x0F;
								break;
						}
						vicReg[addr] = value;
						return;
					case 0xD4: // SID
					case 0xD5:
					case 0xD6:
					case 0xD7:
						sidCard->write(addr & 0x1f, value);
						return;
					case 0xD8: // Color RAM
					case 0xD9:
					case 0xDA:
					case 0xDB:
						colorRAM[addr & 0x03FF] = value;
						return;
					case 0xDC: // CIA1
						switch (addr & 0x0F) {
							// key matrix row select
							case 0:
								break;
						}
						cia[0].write(addr, value);
						return;
					case 0xDD: // CIA2
						switch (addr & 0x0F) {
							case 2:
							case 0:
								cia[1].write(addr, value);
								// VIC base
								changeCharsetBank();
								// serial IEC
								{
									static unsigned char prevPort = 0x01;
									if ((prevPort ^ cia[1].pra) & 0x38) {
										unsigned char port = ~cia[1].pra & 0x38;
										serialPort[0] = ((port << 2) & 0x80)	// DATA OUT -> DATA IN
											| ((port << 2) & 0x40)				// CLK OUT -> CLK IN
											| ((port << 1) & 0x10);			// ATN OUT -> ATN IN (drive)
										updateSerialDevices(serialPort[0]);
										prevPort = cia[1].pra;
#if LOG_SERIAL
		fprintf(stderr, "$DD00 write : %02X @ PC=%04X\n", value, cpuptr->getPC());
		fprintf(stderr, "$DD00 written: %02X.\n", serialPort[0]);
#endif
									}
								}
								return;
							default:
								break;
						}
						cia[1].write(addr, value);
						return;
					default: // $DExx/$DFxx open I/O
						//actram[addr & 0xFFFF] = value;
						return;
				}
			} else {
				actram[addr & 0xFFFF] = value;
			}
			return;
	}
}

void Vic2mem::doHRetrace()
{
	// the beam reached a new line
	TVScanLineCounter += 1;
	if ( TVScanLineCounter >= 340 ) {
		doVRetrace();
	}
	scrptr = screen + TVScanLineCounter * PIXELS_PER_ROW;
	endptr = scrptr + PIXELS_PER_ROW;
}

inline void Vic2mem::newLine()
{
	beamy += 1;
	ff1d_latch = beamy;
	// the beam reached a new line
	doHRetrace();
	flushBuffer(CycleCounter);
	switch (beamy) {

		case 4:
			if (!fltscr && attribFetch) ScreenOn = true;
			break;

		case 8:
			if (fltscr && attribFetch) ScreenOn = true;
			break;

		case 200:
			if (fltscr) ScreenOn = false;
			break;

		case 204:
			if (!fltscr) ScreenOn = false;
			break;

		case 205:
			CharacterCount = 0;
			VertSubActive = false;
			break;

		case 251:
			VBlanking = true;
			break;

		case 261: // Vertical retrace
			doVRetrace();
            // CIA ToD count @ 50 Hz
            cia[0].todUpdate();
            cia[1].todUpdate();
            break;

		case 271:
			VBlanking = false;
			break;

		case 512:
		case 312:
			beamy = 0;
            //
			CharacterPositionReload = 0;
			if (!attribFetch) {
				endOfScreen = true;
			}
			attribFetch = (vicReg[0x11] & 0x10) != 0;
	}
	// is there raster interrupt?
	if (beamy == irqline) {
		vicReg[0x19] |= (vicReg[0x1A] & 1) ? 0x81 : 0x01;
		checkIRQflag();
	}
}

void Vic2mem::ted_process(const unsigned int continuous)
{
    loop_continuous = continuous;
    do {
		beamx += 2;
        switch(beamx) {

			default:
                break;
			case 100:
				newLine();
				break;

            case 102:
                if (VertSubActive)
                	vertSubCount = (vertSubCount+1)&7;
				if (endOfScreen) {
					vertSubCount = 7;
					endOfScreen = false;
				}
				break;

            case 122:
				HBlanking = false;
				break;

			case 124:
				if (attribFetch) {
					BadLine |= (vshift == (beamy & 7)) & (beamy != 203);
					if (BadLine) {
						vertSubCount = 7;
					}
                 	if (beamy == 203) {
						attribFetch = false;
					}
				}
				break;

            case 0:
            case 126:
				if (VertSubActive)
					CharacterPosition = CharacterPositionReload;
				beamx = 0;
                break;

			case 2:
				if (BadLine) {
					if (!delayedDMA)
						doDMA(tmpClrbuf, 0);
				}
				break;

            case 6:
				if (ScreenOn) {
					SideBorderFlipFlop = true;
					memset(scrptr, mcol[0], hshift);
					if (nrwscr)
						CharacterWindow = true;
					x = 0;
				}
				break;

			case 8:
				if (ScreenOn && !nrwscr) {
					CharacterWindow = true;
				}
				break;

            case 82:
				if (VertSubActive && vertSubCount == 6)
					CharacterCount = (CharacterCount + 40) & 0x3FF;
				break;

            case 84:
    			if ( VertSubActive && charPosLatchFlag) // FIXME
					CharacterPositionReload = (CharacterPosition + 40) & 0x3FF;
				if (!nrwscr)
  					SideBorderFlipFlop = CharacterWindow = false;
				break;

 			case 86:
				if (nrwscr)
  					SideBorderFlipFlop = CharacterWindow = false;
  			    break;

		    case 98:
				HBlanking = true;
				break;

			case 94: // $BC (376)
				charPosLatchFlag = vertSubCount == 6;
				break;

			case 96:
				if (BadLine) {
					VertSubActive = true;
					BadLine = 0;
					// swap DMA pointers
					unsigned char *tmpbuf = chrbuf;
					chrbuf = tmpClrbuf;
					tmpClrbuf = tmpbuf;
				}
				break;

			case 256:
			case 128: // overflow
				beamx = 0;
				break;
        }
		// drawing the visible part of the screen
		if (!(HBlanking |VBlanking)) {
			if (SideBorderFlipFlop) {
				// call the relevant rendering function
				render();
				x = (x + 1) & 0x3F;
			}
			if (!CharacterWindow) {
				// we are on the border area, so use the frame color
				*((int*)scrptr) = framecol;
				*((int*)(scrptr + 4)) = framecol;
			}
		}
		unsigned char cycleChr = cycleLookup[BadLine][beamx];
		// sprites
		if (cycleChr >= '0' && cycleChr <= '9') {
			unsigned int spX = cycleChr - '0';
			if (mob[spX].enabled) {
				mob[spX].address = vicBase ;
			}
		}
		//
		if (scrptr != endptr)
			scrptr += 8;
		else
			doHRetrace();
        //
		checkIRQflag();
		cia[0].countTimers();
		cia[1].countTimers();
		cycleChr = cycleLookup[BadLine][beamx|1];
		switch (cycleChr) {
            case ' ':
                cpuptr->process();
                break;
            case '*':
                cpuptr->stopcycle();
		    default:;
		}
        //
        CycleCounter += 1;

		unsigned int i = 0;
		while (Clockable::itemHeap[i]) {
			Clockable *c = Clockable::itemHeap[i];
			while (c->ClockCount >= VIC_REAL_CLOCK_M10) {
				c->ClockCount -= VIC_REAL_CLOCK_M10;
				c->Clock();
			}
			c->ClockCount += c->ClockRate;
			i++;
		}
    } while (loop_continuous);

	loop_continuous = false;
}

// when multi and extended color modes are all on the screen is blank
inline void Vic2mem::mcec()
{
	memset( scrptr, 0, 8);
}

// renders hires text
inline void Vic2mem::hi_text()
{
    unsigned char	chr;
	unsigned char	charcol;
	unsigned char	mask;
	unsigned char	*wbuffer = scrptr + hshift;

	// get the actual physical character column
	charcol = colorRAM[CharacterPosition + x];
	chr = chrbuf[x];

	if (VertSubActive)
		mask = cset[(chr << 3) | vertSubCount];
	else
		mask = Read(0x3FFF);

	wbuffer[0] = (mask & 0x80) ? charcol : mcol[0];
	wbuffer[1] = (mask & 0x40) ? charcol : mcol[0];
	wbuffer[2] = (mask & 0x20) ? charcol : mcol[0];
	wbuffer[3] = (mask & 0x10) ? charcol : mcol[0];
	wbuffer[4] = (mask & 0x08) ? charcol : mcol[0];
	wbuffer[5] = (mask & 0x04) ? charcol : mcol[0];
	wbuffer[6] = (mask & 0x02) ? charcol : mcol[0];
	wbuffer[7] = (mask & 0x01) ? charcol : mcol[0];
}

// renders extended color text
inline void Vic2mem::ec_text()
{
	unsigned char charcol;
	unsigned char chr;
	unsigned char mask;
	unsigned char *wbuffer = scrptr + hshift;

	// get the actual physical character column
	charcol = colorRAM[CharacterPosition + x];
	chr = chrbuf[x];

	if (VertSubActive)
		mask = cset[((chr & 0x3F) << 3) | vertSubCount];
	else
		mask = Read(0x39FF);

	chr >>= 6;

	wbuffer[0] = (mask & 0x80) ? charcol : ecol[chr];
	wbuffer[1] = (mask & 0x40) ? charcol : ecol[chr];
	wbuffer[2] = (mask & 0x20) ? charcol : ecol[chr];
	wbuffer[3] = (mask & 0x10) ? charcol : ecol[chr];
	wbuffer[4] = (mask & 0x08) ? charcol : ecol[chr];
	wbuffer[5] = (mask & 0x04) ? charcol : ecol[chr];
	wbuffer[6] = (mask & 0x02) ? charcol : ecol[chr];
	wbuffer[7] = (mask & 0x01) ? charcol : ecol[chr];
}

// renders multicolor text with reverse bit set
inline void Vic2mem::mc_text()
{
	unsigned char charcol = colorRAM[CharacterPosition + x];
	unsigned char chr = chrbuf[x];
	unsigned char *wbuffer = scrptr + hshift;
	unsigned char mask;

	if (VertSubActive)
		mask = cset[(chr << 3) | vertSubCount];
	else
		mask = Read(0x3FFF);

	if (charcol & 8) { // if character is multicolored

		mcol[3] = charcol & 0x07;

		wbuffer[0] = wbuffer[1] = mcol[ (mask & 0xC0) >> 6 ];
		wbuffer[2] = wbuffer[3] = mcol[ (mask & 0x30) >> 4 ];
		wbuffer[4] = wbuffer[5] = mcol[ (mask & 0x0C) >> 2 ];
		wbuffer[6] = wbuffer[7] = mcol[ mask & 0x03 ];

	} else { // this is a normally colored character

		wbuffer[0] = (mask & 0x80) ? charcol : mcol[0];
		wbuffer[1] = (mask & 0x40) ? charcol : mcol[0];
		wbuffer[2] = (mask & 0x20) ? charcol : mcol[0];
		wbuffer[3] = (mask & 0x10) ? charcol : mcol[0];
		wbuffer[4] = (mask & 0x08) ? charcol : mcol[0];
		wbuffer[5] = (mask & 0x04) ? charcol : mcol[0];
		wbuffer[6] = (mask & 0x02) ? charcol : mcol[0];
		wbuffer[7] = (mask & 0x01) ? charcol : mcol[0];
	}
}

// renders hires bitmap graphics
inline void Vic2mem::hi_bitmap()
{
	unsigned char mask;
	unsigned char *wbuffer = scrptr + hshift;
	// get the actual color attributes
	unsigned char hcol0 = chrbuf[x] & 0x0F;
	unsigned char hcol1 = chrbuf[x] >> 4;

	if (VertSubActive)
		mask = grbank[(((CharacterPosition + x) << 3) & 0x1FFF) | vertSubCount];
	else
		mask = Read(0x3FFF);

	wbuffer[0] = (mask & 0x80) ? hcol1 : hcol0;
	wbuffer[1] = (mask & 0x40) ? hcol1 : hcol0;
	wbuffer[2] = (mask & 0x20) ? hcol1 : hcol0;
	wbuffer[3] = (mask & 0x10) ? hcol1 : hcol0;
	wbuffer[4] = (mask & 0x08) ? hcol1 : hcol0;
	wbuffer[5] = (mask & 0x04) ? hcol1 : hcol0;
	wbuffer[6] = (mask & 0x02) ? hcol1 : hcol0;
	wbuffer[7] = (mask & 0x01) ? hcol1 : hcol0;
}

// renders multicolor bitmap graphics
inline void Vic2mem::mc_bitmap()
{
	unsigned char	mask;
	unsigned char	*wbuffer = scrptr + hshift;
	// get the actual color attributes
	bmmcol[1] = chrbuf[x] >> 4;
	bmmcol[2] = chrbuf[x] & 0x0F;
	bmmcol[3] = colorRAM[CharacterPosition + x] & 0x0F;

	if (VertSubActive)
		mask = grbank[(((CharacterPosition + x) << 3) & 0x1FFF) | vertSubCount];
	else
		mask = Read(0x3FFF);

	wbuffer[0]= wbuffer[1] = bmmcol[(mask & 0xC0) >> 6 ];
	wbuffer[2]= wbuffer[3] = bmmcol[(mask & 0x30) >> 4 ];
	wbuffer[4]= wbuffer[5] = bmmcol[(mask & 0x0C) >> 2 ];
	wbuffer[6]= wbuffer[7] = bmmcol[mask & 0x03];
}

inline void Vic2mem::render()
{
	// call the relevant rendering function
	switch (scrattr) {
		case 0:
			hi_text();
			break;
		case MULTICOLOR :
			mc_text();
			break;
		case EXTCOLOR :
			ec_text();
			break;
		case GRAPHMODE :
			hi_bitmap();
			break;
		case GRAPHMODE|MULTICOLOR :
			mc_bitmap();
			break;
		default:
			mcec();
			break;
	}
}
