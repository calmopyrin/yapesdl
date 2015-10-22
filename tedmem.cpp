/*
	YAPE - Yet Another Plus/4 Emulator

	The program emulates the Commodore 264 family of 8 bit microcomputers

	This program is free software, you are welcome to distribute it,
	and/or modify it under certain conditions. For more information,
	read 'Copying'.

	(c) 2000, 2001, 2004, 2005, 2015 Attila Grósz
*/
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <ctype.h>
#include "tedmem.h"
#include "Sid.h"
#include "sound.h"
#include "cpu.h"
#include "keyboard.h"
#include "tape.h"
#include "roms.h"
#include "tcbm.h"
#include "Clockable.h"
#include "video.h"

unsigned int		TED::vertSubCount;
int				TED::x;
unsigned char	*TED::VideoBase;

ClockCycle TED::CycleCounter;
bool TED::ScreenOn, TED::attribFetch;
bool TED::SideBorderFlipFlop, TED::CharacterWindow;
unsigned int TED::BadLine;
unsigned int	TED::clockingState;
unsigned int	TED::CharacterCount = 0;
bool TED::VertSubActive;
unsigned int	TED::CharacterPosition;
unsigned int	TED::CharacterPositionReload;
//unsigned int	TED::CharacterPositionCount;
unsigned int	TED::TVScanLineCounter;
bool TED::HBlanking;
bool TED::VBlanking;
bool TED::aligned_write;
unsigned char *TED::aw_addr_ptr;
unsigned char TED::aw_value;
unsigned int TED::ff1d_latch;
bool TED::charPosLatchFlag;
bool TED::endOfScreen;
bool TED::delayedDMA;
TED *TED::instance_;

enum {
	TSS = 1 << 1,
	TDS = 1 << 2,
	TRFSH = 1 << 3,
	THALT1 = 1 << 4,
	THALT2 = 1 << 5,
	THALT3 = 1 << 6,
	TDMA = 1 << 7,
	TDMADELAY = 1 << 8,
	TSSDELAY = 1 << 9,
	TDSDELAY = 1 << 10,
	TRFSHDELAY = 1 << 11
};

TED::TED() : sidCard(0), SaveState()
{
	unsigned int i;

	instance_ = this;
	setId("TED0");
	// clearing cartdridge ROMs
	for (i=0;i<4;++i) {
		memset(&(RomHi[i]),0,ROMSIZE);
		memset(&(RomLo[i]),0,ROMSIZE);
		memset(romlopath,0,sizeof(romlopath));
		memset(romhighpath,0,sizeof(romhighpath));
	};
	// default ROM sets
	strcpy(romlopath[0],"BASIC");
	strcpy(romhighpath[0],"KERNAL");

	// 64 kbytes of memory allocated
	RAMMask=0xFFFF;

	// actual ram bank pointer default setting
	actram=Ram;

	// setting screen memory pointer
	scrptr=screen;
	// pointer of the end of the screen memory
	endptr=scrptr + 456;
	// setting the CPU to fast mode
	fastmode=1;
	// initial position of the electron beam (upper left corner)
	irqline=vertSubCount=0;
	beamy=0;
	beamx=0;
	hshift = 0;
	scrblank= false;

	charrombank=charrambank=cset=VideoBase=Ram;
	scrattr=0;
	timer1=timer2=timer3=0;
	chrbuf = DMAbuf;
	clrbuf = DMAbuf + 64;
	tmpClrbuf = DMAbuf + 128;
	memset(DMAbuf, 0, sizeof(DMAbuf));

	// create an instance of the keyboard class
	keys = new KEYS;
	tap = new TAP;
	// setting the TAP::mem pointer to this MEM class
	tap->mem=this;
	tcbmbus = NULL;
	crsrblinkon = false;
	VertSubActive = false;
	CharacterPositionReload = CharacterPosition = 0;
	SideBorderFlipFlop = false;
	render_ok = false;

	irqFlag = 0;
	BadLine = 0;
	CycleCounter = 0;
	
	tedSoundInit(sampleRate);
	if (enableSidCard(true, 0)) {
		//sidCard->setModel(SID8580DB);
	}
}

void TED::soundReset()
{
	if (sidCard) sidCard->reset();
}

void TED::Reset(bool clearmem)
{
	RAMenable = false;
	loadroms();
	ChangeMemBankSetup();
	// this should not be HERE, but where else could I've put it??
	tap->rewind();
	// clear RAM with powerup pattern
	if (clearmem)
		for (int i=0;i<RAMSIZE;Ram[i] = (i>>1)<<1==i ? 0 : 0xFF, i++);
	soundReset();
	fastmode = 2;
}

void TED::showled(int x, int y, unsigned char onoff)
{
	unsigned int j, k;
	char out = 0;
	unsigned char *charset = (unsigned char *) (kernal+0x1000+(0x51<<3));
	unsigned int offset = y * getCyclesPerRow() + x;

	// if either drive motor is on or LED is on, but motor has priority
	if (onoff) {
		out = (onoff & 0x04) ? 0x55 : 0x52;
		for (j = 0; j < 8; j++)
			for (k = 0; k < 8; k++)
				screen[offset + j * getCyclesPerRow() + k] = (charset[j] & (0x80 >> k)) ? out : 0x71;
	}
}

void TED::texttoscreen(int x,int y, const char *scrtxt)
{
	register int i =0;

	while (scrtxt[i]!=0) {
		chrtoscreen(x+i*8,y,scrtxt[i]);
		i++;
	}
}

void TED::copyToKbBuffer(const char *text, unsigned int length)
{
	if (!length)
		length = (unsigned int) strlen(text);
	Write(0xEF, length);
	while (length--)
		Write(0x0527+length, text[length]);
}

void TED::chrtoscreen(int x,int y, char scrchr)
{
	register int j, k;
	const unsigned int CPR = getCyclesPerRow();
	unsigned char *charset = (unsigned char *) kernal + 0x1000;

	if (isalpha(scrchr)) {
		scrchr=toupper(scrchr)-64;
		charset+=(scrchr<<3);
		for (j=0;j<8;j++)
			for (k=0;k<8;k++)
				screen[(y+j)*CPR+x+k] = (*(charset+j) & (0x80>>k)) ? 0x00 : 0x71;
		return;
	}
	charset+=(scrchr<<3);
	for (j=0;j<8;j++)
		for (k=0;k<8;k++)
			screen[(y+j)*CPR+x+k] = (*(charset+j) & (0x80>>k)) ? 0x00 : 0x71;
}

void TED::loadroms()
{
	for (int i=0;i<4;i++) {
		loadhiromfromfile(i,romhighpath[i]);
		loadloromfromfile(i,romlopath[i]);
	}
	mem_8000_bfff = actromlo = &(RomLo[0][0]);
	mem_fc00_fcff = mem_c000_ffff = actromhi = &(RomHi[0][0]);
}

