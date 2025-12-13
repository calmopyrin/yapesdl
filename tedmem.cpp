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

#define RETRACESCANLINEMAX 360
#define RETRACESCANLINEMIN (SCR_VSIZE - (RETRACESCANLINEMAX - SCR_VSIZE))
#define REU_BANK_MASK ((reuBankCount << 1) - 1)

bool			TED::vertSubIncrAllowed;
unsigned int	TED::vertSubCount;
int				TED::x;
unsigned char	*TED::VideoBase;

unsigned int TED::masterClock;
ClockCycle TED::CycleCounter;
bool TED::ScreenOn, TED::attribFetch, TED::dmaAllowed, TED::externalFetchWindow;
bool TED::displayEnable;
bool TED::SideBorderFlipFlop, TED::CharacterWindow;
unsigned int TED::BadLine;
unsigned int	TED::clockingState;
unsigned int	TED::CharacterCount = 0, TED::CharacterCountReload;
bool TED::VertSubActive; // indicates whether the chip is in a non-idle state
unsigned int	TED::CharacterPosition;
unsigned int	TED::CharacterPositionReload;
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
unsigned int TED::retraceScanLine;
unsigned int TED::scanLineOffset;
int TED::scanlinesDone;
unsigned int TED::clockDivisor;
bool TED::ntscMode;
char TED::romlopath[4][260];
char TED::romhighpath[4][260];
unsigned int TED::reuSizeKb;
// 64 kbytes of memory allocated by default
unsigned int TED::RAMMask = 0xFFFF;
unsigned int TED::sidCardEnabled;
unsigned int TED::dmaFetchCountStart = 0;

rvar_t TED::tedSettings[] = {
	//{ "Sid card", "SidCardEnabled", TED::toggleSidCard, &TED::sidCardEnabled, RVAR_TOGGLE, NULL },
	//{ "rom c0 low", "ROMC0LOW", NULL, TED::romlopath[0], RVAR_STRING },
	//{ "rom c1 low", "ROMC1LOW", NULL, TED::romlopath[1], RVAR_STRING },
	//{ "rom c2 low", "ROMC2LOW", NULL, TED::romlopath[2], RVAR_STRING },
	//{ "rom c3 low", "ROMC3LOW", NULL, TED::romlopath[3], RVAR_STRING },
	//{ "rom c0 hi", "ROMC0HIGH", NULL, TED::romhighpath[0], RVAR_STRING },
	//{ "rom c1 hi", "ROMC1HIGH", NULL, TED::romhighpath[1], RVAR_STRING },
	//{ "rom c2 hi", "ROMC2HIGH", NULL, TED::romhighpath[2], RVAR_STRING },
	{ "C264 RAM mask", "RamMask", TED::flipRamMask, &TED::RAMMask, RVAR_HEX, NULL },
	{ "RAM expansion (REU) in kB", "256KBRAM", TED::flipRamExpansion, &TED::reuSizeKb, RVAR_INT, NULL },
	{ "", "", NULL, NULL, RVAR_NULL, NULL }
};

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

TED::TED() : SaveState(), sidCard(0), crsrphase(0), ramExt(0), reuBank(3)
{
	unsigned int i;

	instance_ = this;
	masterClock = TED_REAL_CLOCK_M10;
	clockDivisor = 10;
	ntscMode = false;
	setId("TED0");

	screen = new unsigned char[512 * (SCR_VSIZE*2)];
	// clearing cartdridge ROMs
	for (i=0;i<4;++i) {
		memset(rom, 0, ROMSIZE * 2);
		memset(romlopath,0,sizeof(romlopath));
		memset(romhighpath,0,sizeof(romhighpath));
	};
	// default ROM sets
	strcpy(romlopath[0],"BASIC");
	strcpy(romhighpath[0],"KERNAL");

	// actual ram bank pointer default setting
	actram=Ram;
	Reset(3);

	// setting screen memory pointer
	scrptr=screen;
	// pointer of the end of the screen memory
	endptr=scrptr + 456;
	// setting the CPU to fast mode
	fastmode=-1;
	// initial position of the electron beam (upper left corner)
	irqline=vertSubCount=0;
	beamy = ff1d_latch = 0;
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
	crsrblinkon = 0;
	VertSubActive = false;
	dmaAllowed = false;
	externalFetchWindow = false;
	CharacterPositionReload = CharacterPosition = 0;
	CharacterCountReload = 0x03FF;
	SideBorderFlipFlop = false;
	render_ok = false;
	displayEnable = false;

	irqFlag = 0;
	BadLine = 0;
	CycleCounter = 0;
	retraceScanLine = RETRACESCANLINEMAX;
	scanLineOffset = 0;

	tedSoundInit(sampleRate);
	if (enableSidCard(true, 0)) {
		//sidCard->setModel(SID8580DB);
	}
	enableREU(reuSizeKb);
}

void TED::flipRamMask(void *none)
{
	RAMMask = (RAMMask + (RAMMask == 0x7FFF ? 0x8000 : 0x4000)) & 0xFFFF;
	TED *ted = instance_;
	// only reset if +4
	if (ted->getEmulationLevel() < 2) {
		ted->cpuptr->Reset();
		ted->Reset(3);
	}
}

void TED::soundReset()
{
	if (sidCard) sidCard->reset();
}

