#include <stdio.h>
#include <string.h>
#include <memory.h>
#include "vic2mem.h"
#include "c64rom.h"
#include "Sid.h"
#include "Clockable.h"
#include "video.h"
#include "keys64.h"
#include "sound.h"
#include "tape.h"

#define NEWSDMA 1

#define RASTERX2TVCOL(X) (X < 400 ? X + 104 : X - 400)
#define SET_BITS(REG, VAL) { \
		unsigned int i = 7; \
		do { \
			REG = ((VAL >> i) & 1); \
		} while(i--); \
	}

#define MOB_DO_PIXEL(X, COLOR) \
	do { \
		unsigned char o = out[X]; \
		if (!(o & 0x80)) { \
			if (!(o & 0x40)) { \
				if (!spriteBckgCollReg) { \
					vicReg[0x19] |= ((vicReg[0x1A] & 2) << 6) | 2; \
					checkIRQflag(); \
				} \
				spriteBckgCollReg |= six; \
				if (!priority) out[X] = COLOR; \
			} else \
				out[X] = 0x40 | COLOR;\
		} \
	} while(0);

#define STOP_SPRITE_DMA(X) \
	do { \
		spriteDMAmask &= ~(1 << X); \
		if (!spriteDMAmask) \
			vicBusAccessCycleStart = 0; \
	} while(0);

#if NEWSDMA
#define DO_SPRITE_DMA(X) \
	do { \
		if (mob[X].dmaState) { \
			unsigned int &dc = mob[X].dataCount; \
			unsigned int &dcReload = mob[X].dataCountReload; \
			unsigned char *sData = vicBase + mob[X].dataAddress + dcReload; \
			unsigned char *sBuf = mob[X].sdb[0].shiftRegBuf; \
			sBuf[0] = sData[0]; \
			sBuf[1] = sData[1]; \
			sBuf[2] = sData[2]; \
			dc = dcReload + 3; \
		} \
	} while (0);
#else
#define DO_SPRITE_DMA(X) ;
#endif

#define MOB_READ_ADDRESS(X) mob[X].dataAddress = (VideoBase[0x03F8 + X] << 6); // if (mob[X].dmaState) 

static unsigned char cycleLookup[][128] = {
// SCREEN:             |===========0102030405060708091011121314151617181920212223242526272829303132333435363738391111=========
//     coordinate:                                                                                    111111111111111111111111111111
// 0000000000111111111122222222223333333333444444444455555555556666666666777777777788888888889999999999000000000011111111112222222222
// 0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789
// first cycles:
// 0102030405060708091011121314151617181920....................................................................555657585960616263
//     beamX:
// 11111111111111111111111111
// 000000000011111111112222220000000000111111111122222222223333333333444444444455555555556666666666777777777788888888889999999999
// 012345678901234567890123450123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789
// NO BADLINE
//"3 i 4 i 5 i 6 i 7 i r r r r r g g g g g g g g g g g g g g g g g g g g g g g g g g g g g g g g g g g g g g g g i i 0 i 1 i 2 i "
{ "r r g g g g g g g g g g g g g g g g g g g g g g g g g g g g g g g g g g g g g g g gsisis0sis1sis2sis3sis4sis5sis6sis7sisr r r "},
// bad line
//"33i344i455i566i677i7r r*r*r*rcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcg i i 00i011i122i2"}
{ "r*rcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgcgsisis0sis1sis2sis3sis4sis5sis6sis7sisr r*r*"}
};

static unsigned char prevY;

Vic2mem::Vic2mem() : gamepin(1), exrom(1), reu(0)
{
	instance_ = this;
	setId("VIC2");
	if (!sidCard)
		enableSidCard(true, 0);
	sidCard->setFrequency(VIC_SOUND_CLOCK);
	sidCard->setModel(SID6581);
	masterClock = VIC_REAL_CLOCK_M10;
	colorRAM = new unsigned char[0x0400];
	actram = Ram;
	loadroms();
	chrbuf = DMAbuf;
	// for sideborder effects prefill excess area with space (workaround)
	memset(chrbuf + 40, 32, 24);
	// setting screen memory pointer
	scrptr = screen;
	// important for sprite-bg collisions: fill blank area with border black
	memset(screen, 0x80, VIC_PIXELS_PER_ROW * 312);
	TVScanLineCounter = 0;
	scanlinesDone = 312;
	ntscMode = false;
	beamy = beamx = 0;
	framecol = 0x80808080;
	//
	mobExtCol[0] = 0xFF;
	unsigned int i;
	for(i = 0; i < 256; i++) {
		collisionLookup[i] = i;
	}
	for(i = 0; i < 8; i++) {
		collisionLookup[1ULL << i] = 0;
		mob[i].sdb[0].dwSrDmaBuf = mob[i].sdb[1].dwSrDmaBuf = 0;
	}
	//
	irqFlag = 0;
	vicBase = Ram;
	charrombank = charRomC64;
	charrom = false;
	tap->mem=this;
	keys64 = new KEYS64;
	// CIA's
	cia[0].setIrqCallback(setCiaIrq, this);
	//
	Reset(3);
	// remove TED sound (inherited) from the list
	SoundSource::remove(this);

	enableREU(reuSizeKb);
}

Vic2mem::~Vic2mem()
{
	delete[] colorRAM;
	delete keys64;
}

void Vic2mem::triggerNMI()
{
	cia[1].icr |= 0x10;
	cpuptr->triggerNmi();
	if (cia[1].icr & cia[1].irq_mask & 0x7F) {
		cia[1].icr |= 0x80;
		//cpuptr->clearNmi();
	}
}

void Vic2mem::Reset(unsigned int resetLevel)
{
	// reset memory banks
	if (resetLevel & 1) {
		mem_8000_bfff = rom[0];
		mem_c000_ffff = rom[0] + 0x4000;
	}
	// clear RAM with powerup pattern and reload ROM's
	if (resetLevel & 2) {
		for (int i = 0; i < RAMSIZE; i++)
			Ram[i] = (i >> 1) << 1 == i ? 0 : 0xFF;
		loadroms();
	}
	// empty collision buffers
	memset(spriteCollisions, 0, sizeof(spriteCollisions));
	memset(spriteBckgColl, 0, sizeof(spriteBckgColl));
	spriteBckgCollReg = spriteCollisionReg = 0;
	//
	vicBusAccessCycleStart = spriteDMAmask = 0;
	for (int i = 0; i < 8; i++) {
		mob[i].dataCount = 0;
		mob[i].dataCountReload = 0;
		mob[i].reloadFlipFlop = 0;
		mob[i].x = 0;
		mob[i].y = 0;
		mob[i].dmaState = false;
		mob[i].rendering = false;
		mob[i].enabled = 0;
	}
	//
	soundReset();
	cia[0].reset();
	cia[1].reset();
	vicReg[0x19] = 0;
	prp = 7;
	prddr = 0;
}

void Vic2mem::dumpState()
{
	// always called during end of screen (X=100; Y=0)
	saveVar(Ram, RAMSIZE);
	saveVar(&prp, sizeof(prp));
	saveVar(&prddr, sizeof(prddr));
	saveVar(serialPort, sizeof(serialPort[0]));
	saveVar(colorRAM, 0x0400);
	saveVar(&beamx, sizeof(beamx));
	saveVar(&beamy, sizeof(beamy));
	saveVar(&irqline, sizeof(irqline));
	saveVar(&crsrpos, sizeof(crsrpos));
	saveVar(&scrattr, sizeof(scrattr));
	saveVar(&nrwscr, sizeof(nrwscr));
	saveVar(&hshift, sizeof(hshift));
	saveVar(&vshift, sizeof(vshift));
	saveVar(&fltscr, sizeof(fltscr));
	saveVar(&mcol, sizeof(mcol));
	saveVar(chrbuf, 40);
	saveVar(&charrom, sizeof(charrom));
	saveVar(&charbank, sizeof(charbank));
	saveVar(&framecol, sizeof(framecol));
	//
	saveVar(&vicReg, sizeof(vicReg) / sizeof(vicReg[0]));
	saveVar(&cia[0].reg, sizeof(cia[0].reg) / sizeof(cia[0].reg[0]));
	saveVar(&cia[1].reg, sizeof(cia[1].reg) / sizeof(cia[1].reg[0]));
}

void Vic2mem::readState()
{
	readVar(Ram, RAMSIZE);
	readVar(&prp, sizeof(prp));
	readVar(&prddr, sizeof(prddr));
	readVar(serialPort, sizeof(serialPort[0]));
	readVar(colorRAM, 0x0400);
	readVar(&beamx, sizeof(beamx));
	readVar(&beamy, sizeof(beamy));
	readVar(&irqline, sizeof(irqline));
	readVar(&crsrpos, sizeof(crsrpos));
	readVar(&scrattr, sizeof(scrattr));
	readVar(&nrwscr, sizeof(nrwscr));
	readVar(&hshift, sizeof(hshift));
	readVar(&vshift, sizeof(vshift));
	readVar(&fltscr, sizeof(fltscr));
	readVar(&mcol, sizeof(mcol));
	readVar(chrbuf, 40);
	readVar(&charrom, sizeof(charrom));
	readVar(&charbank, sizeof(charbank));
	readVar(&framecol, sizeof(framecol));
	//
	readVar(&vicReg, sizeof(vicReg) / sizeof(vicReg[0]));
	readVar(&cia[0].reg, sizeof(cia[0].reg) / sizeof(cia[0].reg[0]));
	readVar(&cia[1].reg, sizeof(cia[1].reg) / sizeof(cia[1].reg[0]));
	//
	for (unsigned int i = 0; i < 16; i++) {
		cia[0].write(i, cia[0].reg[i]);
		cia[1].write(i, cia[1].reg[i]);
	}
	for (unsigned int i = 0; i < 0x30; i++) {
		Write(0xD000 + i, vicReg[i]);
	}
	Write(0, prddr);
	Write(1, prp);
}