void TED::loadloromfromfile(int nr, char fname[256])
{
	FILE *img;

	if ((fname[0]!='\0')) {
		if ((img = fopen(fname, "rb"))) {
			// load low ROM file
			fread(&(RomLo[nr]),ROMSIZE,1,img);
			fclose(img);
			return;
		}
		switch (nr) {
			case 0: memcpy(&(RomLo[0]),basic,ROMSIZE);
				break;
			case 1: if (!strncmp(fname,"3PLUS1LOW",9))
						memcpy(&(RomLo[1]),plus4lo,ROMSIZE);
					else
						memset(&(RomLo[1]),0,ROMSIZE);
				break;
			default : memset(&(RomLo[nr]),0,ROMSIZE);
		}
	} else
		memset(&(RomLo[nr]),0,ROMSIZE);
}

void TED::loadhiromfromfile(int nr, char fname[256])
{
	FILE *img;

	if ((fname[0]!='\0')) {
		if ((img = fopen(fname, "rb"))) {
			// load high ROM file
			fread(&(RomHi[nr]),ROMSIZE,1,img);
			fclose(img);
			return;
		}
		switch (nr) {
			case 0:
				memcpy(&(RomHi[0]),kernal,ROMSIZE);
				break;
			case 1: if (!strncmp(fname,"3PLUS1HIGH",10))
						memcpy(&(RomHi[1]),plus4hi,ROMSIZE);
					else
						memset(&(RomHi[1]),0,ROMSIZE);
				break;
			default : memset(&(RomHi[nr]),0,ROMSIZE);
		}
	} else
		memset(&(RomHi[nr]),0,ROMSIZE);
}

ClockCycle TED::GetClockCount()
{
	return CycleCounter;
}

void TED::ChangeMemBankSetup()
{
	if (RAMenable) {
		mem_8000_bfff = actram + (0x8000 & RAMMask);
		mem_fc00_fcff = mem_c000_ffff = actram + (0xC000 & RAMMask);
	} else {
		mem_8000_bfff = actromlo;
		mem_c000_ffff = actromhi;
		mem_fc00_fcff = &(RomHi[0][0]);
	}
}

unsigned char TED::Read(unsigned int addr)
{
	switch ( addr & 0xF000 ) {
		case 0x0000:
			switch ( addr & 0xFFFF ) {
				case 0:
					return prddr;
				case 1:
					{
#if 0
						CSerial *serialDevice = getRoot();
						while (serialDevice) {
							// Half cycle delay elapsed?
							unsigned int devNr = serialDevice->getDeviceNumber();
							if (devNr >= 8 && Clockable::item[devNr]->getClockCount() > 15000000) {
								serialDevice->UpdateSerialState(serialPort[0]);
							}
							serialDevice = serialDevice->getNext();
						}
#endif
						unsigned char retval =
							(readBus()&0xC0)
							|(tap->ReadCSTIn(CycleCounter)&0x10);
						return (prp&prddr)|(retval&~prddr);
					}
				default:
					return actram[addr&0xFFFF];
			}
			break;
		case 0x1000:
		case 0x2000:
		case 0x3000:
			return actram[addr&0xFFFF];
		case 0x4000:
		case 0x5000:
		case 0x6000:
		case 0x7000:
			return actram[addr&RAMMask];
		case 0x8000:
		case 0x9000:
		case 0xA000:
		case 0xB000:
			return mem_8000_bfff[addr&0x3FFF];
		case 0xC000:
		case 0xD000:
		case 0xE000:
			return mem_c000_ffff[addr&0x3FFF];
		case 0xF000:
			switch ( addr >> 8 ) {
				case 0xFF:
					switch (addr) {
						case 0xFF00 : return timer1&0xFF;
						case 0xFF01 : return timer1>>8;
						case 0xFF02 : return timer2&0xFF;
						case 0xFF03 : return timer2>>8;
						case 0xFF04 : return timer3&0xFF;
						case 0xFF05 : return timer3>>8;
						case 0xFF06 : return Ram[0xFF06];
						case 0xFF07 : return Ram[0xFF07];
						case 0xFF08 :
							return keys->readLatch();
						case 0xFF09 : return Ram[0xFF09]|(0x25);
						case 0xFF0A : return Ram[0xFF0A]|(0xA0);
						case 0xFF0B : return irqline & 0xFF;
						case 0xFF0C : return ((crsrpos>>8)&0x03)|0xFC;
						case 0xFF0D : return crsrpos&0xFF;
						case 0xFF10 : return Ram[0xFF10] | 0x7C;
						case 0xFF12 : return Ram[0xFF12] | 0xC0;
						case 0xFF0E :
						case 0xFF0F :
						case 0xFF11 :
						case 0xFF13 :
							return Ram[addr];
						case 0xFF14 : return Ram[addr]|0x07;	// lowest 3 bits are always high
						case 0xFF15 : return ecol[0]|0x80;	// The highest bit is not used and so always 1
						case 0xFF16 : return ecol[1]|0x80;	// A few games (Rockman) used it...
						case 0xFF17 : return ecol[2]|0x80;
						case 0xFF18 : return ecol[3]|0x80;
						case 0xFF19 : return (framecol&0xFF)|0x80;
						case 0xFF1A : return (CharacterPositionReload >> 8) | 0xFC;
						case 0xFF1B : return CharacterPositionReload & 0xFF;
						case 0xFF1C : return (beamy>>8)|0xFE;
						case 0xFF1D : return beamy&0xFF; /// 1-8. bit of the rasterline counter
						case 0xFF1E : return ((98+beamx)<<1)%228; // raster column
						case 0xFF1F : return 0x80|(crsrphase<<3)|vertSubCount;
						default:
							return mem_c000_ffff[addr&0x3FFF];
					}
					break;
				case 0xFE:
					if (tcbmbus)
						return tcbmbus->Read(addr);
					else
						return addr >> 8; // FIXME
				case 0xFD:
					switch (addr>>4) {
						case 0xFD0: // RS232
							return 0xFD;
						case 0xFD1: // User port, PIO & 256 RAM expansion
							return (tap->IsButtonPressed()<<2)^0xFF;
						case 0xFD2: // Speech hardware
							return addr >> 8;
						case 0xFD4: // SID Card
						case 0xFD5:
						case 0xFE8: // FIXME never taken
						case 0xFE9:
							if (sidCard) {
								return sidCard->read(addr & 0x1f);
							}
							return 0xFD;
						case 0xFD3:
							return Ram[0xFD30];
					}
					return 0xFD;
				case 0xFC:
					return mem_fc00_fcff[addr&0x3FFF];
				default:
					return mem_c000_ffff[addr&0x3FFF];
			}
	}
	//fprintf(stderr,"Unhandled read %04X\n", addr);
	return 0;
}

void TED::updateSerialDevices(unsigned char newAtn)
{
	// Let all devices know about the new serial state
	CSerial *SerialDevChain = CSerial::getRoot();
	while (SerialDevChain) {
		SerialDevChain->UpdateSerialState(newAtn);
		SerialDevChain = SerialDevChain->getNext();
	}
}

void TED::UpdateSerialState(unsigned char portVal)
{
	static unsigned char prevVal = 0x00;

	if ((prevVal ^ portVal) & 8)
		tap->SetTapeMotor(CycleCounter, portVal&8);

	if ((prevVal ^ portVal) & 7 ) {		// serial lines changed
		serialPort[0] = ((portVal << 7) & 0x80)	// DATA OUT -> DATA IN
			| ((portVal << 5) & 0x40)			// CLK OUT -> CLK IN
			| ((portVal << 2) & 0x10);			// ATN OUT -> ATN IN (drive)
		updateSerialDevices(serialPort[0]);
#if LOG_SERIAL
		fprintf(stderr, "$01 write : %02X @ PC=%04X\n", portVal, cpuptr->getPC());
		fprintf(stderr, "$01 written: %02X @ PC=%04X.\n", serialPort[0], cpuptr->getPC());
#endif
	}
	prevVal = portVal;
}