void TED::Reset(unsigned int resetLevel)
{
	soundReset();
	fastmode = 2;
	// reset memory banks
	if (resetLevel & 1) {
		reuBank = 0;
		RAMenable = false;
		actram = actramBelow4000 = Ram;
		ChangeMemBankSetup();
	}
	// clear RAM with powerup pattern and reload ROM's
	if (resetLevel & 2) {
		for (int i = 0; i < RAMSIZE; i++)
			Ram[i] = (i >> 1) << 1 == i ? 0 : 0xFF;
		loadroms();
	}
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
	int i =0;

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

void TED::chrtoscreen(int x,int y, unsigned int scrchr)
{
	unsigned int j, k;
	const unsigned int CPR = getCyclesPerRow();
	unsigned char *charset = (unsigned char *) kernal + 0x1000;

	if (isalpha(scrchr)) {
		scrchr = (scrchr & ~0xE0) | ((scrchr & 0x20) << 2);
	}
	charset += (scrchr << 3);
	for (j=0;j<8;j++)
		for (k=0;k<8;k++)
			screen[(y+j)*CPR+x+k] = (*(charset+j) & (0x80>>k)) ? 0x00 : 0x71;
}

void TED::loadroms()
{
	for (int i=0;i<4;i++) {
		loadromfromfile(i,romhighpath[i], 0x4000);
		loadromfromfile(i,romlopath[i], 0);
	}
	mem_8000_bfff = actromlo = rom[0];
	mem_fc00_fcff = mem_c000_ffff = actromhi = rom[0] + 0x4000;
}

void TED::loadromfromfile(int nr, const char fname[512], unsigned int offset)
{
	FILE *img;

	if ((fname[0]!='\0')) {
		if ((img = fopen(fname, "rb"))) {
			// load low ROM file
			unsigned char *buf;
			buf = (unsigned char *)malloc(0x8000);
			size_t r = fread(buf, 1, ROMSIZE * 2, img);
			if (r > 0x4000) offset = 0;
			memcpy(rom[nr] + offset, buf, r);
			strcpy(!offset ? romlopath[nr] : romhighpath[nr], fname);
			free(buf);
			fclose(img);
			return;
		}
		switch (nr) {
			case 0:
				if (!strncmp(fname, "BASIC", 5) || (!strncmp(fname, "*", 1) && offset == 0)) {
					memcpy(rom[0] + offset, basic, ROMSIZE);
					strcpy(romlopath[nr], "BASIC");
				} 
				else if (!strncmp(fname, "KERNAL", 6) || (!strncmp(fname, "*", 1) && offset == 0x4000)) {
					memcpy(rom[0] + offset, kernal, ROMSIZE);
					strcpy(romhighpath[nr], "KERNAL");
					if (ntscMode) {
						for (unsigned int i = 0; i < NTSC_PATCH_SIZE; i++) {
							rom[0][offset + ntsc_kernal05[i].addr] = ntsc_kernal05[i].byte;
						}
					}
				}
				break;
			case 1: 
				if (!strncmp(fname, "3PLUS1LOW", 9)) {
					memcpy(rom[1] + offset, plus4lo, ROMSIZE);
					strcpy(romlopath[nr], fname);
				}
				else if (!strncmp(fname, "3PLUS1HIGH", 10)) {
					memcpy(rom[1] + offset, plus4hi, ROMSIZE);
					strcpy(romhighpath[nr], fname);
				} 
				else {
					memset(rom[1] + offset, 0, ROMSIZE);
					strcpy(offset ? romhighpath[nr] : romlopath[nr], "");
				}
				break;
			default: 
				memset(rom[nr] + offset, 0, ROMSIZE);
				strcpy(offset ? romhighpath[nr] : romlopath[nr], "");
		}
	} else {
		memset(rom[nr] + offset, 0, ROMSIZE);
		strcpy(offset ? romhighpath[nr] : romlopath[nr], "");
	}
}

ClockCycle TED::GetClockCount()
{
	return CycleCounter;
}

void TED::log(unsigned int addr, unsigned int value)
{
	fprintf(stderr, "%04X<-%02X(%03d) Old:%02X HC:(%03d/$%02X) VC:(%03d/$%03X) XY:%03i/%03i PC:$%04X BL:%02X frm:%i cyc:%llu\n",
		addr, value, value, Read(addr), getHorizontalCount(), getHorizontalCount(), beamy, beamy, beamx, TVScanLineCounter, cpuptr->getPC(), BadLine, crsrphase, CycleCounter);
}

void TED::setNtscMode(bool on)
{
	ntscMode = on;
	masterClock = on ? TED_REAL_CLOCK_NTSC_M10 : TED_REAL_CLOCK_M10;
	loadroms();
	Reset(1);
	Write(0xFF07, (Read(0xFF07) & ~0x40) | (on ? 0x40 : 0));
	setFrequency((on ? TED_CLOCK_NTSC : TED_CLOCK) / clockDivisor / 8);
	//cpuptr->Reset();
}

void TED::ChangeMemBankSetup()
{
	if (RAMenable) {
		mem_8000_bfff = actram + (0x8000 & RAMMask);
		mem_fc00_fcff = mem_c000_ffff = actram + (0xC000 & RAMMask);
	} else {
		mem_8000_bfff = actromlo;
		mem_c000_ffff = actromhi;
		mem_fc00_fcff = rom[0] + 0x4000;
	}
}

unsigned char TED::readOpenAddressSpace(unsigned int addr)
{
	unsigned int pc = cpuptr->getPC();

	switch (clockingState) {
		default:
			return addr >> 8;
		case TDS:
			return ((pc ^ addr) & 0xFF00) ? Read(pc) : (addr >> 8);
	}
}

unsigned char* TED::getCharSetPtr()
{ 
	return kernal + 0x1400;
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
							(readBus() & 0xC0)
							|(tap->readCSTIn(CycleCounter) & 0x10);
						return (prp & prddr)|(retval & ~prddr);
					}
				default:
					return Ram[addr&0xFFFF];
			}
			break;
		case 0x1000:
		case 0x2000:
		case 0x3000:
			return actramBelow4000[addr&0xFFFF];
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
						case 0xFF1F :
							{
								unsigned char vsubRetVal;
								if (beamx == 1 && vertSubIncrAllowed) {
									vsubRetVal = (vertSubCount & (vertSubCount + 1)) & 7;
								}
								else {
									vsubRetVal = vertSubCount;
								}
								return 0x80 | (crsrphase << 3) | vsubRetVal;
							}
						case 0xFF3E:
						case 0xFF3F:
							return readOpenAddressSpace(addr);
						default:
							return mem_c000_ffff[addr&0x3FFF];
					}
					break;
				case 0xFE:
					switch (addr >> 4)  {
						case 0xFEC: // U9
						case 0xFED:
						case 0xFEE: // U8
						case 0xFEF:
							if (tcbmbus)
								return tcbmbus->Read(addr);
						default:
								return readOpenAddressSpace(addr);
					}
				case 0xFD:
					switch (addr>>4) {
						case 0xFD0: // RS232
							return readOpenAddressSpace(addr);
						case 0xFD1: // User port, PIO & 256 RAM expansion
						{
							if (!reuSizeKb || addr == 0xFD10)
								return Ram[0xFD10] & ~(tap->IsButtonPressed() << 2);
							if (addr == 0xFD16) {
								unsigned char mask = REU_BANK_MASK;
								return Ram[addr];//  & mask) | (0xFF & ~mask);
							}
							return readOpenAddressSpace(addr);
						}
						case 0xFD2: // Speech hardware
							return readOpenAddressSpace(addr);
						case 0xFD3:
							return Ram[0xFD30];
						case 0xFD4: // SID Card
						case 0xFD5:
							if (sidCard) {
								flushBuffer(CycleCounter, TED_SOUND_CLOCK);
								return sidCard->read(addr & 0x1f);
							}
							return 0xFD;
						case 0xFD8: // Joyport on SID-card
							if (sidCard) {
								return keys->readSidcardJoyport();
							}
					}
					return readOpenAddressSpace(addr);
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
		tap->setTapeMotor(CycleCounter, portVal&8);

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
	unsigned int offset = (Ram[0xFF13] << 8) & tmp;
	charrambank = Ram + offset;
	charrombank = rom[0] + (charbank & (0x7800 | (tmp & 0x0400)));
	cset = charrom ? charrombank : charrambank;
}