void Vic2mem::loadromfromfile(int nr, const char fname[512], unsigned int offset)
{
	FILE *img;

	if ((img = fopen(fname, "rb"))) {
		crtType = 0;
		// note: this is only the minimum!
		const unsigned int crthdrsize = 64;
		unsigned char crtheader[crthdrsize];

		size_t r = fread(crtheader, 1, crthdrsize, img);
		if (!strncmp((char*)crtheader, "C64 CARTRIDGE", 13)) {
			const unsigned int chiphdrsize = 16;
			unsigned char chipheader[chiphdrsize];
			unsigned int size, loadaddress;
			const unsigned int crtversionMain = crtheader[0x14];
			const unsigned int crtversionSub = crtheader[0x15];
			crtType = crtheader[0x17] | (crtheader[0x16] << 8);

			fprintf(stderr, "CRT image version: %u.%u, type: %u\n", crtversionMain, crtversionSub, crtType);

			r = fread(chipheader, 1, chiphdrsize, img);
			if (!strncmp((char*)chipheader, "CHIP", 4)) {
				loadaddress = chipheader[0x0D] | (chipheader[0x0C] << 8);
				size = chipheader[0x0F] | (chipheader[0x0E] << 8);
				exrom = crtheader[0x18];
				gamepin = crtheader[0x19];
				if (size <= 0x2000)
					offset = loadaddress & 0x3000;
				// load ROM/CRT file
				r = fread(rom[nr] + offset, size, 1, img);
				fprintf(stderr, "  CHIP data loaded: %04X-%04X EXROM:%u GAME:%u\n", loadaddress, loadaddress + size - 1, exrom, gamepin);
				fclose(img);
				//
				changeMemoryBank(prp | ~prddr, exrom, gamepin);
				Reset(0);
				cpuptr->Reset();
				return;
			}
		}
	}
	memset(rom[nr] + offset, 0, ROMSIZE);
	bool restart = !(exrom & gamepin);
	exrom = gamepin = 1;
	changeMemoryBank(prp | ~prddr, exrom, gamepin);
	if (restart) {
		Reset(1);
		cpuptr->Reset();
	}
}

void Vic2mem::loadroms()
{
	memcpy(rom[0], basicRomC64, basicRomC64_size);
	memcpy(rom[0] + 0x4000, kernalRomC64, kernalRomC64_size);
	mem_8000_bfff = rom[0];
	mem_8000_9fff = Ram + 0x8000;
	mem_c000_ffff = rom[0] + 0x4000;
#if FAST_BOOT
	// TODO: check ROM pattern
	unsigned char patch[] = { 0xA0, 0xA0, 0xA2, 0x00, 0x84, 0xC1, 0x86, 0xC2 };
	memset(mem_c000_ffff + 0x1D68, 0xEA, 0x24);
	memcpy(mem_c000_ffff + 0x1D68, patch, sizeof(patch));
#endif
}

void Vic2mem::setCpuPtr(CPU *cpu)
{
	cpuptr = cpu;
	cia[1].setIrqCallback(setCiaNmi, cpuptr);
}

void Vic2mem::copyToKbBuffer(const char *text, unsigned int length)
{
	if (!length)
		length = (unsigned int) strlen(text);
	Write(0xc6, length);
	while (length--)
		Write(0x0277 + length, text[length]);
}

Color Vic2mem::getColor(unsigned int ix)
{
	const double bsat = 43.0;
	const Color color[16] = {
		{ 0, 0, 0 }, { 0, 5.0, 0 }, 
#ifndef MOS6569R1
#if 0 // speculative
		{ 112.5, 2.9375, bsat }, { 292.5, 3.875, bsat },
		{ 45, 3.125, bsat }, { 225, 3.5, bsat }, { 0, 2.75, bsat }, { 180, 4.25, bsat},
		{ 135, 3.125, bsat }, { 157.5, 2.75, bsat }, { 112.5, 3.5, bsat }, { 0, 2.9375, 0 },
		{ 0, 3.41, 0 }, { 225, 4.25, bsat }, { 0, 3.41, bsat }, { 0, 3.875, 0 }
#else // measured
#if 0 // my TV card  ~TED hues
		{ 96, 2.9375, bsat },{ 283, 3.875, bsat },
		{ 56, 3.125, bsat },{ 245, 3.5, bsat },{ 350, 2.75, bsat },{ 167, 4.25, bsat },
		{ 126, 3.125, bsat },{ 147, 2.75, bsat },{ 96, 3.5, bsat },{ 0, 2.9375, 0 },
		{ 0, 3.41, 0 },{ 245, 4.25, bsat },{ 350, 3.41, bsat },{ 0, 3.875, 0 }
#else // screenshot
	//{ 96, 2.9375, bsat },{ 283, 3.875, bsat },
	//{ 55, 3.125, bsat },{ 241, 3.5, bsat },{ 347, 2.75, bsat },{ 167, 4.25, bsat },
	//{ 129, 3.125, bsat },{ 148, 2.75, bsat },{ 96, 3.5, bsat },{ 0, 2.9375, 0 },
	//{ 0, 3.41, 0 },{ 241, 4.25, bsat },{ 347, 3.41, bsat },{ 0, 3.875, 0 }
	// measured from a C64C direct composite picture shot
	{ 100, 2.9, bsat },{ 280, 4.1, bsat },
	{ 47, 3.2, bsat },{ 227, 3.68, bsat },{ 343, 2.75, bsat },{ 163, 4.70, bsat },
	{ 123, 3.2, bsat },{ 143, 2.75, bsat },{ 103, 3.68, bsat },{ 0, 2.9, 0 },
	{ 0, 3.5, 0 },{ 233, 4.70, bsat },{ 343, 3.5, bsat },{ 0, 4.1, 0 }
#endif
#endif
#else // 6569R1
		{ 96, 3.0, bsat },{ 282, 4.5, bsat },
		{ 55, 3.5, bsat },{ 245, 3.5, bsat },{ 350, 3.0, bsat },{ 167, 4.5, bsat },
		{ 126, 3.5, bsat },{ 140, 3.0, bsat },{ 96, 3.5, bsat },{ 0, 3.0, 0 },
		{ 0, 3.5, 0 },{ 245, 4.5, bsat },{ 350, 3.5, bsat },{ 0, 4.5, 0 }
#endif
	};
	return color[ix & 0xF];
}

void Vic2mem::soundReset()
{
	if (sidCard)
		sidCard->reset();
}

unsigned int Vic2mem::getVicBaseAddress()
{
	const unsigned int vicBank = (((cia[1].pra | ~cia[1].ddra) ^ 0xFF) & 3) << 14;
	return vicBank;
}

void Vic2mem::changeCharsetBank()
{
	const unsigned int vicBaseAddr = getVicBaseAddress();

	vicBase = Ram + vicBaseAddr;
	// video matrix base address
	const unsigned int vmOffset = ((vicReg[0x18] & 0xF0) << 6);
	VideoBase = vicBase + vmOffset;
	// character bitmap data
	const unsigned int cSetOffset = ((vicReg[0x18] & 0x0E) << 10);

	cset = (!(vicBaseAddr & 0x4000) && ((cSetOffset & 0x3000) == 0x1000)) // 4 or 6
		? charrombank + (cSetOffset & 0x0800) : vicBase + cSetOffset;
	grbank = vicBase + ((vicReg[0x18] & 8) << 10);
#if 0
	fprintf(stderr, "VIC bank: %04X, grbank:%04X matrix:%04X cset:%04X(%u) in line:%03i pra:%02X ddra:%02X vic18:%02X\n",
		vicBaseAddr, (vicReg[0x18] & 8) << 10, vmOffset, cSetOffset, cset != (vicBase + cSetOffset), 
		beamy, cia[1].pra, cia[1].ddra, vicReg[0x18]);
#endif
}