void TED::changeCharsetBank()
{
	unsigned int tmp;
	if (rvsmode || ecmode) {
		tmp = 0xF800 & RAMMask;
	} else {
		tmp = 0xFC00 & RAMMask;
	}
	charrombank = &(RomHi[0][((Ram[0xFF13] & 0x3C)<<8) & tmp]);
	charrambank = Ram + ((Ram[0xFF13]<<8)&tmp);
	cset = charrom ? charrombank : charrambank;
}

void TED::Write(unsigned int addr, unsigned char value)
{
	unsigned int tmp;

	switch (addr&0xF000) {
		case 0x0000:
			switch ( addr & 0xFFFF ) {
				case 0:
					prddr = value & 0xDF;
					UpdateSerialState(prddr & ~prp);
					return;
				case 1:
					prp = value;
					UpdateSerialState(prddr & ~prp);
					return;
				default:
					actram[addr & 0xFFFF] = value;
			}
			return;
		case 0x1000:
		case 0x2000:
		case 0x3000:
			actram[addr&0xFFFF] = value;
			return;
		case 0xD000:
		case 0x4000:
		case 0x5000:
		case 0x6000:
		case 0x7000:
		case 0x8000:
		case 0x9000:
		case 0xA000:
		case 0xB000:
		case 0xC000:
		case 0xE000:
			actram[addr&RAMMask] = value;
			return;
		case 0xF000:
			switch ( addr >> 8 ) {
				case 0xFF:
					switch (addr) {
						case 0xFF00 :
							t1on=false; // Timer1 disabled
							t1start=(t1start & 0xFF00)|value;
							timer1=(timer1 & 0xFF00)|value;
							return;
						case 0xFF01 :
							t1on=true; // Timer1 enabled
							t1start=(t1start & 0xFF)|(value<<8);
							timer1=(timer1 & 0x00FF)|(value<<8);
							return;
						case 0xFF02 :
							t2on=false; // Timer2 disabled
							timer2=(timer2 & 0xFF00)|value;
							return;
						case 0xFF03 :
							t2on=true; // Timer2 enabled
							timer2=(timer2&0x00FF)|(value<<8);
							return;
						case 0xFF04 :
							t3on=false;  // Timer3 disabled
							timer3=(timer3&0xFF00)|value;
							return;
						case 0xFF05 :
							t3on=true; // Timer3 enabled
							timer3=(timer3&0x00FF)|(value<<8);
							return;
						case 0xFF06 :
							/*fprintf(stderr, "%04X write %02X in cycle %d, in line: %d\n", addr,
									value, beamx, beamy);*/
							Ram[0xFF06]=value;
							// get vertical offset of screen when smooth scroll
							vshift = value&0x07;
							// check for flat screen (23 rows)
							fltscr = !(value&0x08);
							// check for extended mode
							ecmode = value & EXTCOLOR;
							// check for graphics mode (5th b14it)
							scrattr = (scrattr & ~(GRAPHMODE|EXTCOLOR))|(value & (GRAPHMODE|EXTCOLOR));
							changeCharsetBank();
							// Check if screen is turned on
							if (value&0x10 && beamy == 0 && !attribFetch) {
								attribFetch = ScreenOn = true;
								vertSubCount = 7;
								if (vshift != (ff1d_latch&7))
								{
									if (beamx>4 && beamx<84)
										clockingState = TSSDELAY;
								}
							} else if (attribFetch && ((fltscr && ff1d_latch == 8) || (!fltscr && ff1d_latch == 4))) {
								ScreenOn = true;
							} else if ((ff1d_latch == 200 && fltscr) || (ff1d_latch == 204 && !fltscr)) {
								ScreenOn = false;
							}
							if ( (Ram[0xFF06] ^ value) & 0x1F)
								return;
							if (attribFetch) {
								if (vshift == (ff1d_latch & 7)) {
									// Delayed DMA?
									if (beamx>=3 && beamx<89) {
										unsigned char idleread = Read((cpuptr->getPC()+1)&0xFFFF);
										unsigned int delay = (BadLine & 2) ? 0 : (beamx - 1) >> 1;
										unsigned int invalidcount = (delay > 3) ? 3 : delay;
										unsigned int invalidpos = delay - invalidcount;
										invalidcount = (invalidcount < 40-invalidpos) ? invalidcount : 40-invalidpos;
										unsigned int newdmapos = (invalidpos+invalidcount < 40) ? invalidpos+invalidcount : 40;
										unsigned int newdmacount = 40 - newdmapos ;
										unsigned int oldcount = 40 - newdmacount - invalidcount;
										memcpy(tmpClrbuf, clrbuf, oldcount);
										memset(tmpClrbuf + oldcount, idleread, invalidcount);
										memcpy(tmpClrbuf + oldcount + invalidcount, VideoBase + CharacterCount + oldcount
											+ invalidcount, newdmacount);
										BadLine |= 1;
										delayedDMA = true;
										if (!(BadLine & 2))
											clockingState = TDMADELAY;
									} else if (beamx<111 && beamx>=89) {
										// FIXME this breaks on FF1E writes
										BadLine |= 1;
									}
								} else if (BadLine & 1) {
									if (beamx>=94) {
										BadLine &= ~1;
									} else if (beamx >= 91) {
										unsigned char *tmpbuf = clrbuf;
										clrbuf = tmpClrbuf;
										tmpClrbuf = tmpbuf;
										BadLine &= ~1;
									}
								}
							}
							return;
						case 0xFF07 :
							Ram[0xFF07]=value;
							// check for narrow screen (38 columns)
							nrwscr=value&0x08;
							// get horizontal offset of screen when smooth scroll
							hshift=value&0x07;
							// check for reversed mode
							rvsmode = value & 0x80;
							// check for multicolor mode
							scrattr = (scrattr & ~(MULTICOLOR|REVERSE)) | (value & (MULTICOLOR|REVERSE));
							changeCharsetBank();
							return;
						case 0xFF08 :
							keys->latch(Ram[0xfd30], Ram[0xFF08] = value);
							return;
						case 0xFF09 :
							// clear the interrupt requester bits
							// by writing 1 into them (!!)
							Ram[0xFF09]=(Ram[0xFF09]&0x7F)&(~value);
							// check if we have a pending IRQ
							if (Ram[0xFF0A]&0x5E&(Ram[0xFF09]|4)) {
								Ram[0xFF09] |= 0x80;
								irqFlag = 0x80;
							} else {
								Ram[0xFF09] &= 0x7F;
								irqFlag = 0;
							}
							return;
						case 0xFF0A :
							{
								Ram[0xFF0A]=value;
								// change the raster irq line
								unsigned int newirqline = (irqline&0xFF)|((value&0x01)<<8);
								if (newirqline != irqline) {
									if (beamy == newirqline) {
										Ram[0xFF0A]&0x02 ? Ram[0xFF09]|=0x82 : Ram[0xFF09]|=0x02;
										irqFlag |= Ram[0xFF09] & 0x80;
									}
									irqline = newirqline;
								}
								// check if we have a pending IRQ m
								if ((Ram[0xFF09]|4) & 0x5E & value) {
									Ram[0xFF09] |= 0x80;
									irqFlag = 0x80;
								} else {
									Ram[0xFF09] &= 0x7F;
									irqFlag = 0;
								}
							}
							return;
						case 0xFF0B :
							{
								Ram[0xFF0B]=value;
								unsigned int newirqline = value|(irqline&0x0100);
								if (newirqline != irqline) {
									if (beamy == newirqline) {
										Ram[0xFF0A]&0x02 ? Ram[0xFF09]|=0x82 : Ram[0xFF09]|=0x02;
										irqFlag |= Ram[0xFF09] & 0x80;
									}
									irqline = newirqline;
								}
							}
							return;
						case 0xFF0C :
							crsrpos=((value<<8)|(crsrpos&0xFF))&0x3FF;
							return;
						case 0xFF0D :
							crsrpos=value|(crsrpos&0xFF00);
							return;
						case 0xFF0E :
							if (value != Ram[0xFF0E]) {
								writeSoundReg(CycleCounter, 0, value);
								Ram[0xFF0E]=value;
							}
							return;
						case 0xFF0F :
							if (value != Ram[0xFF0F]) {
								writeSoundReg(CycleCounter, 1, value);
								Ram[0xFF0F]=value;
							}
							return;
						case 0xFF10 :
							if (value != Ram[0xFF10]) {
								writeSoundReg(CycleCounter, 2, value & 3);
								Ram[0xFF10]=value;
							}
							return;
						case 0xFF11 :
							if (value != Ram[0xFF11]) {
								writeSoundReg(CycleCounter, 3, value);
								Ram[0xFF11]=value;
							}
							return;
						case 0xFF12:
							grbank=Ram+((value&0x38)<<10);
							if ((value ^ Ram[0xFF12]) & 3)
								writeSoundReg(CycleCounter, 4, value & 3);
							// if the 2nd bit is set the chars are read from ROM
							charrom=(value&0x04) != 0;
							if (charrom && Ram[0xFF13] < 0x80)
								scrattr|=ILLEGAL;
							else {
								scrattr&=~ILLEGAL;
								cset = charrom ? charrombank : charrambank;
							}
							Ram[0xFF12]=value;
							return;
						case 0xFF13 :
							// the 0th bit is not writable, it indicates if the ROMs are on
							Ram[0xFF13]=(value&0xFE)|(Ram[0xFF13]&0x01);
							// bit 1 is the fast/slow mode switch
							if ((fastmode ^ value) & 2) {
								fastmode = !(value&0x02);
								clockingState = fastmode ? TDSDELAY : TSSDELAY;
							}
							(ecmode || rvsmode) ? tmp=(0xF800)&RAMMask : tmp=(0xFC00)&RAMMask;
							charbank = ((value)<<8)&tmp;
							charrambank=Ram+charbank;
							charrombank=&(RomHi[0][charbank & 0x3C00]);
							if (charrom && value<0x80)
								scrattr|=ILLEGAL;
							else {
								scrattr&=~ILLEGAL;
								(charrom) ? cset = charrombank : cset = charrambank;
							}
							return;
						case 0xFF14 :
							Ram[0xFF14]=value;
							VideoBase = Ram+(((value&0xF8)<<8)&RAMMask);
							return;
						case 0xFF15 :
							ecol[0]=bmmcol[0]=mcol[0]=value&0x7F;
							return;
						case 0xFF16 :
							ecol[1]=bmmcol[3]=mcol[1]=value&0x7F;
							return;
						case 0xFF17 :
							ecol[2]=mcol[2]=value&0x7F;
							return;
						case 0xFF18 :
							ecol[3]=value&0x7F;
							return;
						case 0xFF19 :
							value &= 0x7F;
							framecol=(value<<24)|(value<<16)|(value<<8)|value;
							return;
						case 0xFF1A :
							CharacterPositionReload = (CharacterPositionReload & 0xFF) | ((value&3)<<8);
							return;
						case 0xFF1B :
							CharacterPositionReload = (CharacterPositionReload & 0x300) | value;
							return;
						case 0xFF1C :
							beamy=((value&0x01)<<8)|(beamy&0xFF);
							return;
						case 0xFF1D :
							beamy=(beamy&0x0100)|value;
							return;
						case 0xFF1E :
							{
								/*fprintf(stderr, "%04X write %02X in cycle %d, in line: %d\n", addr,
									value ^ 0xFF, beamx, beamy);*/
								unsigned int low_x = beamx&1;
								// lowest 2 bits are not writable
								// inverted value must be written
								unsigned int new_beamx=((~value))&0xFC;
								new_beamx >>= 1;
								new_beamx>=98 ?  new_beamx -= 98 : new_beamx += 16;
								// writes are aligned to single clock cycles
								if (low_x) {
									aligned_write = true;
									aw_addr_ptr = (unsigned char*)(&beamx);
									aw_value = new_beamx;
								} else {
									beamx = new_beamx;
								}
							}
							return;
						case 0xFF1F :
							vertSubCount=value&0x07;
							if ((crsrphase & 0x0F) == 0x0F) {
								if (value != 0x78) crsrblinkon = !crsrblinkon;
							}
							crsrphase=(value&0x78)>>3;
							return;
						case 0xFF3E :
							Ram[0xFF13]|=0x01;
							RAMenable=false;
							ChangeMemBankSetup();
							return;
						case 0xFF3F :
							Ram[0xFF13]&=0xFE;
							RAMenable=true;
							ChangeMemBankSetup();
							return;
					}
					actram[addr&RAMMask] = value;
					return;

				case 0xFE:
				case 0xFD:
					switch (addr>>4) {
						default:
						case 0xFD0: // RS232
						case 0xFD1: // User port, PIO & 256 RAM expansion
						case 0xFD2: // Speech hardware
							return;
						case 0xFD3:
							Ram[0xFD30] = value;
							return;
						case 0xFD4: // SID Card
						case 0xFD5:
						case 0xFE8:
						case 0xFE9:
							if (sidCard) {
								sidCard->write(addr & 0x1f, value);
							}
							return;
						case 0xFDD:
							actromlo=&(RomLo[addr&0x03][0]);
							actromhi=&(RomHi[(addr&0x0c)>>2][0]);
							return;
						case 0xFEC:
						case 0xFED:
						case 0xFEE:
						case 0xFEF:
							if (tcbmbus)
								tcbmbus->Write(addr,value);
							return;
					}
					return;
				default:
					actram[addr&RAMMask] = value;
					return;
			}
	}
	return;
}