void TED::Write(unsigned int addr, unsigned char value)
{
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
					Ram[addr & 0xFFFF] = value;
			}
			return;
		case 0x1000:
		case 0x2000:
		case 0x3000:
			actramBelow4000[addr&0xFFFF] = value;
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
						case 0xFF00:
							t1on=false; // Timer1 disabled
							t1start=(t1start & 0xFF00)|value;
							timer1=(timer1 & 0xFF00)|value;
							return;
						case 0xFF01:
							t1on=true; // Timer1 enabled
							t1start=(t1start & 0xFF)|(value<<8);
							timer1=(timer1 & 0x00FF)|(value<<8);
							return;
						case 0xFF02 :
							t2on=false; // Timer2 disabled
							timer2=(timer2 & 0xFF00)|value;
							return;
						case 0xFF03:
							t2on=true; // Timer2 enabled
							timer2=(timer2&0x00FF)|(value<<8);
							return;
						case 0xFF04:
							t3on=false;  // Timer3 disabled
							timer3=(timer3&0xFF00)|value;
							return;
						case 0xFF05:
							t3on=true; // Timer3 enabled
							timer3=(timer3&0x00FF)|(value<<8);
							return;
						case 0xFF06:
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
							displayEnable = !!(value & 0x10);
							if (!attribFetch && displayEnable && ff1d_latch == 0) {
								attribFetch = dmaAllowed = true;
								vertSubCount = 7;
								if (vshift != (ff1d_latch & 7))
								{
									if (beamx > 4 && beamx < 84)
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
									if (beamx>=3 && beamx<86) {
										const unsigned char idleread = Read((cpuptr->getPC()+1)&0xFFFF);
										const unsigned int delay = (BadLine & 2) ? 0 : (beamx - 1) >> 1;
										unsigned int invalidcount = (delay > 3) ? 3 : delay;
										const unsigned int invalidpos = delay - invalidcount;
										invalidcount = (invalidcount < 40-invalidpos) ? invalidcount : 40-invalidpos;
										const unsigned int newdmapos = (invalidpos+invalidcount < 40) ? invalidpos+invalidcount : 40;
										const unsigned int newdmacount = 40 - newdmapos ;
										const unsigned int oldcount = 40 - newdmacount - invalidcount;
										const unsigned int dmaposition = (CharacterCountReload + 1) & 0x03FF;

										memcpy(tmpClrbuf, clrbuf, oldcount);
										memset(tmpClrbuf + oldcount, idleread, invalidcount);
										memcpy(tmpClrbuf + oldcount + invalidcount, VideoBase + dmaposition + oldcount + invalidcount, newdmacount);
										BadLine |= 1;
										// Do not refetch if on or before stop cycle
										if (beamx < 11)
											delayedDMA = true;
										if (!(BadLine & 2))
											clockingState = TDMADELAY;
									} else if (beamx<111 && beamx>=86) {
										// FIXME this breaks on FF1E writes
										if (!(BadLine & 1) && beamx < 94)
										{
											unsigned char* tmpbuf = clrbuf;
											clrbuf = tmpClrbuf;
											tmpClrbuf = tmpbuf;
										}
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
						case 0xFF07:
							Ram[0xFF07]=value;
							// check for narrow screen (38 columns)
							nrwscr=value&0x08;
							// get horizontal offset of screen when smooth scroll
							hshift=value&0x07;
							// NTSC/PAL clock
							clockDivisor = (value & 0x40) ? 8 : 10;
							// check for reversed mode
							rvsmode = value & 0x80;
							// check for multicolor mode
							scrattr = (scrattr & ~(MULTICOLOR|REVERSE)) | (value & (MULTICOLOR|REVERSE));
							changeCharsetBank();
							return;
						case 0xFF08:
							keys->latch(Ram[0xfd30], Ram[0xFF08] = value);
							return;
						case 0xFF09:
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
						case 0xFF0A:
							{
								Ram[0xFF0A]=value;
								// change the raster irq line
								unsigned int newirqline = (irqline&0xFF)|((value&0x01)<<8);
								if (newirqline != irqline) {
									if (beamy == newirqline) {
										Ram[0xFF09]|=0x02;
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
						case 0xFF0B:
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
						case 0xFF0C:
							crsrpos=((value<<8)|(crsrpos&0xFF))&0x3FF;
							return;
						case 0xFF0D:
							crsrpos=value|(crsrpos&0xFF00);
							return;
						case 0xFF0E:
							if (value != Ram[0xFF0E]) {
								writeSoundReg(CycleCounter, 0, value);
								Ram[0xFF0E]=value;
							}
							return;
						case 0xFF0F:
							if (value != Ram[0xFF0F]) {
								writeSoundReg(CycleCounter, 1, value);
								Ram[0xFF0F]=value;
							}
							return;
						case 0xFF10:
							if (value != Ram[0xFF10]) {
								writeSoundReg(CycleCounter, 2, value & 3);
								Ram[0xFF10]=value;
							}
							return;
						case 0xFF11:
							if (value != Ram[0xFF11]) {
								writeSoundReg(CycleCounter, 3, value);
								Ram[0xFF11]=value;
							}
							return;
						case 0xFF12:
							if ((value ^ Ram[0xFF12]) & 3)
								writeSoundReg(CycleCounter, 4, value & 3);
							// if the 2nd bit is set the chars are read from ROM
							charrom=(value&0x04) != 0;
							grbank = charrom ? rom[0] + (((value & 0x38) << 10) & 0x7000) : (Ram + ((value & 0x38) << 10));
							if (charrom && Ram[0xFF13] < 0x80)
								scrattr|=ILLEGAL;
							else {
								scrattr&=~ILLEGAL;
								cset = charrom ? charrombank : charrambank;
							}
							Ram[0xFF12]=value;
							return;
						case 0xFF13:
							// the 0th bit is not writable, it indicates if the ROMs are on
							Ram[0xFF13]=(value&0xFE)|(Ram[0xFF13]&0x01);
							// bit 1 is the fast/slow mode switch
							if ((fastmode ^ value ^ 2) & 2) {
								fastmode = (value & 2) ^ 2;
								if (!fastmode) {
									if (clockingState == TDS) {
										clockingState = TSSDELAY;
									} else if (clockingState == TRFSH) {
										clockingState = TSS;
									}
								} else {
									if (clockingState == TSS) {
										clockingState = TDSDELAY;
									}
								}
							}
							charbank = value << 8;
							changeCharsetBank();
							if (charrom && value<0x80)
								scrattr|=ILLEGAL;
							else {
								scrattr&=~ILLEGAL;
								(charrom) ? cset = charrombank : cset = charrambank;
							}
							return;
						case 0xFF14:
							Ram[0xFF14]=value;
							VideoBase = Ram+(((value&0xF8)<<8)&RAMMask);
							return;
						case 0xFF15:
							ecol[0]=bmmcol[0]=mcol[0]=value&0x7F;
							return;
						case 0xFF16:
							ecol[1]=bmmcol[3]=mcol[1]=value&0x7F;
							return;
						case 0xFF17:
							ecol[2]=mcol[2]=value&0x7F;
							return;
						case 0xFF18:
							ecol[3]=value&0x7F;
							return;
						case 0xFF19:
							value &= 0x7F;
							framecol=(value<<24)|(value<<16)|(value<<8)|value;
							return;
						case 0xFF1A:
							CharacterPositionReload = (CharacterPositionReload & 0xFF) | ((value&3)<<8);
							if (beamx == 89)
								charPosLatchFlag = false;
							return;
						case 0xFF1B:
							CharacterPositionReload = (CharacterPositionReload & 0x300) | value;
							if (beamx == 89)
								charPosLatchFlag = false;
							return;
						case 0xFF1C:
							beamy=((value&0x01)<<8)|(beamy&0xFF);
							return;
						case 0xFF1D:
							//log(0xff1d, value);
							beamy=(beamy&0x0100)|value;
							if (beamx == 113)
								ff1d_latch = beamy;
							return;
						case 0xFF1E:
							{
								unsigned int low_x = beamx&1;
								// lowest 2 bits are not writable
								// inverted value must be written
								unsigned int new_beamx=((~value))&0xFC;
								new_beamx >>= 1;
								if (new_beamx < 114)
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
								if (value != 0x78) crsrblinkon ^= 0xFF;
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
						case 0xFD2: // Speech hardware
							return;
						case 0xFD1: // User port/PIO & Hannes RAM expansion
							if (ramExt) {
								if (addr == 0xFD16) {
									reuWrite(value);
									Ram[0xFD16] = value;
									return;
								} else if (addr != 0xFD10)
									return;
							}
							Ram[0xFD10] = value;
							return;
						case 0xFD3:
							Ram[0xFD30] = value;
							return;
						case 0xFD4: // SID Card
						case 0xFD5:
						case 0xFE8:
						case 0xFE9:
							if (sidCard) {
								flushBuffer(CycleCounter, TED_SOUND_CLOCK);
								sidCard->write(addr & 0x1f, value);
							}
							return;
						case 0xFDD:
							actromlo = rom[addr&0x03];
							actromhi = rom[(addr&0x0c)>>2] + 0x4000;
							ChangeMemBankSetup();
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
	charrombank = rom[0] + (charbank & 0x7C00);
	(charrom) ? cset = charrombank : cset = charrambank;
	ChangeMemBankSetup();
}

// when multi and extended color modes are all on the screen is blank
inline void TED::mcec()
{
	memset( scrptr, 0, 8);
}

// renders hires text with reverse (128 chars)
inline void TED::hi_text()
{
	const unsigned char col[] = { mcol[0], clrbuf[x] };
	const unsigned char	chr = chrbuf[x];
	unsigned char* wbuffer = scrptr + hshift;
	unsigned char mask;

	if (col[1] & 0x80 && !crsrblinkon)
		mask = 00;
	else if (VertSubActive)
		mask = cset[((chr & 0x7F) << 3) | vertSubCount];
	else
		mask = (clockingState & TDS) ? cpuptr->getcins() : Read(0xFFFF);

	mask ^= (chr >> 7) * 0xFF;
	mask ^= (crsrpos == ((CharacterPosition + x) & 0x3FF)) * crsrblinkon;

	wbuffer[0] = col[mask >> 7];
	wbuffer[1] = col[(mask & 0x40) >> 6];
	wbuffer[2] = col[(mask & 0x20) >> 5];
	wbuffer[3] = col[(mask & 0x10) >> 4];
	wbuffer[4] = col[(mask & 0x08) >> 3];
	wbuffer[5] = col[(mask & 0x04) >> 2];
	wbuffer[6] = col[(mask & 0x02) >> 1];
	wbuffer[7] = col[mask & 1];
}

// renders text without the reverse (all 256 chars)
inline void TED::rv_text()
{
	const unsigned char col[] = { mcol[0], clrbuf[x] };
	const unsigned char	chr = chrbuf[x];
	unsigned char* wbuffer = scrptr + hshift;
	unsigned char mask;

	if (col[1] & 0x80 && !crsrblinkon)
		mask = 00;
	else if (VertSubActive)
		mask = cset[(chr << 3) | vertSubCount];
	else
		mask = (clockingState & TDS) ? cpuptr->getcins() : Read(0xFFFF);

	mask ^= (crsrpos == ((CharacterPosition + x) & 0x3FF)) * crsrblinkon;

	wbuffer[0] = col[mask >> 7];
	wbuffer[1] = col[(mask & 0x40) >> 6];
	wbuffer[2] = col[(mask & 0x20) >> 5];
	wbuffer[3] = col[(mask & 0x10) >> 4];
	wbuffer[4] = col[(mask & 0x08) >> 3];
	wbuffer[5] = col[(mask & 0x04) >> 2];
	wbuffer[6] = col[(mask & 0x02) >> 1];
	wbuffer[7] = col[mask & 1];
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
		mask = (clockingState & TDS) ? cpuptr->getcins() : Read(0xFFFF);

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
	const unsigned char chr = chrbuf[x];
	const unsigned char col[] = { mcol[0], clrbuf[x] };
	unsigned char *wbuffer = scrptr + hshift;
	unsigned char mask;

	if (VertSubActive)
		mask = cset[(chr << 3) | vertSubCount];
	else
		mask = (clockingState & TDS) ? cpuptr->getcins() : Read(0xFFFF);

	if (col[1] & 0x08) { // if character is multicolored

		mcol[3] = col[1] & 0xF7;

		wbuffer[0] = wbuffer[1] = mcol[mask >> 6];
		wbuffer[2] = wbuffer[3] = mcol[(mask & 0x30) >> 4];
		wbuffer[4] = wbuffer[5] = mcol[(mask & 0x0C) >> 2];
		wbuffer[6] = wbuffer[7] = mcol[mask & 0x03];

	} else { // this is a normally colored character

		wbuffer[0] = col[mask >> 7];
		wbuffer[1] = col[(mask & 0x40) >> 6];
		wbuffer[2] = col[(mask & 0x20) >> 5];
		wbuffer[3] = col[(mask & 0x10) >> 4];
		wbuffer[4] = col[(mask & 0x08) >> 3];
		wbuffer[5] = col[(mask & 0x04) >> 2];
		wbuffer[6] = col[(mask & 0x02) >> 1];
		wbuffer[7] = col[mask & 1];
	}
}

// renders multicolor text with reverse bit set
inline void TED::mc_text()
{
	unsigned char chr = chrbuf[x] & 0x7F;
	const unsigned char col[] = { mcol[0], clrbuf[x] };
	unsigned char *wbuffer = scrptr + hshift;
	unsigned char mask;

	if (VertSubActive)
		mask = cset[(chr << 3) | vertSubCount];
	else
		mask = (clockingState & TDS) ? cpuptr->getcins() : Read(0xFFFF);

	if (col[1] & 0x08) { // if character is multicolored

		mcol[3] = col[1] & 0xF7;

		wbuffer[0] = wbuffer[1] = mcol[mask >> 6];
		wbuffer[2] = wbuffer[3] = mcol[(mask & 0x30) >> 4];
		wbuffer[4] = wbuffer[5] = mcol[(mask & 0x0C) >> 2];
		wbuffer[6] = wbuffer[7] = mcol[mask & 0x03];

	} else { // this is a normally colored character

		wbuffer[0] = col[mask >> 7];
		wbuffer[1] = col[(mask & 0x40) >> 6];
		wbuffer[2] = col[(mask & 0x20) >> 5];
		wbuffer[3] = col[(mask & 0x10) >> 4];
		wbuffer[4] = col[(mask & 0x08) >> 3];
		wbuffer[5] = col[(mask & 0x04) >> 2];
		wbuffer[6] = col[(mask & 0x02) >> 1];
		wbuffer[7] = col[mask & 1];
	}
}

// renders hires bitmap graphics
inline void TED::hi_bitmap()
{
	const unsigned char chr = chrbuf[x];
	const unsigned char clr = clrbuf[x];
	const unsigned char hcol0 = (chr & 0x0F) | (clr & 0x70);
	const unsigned char hcol1 = (chr >> 4) | ((clr & 7) << 4);
	const unsigned char col[] = { hcol0, hcol1 };
	unsigned char* wbuffer = scrptr + hshift;
	unsigned char mask;

	if (VertSubActive)
		mask = grbank[(((CharacterPosition + x) << 3) & 0x1FFF) | vertSubCount];
	else
		mask = (clockingState & TDS) ? cpuptr->getcins() : Read(0xFFFF);

	wbuffer[0] = col[mask >> 7];
	wbuffer[1] = col[(mask & 0x40) >> 6];
	wbuffer[2] = col[(mask & 0x20) >> 5];
	wbuffer[3] = col[(mask & 0x10) >> 4];
	wbuffer[4] = col[(mask & 0x08) >> 3];
	wbuffer[5] = col[(mask & 0x04) >> 2];
	wbuffer[6] = col[(mask & 0x02) >> 1];
	wbuffer[7] = col[mask & 1];
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
		mask = grbank[ (((CharacterPosition + x) <<3 ) & 0x1FFF) + vertSubCount ];
	else
		mask = (clockingState & TDS) ? cpuptr->getcins() : Read(0xFFFF);

	wbuffer[0]= wbuffer[1] = bmmcol[ mask >> 6 ];
	wbuffer[2]= wbuffer[3] = bmmcol[ (mask & 0x30) >> 4 ];
	wbuffer[4]= wbuffer[5] = bmmcol[ (mask & 0x0C) >> 2 ];
	wbuffer[6]= wbuffer[7] = bmmcol[  mask & 0x03 ];
}

// "illegal" mode: when $FF13 points to an illegal ROM address
//  the current data on the bus is displayed
inline void TED::illegalbank()
{
	const unsigned char	chr = chrbuf[x];
	const unsigned char	clr = clrbuf[x];
	const unsigned char col[] = { mcol[0], clrbuf[x] };
	unsigned char	mask;
	unsigned char	*wbuffer = scrptr + hshift;

	if (col[1] & 0x80 && crsrblinkon)
		mask = 00;
	else {
		if (BadLine == 1)
			mask = clr;
		else if (BadLine == 2)
			mask = chr;
		else
			mask = Read(VertSubActive ? cpuptr->getPC() : 0xFFFF);
	}

	mask ^= (chr >> 7) * 0xFF;
	mask ^= (crsrpos == ((CharacterPosition + x) & 0x3FF)) * crsrblinkon;

	wbuffer[0] = col[mask >> 7];
	wbuffer[1] = col[(mask & 0x40) >> 6];
	wbuffer[2] = col[(mask & 0x20) >> 5];
	wbuffer[3] = col[(mask & 0x10) >> 4];
	wbuffer[4] = col[(mask & 0x08) >> 3];
	wbuffer[5] = col[(mask & 0x04) >> 2];
	wbuffer[6] = col[(mask & 0x02) >> 1];
	wbuffer[7] = col[mask & 1];
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
	//fprintf(stderr, "Rasterline: %03i TV line:%03u\n", beamy, TVScanLineCounter);
	// frame ready...
	loop_continuous = 0;
	// reset screen pointer ("TV" electron beam)
	TVScanLineCounter = 0;
	scrptr = screen;
	retraceScanLine = 0;
	VBlanking = false;
}

void TED::doHRetrace()
{
	if (!retraceScanLine) {
		unsigned int maxLinessThreshold = RETRACESCANLINEMAX;
		if (ntscMode) maxLinessThreshold -= 48;
		if (TVScanLineCounter >= maxLinessThreshold)
			retraceScanLine = 1;
	} else {
		retraceScanLine += 1;
		if ((Ram[0xFF07] & 0x40 ? 20U : 22U) <= retraceScanLine) {
			unsigned int minLinessThreshold = RETRACESCANLINEMIN;
			if (ntscMode) minLinessThreshold -= 48;
			if (TVScanLineCounter >= minLinessThreshold) {
				const int linesInFrame = ntscMode ? 240 : 288;
				scanlinesDone = int(TVScanLineCounter) - int(scanLineOffset) + 1;
				if (retraceScanLine <= 22) scanlinesDone -= retraceScanLine + 2;
				if (scanlinesDone < linesInFrame) {
					const int linesSkipped = (linesInFrame - scanlinesDone) / 2;
					memset(scrptr, 0, SCR_HSIZE * linesSkipped);
					// center the picture if fewer lines
					const unsigned int pixelOffset = SCR_HSIZE * linesSkipped;
					memset(screen, 0, pixelOffset);
					scrptr = screen + pixelOffset;
					loop_continuous = 0;
					TVScanLineCounter = scanLineOffset = linesSkipped;
					retraceScanLine = 0;
					VBlanking = false;
				}
				else {
					scanLineOffset = 0;
					doVRetrace();
				}
				return;
			}
		}
	}
	// the beam reached a new line
	TVScanLineCounter += 1;
	if (scrptr != endptr)
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
			if (!fltscr && displayEnable) ScreenOn = true;
			break;

		case 8:
			if (fltscr && displayEnable) ScreenOn = true;
			break;

		case 200:
			if (fltscr) ScreenOn = false;
			break;

		case 203:
			dmaAllowed = false;
			break;

		case 204:
			if (!fltscr) ScreenOn = false;
			VertSubActive = false;
			attribFetch = false;
			break;

		case 205:
			// cursor phase counter in TED register $1F
			if ((++crsrphase&0x0F) == 0x0F)
				crsrblinkon ^= 0xFF;
			break;

		case 226: // NTSC
			if (Ram[0xFF07] & 0x40) VBlanking = true;
			break;

		case 229: // NTSC
			if ((Ram[0xFF07] & 0x40) && VBlanking && !retraceScanLine)
				retraceScanLine = 1;
			break;

		case 247: // NTSC
			if (Ram[0xFF07] & 0x40)
				VBlanking = false;
			break;

		case 251:
			if (!(Ram[0xFF07] & 0x40))
				VBlanking = true;
			break;

		case 254:
			// Schedule vertical retrace @ 274
			if (!(Ram[0xFF07] & 0x40) && !retraceScanLine)
				retraceScanLine = 1;
			break;

		case 274:
			if (!(Ram[0xFF07] & 0x40))
				VBlanking = false;
			break;

		case 262:
			if (!(Ram[0xFF07] & 0x40))
				break;
		case 512:
		case 312:
			beamy = ff1d_latch = 0;
			CharacterPositionReload = 0;
			if (displayEnable) {
				if (!attribFetch) {
					attribFetch = true;
					endOfScreen = true;
				}
				if (attribFetch) {
					dmaAllowed = true;
				}
			}
	}
	// is there raster interrupt?
	if (beamy == irqline) {
		Ram[0xFF09] |= ((Ram[0xFF0A]&0x02) << 6) | 0x02;
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
				if (vertSubIncrAllowed) {
					vertSubCount = (vertSubCount + 1) & 7;
					vertSubIncrAllowed = dmaAllowed;
				}
				if (ff1d_latch == 204) {
					externalFetchWindow = false;
				}
				break;

			case 3:
				if (endOfScreen) {
					if (!externalFetchWindow)
						vertSubCount = 7;
					endOfScreen = false;
				}
				break;

			 case 4:
				if (dmaAllowed) {
					BadLine |= (vshift == (ff1d_latch & 7));
					if (BadLine) {
						if (clockingState != TDMADELAY) clockingState = THALT1;
					} else
						clockingState = TSS;
				}
				else {
					clockingState = VertSubActive || !fastmode ? TSS : TDS;
				}
				break;

			case 8:
				HBlanking = false;
				break;

			case 9:
				if (dmaAllowed) {
					if (dmaFetchCountStart) {
						CharacterCountReload &= CharacterCount;
					}
					dmaFetchCountStart = 1;
					CharacterCount = CharacterCountReload;
				}
				break;

			case 10:
				if (VertSubActive)
					CharacterPosition = CharacterPositionReload;
				if (BadLine & 1) {
					if (!delayedDMA)
						doDMA(tmpClrbuf, 0);
					else
						delayedDMA = false;
				}
				if (BadLine & 2)
					doDMA(chrbuf, (BadLine & 1) ? 0 : 0x400);
				break;

			case 16:
				if (ScreenOn) {
					SideBorderFlipFlop = true;
					memset( scrptr, mcol[0], hshift);
					if (nrwscr)
						CharacterWindow = true;
					x = 0;
				}
				break;

			case 18:
				if (ScreenOn && !nrwscr) {
					CharacterWindow = true;
				}
				break;

			case 89:
				//fprintf(stderr, "DMApos:%i |reload:%i,VC=%i,VSUB=%i,BL:%i,TVline:%i, VSHIFT=%i, frame:%i\n", CharacterCount, CharacterCountReload, beamy, vertSubCount, BadLine, TVScanLineCounter, vshift, crsrphase);
				if (dmaAllowed) {
					if (vertSubCount == 6)
						CharacterCountReload = CharacterCount;
					dmaFetchCountStart = 0;
				}
				if (beamy == 205) {
					CharacterCountReload = 0x03FF;
				}
				break;

			case 90:
				//fprintf(stderr, "charpos:%i |reload:%i,VC=%i,VSUB=%i,BL:%i,TVline:%i, VSHIFT=%i, frame:%i\n", CharacterPosition, CharacterPositionReload, beamy, vertSubCount, BadLine, TVScanLineCounter, vshift, crsrphase);
				if (VertSubActive && charPosLatchFlag) // FIXME
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

			case 101:
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
					VertSubActive = externalFetchWindow = true;
				} else if (BadLine & 2) {// in the second bad line, we're finished...
					BadLine &= ~2;
				}
				break;

			case 113:
				ff1d_latch = beamy + 1;
				break;

			case 114: // HSYNC end
				newLine();
				if (externalFetchWindow)
					vertSubIncrAllowed = true;
				break;

			case 127:
				beamx = 15;
				doHRetrace();
				break;
		}

		if (beamx&1) {	// perform these only in every second cycle
			if (t2on && !((timer2--)&0xFFFF)) {// Timer2 permitted
				Ram[0xFF09] |= ((Ram[0xFF0A]&0x10) << 3) | 0x10; // interrupt
				irqFlag |= Ram[0xFF09] & 0x80;
			}
			if (t3on && !((timer3--)&0xFFFF)) {// Timer3 permitted
				Ram[0xFF09] |= ((Ram[0xFF0A]&0x40) << 1) | 0x40; // interrupt
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
			switch (clockingState) {
				case TRFSH:
				case TSS:
				case TDS:
					cpuptr->process();
					break;
				case TDMADELAY:
					cpuptr->process();
					clockingState = THALT1;
					break;
				case THALT1:
				case THALT2:
				case THALT3:
					cpuptr->stopcycle();
					clockingState <<= 1;
					break;
				case TSSDELAY:
					cpuptr->process();
					break;
				case TDSDELAY:
					clockingState = TDS;
					cpuptr->process();
					break;
				default:;
			}
			CycleCounter += 2;
			CharacterCount = (CharacterCount + (dmaFetchCountStart != 0)) & 0x3FF;
		} else {
			if (t1on) { // Timer1 permitted?
				if (!timer1) {
					timer1 = t1start;
					Ram[0xFF09] |= ((Ram[0xFF0A] & 0x08) << 4) | 8; // interrupt
					irqFlag |= Ram[0xFF09] & 0x80;
				}
				timer1--;
			}
			if (!(HBlanking||VBlanking)) {
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
			if (aligned_write) {
				*aw_addr_ptr = aw_value;
				aligned_write = false;
			}
			switch (clockingState) {
				case TSSDELAY:
					clockingState = TSS;
				case TDS:
					cpuptr->process();
					break;
				case TDSDELAY:
					clockingState = TDS;
				default:
				case TRFSH:
					break;
			}
		}

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

	} while (loop_continuous);

}

bool TED::enableSidCard(bool enable, unsigned int disableMask)
{
	if (enable) {
		if (sidCard) {
			return true;
		}
		sidCard = new SIDsound(SID8580DB, disableMask);
		sidCard->setSampleRate(SAMPLE_FREQ);
		sidCard->setFrequency(TED_SOUND_CLOCK / 2);
		sidCardEnabled = 1;
		sidCard->setPaddleReadCallback(keys->readPaddleAxis);
	} else {
		if (!sidCard)
			return false;
		delete sidCard;
		sidCard = 0;
		sidCardEnabled = 0;
	}
	return false;
}

SIDsound *TED::getSidCard()
{
	return sidCard;
}

void TED::enableREU(unsigned int sizekb)
{
	if (sizekb) {
		if (ramExt)
			delete[] ramExt;
		unsigned int size = sizekb * 1024 * 1024;
		ramExt = new unsigned char[size];
		reuSizeKb = sizekb;
		reuBankCount = sizekb / 128;
		unsigned int maxBankIndex = REU_BANK_MASK;
 		reuMemMask = maxBankIndex << 26;// size - 1;
		Ram[0xFD16] = maxBankIndex;
		reuWrite(0);
	}
	else { 
		reuSizeKb = reuMemMask = 0;
		delete[] ramExt;
		ramExt = NULL;
		actram = actramBelow4000 = Ram;
	}
}

void TED::flipRamExpansion(void* none)
{
	TED* m = instance_;
	reuSizeKb = (reuSizeKb + (reuSizeKb <= 128 ? 128 : 256)) % 768;
	m->enableREU(reuSizeKb);
}

void TED::reuWrite(unsigned char value)
{
	unsigned int mask = REU_BANK_MASK;
	// bank register bits are inverted
	value ^= (mask & 0x3F);
	// first 2 bits (originally) bank number
	reuBank = value & (reuMemMask >> 26);
	// when 7th bit is set area below $4000 is normal RAM
	// FIXME: bit 6 is indicating a TED DMA bank
	if (reuBank & mask) {
		actram = ramExt + (reuBank << 16);
		actramBelow4000 = (value & 0x80) ? Ram : actram;
	} else
		actram = actramBelow4000 = Ram;
	
	ChangeMemBankSetup();
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
		color.hue = (ntscMode && code == 14) ? 23 : thishue;
	}
	if (thishue == -2) // black
		color.luma = 0;
	else
		color.luma = thisluma;
	return color;
}

TED::~TED()
{
	delete [] screen;
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
			CharacterCount = 0;
		}
	}
	if (beamy == 204) {
		clockingState = CLK_BORDER;
		externalFetchWindow = false; // -?
	} else if (beamy == (ntscMode ? 261 : 311)) {
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
			Ram[0xFF09] |= ((Ram[0xFF0A]&0x08) << 4) | 8; // interrupt
			irqFlag |= Ram[0xFF09] & 0x80;
		} else {
			timer1 = newTimerValue;
		}
	}
	if (t2on) {
		newTimerValue = (int) timer2 - (int) clocks;
		if (newTimerValue <= 0) { // Timer2 permitted
			Ram[0xFF09] |= ((Ram[0xFF0A]&0x10) << 3) | 0x10; // interrupt
			irqFlag |= Ram[0xFF09] & 0x80;
		}
		timer2 = newTimerValue & 0xFFFF;
	}
	if (t3on) {
		newTimerValue = (int) timer3 - (int) clocks;
		if (newTimerValue <= 0) {// Timer3 permitted
			Ram[0xFF09] |= ((Ram[0xFF0A]&0x40) << 1) | 0x40; // interrupt
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

unsigned int TEDFAST::getHorizontalCount()
{
	unsigned int cyclesPerLine = clocksPerLine[clockingState + fastmode ? 0 : 3];
	return ((98 + ((cyclesPerLine - cpuptr->getRemainingCycles()) * 114
		/ cyclesPerLine)) << 1) % 228;
}