void Vic2mem::changeMemoryBank(unsigned int port, unsigned int ex, unsigned int game)
{
	bool ultimax = exrom && !game;
	// TODO: make it a table
	mem_8000_bfff = ((port & 3) == 3) ? rom[0] : Ram + 0xa000; // a000..bfff
	mem_c000_ffff = ((port & 2) == 2) ? rom[0] + 0x4000 : Ram + 0xe000; // e000..ffff
	charrom = (!(port & 4) && (port & 3)) && !ultimax;
	mem_8000_9fff = Ram + 0x8000;
	mem_1000_3fff = Ram;
	// Ultimax mode?
	if (ultimax) {
		mem_8000_9fff = rom[1];
		mem_c000_ffff = rom[1] + 0x2000;
		mem_1000_3fff = rom[1];
		// HACK! Cart ROM is mirrored at lowest VIC bank in Ultimax mode
		memcpy(Ram + 0x1000, rom[1] + 0x1000, 0x3000);
		changeCharsetBank();
	} else if (!exrom) {
		if ((port & 3) == 3) {
			mem_8000_9fff = rom[1];
		}
		if (!gamepin && (port & 2)) {
			mem_8000_bfff = rom[1] + 0x2000;
		}
	}
#if 0
	static int oldval = -1;
	int newval = ex + game + port;
	if (oldval != newval) {
		fprintf(stderr, "Mem banking. prp:%02X ddrp:%02X ex:%02X game:%02X port:%02X in line:%03i.\n", prp, prddr, ex, game, port, beamy);
		oldval = newval;
	}
#endif
}

void Vic2mem::setCiaIrq(void *param)
{
	Vic2mem *mh = reinterpret_cast<Vic2mem*>(param);
	mh->irqFlag |= 0x40;
	//fprintf(stderr, "CIA1 irq @ PC=%04X @ cycle=%i\n", mh->cpuptr->getPC(), CycleCounter);
}

void Vic2mem::setCiaNmi(void *param)
{
	CPU *cpu = reinterpret_cast<CPU*>(param);
	cpu->triggerNmi();
}

inline void Vic2mem::checkIRQflag()
{
	irqFlag |= (vicReg[0x19] & 0x80);
}

void Vic2mem::doDelayedDMA()
{
	if (attribFetch) {
		bool nowBadLine = (vshift == (beamy & 7)) && (beamy != 247);
		if (nowBadLine) {
			if (!BadLine && (beamx <= 86 || beamx >= 124)) {
				unsigned int delay;

				if (!vicBusAccessCycleStart) {
					vicBusAccessCycleStart = CycleCounter;
					// FIXME one cycle delay
					delay = ((beamx) >> 1);
				} else {
					delay = 0;
				}
				if (delay <= 40) {
					// FIXME it is different whether in idle state or not
					const unsigned int illegalCnt = MIN(40 - delay, MIN(3, delay));
					const unsigned int skippedCnt = delay - illegalCnt;
					const unsigned char illegalClr = Read(cpuptr->getPC()) & 0x0F;
					
					unsigned int i = 0;
					while (i < illegalCnt) {
						chrbuf[i] = 0xFF;
						clrbuf[i] = illegalClr;
						i++;
					}
					delayedDMA = true;
					dmaCount = 40 - delay;
					if (CharacterPosition + dmaCount - illegalCnt > 0x0400) {
						memcpy(chrbuf + illegalCnt, VideoBase + CharacterPosition, 0x400 - CharacterPosition - illegalCnt);
						memcpy(chrbuf + illegalCnt + 0x400 - CharacterPosition, VideoBase, (CharacterPosition + dmaCount - illegalCnt) & 0x03FF);
						memcpy(clrbuf + illegalCnt, colorRAM + CharacterPosition, 0x400 - CharacterPosition - illegalCnt);
						memcpy(clrbuf + illegalCnt + 0x400 - CharacterPosition, colorRAM, (CharacterPosition + dmaCount - illegalCnt) & 0x03FF);
					} else {
						memcpy(chrbuf + illegalCnt, VideoBase + CharacterPosition, dmaCount - illegalCnt);
						memcpy(clrbuf + illegalCnt, colorRAM + CharacterPosition, dmaCount - illegalCnt);
					}
				} else {
					if (!VertSubActive)
						dmaCount = 0;
				}
			}
			/*fprintf(stderr, "Bad line (DMAdelay:%i) @ XSCR=%i X=%i Y=%i(%02X) VSC=%u CP=%04u DMAC=%i @ PC=%04X\n", delayedDMA,
				hshift, beamx, beamy, beamy, vertSubCount, CharacterPosition, dmaCount, cpuptr->getPC());*/
			VertSubActive = true;
			BadLine = 1;
		} else {
			//if (BadLine) 
			// fprintf(stderr, "Bad line stopped @ XSCR=%i X=%i Y=%i(%02X) VSC=%02X DMAC=%i @ PC=%04X\n", 
			//		hshift, beamx, beamy, beamy, vertSubCount, dmaCount, cpuptr->getPC());
			BadLine = 0;
			if (!spriteDMAmask)
				vicBusAccessCycleStart = 0;
		}
	}
}

void Vic2mem::UpdateSerialState(unsigned char newPort)
{
	static unsigned char prevPort = 0x01;
	if (prevPort ^ newPort) {
		serialPort[0] = ((newPort << 2) & 0x80)	// DATA OUT -> DATA IN
			| ((newPort << 2) & 0x40)				// CLK OUT -> CLK IN
			| ((newPort << 1) & 0x10);			// ATN OUT -> ATN IN (drive)
		updateSerialDevices(serialPort[0]);
		prevPort = newPort;
#if LOG_SERIAL
		fprintf(stderr, "$DD00 write : %02X @ PC=%04X in cycle:%llu\n", newPort, cpuptr->getPC(), CycleCounter);
		fprintf(stderr, "  serial port written: %02X.\n", serialPort[0]);
#endif
	}
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
					return (prp & prddr) | ( (((tap->IsButtonPressed() << 4) ^ 0x10) | portState | 0x07) & ~prddr);
				default:
					return actram[addr & 0xFFFF];
			}
		default:
			return actram[addr & 0xFFFF];
		case 0x8000:
		case 0x9000:
			return mem_8000_9fff[addr & 0x1FFF];
		case 0xA000:
		case 0xB000:
			return mem_8000_bfff[addr & 0x1FFF];
		case 0xE000:
		case 0xF000:
			return mem_c000_ffff[addr & 0x1FFF];
		case 0xD000:
			if (!((prp | ~prddr) & 3) && !(exrom & ~gamepin))
				return actram[addr & 0xFFFF];
			else if (charrom && !(exrom & ~gamepin)) {
				return charRomC64[addr & 0x0FFF];
			} else {
				switch ( addr >> 8 ) {
					case 0xD0: // VIC2
					case 0xD1:
					case 0xD2:
					case 0xD3:
						addr &= 0x3F;
						switch (addr) {
							case 0x11:
								return (vicReg[0x11] & 0x7f) | ((beamy & 0x100) >> 1);
							case 0x12:
								return beamy & 0xFF;
							case 0x13: // LPX
								return lpLatchX;
							case 0x14: // LPY
								return lpLatchY;
							case 0x16:
								return vicReg[0x16] | 0xC0;
							case 0x18:
								return vicReg[0x18] | 1;
							case 0x19:
								return vicReg[0x19] | 0x70;
							case 0x1A:
								return vicReg[0x1A] | 0xF0;
							case 0x1E:
								{	// sprite-sprite collision
									unsigned char rv = spriteCollisionReg;
									spriteCollisionReg = 0;
									return rv;
								}
							case 0x1F:
								{	// sprite-background collision
									unsigned char rv = spriteBckgCollReg;
									spriteBckgCollReg = 0;
									return rv;
								}
							case 0x20:
								return framecol | 0xF0;
							case 0x21:
							case 0x22:
							case 0x23:
							case 0x24:
								return ecol[(addr & 0x3F) - 0x21] | 0xF0;
							case 0x25:
							case 0x26:
								return mobExtCol[((addr - 0x25) << 1) + 1] | 0xF0;
							case 0x27:
							case 0x28:
							case 0x29:
							case 0x2A:
							case 0x2B:
							case 0x2C:
							case 0x2D:
							case 0x2E:
								return vicReg[addr] | 0xF0;
							case 0x2F: // unconnected
								return 0xFF;
						}
						return vicReg[addr];
					case 0xD4: // SID
					case 0xD5:
					case 0xD6:
					case 0xD7:
						if (sidCard) {
							//flushBuffer(CycleCounter, VIC_SOUND_CLOCK);
							sidCard->catchUpOnState(CycleCounter);
							return sidCard->read(addr & 0x1F);
						}
						return 0xD4;
					case 0xD8: // Color RAM
					case 0xD9:
					case 0xDA:
					case 0xDB:
						return colorRAM[addr & 0x03FF] | (readFloatingBus(addr) & 0xF0);
					case 0xDC: // CIA1
						{
							unsigned char retval;
							switch (addr & 0x0F) {
								case 0x00:
									retval = cia[0].read(0)
										& keys64->getJoyState(1)
										& keys64->feedKeyColumn((cia[0].prb | ~cia[0].ddrb) & keys64->getJoyState(0));
									return retval;
								case 0x01: // port B usually not driven low by port A.
#if 1
									{
										static unsigned char oldRetval = 0xFF;
										retval = ((keys64->feedkey((cia[0].pra | ~cia[0].ddra) 
											& keys64->getJoyState(1)) ) //  | (cia[0].read(1) & 0xC0)
											& keys64->readPaddleFireButton(0)
											& ~cia[0].ddrb)
											| (cia[0].read(1) & cia[0].ddrb);
										if ((oldRetval & 0x10) && !(retval & 0x10))
											latchCounters();
										oldRetval = retval;
									}
#else
									retval = cia[0].read(1)
										& (keys64->feedkey(cia[0].read(0)) & keys64->getJoyState(1) | cia[0].ddrb);
#endif
									//fprintf(stderr, "$Kb(%02X,%02X) read: %02X\n", cia[0].pra, cia[0].ddra, retval);
									return retval;
								case 0x0D:
									retval = cia[0].read(0x0D);
									irqFlag &= ~0x40;
									break;
								default:
									retval = cia[0].read(addr);
							}
							/*fprintf(stderr, "CIA1(%02X) read:%02X @ PC=%04X @ cycle=%i\n", addr & 0x1f, retval,
								cpuptr->getPC(), CycleCounter);*/
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
									/*fprintf(stderr, "CIA2(%02X) read:%02X @ PC=%04X @ cycle=%lli\n", addr & 0x1f, retval,
										cpuptr->getPC(), CycleCounter);*/
									return retval;
								}
							default:
								;
						}
						/*fprintf(stderr, "CIA2(%02X) read:%02X @ PC=%04X @ cycle=%lli\n", addr & 0x1f, cia[1].read(addr & 0xf),
								cpuptr->getPC(), CycleCounter);*/
						return cia[1].read(addr);
					case 0xDE:
						if (crtType == 66) {
							exrom = gamepin = 0;
						}
						return readFloatingBus(addr);
					case 0xDF:
						if (crtType == 66) {
							exrom = gamepin = 1;
						}
						if (reu) {
							return reu->Read(addr);
						}
					default: // open address space
						return readFloatingBus(addr);
				}
			}
	}
}