void TED::dumpState()
{
	// this is ugly :-P
	saveVar(Ram,RAMSIZE);
	saveVar(&prp, sizeof(prp));
	saveVar(&prddr, sizeof(prddr));
	saveVar(serialPort, sizeof(serialPort[0]));
	saveVar(&RAMenable,sizeof(RAMenable));
	saveVar(&t1start,sizeof(t1start));
	saveVar(&t1on,sizeof(t1on));
	saveVar(&t2on,sizeof(t2on));
	saveVar(&t3on,sizeof(t3on));
	saveVar(&timer1,sizeof(timer1));
	saveVar(&timer2,sizeof(timer2));
	saveVar(&timer3,sizeof(timer3));
	saveVar(&beamx,sizeof(beamx));
	saveVar(&beamy,sizeof(beamy));
	//fwrite(&x,sizeof(x));
	saveVar(&irqline,sizeof(irqline));
	saveVar(&crsrpos,sizeof(crsrpos));
	saveVar(&scrattr,sizeof(scrattr));
	saveVar(&nrwscr,sizeof(nrwscr));
	saveVar(&hshift,sizeof(hshift));
	saveVar(&vshift,sizeof(vshift));
	saveVar(&fltscr,sizeof(fltscr));
	saveVar(&mcol,sizeof(mcol));
	saveVar(chrbuf,40);
	saveVar(clrbuf,40);
	saveVar(&charrom,sizeof(charrom));
	saveVar(&charbank,sizeof(charbank));
	saveVar(&framecol,sizeof(framecol));
}