void Vic2mem::Write(unsigned int addr, unsigned char value)
{
	switch (addr & 0xF000) {
		case 0x0000:
			{
				unsigned char port;
				switch ( addr & 0xFFFF ) {
					case 0:
						prddr = value;
						goto skip;

					case 1:
						if ((prp ^ value) & 0x20 & prddr)
							tap->setTapeMotor(CycleCounter, !(value & 0x20));
						prp = value;
skip:
						portState = (portState & ~prddr) | (prp & 0xC8 & prddr);
						port = prp | ~prddr;
						changeMemoryBank(port, exrom, gamepin);
						return;
				default:
						actram[addr & 0xFFFF] = value;
				}
			}
			return;
		case 0xF000:
			if (reu && addr == 0xFF00) {
				reu->startDMA();
			}
		default:
			actram[addr & 0xFFFF] = value;
			return;
		case 0xD000:
			if (!((prp | ~prddr) & 3) && !(exrom & ~gamepin)) { // should be read(1)
				actram[addr & 0xFFFF] = value;
			} else if (!charrom || (exrom & ~gamepin)) {
				//unsigned int i;
				switch ( addr >> 8 ) {
					case 0xD0: // VIC2
					case 0xD1:
					case 0xD2:
					case 0xD3:
						addr &= 0x3F;
						switch (addr) {
							case 0x12:
								if ((irqline ^ value) & 0xFF )
								{
									irqline = (irqline & 0x100) | value;
									if (beamy == irqline) {
										vicReg[0x19] |= ((vicReg[0x1A] & 1) << 7) | 1;
										checkIRQflag();
									}
								}
								break;
							case 0x11:
								// raster IRQ line
								if (((irqline >> 1) ^ value) & 0x80 )
								{
									irqline = (irqline & 0xFF) | ((value & 0x80) << 1);
									if (beamy == irqline) {
										vicReg[0x19] |= ((vicReg[0x1A] & 1) << 7) | 1;
										checkIRQflag();
									}
								}
								// get vertical offset of screen when smooth scroll
								vshift = value & 0x07;
								// check for flat screen (23 rows)
								fltscr = !(value&0x08);
								// check for extended mode
								// check for graphics mode (5th b14it)
								scrattr = (scrattr & ~(GRAPHMODE|EXTCOLOR))|(value & (GRAPHMODE|EXTCOLOR));
								// Check if screen is turned on
								if (value & 0x10 && beamy == 48 && !attribFetch) {
									attribFetch = true;
								} else if (attribFetch && ((fltscr && beamy == 48+7) || (!fltscr && beamy == 48+3))) {
									ScreenOn = true;
								} else if ((beamy == 48+199 && fltscr) || (beamy == 48+203 && !fltscr)) {
									ScreenOn = false;
								}
								doDelayedDMA();
								break;
							case 0x16:
								// check for narrow screen (38 columns)
								nrwscr = value & 0x08;
								// get horizontal offset of screen when smooth scroll
								if (CharacterWindow)
									doXscrollChange(hshift, value & 0x07);
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
								if ((vicReg[0x1a]) & 0x0F & vicReg[0x19]) {
									vicReg[0x19] |= 0x80;
									irqFlag |= 0x80;
								} else {
									vicReg[0x19] &= 0x7F;
									irqFlag &= ~0x80;
								}
								//fprintf(stderr, "IRQ ack. write:%02X value:%02X @ PC=%04X @ cycle=%i\n",
								//	value, vicReg[0x19], cpuptr->getPC(), CycleCounter);
								return;
							case 0x1a:
								// check if we have a pending IRQ
								if ((vicReg[0x19]) & 0x0F & value) {
									vicReg[0x19] |= 0x80;
									irqFlag |= 0x80;
								} else {
									vicReg[0x19] &= 0x7F;
									irqFlag &= ~0x80;
								}
								break;
							case 0x20:
								// distinguish border in the rendered screen with 0x80
								value = (value & 0x0F) | 0x80;
								framecol = (value << 24) | (value << 16) | (value << 8) | value;
								break;
							case 0x21:
								ecol[0] = bmmcol[0] = mcol[0] = (value & 0x0F) | 0x40;
								break;
							case 0x22: // '01' counts as background as well
								ecol[1] = mcol[1] = (value & 0x0F) | 0x40;
								break;
							case 0x23:
								ecol[2] = mcol[2] = value & 0x0F;
								break;
							case 0x24:
								ecol[3] = value & 0x0F;
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
								mob[addr >> 1].x = (mob[addr >> 1].x & 0x0100) | value;
								//fprintf(stderr, "Sprite%i:%i m_x: %u\n", addr >> 1, value, mob[addr >> 1].x);
								break;
							case 0x01:
							case 0x03:
							case 0x05:
							case 0x07:
							case 0x09:
							case 0x0B:
							case 0x0D:
							case 0x0F:
								mob[addr >> 1].y = (value);
								//fprintf(stderr, "Sprite%i:%i : %u\n", addr >> 1, value, mob[addr >> 1].y);
								break;
							case 0x10:
								{
									unsigned int i = 7;
									do {
										mob[i].x = ((mob[i].x & 0xFF) | ((value << (8 - i)) & 0x100));
										//fprintf(stderr, "Sprite%i:%i m8x: %u\n", i, value, mob[i].x);
									} while (i--);
								}
								break;
							case 0x15:
								SET_BITS(mob[i].enabled, value);
								break;
							case 0x17:
								// sprite crunch?
								if (beamx == 2) {
									unsigned int i = 7; 
									do {
										unsigned int bit = (1 << i);
										unsigned int newBitOn = value & bit;
										if ((vicReg[0x17] & bit) && !newBitOn) {
											unsigned int &dcReload = mob[i].dataCountReload;
											unsigned int dc = (dcReload + 3) & 0x3F;
											dcReload = (0x2A & dcReload & dc) | (0x15 & (dcReload | dc));
										}
										mob[i].expandY = mob[i].reloadFlipFlop = !!newBitOn;
									} while(i--);
								} else 
								{
									SET_BITS(mob[i].expandY, value);
									SET_BITS(mob[i].reloadFlipFlop, value);
								}
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
								mobExtCol[((addr - 0x25) << 1) + 1] = value & 0x0F;
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
						if (sidCard) {
							//sidCard->catchUpOnState(CycleCounter);
							sidCard->updateLastCycleCount(CycleCounter);
							flushBuffer(CycleCounter, VIC_SOUND_CLOCK);
							sidCard->write(addr & 0x1f, value);
						}
						return;
					case 0xD8: // Color RAM
					case 0xD9:
					case 0xDA:
					case 0xDB:
						colorRAM[addr & 0x03FF] = value & 0x0F;
						return;
					case 0xDC: // CIA1
						switch (addr & 0x0F) {
							// key matrix row select & LP irq
							case 1:
							case 3:
								{
									unsigned char oldPortOut = cia[0].prb | ~cia[0].ddrb;
									cia[0].write(addr, value);
									unsigned char newPortOut = cia[0].prb | ~cia[0].ddrb;
									if ((oldPortOut & 0x10) && !(newPortOut & 0x10)) {
										latchCounters();
									}
								}
								return;
							case 0:
								cia[0].write(addr, value);
								return;
							default:;
						}
						//fprintf(stderr, "CIA1(%02X) write: %02X @ PC=%04X\n", addr & 0x0f, value, cpuptr->getPC());
						cia[0].write(addr, value);
						return;
					case 0xDD: // CIA2
						switch (addr & 0x0F) {
							case 2:
								cia[1].write(2, value);
								UpdateSerialState(~cia[1].pra & cia[1].ddra);
								changeCharsetBank();
								return;
							case 0:
								cia[1].write(0, value & 0x3F);
								// VIC base
								changeCharsetBank();
								// serial IEC
								UpdateSerialState(~cia[1].pra & cia[1].ddra);
								return;
							default:
								break;
						}
						//fprintf(stderr, "CIA2(%02X) write: %02X @ PC=%04X\n", addr & 0x0f, value, cpuptr->getPC());
						cia[1].write(addr, value);
						return;
					case 0xDF:
						if (reu) {
							reu->Write(addr, value);
						}
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

void Vic2mem::latchCounters()
{
	// once per frame only
	if (!lpLatched) {
		lpLatched = true;
		lpLatchX = beamx << 1;
		lpLatchY = beamy;
		vicReg[0x19] |= ((vicReg[0x1A] & 8) << 4) | 8;
		checkIRQflag();
	}
}

void Vic2mem::doHRetrace()
{
	static unsigned char *sPtr = scrptr;
	//if (vicReg[0x15]) 
		drawSpritesPerLine(sPtr);
	// the beam reached a new line
	sPtr = scrptr;
}

inline void Vic2mem::newLine()
{
	prevY = beamy;
	beamy += 1;
	switch (beamy) {

		case 48:
			attribFetch = (vicReg[0x11] & 0x10) != 0;
			break;

		case 48 + 3:
			if (!fltscr && attribFetch) ScreenOn = true;
			break;

		case 48 + 7:
			if (fltscr && attribFetch) ScreenOn = true;
			break;

		case 199 + 48:
			if (fltscr) ScreenOn = false;
			break;

		case 203 + 48:
			if (!fltscr) ScreenOn = false;
			break;

		case 204 + 48:
			//VertSubActive = false;
			break;

		case 250 + 48: // VIC article: 300
			VBlanking = true;
			break;

		case 261:
			CharacterPositionReload = 0; // preliminary docs
			break;

		case 0:
		case 512:
		case 312: // Vertical retrace
			if (!attribFetch) {
				endOfScreen = true;
			}
			beamy = 0;
			//CharacterPositionReload = 0;
			doVRetrace();
			// CIA ToD count @ 50/60 Hz
			cia[0].todUpdate();
			cia[1].todUpdate();
			// skip checking raster IRQ
			return;

		case 6:
			VBlanking = false;
			break;
	}
	// is there raster interrupt?
	if (beamy == irqline) {
		vicReg[0x19] |= ((vicReg[0x1A] & 1) << 7) | 1;
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
				// the beam reached a new line
				doHRetrace();
				newLine();
				sidCard->updateLastCycleCount(CycleCounter);
				flushBuffer(CycleCounter, VIC_SOUND_CLOCK);
				if (attribFetch) {
					BadLine = (vshift == (beamy & 7));
					if (BadLine) {
						VertSubActive = true;
					}
				}
				MOB_READ_ADDRESS(3);
				DO_SPRITE_DMA(3);
				STOP_SPRITE_DMA(2);
				break;

			case 102:
				if (endOfScreen) {
					// is there raster interrupt? line 0 IRQ is delayed by 0 cycle
					if (0 == irqline) {
						vicReg[0x19] |= ((vicReg[0x1A] & 1) << 7) | 1;
						checkIRQflag();
					}
					lpLatched = false;
					endOfScreen = false;
					dmaCount = 0;
				}
				checkSpriteDMA(5);
				break;

			case 104:
				MOB_READ_ADDRESS(4);
				DO_SPRITE_DMA(4);
				STOP_SPRITE_DMA(3);
				break;

			case 106:
				checkSpriteDMA(6);
				break;

			case 108:
				MOB_READ_ADDRESS(5);
				DO_SPRITE_DMA(5);
				STOP_SPRITE_DMA(4);
				break;

			case 110:
				checkSpriteDMA(7);
				break;

			case 112:
				MOB_READ_ADDRESS(6);
				DO_SPRITE_DMA(6);
				STOP_SPRITE_DMA(5);
				break;

			case 116:
				MOB_READ_ADDRESS(7);
				DO_SPRITE_DMA(7);
				STOP_SPRITE_DMA(6);
				break;

			case 120:
				// Stop sprite DMA
				vicBusAccessCycleStart = 0;
				spriteDMAmask = 0;
				for (unsigned int i = 0; i < 8; i++) {
					mob[i].sdb[1].dwSrDmaBuf = mob[i].sdb[0].dwSrDmaBuf;
				}
				break;

			case 122:
				HBlanking = false;
				if (beamy == 247) {
					attribFetch = false;
				}
				if (BadLine && !vicBusAccessCycleStart)
					vicBusAccessCycleStart = CycleCounter;
				//fprintf(stderr, "Line %03i - AttribFetch:%i Badline:%i VSA:%i VSC:%i Screen:%i Y=%03X YSCR=%X\n", beamy, attribFetch,
				//	BadLine, VertSubActive, vertSubCount, ScreenOn, beamy, vshift);
				break;

			case 126:
				beamx = 0;
			case 0:
				if (BadLine && !delayedDMA) {
					vertSubCount = 0;
				}
				CharacterPosition = CharacterPositionReload;
				break;

			case 2:
				if (BadLine && !delayedDMA) {
					if (CharacterPosition >= 0x03d9) {
						const unsigned int lbCnt = 0x400 - CharacterPosition;
						const unsigned int nbCnt = (CharacterPosition + 40) & 0x03FF;
						memcpy(chrbuf, VideoBase + CharacterPosition, lbCnt);
						memcpy(chrbuf + lbCnt, VideoBase, nbCnt);
						memcpy(clrbuf, colorRAM + CharacterPosition, lbCnt);
						memcpy(clrbuf + lbCnt, colorRAM, nbCnt);
					} else {
						memcpy(chrbuf, VideoBase + CharacterPosition, 40);
						memcpy(clrbuf, colorRAM + CharacterPosition, 40);
					}
				}
				break;

			case 4:
				break;

			case 6:
				stopSpriteDMA(); // FIXME cycle 0
				if (ScreenOn) {
					SideBorderFlipFlop = true;
					if (nrwscr) {
						if (hshift) {
							int oldX = x;
							x = 39;
							doXscrollChange(0, hshift);
							x = oldX;
						}
						CharacterWindow = true;
					}
					x = 0;
				}
				break;

			case 8:
				if (ScreenOn && !nrwscr) {
					CharacterWindow = true;
				}
				break;

			case 82:
				if (VertSubActive && !delayedDMA && !dmaCount)
					dmaCount = 40;
				checkSpriteEnable();
				// On bad line with sprite 0 on, all CPU cycles are stolen
				if (!checkSpriteDMA(0))
					vicBusAccessCycleStart = 0;
				break;

			case 84:
				if (!nrwscr)
					SideBorderFlipFlop = CharacterWindow = false;
				spriteReloadCounters();
				break;

			case 86:
				if (nrwscr)
					SideBorderFlipFlop = CharacterWindow = false;
				checkSpriteDMA(1);
				break;

			case 88:
				if (vertSubCount == 7) {// FIXME
					CharacterPositionReload = (CharacterPosition + dmaCount) & 0x3FF;
					dmaCount = 0;
					VertSubActive = false;
				}
				if (BadLine) {
					BadLine = 0;
					delayedDMA = false;
					VertSubActive = true;
				}
				if (VertSubActive)
					vertSubCount = (vertSubCount + 1) & 7;
				//
				MOB_READ_ADDRESS(0);
				DO_SPRITE_DMA(0);
				break;

			case 90:
				checkSpriteDMA(2);
				break;

			case 92:
				MOB_READ_ADDRESS(1);
				DO_SPRITE_DMA(1);
				STOP_SPRITE_DMA(0);
				break;

			case 94:
				checkSpriteDMA(3);
				break;

			case 96:
				MOB_READ_ADDRESS(2);
				DO_SPRITE_DMA(2);
				STOP_SPRITE_DMA(1);
				break;

			case 98:
				checkSpriteDMA(4);
				HBlanking = true;
				break;
		}

		// CPU clocking
		if (!vicBusAccessCycleStart)
			cpuptr->process();
		else if (CycleCounter - vicBusAccessCycleStart < 3)
			cpuptr->stopcycle();

		// drawing the visible part of the screen
		if (!(HBlanking || VBlanking)) {
			if (SideBorderFlipFlop) {
				// call the relevant rendering function
				render();
				x += 1;
			}
			if (!CharacterWindow) {
				// we are on the border area, so use the frame color
				*((int*)scrptr) = framecol;
				*((int*)(scrptr + 4)) = framecol;
			}
		}
		scrptr += 8;
		//
		cia[0].countTimers();
		cia[1].countTimers();
		//
		CycleCounter += 1;
		//
		if (tap->isMotorOn()) {
			if (tap->getFallingEdgeState(CycleCounter)) {
				cia[0].icr |= 0x10;
				cia[0].setIRQflag(cia[0].icr & cia[0].irq_mask);
				tap->resetFallingEdge(CycleCounter);
			}
		}
		//
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
}

inline void Vic2mem::doXscrollChange(unsigned int oldXscr, unsigned int newXscr)
{
	if (newXscr > oldXscr) {
		unsigned char a;
		switch (scrattr) {
		default:
			a = mcol[0];
			break;
		case GRAPHMODE:
			a = (chrbuf[x] & 0x0F) | 0x40;
			break;
		}
		memset(scrptr + oldXscr, a, newXscr - oldXscr);
	}
}

inline unsigned char Vic2mem::readFloatingBus(unsigned int adr)
{
	const unsigned int prevcycle = beamx;
	unsigned char c = cycleLookup[BadLine][prevcycle];
	unsigned int tmpIx;

	switch (c) {
	default: case 'g':
		if (VertSubActive) {
			if (scrattr & GRAPHMODE) {
				tmpIx = (((CharacterPosition + x) << 3) & 0x1FF8) | vertSubCount;
				return grbank[tmpIx];
			} else {
				tmpIx = (chrbuf[x] << 3) | vertSubCount;
				return cset[tmpIx & (scrattr & EXTCOLOR ? 0x1FF : 0x7FF)];
			}
		} else {
			return vicBase[scrattr & EXTCOLOR ? 0x39FF : 0x3FFF];
		}
	case 'c':
		return chrbuf[x];
	case 's':
	case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '0':
		tmpIx = c == 's' ? 0 : c - '0'; 
		return VideoBase[0x03F8 + tmpIx];
	case 'r':
		return vicBase[0x3F00 | (CycleCounter & 0xFF)];
	case 'i':
		return vicBase[0x3FFF];
	case ' ': case '*':
		return cpuptr->getcins();
	}
}

// renders hires text
inline void Vic2mem::hi_text()
{
	unsigned char	charcol;
	unsigned char	mask;
	unsigned char	*wbuffer = scrptr + hshift;

	if (VertSubActive) {
		charcol = clrbuf[x];
		mask = cset[(chrbuf[x] << 3) | vertSubCount];
	} else {
		charcol = 0;
		mask = vicBase[0x3FFF];
	}
	const unsigned char col[] = { mcol[0], charcol };

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
inline void Vic2mem::ec_text()
{
	unsigned char charcol;
	unsigned char chr;
	unsigned char mask;
	unsigned char *wbuffer = scrptr + hshift;

	if (VertSubActive) {
		charcol = clrbuf[x];
		chr = chrbuf[x];
		mask = cset[((chr & 0x3F) << 3) | vertSubCount];
		chr >>= 6;
	} else {
		mask = vicBase[0x39FF];
		charcol = chr = 0;
	}

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
	unsigned char charcol;
	unsigned char chr;
	unsigned char *wbuffer = scrptr + hshift;
	unsigned char mask;

	if (VertSubActive) {
		charcol = clrbuf[x];
		chr = chrbuf[x];
		mask = cset[(chr << 3) | vertSubCount];
	} else {
		mask = vicBase[0x3FFF];
		charcol = 0;
	}

	if (charcol & 8) { // if character is multicolored

		mcol[3] = charcol & 0x07;

		wbuffer[0] = wbuffer[1] = mcol[ mask >> 6 ];
		wbuffer[2] = wbuffer[3] = mcol[ (mask & 0x30) >> 4 ];
		wbuffer[4] = wbuffer[5] = mcol[ (mask & 0x0C) >> 2 ];
		wbuffer[6] = wbuffer[7] = mcol[ mask & 0x03 ];

	} else { // this is a normally colored character
		const unsigned char col[] = { mcol[0], charcol };

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
inline void Vic2mem::hi_bitmap()
{
	unsigned char mask;
	unsigned char *wbuffer = scrptr + hshift;
	unsigned char hcol0;
	unsigned char hcol1;

	if (VertSubActive) {
		// get the actual color attributes
		hcol0 = (chrbuf[x] & 0x0F) | 0x40;
		hcol1 = chrbuf[x] >> 4;
		mask = grbank[(((CharacterPosition + x) << 3) & 0x1FF8) | vertSubCount];
	} else {
		hcol0 = 0x40;
		hcol1 = 0;
		mask = vicBase[0x3FFF];
	}
	const unsigned char col[] = { hcol0, hcol1 };

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
inline void Vic2mem::mc_bitmap()
{
	unsigned char	mask;
	unsigned char	*wbuffer = scrptr + hshift;

	if (VertSubActive) {
		unsigned int cp = (CharacterPosition + x) & 0x03FF;
		bmmcol[1] = (chrbuf[x] >> 4) | 0x40;
		bmmcol[2] = chrbuf[x] & 0x0F;
		bmmcol[3] = clrbuf[x];
		mask = grbank[(cp << 3) | vertSubCount];
	} else {// FIXME
		bmmcol[1] = 0x40;
		bmmcol[2] = bmmcol[3] = 0;
		mask = vicBase[0x3FFF];
	}

	wbuffer[0]= wbuffer[1] = bmmcol[mask >> 6];
	wbuffer[2]= wbuffer[3] = bmmcol[(mask & 0x30) >> 4 ];
	wbuffer[4]= wbuffer[5] = bmmcol[(mask & 0x0C) >> 2 ];
	wbuffer[6]= wbuffer[7] = bmmcol[mask & 0x03];
}

// when multi and extended color modes are all on the screen is blank
inline void Vic2mem::mcec()
{
	unsigned char *wbuffer = scrptr + hshift;
	unsigned char mask, charcol;
	unsigned char imcol[4] = { 0x40, 0x40, 0, 0 };

	if (VertSubActive) {
		mask = cset[((chrbuf[x] & 0x3F) << 3) | vertSubCount];
		charcol = clrbuf[x];
	} else {
		mask = vicBase[0x39FF];
		charcol = 0;
	}

	if (charcol & 8) {
		wbuffer[0] = wbuffer[1] = imcol[mask >> 6];
		wbuffer[2] = wbuffer[3] = imcol[(mask & 0x30) >> 4];
		wbuffer[4] = wbuffer[5] = imcol[(mask & 0x0C) >> 2];
		wbuffer[6] = wbuffer[7] = imcol[mask & 0x03];
	} else {
		wbuffer[0] = (mask & 0x80) ? 0 : 0x40;
		wbuffer[1] = (mask & 0x40) ? 0 : 0x40;
		wbuffer[2] = (mask & 0x20) ? 0 : 0x40;
		wbuffer[3] = (mask & 0x10) ? 0 : 0x40;
		wbuffer[4] = (mask & 0x08) ? 0 : 0x40;
		wbuffer[5] = (mask & 0x04) ? 0 : 0x40;
		wbuffer[6] = (mask & 0x02) ? 0 : 0x40;
		wbuffer[7] = (mask & 0x01) ? 0 : 0x40;
	}
}

// renders hires bitmap graphics
inline void Vic2mem::hi_bmec()
{
	unsigned char mask;
	unsigned char *wbuffer = scrptr + hshift;
	const unsigned char col[] = { 0x40, 0 };

	if (VertSubActive) {
		mask = grbank[(((CharacterPosition + x) << 3) & 0x19FF) | vertSubCount];
	} else {
		mask = vicBase[0x39FF];
	}

	wbuffer[0] = col[mask >> 7];
	wbuffer[1] = col[(mask & 0x40) >> 6];
	wbuffer[2] = col[(mask & 0x20) >> 5];
	wbuffer[3] = col[(mask & 0x10) >> 4];
	wbuffer[4] = col[(mask & 0x08) >> 3];
	wbuffer[5] = col[(mask & 0x04) >> 2];
	wbuffer[6] = col[(mask & 0x02) >> 1];
	wbuffer[7] = col[mask & 1];
}

inline void Vic2mem::mc_bmec()
{
	unsigned char	mask;
	unsigned char	*wbuffer = scrptr + hshift;
	unsigned char	imcol[4] = { 0x40, 0x40, 0, 0 };

	if (VertSubActive) {
		unsigned int cp = (CharacterPosition + x) & 0x039F;
		mask = grbank[(cp << 3) | vertSubCount];
	} else {
		mask = vicBase[0x39FF];
	}

	wbuffer[0]= wbuffer[1] = imcol[mask >> 6];
	wbuffer[2]= wbuffer[3] = imcol[(mask & 0x30) >> 4 ];
	wbuffer[4]= wbuffer[5] = imcol[(mask & 0x0C) >> 2 ];
	wbuffer[6]= wbuffer[7] = imcol[mask & 0x03];
}

inline void Vic2mem::render()
{
	// call the relevant rendering function
	switch (scrattr) {
		case 0:
			hi_text();
			break;
		case MULTICOLOR:
			mc_text();
			break;
		case EXTCOLOR:
			ec_text();
			break;
		case GRAPHMODE:
			hi_bitmap();
			break;
		case GRAPHMODE|MULTICOLOR:
			mc_bitmap();
			break;
		// illegal modes
		case GRAPHMODE|EXTCOLOR:
			hi_bmec();
			break;
		case GRAPHMODE|EXTCOLOR|MULTICOLOR:
			mc_bmec();
			break;
		case EXTCOLOR|MULTICOLOR:
		default:
			mcec();
			break;
	}
}

void Vic2mem::renderSprite(unsigned char *in, unsigned char *out, Mob &m, unsigned int cx, const unsigned int six)
{
	unsigned int i;
	const unsigned int priority = m.priority;

	if (!m.multicolor) {
		const unsigned char spc = m.color;
		if (m.expandX) {
			// sprite X expansion, hires mode
			for(i = 0; i < 3; i++, out += 16, cx += 16) {
				const unsigned char data = in[i];
				if (data & 0x80) {
					spriteCollisions[cx] |= six;
					spriteCollisions[cx + 1] |= six;
					MOB_DO_PIXEL(0, spc);
					MOB_DO_PIXEL(1, spc);
				}
				if (data & 0x40) {
					spriteCollisions[cx + 2] |= six;
					spriteCollisions[cx + 3] |= six;
					MOB_DO_PIXEL(2, spc);
					MOB_DO_PIXEL(3, spc);
				}
				if (data & 0x20) {
					spriteCollisions[cx + 4] |= six;
					spriteCollisions[cx + 5] |= six;
					MOB_DO_PIXEL(4, spc);
					MOB_DO_PIXEL(5, spc);
				}
				if (data & 0x10) {
					spriteCollisions[cx + 6] |= six;
					spriteCollisions[cx + 7] |= six;
					MOB_DO_PIXEL(6, spc);
					MOB_DO_PIXEL(7, spc);
				}
				if (data & 0x08) {
					spriteCollisions[cx + 8] |= six;
					spriteCollisions[cx + 9] |= six;
					MOB_DO_PIXEL(8, spc);
					MOB_DO_PIXEL(9, spc);
				}
				if (data & 0x04) {
					spriteCollisions[cx + 10] |= six;
					spriteCollisions[cx + 11] |= six;
					MOB_DO_PIXEL(10, spc);
					MOB_DO_PIXEL(11, spc);
				}
				if (data & 0x02) {
					spriteCollisions[cx + 12] |= six;
					spriteCollisions[cx + 13] |= six;
					MOB_DO_PIXEL(12, spc);
					MOB_DO_PIXEL(13, spc);
				}
				if (data & 0x01) {
					spriteCollisions[cx + 14] |= six;
					spriteCollisions[cx + 15] |= six;
					MOB_DO_PIXEL(14, spc);
					MOB_DO_PIXEL(15, spc);
				}
			}
		} else {
			// sprite, normal size, hires mode
			for(i = 0; i < 3; i++, out += 8, cx += 8) {
				const unsigned char data = in[i];
				if (data & 0x80) {
					spriteCollisions[cx] |= six;
					MOB_DO_PIXEL(0, spc);
				}
				if (data & 0x40) {
					spriteCollisions[cx + 1] |= six;
					MOB_DO_PIXEL(1, spc);
				}
				if (data & 0x20) {
					spriteCollisions[cx + 2] |= six;
					MOB_DO_PIXEL(2, spc);
				}
				if (data & 0x10) {
					spriteCollisions[cx + 3] |= six;
					MOB_DO_PIXEL(3, spc);
				}
				if (data & 0x08) {
					spriteCollisions[cx + 4] |= six;
					MOB_DO_PIXEL(4, spc);
				}
				if (data & 0x04) {
					spriteCollisions[cx + 5] |= six;
					MOB_DO_PIXEL(5, spc);
				}
				if (data & 0x02) {
					spriteCollisions[cx + 6] |= six;
					MOB_DO_PIXEL(6, spc);
				}
				if (data & 0x01) {
					spriteCollisions[cx + 7] |= six;
					MOB_DO_PIXEL(7, spc);
				}
			}
		}
	} else {
		mobExtCol[2] = m.color;
		if (m.expandX) {
			unsigned int cDword = (six << 24) | (six << 16) | (six << 8) | six;
			// sprite X expansion, multi mode
			for(i = 0; i < 3; i++, out += 16, cx += 16) {
				const unsigned char data = in[i];
				unsigned int bitlet = data >> 6;
				if (bitlet) {
					*((unsigned int*)(spriteCollisions + cx)) |= cDword;
					MOB_DO_PIXEL(0, mobExtCol[bitlet]);
					MOB_DO_PIXEL(1, mobExtCol[bitlet]);
					MOB_DO_PIXEL(2, mobExtCol[bitlet]);
					MOB_DO_PIXEL(3, mobExtCol[bitlet]);
				}
				bitlet = data & 0x30;
				if (bitlet) {
					*((unsigned int*)(spriteCollisions + cx + 4)) |= cDword;
					MOB_DO_PIXEL(4, mobExtCol[bitlet >> 4]);
					MOB_DO_PIXEL(5, mobExtCol[bitlet >> 4]);
					MOB_DO_PIXEL(6, mobExtCol[bitlet >> 4]);
					MOB_DO_PIXEL(7, mobExtCol[bitlet >> 4]);
				}
				bitlet = data & 0x0C;
				if (bitlet) {
					*((unsigned int*)(spriteCollisions + cx + 8)) |= cDword;
					MOB_DO_PIXEL(8, mobExtCol[bitlet >> 2]);
					MOB_DO_PIXEL(9, mobExtCol[bitlet >> 2]);
					MOB_DO_PIXEL(10, mobExtCol[bitlet >> 2]);
					MOB_DO_PIXEL(11, mobExtCol[bitlet >> 2]);
				}
				bitlet = data & 0x03;
				if (bitlet) {
					*((unsigned int*)(spriteCollisions + cx + 12)) |= cDword;
					MOB_DO_PIXEL(12, mobExtCol[bitlet]);
					MOB_DO_PIXEL(13, mobExtCol[bitlet]);
					MOB_DO_PIXEL(14, mobExtCol[bitlet]);
					MOB_DO_PIXEL(15, mobExtCol[bitlet]);
				}
			}
		} else {
			// normal size, multicolor
			for(i = 0; i < 3; i++, out += 8, cx += 8) {
				const unsigned char data = in[i];
				unsigned int bitlet = data >> 6;
				if (bitlet) {
					spriteCollisions[cx] |= six;
					spriteCollisions[cx + 1] |= six;
					MOB_DO_PIXEL(0, mobExtCol[bitlet]);
					MOB_DO_PIXEL(1, mobExtCol[bitlet]);
				}
				bitlet = data & 0x30;
				if (bitlet) {
					spriteCollisions[cx + 2] |= six;
					spriteCollisions[cx + 3] |= six;
					MOB_DO_PIXEL(2, mobExtCol[bitlet >> 4]);
					MOB_DO_PIXEL(3, mobExtCol[bitlet >> 4]);
				}
				bitlet = data & 0x0C;
				if (bitlet) {
					spriteCollisions[cx + 4] |= six;
					spriteCollisions[cx + 5] |= six;
					MOB_DO_PIXEL(4, mobExtCol[bitlet >> 2]);
					MOB_DO_PIXEL(5, mobExtCol[bitlet >> 2]);
				}
				bitlet = data & 0x03;
				if (bitlet) {
					spriteCollisions[cx + 6] |= six;
					spriteCollisions[cx + 7] |= six;
					MOB_DO_PIXEL(6, mobExtCol[bitlet]);
					MOB_DO_PIXEL(7, mobExtCol[bitlet]);
				}
			}
		}
	}
}

inline void Vic2mem::checkSpriteEnable()
{
#if NEWSDMA
	unsigned int i = 7;
	do {
		if (mob[i].enabled && mob[i].y == beamy /*&& !mob[i].dmaState*/) {
			mob[i].dmaState = true;
			mob[i].dataCountReload = 0;
			mob[i].dataCount = 0;
			mob[i].reloadFlipFlop = mob[i].expandY ^ 1;
		}
	} while (i--);
#endif
}

inline bool Vic2mem::checkSpriteDMA(unsigned int i)
{
#if !NEWSDMA
	if (mob[i].enabled && mob[i].y == prevY && !mob[i].dmaState) {
		mob[i].dmaState = true;
		mob[i].dataCountReload = 0;
		mob[i].dataCount = 0;
		mob[i].reloadFlipFlop = mob[i].expandY;
	}
#endif
	if (mob[i].dmaState) {
		spriteDMAmask |= (1 << i);
		if (!vicBusAccessCycleStart)
			vicBusAccessCycleStart = CycleCounter;
		return true;
	}
	return false;
}

inline void Vic2mem::stopSpriteDMA()
{
#if NEWSDMA
	unsigned int i = 7;

	do {
		//unsigned int &dc = mob[i].dataCount;
		// check end of sprite DMA
		if (mob[i].dataCountReload == 0x3F) {
			mob[i].dmaState = mob[i].rendering = false;
			//mob[i].dataCountReload = dc;
		}
	} while (i--);
#endif
}

inline void Vic2mem::spriteReloadCounters()
{
#if NEWSDMA
	unsigned int i = 7;

	do {
		if (mob[i].dmaState) {
			unsigned int &dc = mob[i].dataCount;
			unsigned int &flipFlop = mob[i].reloadFlipFlop;

			flipFlop ^= 1;
			if (flipFlop) {
				mob[i].dataCountReload = dc;
				// set flipflop to Y expension bit
				flipFlop = mob[i].expandY;
			}
		}
	} while (i--);
#endif
}

inline void Vic2mem::drawSpritesPerLine(unsigned char *lineBuf)
{
	unsigned int i = 7;

	do {
#if !NEWSDMA
		if (mob[i].dmaState) {
			unsigned int &dc = mob[i].dataCount;
			unsigned int &dcReload = mob[i].dataCountReload;
			unsigned int &flipFlop = mob[i].reloadFlipFlop;

			flipFlop ^= 1;
			if (flipFlop) {
				dcReload = dc;
				// set flipflop to Y expension bit
				flipFlop = mob[i].expandY;
			}
			dc = (dcReload + 3) & 0x3F;
			const unsigned int tvX = RASTERX2TVCOL(mob[i].x);
			unsigned char *d = vicBase + mob[i].dataAddress + dcReload;
			unsigned char *p = lineBuf + tvX;
			renderSprite(d, p, mob[i], tvX, 1 << i);
			// check end of sprite DMA
			if (dc == 0x3F) {
				mob[i].dmaState = false;
				dcReload = dc;
			}
#else
		if (mob[i].rendering) {
			if (mob[i].x < VIC_PIXELS_PER_ROW) {
				const unsigned int tvX = RASTERX2TVCOL(mob[i].x);
				unsigned char *d = mob[i].sdb[1].shiftRegBuf;
				unsigned char *p = lineBuf + tvX;
				renderSprite(d, p, mob[i], tvX, 1 << i);
			}
			if (!mob[i].dmaState) {
				mob[i].rendering = false;
			}
		} else if (mob[i].dmaState) {
			mob[i].rendering = true;
#endif
		}
	} while (i--);
	// check collisions
	for(i = 0; i < VIC_PIXELS_PER_ROW; i++) {
		if (spriteCollisions[i]) {
			const unsigned char newReg = spriteCollisionReg | collisionLookup[spriteCollisions[i]];
			if (!spriteCollisionReg && newReg) {
				vicReg[0x19] |= ((vicReg[0x1A] & 4) << 5) | 4;
				checkIRQflag();
			}
			spriteCollisionReg = newReg;
			spriteCollisions[i] = 0;
		}
	}
}

void Vic2mem::enableREU(unsigned int sizekb)
{
	if (sizekb) {
		if (reu)
			delete reu;
		reu = new REU(sizekb, this);
		reuSizeKb = sizekb;
	}
	else if (reu && !sizekb) {
		delete reu;
		reu = NULL;
		reuSizeKb = 0;
	}
}

//#define LOG_REU

void Vic2mem::REU::doTransfer(unsigned int type)
{
	unsigned int i;
	
	i = (transferLen - 1) & 0xFFFF;

	switch (type) {
	// 00 = transfer Machine -> REU
	default:
#ifdef LOG_REU
		fprintf(stderr, "$%04X : Transfer %04X bytes from Machine (%04X) to REU (%010X)\n", machine->cpuptr->getPC(), transferLen,
			machineBaseAddress, ((bank << 16) | baseAddress) & memMask);
#endif
		do {
			ram[(((bank << 16) | baseAddress) + i) & memMask] = machine->Read((machineBaseAddress + i) & 0xFFFF);
		} while (i--);
		break;
	// 01 = transfer REU -> Machine
	case 0x01:
		do {
			machine->Write((machineBaseAddress + i) & 0xFFFF, ram[(((bank << 16) | baseAddress) + i) & memMask]);
		} while (i--);
		break;
	// 10 = swap Machine <-> REU
	case 0x02:
#ifdef LOG_REU
		fprintf(stderr, "$%04X : Swapping %04X bytes bw Machine (%04X) and REU (%010X)\n", machine->cpuptr->getPC(), transferLen,
			machineBaseAddress, ((bank << 16) | baseAddress) & memMask);
#endif
		do {
			unsigned int machineMemAddr = (machineBaseAddress + i) & 0xFFFF;
			unsigned int reuMemAddr = (((bank << 16) | baseAddress) + i) & memMask;
			unsigned char tmp = machine->Read(machineMemAddr);
			machine->Write(machineMemAddr, ram[reuMemAddr]);
			ram[reuMemAddr] = tmp;
		} while (i--);
		break;
	// 11 = compare Machine -- REU
	case 0x03:
#ifdef LOG_REU
		fprintf(stderr, "$%04X : Comparing %04X bytes bw Machine (%04X) and REU (%010X)\n", machine->cpuptr->getPC(), transferLen,
			machineBaseAddress, ((bank << 16) | baseAddress) & memMask);
#endif
		regs[0] &= ~0x20;
		do {
			unsigned int machineMemAddr = (machineBaseAddress + i) & 0xFFFF;
			unsigned int reuMemAddr = (((bank << 16) | baseAddress) + i) & memMask;
			if (machine->Read(machineMemAddr) != ram[reuMemAddr]) {
				// set error
				regs[0] |= 0x20;
				break;
			}
		} while (i--);
		break;
	}
	if (regs[0] & 0x20) {// autoload
		baseAddress += transferLen;
		machineBaseAddress += transferLen;
		transferLen = 1;
	}
	xferReady = 1;
	command &= ~0x80;
}

void Vic2mem::REU::doCommand(unsigned int cmd)
{
	command = cmd;
	if (cmd & 0x80) {
		// start immediately?
		if (cmd & 0x10) {
			doTransfer(command & 3);
			if (imr & 0x80)
				irqFlag = 1;
		}
	}
}

void Vic2mem::REU::Write(unsigned int addr, unsigned char data)
{
	addr &= 0x0F;
	switch (addr) {
	case 1:
		doCommand(data);
		break;
	case 2:
		machineBaseAddress = (machineBaseAddress & 0xFF00) | data;
		break;
	case 3:
		machineBaseAddress = (data << 8)|(machineBaseAddress & 0xFF);
		break;
	case 4:
		baseAddress = (baseAddress & 0xFF00) | data;
		break;
	case 5:
		baseAddress = ((data) << 8) | (baseAddress & 0xFF);
		break;
	case 6:
		bank = data & bankMask;
		break;
	case 7:
		transferLen = (transferLen & 0xFF00) | data;
		if (!transferLen) transferLen = 0x10000;
		break;
	case 8:
		transferLen = (data << 8) | (transferLen & 0xFF);
		if (!transferLen) transferLen = 0x10000;
		break;
	case 9:
		imr = (imr & ~0xE0) | (data & 0xE0);
		break;
	default:;
	}
#ifdef LOG_REU
	fprintf(stderr, "$%04X : REU reg(%02X) written: %02X\n", machine->cpuptr->getPC(), addr, data);
#endif
	regs[addr] = data;
}

unsigned char Vic2mem::REU::Read(unsigned int addr)
{
	unsigned char rv;
	addr &= 0x0F;
	switch (addr) {
	default:
		rv = regs[addr];
		break;
	case 0:
		rv |= (sizeKb >= 256) ? 0x10 : 0x00;
		rv |= xferReady ? 0x40 : 0x00;
		rv |= irqFlag ? 0x80 : 0x00;
		xferReady = 0;
		break;
	case 1:
		rv = command & ~0x4C;
		break;
	case 4:
		rv = baseAddress & 0xFF;
		break;
	case 5:
		rv = baseAddress >> 8;
		break;
	case 6:
		rv = bank | ~0x03;
		break;
	case 7:
		rv = transferLen & 0xFF;
		break;
	case 8:
		rv = (transferLen >> 8) & 0xFF;
		break;
	case 9:
		rv = imr | 0x1F;
		break;
	case 0xA:
		rv = regs[10] | 0x3F;
		break;
	}
#ifdef LOG_REU
	fprintf(stderr, "$%04X : REU reg(%02X) read: %02X\n", machine->cpuptr->getPC(), addr, rv);
#endif
	return rv;
}

void Vic2mem::REU::startDMA()
{
	if (!(command & 0x10)) //  && (command & 0x80)
		doTransfer(command & 3);
}

void Vic2mem::REU::Reset() 
{
	command = 0;
	xferReady = 0;
	regs[0] = 0;
	machineBaseAddress = 0;
	baseAddress = 0;
	transferLen = 1;
	memset(ram, 0, sizeInBytes);
}

void Vic2mem::REU::initRam(unsigned int sizekb)
{
	sizeKb = sizekb;
	sizeInBytes = sizekb * 1024;
	memMask = sizeInBytes - 1;
	bankMask = (sizekb - 1) >> 6;
	if (ram)
		delete[] ram;
	ram = new unsigned char[sizeInBytes];
}

Vic2mem::REU::REU(unsigned int sizekb, TED *machine_) : ram(0)
{
	setId("REU0");
	initRam(sizekb);
	machine = machine_;
	Reset();
}

void Vic2mem::REU::dumpState()
{
	unsigned int i;

	saveVar(&sizeInBytes, sizeof(sizeInBytes));
	saveVar(ram, sizeInBytes);
	for (i = 0; i < 16; i++)
		saveVar(regs + i, sizeof(regs[i]));
}

void Vic2mem::REU::readState()
{
	unsigned int i;

	readVar(&sizeInBytes, sizeof(sizeInBytes));
	initRam(sizeInBytes >> 10);
	readVar(ram, sizeInBytes);
	// discard first two registers
	readVar(regs, sizeof(regs[0]));
	readVar(regs + 1, sizeof(regs[1]));
	for (i = 2; i < 16; i++) {
		readVar(regs + i, sizeof(regs[i]));
		Write(i, regs[i]);
	}
}

Vic2mem::REU::~REU()
{
	delete[] ram;
}