void TED::readState()
{
	// this is ugly :-P
   	readVar(Ram,RAMSIZE);
	readVar(&prp, sizeof(prp));
	readVar(&prddr, sizeof(prddr));
	readVar(serialPort, sizeof(serialPort[0]));
	readVar(&RAMenable,sizeof(RAMenable));
	readVar(&t1start,sizeof(t1start));
	readVar(&t1on,sizeof(t1on));
	readVar(&t2on,sizeof(t2on));
	readVar(&t3on,sizeof(t3on));
	readVar(&timer1,sizeof(timer1));
	readVar(&timer2,sizeof(timer2));
	readVar(&timer3,sizeof(timer3));
	readVar(&beamx,sizeof(beamx));
	readVar(&beamy,sizeof(beamy));
	//readVar(&x,sizeof(x));
	readVar(&irqline,sizeof(irqline));
	readVar(&crsrpos,sizeof(crsrpos));
	readVar(&scrattr,sizeof(scrattr));
	readVar(&nrwscr,sizeof(nrwscr));
	readVar(&hshift,sizeof(hshift));
	readVar(&vshift,sizeof(vshift));
	readVar(&fltscr,sizeof(fltscr));
	readVar(&mcol,sizeof(mcol));
	readVar(chrbuf,40);
	readVar(clrbuf,40);
	readVar(&charrom,sizeof(charrom));
	readVar(&charbank,sizeof(charbank));
	readVar(&framecol,sizeof(framecol));

	for (int i=0; i<5; i++)
		writeSoundReg(0, i, Ram[0xFF0E + i]);
	beamy=0;
	beamx=0;
	scrptr=screen;
	charrambank=Ram+charbank;
	charrombank=&(RomHi[0][charbank & 0x3C00]);
	(charrom) ? cset = charrombank : cset = charrambank;
}

// when multi and extended color modes are all on the screen is blank
inline void TED::mcec()
{
	memset( scrptr, 0, 8);
}

// renders hires text with reverse (128 chars)
inline void TED::hi_text()
{
    unsigned char	chr;
	unsigned char	charcol;
	unsigned char	mask;
	unsigned char	*wbuffer = scrptr + hshift;

	// get the actual physical character column
	charcol=clrbuf[x];
	chr=chrbuf[x];

	if ((charcol)&0x80 && !crsrblinkon)
		mask = 00;
	else if (VertSubActive)
		mask = cset[((chr&0x7F)<<3)|vertSubCount];
	else
		mask = Read(0xFFFF);

	if (chr&0x80)
		mask ^= 0xFF;
	if (crsrpos==((CharacterPosition+x)&0x3FF) && crsrblinkon )
		mask ^= 0xFF;

	wbuffer[0] = (mask & 0x80) ? charcol : mcol[0];
	wbuffer[1] = (mask & 0x40) ? charcol : mcol[0];
	wbuffer[2] = (mask & 0x20) ? charcol : mcol[0];
	wbuffer[3] = (mask & 0x10) ? charcol : mcol[0];
	wbuffer[4] = (mask & 0x08) ? charcol : mcol[0];
	wbuffer[5] = (mask & 0x04) ? charcol : mcol[0];
	wbuffer[6] = (mask & 0x02) ? charcol : mcol[0];
	wbuffer[7] = (mask & 0x01) ? charcol : mcol[0];
}

// renders text without the reverse (all 256 chars)
inline void TED::rv_text()
{
	unsigned char	chr;
	unsigned char	charcol;
	unsigned char	mask;
	unsigned char	*wbuffer = scrptr + hshift;

	// get the actual physical character column
	charcol=clrbuf[x];
	chr=chrbuf[x];

	if ((charcol)&0x80 && !crsrblinkon)
		mask = 00;
	else if (VertSubActive)
		mask = cset[(chr<<3)|vertSubCount];
	else
		mask = Read(0xFFFF);

	if (crsrpos==((CharacterPosition+x)&0x3FF) && crsrblinkon )
		mask ^= 0xFF;

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
inline void TED::ec_text()
{
	unsigned char charcol;
	unsigned char chr;
	unsigned char mask;
	unsigned char *wbuffer = scrptr + hshift;

	// get the actual physical character column
	charcol = clrbuf[x] & 0x7F;
	chr = chrbuf[x];

	if (VertSubActive)
		mask = cset[ ((chr & 0x3F) << 3)|vertSubCount ];
	else
		mask = Read(0xFFFF);

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

// renders multicolor text
inline void TED::mc_text_rvs()
{
	unsigned char chr = chrbuf[x];
	unsigned char charcol = clrbuf[x];
	unsigned char *wbuffer = scrptr + hshift;
	unsigned char mask;

	if (VertSubActive)
		mask = cset[(chr << 3) | vertSubCount];
	else
		mask = Read(0xFFFF);

	if (charcol&0x08) { // if character is multicolored

		mcol[3]=charcol & 0xF7;

		wbuffer[0] = wbuffer[1] = mcol[mask >> 6];
		wbuffer[2] = wbuffer[3] = mcol[(mask & 0x30) >> 4];
		wbuffer[4] = wbuffer[5] = mcol[(mask & 0x0C) >> 2];
		wbuffer[6] = wbuffer[7] = mcol[mask & 0x03];

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

// renders multicolor text with reverse bit set
inline void TED::mc_text()
{
	unsigned char charcol = clrbuf[x];
	unsigned char chr = chrbuf[x] & 0x7F;
	unsigned char *wbuffer = scrptr + hshift;
	unsigned char mask;

	if (VertSubActive)
		mask = cset[(chr << 3) | vertSubCount];
	else
		mask = Read(0xFFFF);

	if ((charcol)&0x08) { // if character is multicolored

		mcol[3] = charcol & 0xF7;

		wbuffer[0] = wbuffer[1] = mcol[mask >> 6];
		wbuffer[2] = wbuffer[3] = mcol[(mask & 0x30) >> 4];
		wbuffer[4] = wbuffer[5] = mcol[(mask & 0x0C) >> 2];
		wbuffer[6] = wbuffer[7] = mcol[mask & 0x03];

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
inline void TED::hi_bitmap()
{
	unsigned char mask;
	unsigned char *wbuffer = scrptr + hshift;
	// get the actual color attributes
	hcol[0] = (chrbuf[x] & 0x0F) | (clrbuf[x] & 0x70);
	hcol[1] = (chrbuf[x] >> 4) | ((clrbuf[x] & 0x07) << 4);

	if (VertSubActive)
		mask = grbank[(((CharacterPosition + x) << 3) & 0x1FFF) | vertSubCount];
	else
		mask = Read(0xFFFF);

	wbuffer[0] = (mask & 0x80) ? hcol[1] : hcol[0];
	wbuffer[1] = (mask & 0x40) ? hcol[1] : hcol[0];
	wbuffer[2] = (mask & 0x20) ? hcol[1] : hcol[0];
	wbuffer[3] = (mask & 0x10) ? hcol[1] : hcol[0];
	wbuffer[4] = (mask & 0x08) ? hcol[1] : hcol[0];
	wbuffer[5] = (mask & 0x04) ? hcol[1] : hcol[0];
	wbuffer[6] = (mask & 0x02) ? hcol[1] : hcol[0];
	wbuffer[7] = (mask & 0x01) ? hcol[1] : hcol[0];
}

// renders multicolor bitmap graphics
inline void TED::mc_bitmap()
{
	unsigned char	mask;
	unsigned char	*wbuffer = scrptr + hshift;
	// get the actual color attributes
	bmmcol[1] = (chrbuf[x] >> 4)|((clrbuf[x] & 0x07) << 4);
	bmmcol[2] = (chrbuf[x] & 0x0F)|(clrbuf[x] & 0x70);

	if (VertSubActive)
		mask = grbank[ (((CharacterPosition+x)<<3)&0x1FFF)+vertSubCount ];
	else
		mask = Read(0xFFFF);

	wbuffer[0]= wbuffer[1] = bmmcol[ mask >> 6 ];
	wbuffer[2]= wbuffer[3] = bmmcol[ (mask & 0x30) >> 4 ];
	wbuffer[4]= wbuffer[5] = bmmcol[ (mask & 0x0C) >> 2 ];
	wbuffer[6]= wbuffer[7] = bmmcol[  mask & 0x03 ];
}

// "illegal" mode: when $FF13 points to an illegal ROM address
//  the current data on the bus is displayed
inline void TED::illegalbank()
{
	unsigned char	chr = chrbuf[x];
	unsigned char	charcol = clrbuf[x];
	unsigned char	mask;
	unsigned char	*wbuffer = scrptr + hshift;

	if (charcol&0x80 && crsrblinkon)
		mask = 00;
	else {
		if (BadLine==1)
			mask = clrbuf[x];
		else if (BadLine==2)
			mask = chrbuf[x];
		else
			mask = Read(VertSubActive ? cpuptr->getPC() : 0xFFFF);
	}

	if (chr&0x80)
		mask ^= 0xFF;
	if (crsrpos==((CharacterPosition+x)&0x3FF) && crsrblinkon)
		mask ^= 0xFF;

	wbuffer[0] = (mask & 0x80) ? charcol : mcol[0];
	wbuffer[1] = (mask & 0x40) ? charcol : mcol[0];
	wbuffer[2] = (mask & 0x20) ? charcol : mcol[0];
	wbuffer[3] = (mask & 0x10) ? charcol : mcol[0];
	wbuffer[4] = (mask & 0x08) ? charcol : mcol[0];
	wbuffer[5] = (mask & 0x04) ? charcol : mcol[0];
	wbuffer[6] = (mask & 0x02) ? charcol : mcol[0];
	wbuffer[7] = (mask & 0x01) ? charcol : mcol[0];
}

void TED::doDMA( unsigned char *Buf, unsigned int Offset )
{
	if (CharacterCount>=0x03d9) {
		memcpy( Buf, VideoBase + CharacterCount + Offset, 0x400 - CharacterCount);
		memcpy( Buf + 0x400 - CharacterCount, VideoBase + Offset, (CharacterCount + 40)&0x03FF);
	} else {
		memcpy( Buf, VideoBase + CharacterCount + Offset, 40);
	}
}

void TED::doVRetrace()
{
	// frame ready...
	loop_continuous = 0;
	// reset screen pointer ("TV" electron beam)
	TVScanLineCounter = 0;
}

void TED::doHRetrace()
{
	// the beam reached a new line
	TVScanLineCounter += 1;
	if ( TVScanLineCounter >= 340 ) {
		doVRetrace();
	}
	scrptr = screen + TVScanLineCounter * SCR_HSIZE;
	endptr = scrptr + SCR_HSIZE;
}

inline void TED::render()
{
	// call the relevant rendering function
	switch (scrattr) {
		case 0:
			hi_text();
			break;
		case REVERSE :
			rv_text();
			break;
		case MULTICOLOR|REVERSE :
			mc_text_rvs();
			break;
		case MULTICOLOR :
			mc_text();
			break;
		case EXTCOLOR|REVERSE :
		case EXTCOLOR :
			ec_text();
			break;
		case GRAPHMODE|REVERSE :
		case GRAPHMODE :
			hi_bitmap();
			break;
		case EXTCOLOR|MULTICOLOR :
		case GRAPHMODE|EXTCOLOR :
		case GRAPHMODE|MULTICOLOR|EXTCOLOR :
		case REVERSE|MULTICOLOR|EXTCOLOR :
		case GRAPHMODE|MULTICOLOR|EXTCOLOR|REVERSE :
			mcec();
			break;
		case GRAPHMODE|MULTICOLOR :
		case GRAPHMODE|MULTICOLOR|REVERSE :
			mc_bitmap();
			break;
		default:
			illegalbank();
			break;
	}
}

inline void TED::newLine()
{
	beamx = 0;
	beamy = ff1d_latch;
	doHRetrace();
	flushBuffer(CycleCounter, TED_SOUND_CLOCK);
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
			// cursor phase counter in TED register $1F
			if ((++crsrphase&0x0F) == 0x0F)
        		crsrblinkon ^= 1;
			break;

		case 251:
			VBlanking = true;
			break;

		case 261: // Vertical retrace
			doVRetrace();
			break;

		case 271:
			VBlanking = false;
			break;

		case 512:
		case 312:
			beamy = 0;
			CharacterPositionReload = 0;
			if (!attribFetch) {
				endOfScreen = true;
			}
			attribFetch = (Ram[0xFF06]&0x10) != 0;
	}
	// is there raster interrupt?
	if (beamy == irqline) {
		Ram[0xFF09] |= Ram[0xFF0A]&0x02 ? 0x82 : 0x02;
		irqFlag |= Ram[0xFF09] & 0x80;
	}
}

// main loop of the whole emulation as the TED feeds the CPU with clock cycles
void TED::ted_process(const unsigned int continuous)
{
	loop_continuous = continuous;
    do {
        switch(++beamx) {

            default:
                break;

            case 2:
                if (VertSubActive)
                	vertSubCount = (vertSubCount+1)&7;
				break;

			case 3:
				if (endOfScreen) {
					vertSubCount = 7;
					endOfScreen = false;
				}
				break;

             case 4:
                if (attribFetch) {
					BadLine |= (vshift == (ff1d_latch & 7)) & (ff1d_latch != 203);
					if ( BadLine ) {
						if (clockingState != TDMADELAY) clockingState = THALT1;
					} else
                 		clockingState = TSS;
                 	if (beamy==203) {
						attribFetch = false;
						if (!(BadLine & 2)) clockingState = TSS;
					}
				}
                break;

            case 8:
				HBlanking = false;
				break;

            case 10:
				if (VertSubActive)
					CharacterPosition = CharacterPositionReload;
                break;

            case 16:
				if (ScreenOn) {
					SideBorderFlipFlop = true;
					memset( scrptr, mcol[0], hshift);
					if (nrwscr)
						CharacterWindow = true;
					x = 0;
				}
				if (BadLine & 1) {
					if (!delayedDMA)
						doDMA(tmpClrbuf, 0);
					else
						delayedDMA = false;
				}
				if (BadLine & 2)
					doDMA(chrbuf, (BadLine & 1) ? 0 : 0x400);
                break;

			case 18:
				if (ScreenOn && !nrwscr) {
					CharacterWindow = true;
				}
				break;

			case 89:
				if (VertSubActive && vertSubCount == 6)
					CharacterCount = (CharacterCount + 40)&0x3FF;
				break;

            case 90:
    			if ( VertSubActive && charPosLatchFlag) // FIXME
					CharacterPositionReload = (CharacterPosition + x + 3)&0x3FF;
				break;

            case 91:
		    	clockingState = TRFSH;
		    	break;

			case 94:
				if (!nrwscr)
  					SideBorderFlipFlop = CharacterWindow = false;
				break;

  			case 96:
				if (nrwscr)
  					SideBorderFlipFlop = CharacterWindow = false;
				// FIXME this breaks on FF1E writes
				if (BadLine & 1) {
					// swap DMA fetch pointers for colour DMA...
					unsigned char *tmpbuf = clrbuf;
					clrbuf = tmpClrbuf;
					tmpClrbuf = tmpbuf;
				}
  			    break;

            case 102:
		    	clockingState = fastmode ? TDS : TSS;
		    	break;

		    case 104:
				HBlanking = true;
				break;

		    case 107: // HSYNC start
				break;

			case 110: // $BC (376)
				charPosLatchFlag = vertSubCount == 6;
				break;

			case 111:
				if (BadLine & 1) {
    			    BadLine = 2;
					VertSubActive = true;
				} else if (BadLine & 2) {// in the second bad line, we're finished...
   					BadLine &= ~2;
				}
				break;

			case 255:
			case 128:
				doHRetrace();
				break;
			case 113:
				ff1d_latch = (beamy + 1) & 0x1FF;
				break;
			case 114: // HSYNC end
    			newLine();
		}

		if (beamx&1) {	// perform these only in every second cycle
			if (t2on && !((timer2--)&0xFFFF)) {// Timer2 permitted
				timer2=0xFFFF;
				Ram[0xFF09] |= Ram[0xFF0A]&0x10 ? 0x90 : 0x10; // interrupt
				irqFlag |= Ram[0xFF09] & 0x80;
			}
			if (t3on && !((timer3--)&0xFFFF)) {// Timer3 permitted
				timer3=0xFFFF;
				Ram[0xFF09] |= Ram[0xFF0A]&0x40 ? 0xC0 : 0x40; // interrupt
				irqFlag |= Ram[0xFF09] & 0x80;
			}
			if (!CharacterWindow && !HBlanking && !VBlanking) {
				// we are on the border area, so use the frame color
				*((int*)(scrptr+4)) = framecol;
			}
			if (scrptr != endptr)
				scrptr+=8;
			else
				doHRetrace();
		} else {
			if (t1on && !timer1--) { // Timer1 permitted decreased and zero
				timer1=(t1start-1)&0xFFFF;
				Ram[0xFF09] |= Ram[0xFF0A]&0x08 ? 0x88 : 0x08; // interrupt
				irqFlag |= Ram[0xFF09] & 0x80;
			}
			if (!(HBlanking |VBlanking)) {
				if (SideBorderFlipFlop) { // drawing the visible part of the screen
					// call the relevant rendering function
					render();
					x = (x + 1) & 0x3F;
				}
				if (!CharacterWindow) {
					// we are on the border area, so use the frame color
					*((int*)scrptr) = framecol;
				}
			}
		}
		if (aligned_write) {
			*aw_addr_ptr = aw_value;
			aligned_write = false;
		}
		switch (clockingState|(beamx&1)) {
        	case TRFSH|1:
 	    	case TSS|1:
 	    	case TDS|1:
       		case TDS:
     	    	cpuptr->process();
     	    	break;
			case TDMADELAY|1:
				cpuptr->process();
				clockingState = THALT1;
				break;
      		case THALT1|1:
  	    	case THALT2|1:
        	case THALT3|1:
            	cpuptr->stopcycle();
            	clockingState<<=1;
            	break;

			case TSSDELAY:
				clockingState = TSS;
			case TSSDELAY|1:
				cpuptr->process();
				break;
			case TDSDELAY|1:
				clockingState = TDS;
				cpuptr->process();
				break;
			case TDSDELAY:
				clockingState = TDS;
				break;
			case TRFSH:
				break;
		}

		CycleCounter++;

#if 1
		unsigned int i = 0;
		while (Clockable::itemHeap[i]) {
			Clockable *c = Clockable::itemHeap[i];
			while (c->ClockCount >= TED_REAL_CLOCK_M10) {
				c->ClockCount -= TED_REAL_CLOCK_M10;
				c->Clock();
			}
			c->ClockCount += c->ClockRate;
			i++;
		}
#endif

	} while (loop_continuous);

	loop_continuous = false;

};

bool TED::enableSidCard(bool enable, unsigned int disableMask)
{
	if (enable) {
		if (sidCard) {
			return true;
			//enableSidCard(false, 0);
        }
		sidCard = new SIDsound(SID8580DB, disableMask);
		sidCard->setSampleRate(SAMPLE_FREQ);
		sidCard->setFrequency(TED_SOUND_CLOCK / 2);
	} else {
		if (!sidCard)
			return false;
		delete sidCard;
		sidCard = 0;
	}
	return false;
}

SIDsound *TED::getSidCard()
{
    return sidCard;
}

Color TED::getColor(unsigned int ix)
{
	Color color;
	unsigned int code = ix & 0x0F;
	int thishue = hue(code);
	double thisluma = luma(ix >> 4);
	if (thishue < 0) {
		color.saturation = 0;
		color.hue = 0;
	} else {
		color.saturation = 45.0;
		color.hue = thishue;
	}
	if (thishue == -2) // black
		color.luma = 0;
	else
		color.luma = thisluma;
	return color;
}

TED::~TED()
{
    if (keys) {
        delete keys;
        keys = NULL;
    }
    if (tap) {
        delete tap;
        tap = NULL;
    }
	if (sidCard)
		enableSidCard(false, 0);
}

//--------------------------------------------------------------
// TED - fast emulation (line based) FIXME: could be made much faster
//--------------------------------------------------------------

enum {
	CLK_BORDER = 0,
	CLK_DMA,
	CLK_SCREEN
};

static bool externalFetchWindow;

TEDFAST::TEDFAST() : TED()
{
	unsigned int i = 0;
	endOfDMA = false;
	//emulationLevel = 0;
	Clockable *device = Clockable::itemHeap[0];
	while (device) {
		device->ClockCount = 0;
		device = Clockable::itemHeap[++i];
	}
}
inline void TEDFAST::dmaLineBased()
{
	static bool endOfDMA;

	if (attribFetch) {

		bool bad_line = (vshift == (beamy & 7));
		// First line after second line of DMA
		if (endOfDMA) {
			endOfDMA = false;
			clockingState = CLK_SCREEN;
		}
		// Check second line DMA
		if (BadLine & 2) {
			unsigned int offset;
			unsigned char *tmp = tmpClrbuf;

			tmpClrbuf = clrbuf;
			clrbuf = tmp;
			offset = bad_line ? 0 : 0x400;
			doDMA(chrbuf, offset);
			endOfDMA = true;
			BadLine &= ~2;
			externalFetchWindow = true;
			VertSubActive = true;
			clockingState = CLK_DMA;
		}
		// Check first line DMA
		if (bad_line) {
			BadLine |= 2;
//			idleState = false;
			clockingState = CLK_DMA;
			doDMA(tmpClrbuf, 0);
		}
		if (externalFetchWindow) {
			vertSubCount = (vertSubCount + 1) & 7;
			if (6 == vertSubCount) {
				CharacterCount = (CharacterCount + 40) & 0x3FF;
				charPosLatchFlag = true;
			} else if (0 == vertSubCount) {
				if (charPosLatchFlag) {
					CharacterPosition = (CharacterPositionReload + 40) & 0x3FF;
					CharacterPositionReload = CharacterPosition;
					charPosLatchFlag = false;
				}
			}
		}
		if (beamy == 203) {
			if (BadLine & 2) {
				BadLine = 0;
				endOfDMA = false;
			}
			attribFetch = false;
		}
	}
	if (beamy == 204) {
		clockingState = CLK_BORDER;
		externalFetchWindow = false; // -?
	} else if (beamy == 311 /*Clock::RasterLinesPerFrame*/) {
		CharacterPositionReload = CharacterPosition = 0;
		charPosLatchFlag = false;
		if (endOfScreen) {
			vertSubCount = 7;
			endOfScreen = false;
		}
	}
}

inline void TEDFAST::countTimers(unsigned int clocks)
{
	int newTimerValue;

	if (t1on) { // Timer1 permitted decreased and zero
		newTimerValue = (int) timer1 - (int) clocks;
		if (newTimerValue <= 0) {
			if (t1start >= clocks)
				timer1 = (t1start + newTimerValue - 1)&0xFFFF;
			Ram[0xFF09] |= Ram[0xFF0A]&0x08 ? 0x88 : 0x08; // interrupt
			irqFlag |= Ram[0xFF09] & 0x80;
		} else {
			timer1 = newTimerValue;
		}
	}
	if (t2on) {
		newTimerValue = (int) timer2 - (int) clocks;
		if (newTimerValue <= 0) { // Timer2 permitted
			Ram[0xFF09] |= Ram[0xFF0A]&0x10 ? 0x90 : 0x10; // interrupt
			irqFlag |= Ram[0xFF09] & 0x80;
		}
		timer2 = newTimerValue & 0xFFFF;
	}
	if (t3on) {
		newTimerValue = (int) timer3 - (int) clocks;
		if (newTimerValue <= 0) {// Timer3 permitted
			Ram[0xFF09] |= Ram[0xFF0A]&0x40 ? 0xC0 : 0x40; // interrupt
			irqFlag |= Ram[0xFF09] & 0x80;
		}
		timer3 = newTimerValue & 0xFFFF;
	}
//	checkIrq();
}

void TEDFAST::renderLine()
{
	if (ScreenOn) {

		unsigned char *screen;

		scrptr += 4 * 8;

		DRAW_BORDER(0,framecol);
		DRAW_BORDER(4,framecol);
		DRAW_BORDER(8,framecol);
		DRAW_BORDER(12,framecol);
		DRAW_BORDER(16,framecol);
		DRAW_BORDER(20,framecol);
		DRAW_BORDER(24,framecol);
		DRAW_BORDER(28,framecol);

		scrptr += 4 * 8;
		screen = scrptr;

		for(x = 0; x < 40; x++) {
			//(this->*scrmode)();
			render();
			scrptr += 8;
		}
		if (!nrwscr) {
			unsigned char bc = framecol & 0xFF;

			memset(screen, bc, 8);
			memset(screen + 312, bc, 8);
		} else if (hshift) {
			unsigned char a;
			switch (scrattr) {
				case 0:
				case REVERSE :
					a = mcol[0];
					break;
				case GRAPHMODE:
				case GRAPHMODE|REVERSE:
					a = (clrbuf[39]&0x70)+(chrbuf[39]&0x0F);
					break;
				default:
					a = ecol[0];
			}
			memset( screen, a, hshift);
		}
		DRAW_BORDER(0,framecol);
		DRAW_BORDER(4,framecol);
		DRAW_BORDER(8,framecol);
		DRAW_BORDER(12,framecol);
		DRAW_BORDER(16,framecol);
		DRAW_BORDER(20,framecol);
		DRAW_BORDER(24,framecol);
		DRAW_BORDER(28,framecol);
	} else {
		unsigned char bc = framecol & 0xFF;
		memset(scrptr + 4 * 8, bc, 384);
	}
}

const unsigned int clocksPerLine[6] = { 109, 22, 65, 57, 14, 57 };

void TEDFAST::ted_process(const unsigned int continuous)
{
	loop_continuous = continuous;

	Clockable *drive = Clockable::itemHeap[0];
	if (drive) {
		for (;loop_continuous;) {

			const unsigned int clkIx = clocksPerLine[clockingState + fastmode ? 0 : 3];

			ff1d_latch = (beamy + 1) & 0x1FF;
			newLine();
			dmaLineBased();
			//if (isFrameRendered()) {
				renderLine();
			//}
			// Drives
			const unsigned int driveStepFactor = 1;
			const int driveCycles = 64; // 312 * 50 * 64 = 998400 ~= 1 MHz

			int machineCycles = clkIx;
			do {
				unsigned int i = 0;

				cpuptr->process();
				Clockable *device = drive;
				do {
					device->ClockCount += driveCycles;
					while (device->ClockCount >= clkIx) { // clkIx 57
						device->Clock(driveStepFactor);
						device->ClockCount -= clkIx; // clkIx 57
					}
					device = Clockable::itemHeap[++i];
				} while (device);
			} while (--machineCycles);
			countTimers(57);
			CycleCounter += 114;
		}
	} else {
		for (;loop_continuous;) {
			const unsigned int clkIx = clocksPerLine[clockingState + fastmode ? 0 : 3];

			ff1d_latch = (beamy + 1) & 0x1FF;
			newLine();
			dmaLineBased();
			//if (isFrameRendered()) {
				renderLine();
			//}
			cpuptr->process(clkIx);
			countTimers(57);
			CycleCounter += 114;
		}
	}
}

void TEDFAST::process_debug(unsigned int continuous)
{
	TED::ted_process(continuous);
}

unsigned char TEDFAST::getHorizontalCount()
{
	unsigned int cyclesPerLine = clocksPerLine[clockingState + fastmode ? 0 : 3];
	return ((98 + ((cyclesPerLine - cpuptr->getRemainingCycles()) * 114
		/ cyclesPerLine)) << 1) % 228;
}
