#include <cstdio>
#include <cstring>
#include <memory.h>
#include "vicmem.h"
#include "vic20rom.h"
#include "Clockable.h"
#include "video.h"
#include "keysvic.h"
#include "sound.h"
#include "Sid.h"
#include "tape.h"

#define VIC_READS_TRANSLATED

unsigned int Vicmem::ramExpSizeKb = 0;
rvar_t Vicmem::vicSettings[] = {
	{ "VIC20 RAM expansion (kB)", "Vic20RamExp", Vicmem::flipVicramExpansion, &Vicmem::ramExpSizeKb, RVAR_INT, NULL },
	{ "", "", NULL, NULL, RVAR_NULL, NULL }
};

Vicmem::Vicmem()
{
	instance_ = this;
	setId("VIC1");

	if (1) {
		masterClock = VIC20_REAL_CLOCK_M10;
		clocks_per_line = 71;
	}
	else {
		masterClock = VIC20_REAL_CLOCK_NTSC_M10;
		clocks_per_line = 65; // 65 for NTSC
	}

	cpuptr = NULL;
	_delayedEventCallBack = NULL;
	memset(mcol, 0, sizeof(mcol));
	memset(firstmcol, 0, sizeof(firstmcol));
	cset = vic20charset;
	colorRam_ = Ram;
	VideoBase = Ram + 0x1E00;
	vicCharsetBase = 0;
	//
	actram = Ram;
	loadroms();
	// setting screen memory pointer
	scrptr = screen;
	endptr = scrptr + 312 * 71 * 8;
	scanlinesDone = 312;
	ntscMode = false;
	TVScanLineCounter = 0;
	beamy = beamx = 0;
	framecol = 0;
	memset(screen, 0, SCR_HSIZE * SCR_VSIZE * 2);
	//
	irqFlag = 0;
	charrombank = vic20charset;
	videoLatchPhase = latchedColumns = latchedRows = 0;
	cartAttached = false;
	//
	tap->mem = this;
	keysvic = new KEYSVIC;
// VIA
	via[0].setCheckIrqCallback(this, checkNmi);
	via[1].setCheckIrqCallback(this, checkIrq);
	//
	Reset(true);
	// remove SID sound (inherited) from the list
	SoundSource::remove(dynamic_cast<SoundSource*>(sidCard));
	SaveState::remove(dynamic_cast<SaveState*>(sidCard));
	vicSoundInit();
}

Vicmem::~Vicmem()
{
	delete keysvic;
}

unsigned char* Vicmem::getCharSetPtr()
{
	return vic20charset + 0x0800;
}

void Vicmem::flipVicramExpansion(void* none)
{
	Vicmem* vicm = dynamic_cast<Vicmem*>(instance_);
	const unsigned int ramExpSizeArray[] = { 0, 3, 8, 16, 24, 32, 35 };
	const unsigned int ramExpSizeArraySize = sizeof(ramExpSizeArray) / sizeof(ramExpSizeArray[0]);
	unsigned int i = 0;

	if (vicm) {
		while (i < ramExpSizeArraySize) {
			if (ramExpSizeKb == ramExpSizeArray[i])
				break;
			i++;
		}
		ramExpSizeKb = ramExpSizeArray[(i + 1) % ramExpSizeArraySize];
		vicm->cpuptr->Reset();
		vicm->Reset(2);
	}
}

void Vicmem::checkNmi(void* cptr, unsigned char m)
{
	Vicmem* mh = reinterpret_cast<Vicmem*>(cptr);
	if (m)
		mh->cpuptr->triggerNmi();
	else
		mh->cpuptr->clearNmi();
}

void Vicmem::triggerNMI()
{
	via[0].ifr |= Via::INPUTCA1;
	if (via[0].ifr & via[0].ier & 0x7F) {
		cpuptr->triggerNmi();
		via[0].ifr |= 0x80;
		cpuptr->clearNmi();
	}
}

void Vicmem::Reset(unsigned int resetLevel)
{
	// clear RAM with powerup pattern and reload ROM's
	if (resetLevel & 2) {
		for (int i = 0; i < RAMSIZE; i++)
			Ram[i] = (i >> 1) << 1 == i ? 0 : 0xFF;
		loadroms();
		// we detach carts, too
		cartRemove();
	}
	//
	soundReset();
	via[0].reset();
	via[1].reset();
	if (cpuptr)
		cpuptr->Reset();
}
void Vicmem::dumpState()
{
	// always called during end of screen
	saveVar(Ram, RAMSIZE);
	saveVar(serialPort, sizeof(serialPort[0]));
	saveVar(&beamx, sizeof(beamx));
	saveVar(&beamy, sizeof(beamy));
	saveVar(&mcol, sizeof(mcol));
	saveVar(&videoLatchPhase, sizeof(videoLatchPhase));
	saveVar(&videoBaseValue, sizeof(videoBaseValue));
	saveVar(&CharacterWindow, sizeof(CharacterWindow));
	saveVar(&CharacterPosition, sizeof(CharacterPosition));
	saveVar(&CharacterPositionReload, sizeof(CharacterPositionReload));
	saveVar(&SideBorderFlipFlop, sizeof(SideBorderFlipFlop));
	saveVar(&vertSubCount, sizeof(vertSubCount));
	saveVar(&HBlanking, sizeof(HBlanking));
	saveVar(&VBlanking, sizeof(VBlanking));
	saveVar(&TVScanLineCounter, sizeof(TVScanLineCounter));
	saveVar(&framecol, sizeof(framecol));
	//
	saveVar(&vicReg, sizeof(vicReg) / sizeof(vicReg[0]));
	for (unsigned int i = 0; i < 2; i++) {
		via[i].reg[0] = via[i].prb;
		via[i].reg[1] = via[i].pra;
		via[i].reg[2] = via[i].ddrb;
		via[i].reg[3] = via[i].ddra;
		via[i].reg[4] = via[i].reg[6] = via[i].t1l & 0xFF;
		via[i].reg[5] = via[i].reg[7] = via[i].t1l >> 8;
		via[i].reg[8] = via[i].t2l & 0xFF;
		via[i].reg[9] = via[i].t2l >> 8;
		via[i].reg[0x0B] = via[i].acr;
		via[i].reg[0x0C] = via[i].pcr;
		via[i].reg[0x0D] = via[i].ifr;
		via[i].reg[0x0E] = via[i].ier;
	}
	saveVar(&via[0].reg, sizeof(via[0].reg) / sizeof(via[0].reg[0]));
	saveVar(&via[1].reg, sizeof(via[1].reg) / sizeof(via[1].reg[0]));
}

void Vicmem::readState()
{
	readVar(Ram, RAMSIZE);
	readVar(serialPort, sizeof(serialPort[0]));
	readVar(&beamx, sizeof(beamx));
	readVar(&beamy, sizeof(beamy));
	readVar(&mcol, sizeof(mcol));
	readVar(&videoLatchPhase, sizeof(videoLatchPhase));
	readVar(&videoBaseValue, sizeof(videoBaseValue));
	readVar(&CharacterWindow, sizeof(CharacterWindow));
	readVar(&CharacterPosition, sizeof(CharacterPosition));
	readVar(&CharacterPositionReload, sizeof(CharacterPositionReload));
	readVar(&SideBorderFlipFlop, sizeof(SideBorderFlipFlop));
	readVar(&vertSubCount, sizeof(vertSubCount));
	readVar(&HBlanking, sizeof(HBlanking));
	readVar(&VBlanking, sizeof(VBlanking));
	readVar(&TVScanLineCounter, sizeof(TVScanLineCounter));
	readVar(&framecol, sizeof(framecol));
	//
	readVar(&vicReg, sizeof(vicReg) / sizeof(vicReg[0]));
	readVar(&via[0].reg, sizeof(via[0].reg) / sizeof(via[0].reg[0]));
	readVar(&via[1].reg, sizeof(via[1].reg) / sizeof(via[1].reg[0]));
	//
	framecol2 = framecol;
	for (unsigned int i = 0; i < 16; i++) {
		Write(0x9000 + i, vicReg[i]);
	}
	for (unsigned int i = 0; i < 2; i++) {
		via[i].prb = via[i].reg[0];
		via[i].pra = via[i].reg[1];
		via[i].ddrb = via[i].reg[2];
		via[i].ddra = via[i].reg[3];
		via[i].t1c = via[i].t1l = (via[i].reg[5] << 8) | via[i].reg[4];
		via[i].t2c = via[i].t2l = (via[i].reg[9] << 8) | via[i].reg[8];
		via[i].reg[0x0B] = via[i].acr;
		via[i].reg[0x0C] = via[i].pcr;
		via[i].reg[0x0D] = via[i].ifr;
		via[i].reg[0x0E] = via[i].ier;
	}
	changeVideoBase(videoBaseValue);
}

inline static size_t getFileSize(FILE* fp)
{
	if (!fp)
		return 0;

	unsigned int orgPos;
	unsigned int s;

	orgPos = ftell(fp);
	fseek(fp, 0L, SEEK_END);
	s = ftell(fp);
	fseek(fp, orgPos, SEEK_SET);
	return s;
}

void Vicmem::cartRemove()
{
	unsigned int loadAddress = 0xA000;
	unsigned int i = 0;
	while (i < 0x4000) {
		rom[1][(loadAddress & 0x1FFF) + i] = 0xFF;
		i++;
	}
	cartAttached = false;
}

void Vicmem::cartDetach(bool emptyUpper)
{
	if (!emptyUpper) {
		cartRemove();
	}
	Reset(2);
}

void Vicmem::loadromfromfile(int nr, const char fname[512], unsigned int offset)
{
	FILE* img;
	bool emptyUpper = true;

	if ((img = fopen(fname, "rb"))) {
		unsigned short loadAddress = 0xA000;
		unsigned int i = 0;
		size_t fileSize = getFileSize(img);
		if (fileSize == 16386) {
			loadAddress = fgetc(img);
			loadAddress |= (fgetc(img) << 8);
			fileSize -= 2;
			if (loadAddress == 0x6000) {
				while (fileSize-- > 0x4000) {
					int byteIn = fgetc(img);
					poke(loadAddress + i, byteIn);
					i++;
				}
				loadAddress = 0xA000;
				fileSize = 0x4000;
				emptyUpper = false;
				i = 0;
			}
		}
		else if (fileSize == 8194 || fileSize == 4098) {
			loadAddress = fgetc(img);
			loadAddress |= (fgetc(img) << 8);
			fileSize -= 2;
		}
		else if (fileSize != 8192 && fileSize != 4096) {
			fclose(img);
			return;
		}
		cartRemove();
		while (i < fileSize) {
			int byteIn = fgetc(img);
			rom[1][(loadAddress & 0x1FFF) + i] = byteIn;
			i++;
		}
		Reset(1);
		cartAttached = true;
		fclose(img);
	} else
		cartDetach(false);
}

void Vicmem::loadroms()
{
	memcpy(rom[0], vic20rom, VIC20_ROM_SIZE);
#if FAST_BOOT
	// TODO: check ROM pattern
	//rom[0][0xFE91 & 0x3FFF] = 0x60;
#endif
}

void Vicmem::copyToKbBuffer(const char* text, unsigned int length)
{
	if (!length)
		length = (unsigned int)strlen(text);
	Write(0xc6, length);
	while (length--)
		Write(0x0277 + length, text[length]);
}

Color Vicmem::getColor(unsigned int ix)
{
	const double bsat = 49.0;
	const Color color[16] = {
		{   0, 0.0, 0 }, 
		{   0, 5.0, 0 },
		// measured from a real VIC20 direct composite picture shot
		{ 104, 3.1, 100 },
		{ 284, 4.1, 100 },
		{  61, 3.5, 79 },
		{ 241, 4.1, 79 },
		{ 347, 2.9, 87 },
		{ 167, 4.4, 87 },
		{ 123, 3.5, 87 },
		//
		{ 123, 4.3, 40 },
		{ 104, 4.5, 40 },
		{ 284, 5.0, 50 },
		{  61, 4.5, 40 },
		{ 244, 5.0, 70 },
		{ 347, 4.3, 45 },
		{ 167, 5.0, 40 }
	};
	return color[ix & 0xF];
}

void Vicmem::updateColorRegs()
{
	// Changes to $900F and $900E colors appear 1 hires pixel too late with respect to half char boundaries
	firstmcol[SCRN] = vicReg[15] >> 4;
	firstmcol[BORDER] = vicReg[15] & 7;
	framecol = (mcol[BORDER] << 24) | (mcol[BORDER] << 16) | (firstmcol[BORDER] << 8) | firstmcol[BORDER];
	// FIXME: changes to the reverse mode bit appear 3 hires pixels late!
	reverseMode = vicReg[15] & 0x08;
	_delayedEventCallBack = 0;
}

void Vicmem::updateFirstPixelColor()
{
	firstmcol[AUX] = vicReg[14] >> 4;
	_delayedEventCallBack = 0;
}

unsigned char *Vicmem::changeVideoBase(int address)
{
	unsigned char* videoRamPtr = Ram;
	unsigned int RAMMask = 0xFFFF;

	switch (address & 0xF000) {
		default:
			return videoRamPtr + (address & RAMMask);
		case 0x8000:
			return vic20charset + (address & 0x0FFF);
		case 0x9000:
			return videoRamPtr + (address & RAMMask);
		case 0xA000:
		case 0xB000:
			return videoRamPtr + (address & 0x1FFF);
		case 0xC000:
		case 0xD000:
			return videoRamPtr + (address & 0x1FFF);
		case 0xE000:
		case 0xF000:
			return videoRamPtr + (address & 0x1FFF);
	}
}

inline unsigned char Vicmem::fetchCharBitmap(unsigned int cpos)
{
	unsigned int pointer = vicCharsetBase;
	unsigned char mask;

	if (pointer & 0x1000 && pointer & 0x0C00) {
		if (cpos > csetmask) {
			unsigned int limit = (pointer & 0x0C00) ^ 0x0C00;
			mask = Read((((charbank & 8) << 12) | limit + (cpos & csetmask)));
		}
		else
			mask = Read(pointer + cpos);
	}
	else {
		if (cpos > csetmask)
			pointer |= 0x1000;
		mask = Read(pointer | (cpos & 0x0FFF));
	}

	return mask;
}

inline void Vicmem::changeCharsetPtr(int value)
{
	int pointer = ((value & 0x07) << 10) | (((value ^ 8) & 8) << 12);

	if (pointer & 0x1000 && pointer & 0x0C00) {
		unsigned int limit = (pointer & 0x0C00) ^ 0x0C00;
		csetmask = limit | 0x3FF;
	}
	else {
		csetmask = 0x0FFF;
	}
}

unsigned char Vicmem::readUnconnected(unsigned int addr, bool isCpu)
{
	unsigned int cc = cpuptr->getcycle();
	unsigned int lcb = cc ? cpuptr->getnextins() : cpuptr->getcins();
	return !(beamx & 1) ? dataRead[0] : addr >> 8;
}

void Vicmem::vic6560write(unsigned int addr, unsigned char value)
{
	addr &= 0xF;

	switch (addr) {

	case 0x0:
		screenOriginX = (value & 0x7F) << 1;
		interlaceMode = value & 0x80;
		break;
	case 0x1:
		screenOriginY = value << 1;
		if (!SideBorderFlipFlop && ((beamx >= 2 && screenOriginY == beamy) || screenOriginY + 1 == beamy))
			SideBorderFlipFlop = true;
		break;
	case 0x2:
		nrOfColumns = value & 0x7F;
		scrclr_bit9 = (value & 0x80) << 2;
		videoBaseValue = (videoBaseValue & ~0x200) | scrclr_bit9;
		VideoBase = changeVideoBase(videoBaseValue);
		break;
	case 0x3:
		charHeight = 1 << (3 + (value & 1));
		nrOfRows = (value & 0x7E);
		break;
	case 0x5:
		charbank = value;
		vicCharsetBase = ((value & 0x07) << 10) | (((value ^ 8) & 8) << 12);
		videoBaseValue = (((value ^ 0x80) & 0x80) << 8) | ((value & 0x70) << 6) | (videoBaseValue & 0x200);
		VideoBase = changeVideoBase(videoBaseValue);
		changeCharsetPtr(value);
		break;

	case 0xA:
	case 0xB:
	case 0xC:
	case 0xD:
		if (vicReg[addr] ^ value) {
			soundWrite((addr - 0xA) & 3, value);
		}
		break;

	case 0xE:
		_delayedEventCallBack = (delayedEventCallback)&Vicmem::updateFirstPixelColor;
		mcol[AUX] = value >> 4;
		if ((vicReg[addr] ^ value) & 0xF)
			soundWrite(4, value & 0xF);
		break;

	case 0xF:
		_delayedEventCallBack = (delayedEventCallback) &Vicmem::updateColorRegs;
		mcol[SCRN] = value >> 4;
		mcol[BORDER] = value & 7;
		framecol = (mcol[BORDER] << 24) | (mcol[BORDER] << 16) | (firstmcol[BORDER] << 8) | firstmcol[BORDER];
		framecol2 = (mcol[BORDER] << 24) | (mcol[BORDER] << 16) | (mcol[BORDER] << 8) | mcol[BORDER];
		break;

	default:
		break;
	}
	vicReg[addr] = value;
}

unsigned char Vicmem::vic6560read(unsigned int addr)
{
	addr &= 0xF;

	switch (addr) {

	case 0x3:
		return nrOfRows | ((beamy & 1) << 7) | (charHeight == 8 ? 0 : 1);
	case 0x4:
		//fprintf(stderr, "%04X Read:%02X HC:(%03d/$%02X) VC:(%03d/$%03X) XY:%03i/%03i PC:$%04X BL:%02X frm:%i cyc:%llu\n",
		//	addr, beamy >> 1, getHorizontalCount(), getHorizontalCount(), beamy, beamy, beamx, TVScanLineCounter, cpuptr->getPC(), BadLine, crsrphase, CycleCounter);
		return beamy >> 1;
	case 0x6:
		return 0;
	case 0x7:
		return 0;
	case 0x8:
		return keysvic->readPaddleAxis(0);
	case 0x9:
		return keysvic->readPaddleAxis(1);

	case 0x0:
	case 0x1:
	case 0x2:
	case 0x5:
	case 0xA:
	case 0xB:
	case 0xC:
	case 0xD:
	case 0xE:
	case 0xF:
		return vicReg[addr];
	}
	return 0;
}

unsigned char Vicmem::Read(unsigned int addr)
{
	switch (addr & 0xF000) {
		default:
		case 0x0000:
			switch (addr & 0x0F00) {
			case 0x0000:
			case 0x0100:
			case 0x0200:
			case 0x0300:
				return Ram[addr];
			default:
				if (!((ramExpSizeKb & 3) == 3))
					return readUnconnected(addr);
			}
			return Ram[addr];
		case 0x1000:
			return Ram[addr];
		case 0x2000:
		case 0x3000:
		case 0x4000:
		case 0x5000:
		case 0x6000:
		case 0x7000:
			return Ram[addr & 0x7FFF];
		case 0x8000:
			return charrombank[addr & 0x0FFF];
		case 0x9000:
			switch (addr & 0x0F00) {
				default:
					//return Ram[addr & 0xFFFF];
					return (addr & 0xff);// readUnconnected(addr);
				case 0x0000:
					if (!(addr & 0x30))
						return vic6560read(addr);
				case 0x0100:
				case 0x0200:
				case 0x0300:
					if (addr & 0x10)
						return via1read(addr);
					else if (addr & 0x20)
						return via2read(addr);
					return readUnconnected(addr & 0xF000);
				case 0x0400:
				case 0x0500:
				case 0x0600:
				case 0x0700:
					return Ram[addr] & 0x0F;
			}
		case 0xA000:
		case 0xB000:
			if (!cartAttached) {
				if (ramExpSizeKb < 32)
					return readUnconnected(addr);
				return Ram[addr];
			}
			return rom[1][addr & 0x1FFF];
		case 0xC000:
		case 0xD000:
		case 0xE000:
		case 0xF000:
			return rom[0][addr & 0x3FFF];
	}
}

void Vicmem::Write(unsigned int addr, unsigned char value)
{
	switch (addr & 0xF000) {
		default:
			break;
		case 0x0000:
			switch (addr & 0x0F00) {
				case 0x0000:
				case 0x0100:
				case 0x0200:
				case 0x0300:
					Ram[addr & 0x0FFF] = value;
					break;
				default:
					if ((ramExpSizeKb & 3) == 3)
						Ram[addr] = value;
					break;
			}
			break;
		case 0x1000:
			Ram[addr] = value;
			break;
		case 0x2000:
		case 0x3000:
			if (ramExpSizeKb >= 8)
				Ram[addr] = value;
			break;
		case 0x4000:
		case 0x5000:
			if (ramExpSizeKb >= 16)
				Ram[addr] = value;
		case 0x6000:
		case 0x7000:
			if (ramExpSizeKb >= 24)
				Ram[addr] = value;
			break;
		case 0x8000:
			break;
		case 0x9000:
			switch (addr & 0x0F00) {
				default:
					Ram[addr] = value;
					break;
				case 0x0000:
					if (!(addr & 0x30))
						vic6560write(addr, value);
				case 0x0100:
				case 0x0200:
				case 0x0300:
					if (addr & 0x10)
						via1write(addr, value);
					if (addr & 0x20)
						via2write(addr, value);
					break;
				case 0x0400: // color RAM
				case 0x0500:
				case 0x0600:
				case 0x0700:
					Ram[addr] = value;
					break;
			}
			break;
		case 0xA000:
		case 0xB000:
			if (ramExpSizeKb >= 32)
				Ram[addr] = value;
			break;
		case 0xC000:
		case 0xD000:
		case 0xE000:
		case 0xF000:
			break;
	}
}

unsigned char Vicmem::via1read(unsigned int addr)
{
	switch (addr & 0xf) {
	case 0x01:
	case 0x0F:
		//fprintf(stderr, "addr(%04X): %02X\n", addr, via[0].read(1) & (keysvic->getJoyState(0) | ~0x3C));
		return via[0].read(1) &
			(keysvic->getJoyState(0) | ~0x3C) & ((readBus() >> 6) ^ 0xFC) & (tap->IsButtonPressed() ? ~0x40 : 0xFF);
	default:
		return via[0].read(addr);
	}
}

unsigned char Vicmem::via2read(unsigned int addr)
{
	switch (addr & 0xf) {
	case 0x00:
		//fprintf(stderr, "(%04X): %02X\n", addr, via[1].read(0) & (keysvic->getJoyState(0) | 0x7F));
		return via[1].read(0) & (keysvic->getJoyState(0) | 0x7F)
			& keysvic->feedKeyColumn(via[1].pra | ~via[1].ddra) // FIXME?
			;
	case 0x01:
	case 0x0F:
		return via[1].read(1) & keysvic->feedkey(via[1].prb | ~via[1].ddrb);
	default:
		return via[1].read(addr);
	}
}

void Vicmem::via1write(int addr, int value)
{
	switch (addr & 0xf) {

	case 0x01:
	case 0x0F:
		// ATN OUT -> ATN IN (drive)
		serialPort[0] = (serialPort[0] & ~0x10) | (((value & 0x80) ^ 0x80) >> 3);
		updateSerialDevices(serialPort[0]);
		via[0].write(addr, value);
		break;
	case 0x0C:
		// CA1 output
		if (via[0].pcr ^ value && (value & 0x0C) == 0x0C) {
			tap->setTapeMotor(CycleCounter*2, !(value & 0x02));
		}
	default:
		via[0].write(addr, value);
		break;
	}
}

void Vicmem::via2write(int addr, int value)
{
	switch (addr & 0xf) {
	case 0x00:
		/*if (((value ^ via[1].reg[0]) & 0x08 & via[1].reg[2]) && tap->isMotorOn()) {
			tap->writeCSTOut(CycleCounter*2,!(value & 0x08));
		}*/
		via[1].write(0, value);
		break;
	case 0x0C:
		if (value != via[1].pcr)
		{
			unsigned char clock = 0;
			unsigned char data = 0;
			if ((value & 0x0A) == 0x08) {
				clock = 0x40;
			}
			if ((value & 0xA0) == 0x80) {
				data = 0x80;
			}
			serialPort[0] = (serialPort[0] & ~0xC0) | clock | data;
			updateSerialDevices(serialPort[0]);
		}
		// fall thru
	default:
		via[1].write(addr, value);
	}
}

inline void Vicmem::fetchMatrixData(unsigned int d)
{
	const unsigned int curr_pos = (CharacterPosition + x);
	clrbuf[d] = Read(0x9400 | ((curr_pos + scrclr_bit9) & 0x03FF));
	const unsigned int offset = videoBaseValue + curr_pos;
	const unsigned int vbase = ((offset & 0x3FFF) > 0x2000) ? (offset ^ 0x8000) & 0x83FF : offset;
	chrbuf[d] = Read(vbase);
	dataRead[0] = chrbuf[d];
}

inline void Vicmem::fetchBitmapData(unsigned int d)
{
	const unsigned int maskaddr = (chrbuf[d] * charHeight) | vertSubCount;
	chrbuf[d] = fetchCharBitmap(maskaddr);
}

inline void Vicmem::renderBitmap(unsigned char* scr, unsigned int d, const unsigned int half)
{
	unsigned char mask = chrbuf[d];
	unsigned char charcol = clrbuf[d];

	if (!half) mask >>= 4;

	if (clrbuf[d] & 0x8) { // if character is multicolored
		mcol[CHARS] = charcol & 0x7;
		scr[0] = scr[1] = scr[2] = scr[3] = mcol[(mask & 0x0C) >> 2];
		scr[4] = scr[5] = scr[6] = scr[7] = mcol[mask & 0x03];
	}
	else { // this is a normally colored character
		if (!reverseMode)
			mask ^= 0xFF;
		charcol &= 0x7;
		scr[0] = scr[1] = (mask & 8) ? charcol : mcol[SCRN];
		scr[2] = scr[3] = (mask & 4) ? charcol : mcol[SCRN];
		scr[4] = scr[5] = (mask & 2) ? charcol : mcol[SCRN];
		scr[6] = scr[7] = (mask & 1) ? charcol : mcol[SCRN];
	}
}

//
// 00 = screen colour
// 01 = character colour
// 10 = border colour
// 11 = auxiliary colour
// multicolour: character colour b7 = 1
//
inline void Vicmem::render4px(unsigned char *scr, const unsigned int x, const unsigned int half)
{
	const unsigned int curr_pos = (CharacterPosition + x);
#ifndef VIC_READS_TRANSLATED
	unsigned int charcol = colorRam_[(0x9400 | scrclr_bit9) + curr_pos];
	const unsigned char chr = VideoBase[curr_pos];
#else
	unsigned int charcol = Ram[0x9400 | ((curr_pos + scrclr_bit9) & 0x03FF)];
	const unsigned int offset = videoBaseValue + curr_pos;
	const unsigned int vbase = ((offset & 0x3FFF) > 0x2000) ? (offset ^ 0x8000) & 0x83FF : offset;
	const unsigned char chr = Read(vbase);
#endif
	const unsigned int maskaddr = (chr * charHeight) | vertSubCount;
	unsigned char mask;

#ifndef VIC_READS_TRANSLATED
	if (maskaddr > csetmask)
		mask = cset2[maskaddr & csetmask];
	else
		mask = cset[maskaddr];
#else
	mask = fetchCharBitmap(maskaddr);
	dataRead[0] = (beamx & 2) ? mask : chr;
#endif

	if (!half) mask >>= 4;

	if (charcol & 0x8) { // if character is multicolored

		firstmcol[CHARS] = mcol[CHARS] = charcol & 0x7;

		scr[0] = scr[1] = firstmcol[(mask & 0x0C) >> 2];
		scr[2] = scr[3] = mcol[(mask & 0x0C) >> 2];
		scr[4] = scr[5] = scr[6] = scr[7] = mcol[mask & 0x03];

	}
	else { // this is a normally colored character

		if (!reverseMode) 
			mask ^= 0xFF;

		charcol &= 0x7;

		scr[0] = scr[1] = (mask & 8) ? charcol : firstmcol[SCRN];
		scr[2] = scr[3] = (mask & 4) ? charcol : mcol[SCRN];
		scr[4] = scr[5] = (mask & 2) ? charcol : mcol[SCRN];
		scr[6] = scr[7] = (mask & 1) ? charcol : mcol[SCRN];
	}
}

inline void Vicmem::newFrame()
{
	loop_continuous = 0;
	VBlanking = true;
	SideBorderFlipFlop = CharacterWindow = false;
	externalFetchWindow = false;
	vertSubCount = 0;
	rowCount = 0;
	CharacterPositionReload = CharacterPosition = x = 0;
#if 0
	static FILE* fp = fopen("sndlog.bin", "ab+");
	fputc(vicReg[0xA], fp);
	fputc(vicReg[0xb], fp);
	fputc(vicReg[0xc], fp);
	fputc(vicReg[0xd], fp);
#endif
}

inline void Vicmem::incrementVerticalSub()
{
	vertSubCount = (vertSubCount + 1);// &(charHeight - 1);
	if ((charHeight - 0) == vertSubCount || vertSubCount >= 16) {
		rowCount += 1;
		CharacterPositionReload = (CharacterPosition + latchedColumns);
		if (rowCount == (latchedRows >> 1)) {
			SideBorderFlipFlop = false;
		}
		vertSubCount = 0;
	}
}

inline void Vicmem::doHRetrace()
{
	TVScanLineCounter += 1;
	if (TVScanLineCounter >= SCR_VSIZE * 2)
		TVScanLineCounter = 0;
	scrptr = screen + TVScanLineCounter * clocks_per_line * 8;
	//fprintf(stderr, "Scr:%llu Cycle:%llu TV:%03u beamy:%03u  \n", scrptr, CycleCounter, TVScanLineCounter, beamy);
}

/*
	 0 ?
	 1 (start of horizontal sync)
	 2 ?
	 6 (end of horizontal sync)
	 7 (start of colour burst)
	11 (end of colour burst)
	12 (end of horizontal blanking)
	35 ?
	38 ?
	41 ?
	70 (start of horizontal blanking)

	41,6,2,38 : All enter the same logic gate, which is formed from two cross coupled 3-input NOR gates. 
	So it's a latch but this time with two inputs that can set it to ON and another two inputs that can set it to OFF. 
	The "2" and "38" are connected to one of the NOR gates and the "6" and "41" to the other.
	It is associated with the vertical sync pulses.
*/
void Vicmem::ted_process(const unsigned int continuous)
{
	loop_continuous = continuous;

	do {
		beamx += 1;
		// hsync takes 5 cycles
		// hblank at the end of a line is 3 cycles, and 6 cycles at the beginning
		// of a new line, right after hsync
		switch (beamx) {
			default:
				break;
			case 1:
				if (SideBorderFlipFlop)
					incrementVerticalSub();
				break;
			case 2:
				latchedColumns = nrOfColumns > 32 ? 32 : nrOfColumns;
				if (SideBorderFlipFlop) {
					// Reload in t + 1
					CharacterPosition = CharacterPositionReload;
					x = 0;
				}
				if (screenOriginY == beamy && !(beamy & 1)) {
					SideBorderFlipFlop = true;
				}
				break;
			case 4:
				if (beamy == 0)
					latchedRows = nrOfRows;
				break;
			case 12:
				HBlanking = false;
				if (!VBlanking)
					doHRetrace();
				break;
			case 136: // 134 140 115 96
				CharacterWindow = false;
				HBlanking = true;
				break;
			// NTSC
			// case 130:
			// PAL (clocks_per_line << 1)
			case 142: 
				// sound
				flushBuffer(CycleCounter, VIC20_SOUND_CLOCK);
				beamx = 0;
				beamy++;
				switch (beamy) {
					case 28: // 28 ?
						TVScanLineCounter = 0;
						scrptr = screen;
						VBlanking = false;
						break;
					case 312:
					case 512:
						beamy = 0;
						newFrame();
					default:
						break;
				}
				break;
		}
		// in phi phase2 the CPU owns the bus
		if (beamx & 1) {
			// VIA
			via[0].countTimers();
			via[1].countTimers();
			// CPU
			cpuptr->process();
			// tape
			if (tap->isMotorOn()) {
				if (tap->getFallingEdgeState(CycleCounter * 2)) {
					via[1].ifr |= Via::IRQM_CA1;
					setIRQflag(via[1].ifr & via[1].ier & 0x7F);
					tap->resetFallingEdge(CycleCounter * 2);
				}
			}
			// disk
			unsigned int i = 0;
			while (Clockable::itemHeap[i]) {
				Clockable* c = Clockable::itemHeap[i];
				while (c->ClockCount >= VIC20_REAL_CLOCK_M10) {
					c->ClockCount -= VIC20_REAL_CLOCK_M10;
					c->Clock();
				}
				c->ClockCount += c->ClockRate;
				i++;
			}
			CycleCounter += 1;
		}
		else {
			// State machines
			switch (videoLatchPhase) {
			case 0:
				break;
			case 7:
				CharacterWindow = true;
				//if (!externalFetchWindow) {
				//	// on the first row, latch number of rows
				//	//latchedRows = nrOfRows;
				//	externalFetchWindow = true;
				//}
				videoLatchPhase = 0;
				break;
			default:
				videoLatchPhase += 1;
				break;
			}
			if (beamx == screenOriginX) {
				if (SideBorderFlipFlop) {
					if (!CharacterWindow)
					{
						videoLatchPhase = 1;
					}
				}
			}
			if (CharacterWindow)
			{
				//if (beamx == screenOriginX + (latchedColumns << 2)) {
				if (x == (latchedColumns << 1)) {
					CharacterWindow = false;
				}
			}
			// VIDEO
			if (!(HBlanking || VBlanking)) {
				if (CharacterWindow) { // drawing the visible part of the screen
					// call the relevant rendering function
					render4px(scrptr, x >> 1, x & 1);
					x = (x + 1) & 0xFF;
				}
				else {
					// we are on the border area, so use the frame color
					*((int*)scrptr) = framecol;
					*((int*)(scrptr + 4)) = framecol2;
				}
				//if (scrptr != endptr)
				scrptr += 8;
			}
			if (_delayedEventCallBack) {
				(this->*_delayedEventCallBack)();
			}
		}
	} while (loop_continuous);
} 

// ------------------------------------
// VIC Sound
// ------------------------------------
#define PRECISION	8
#define RELOAD_VAL	(0x7F<<PRECISION)
#define SNDAMP		0x0200

void Vicmem::setSampleRate(unsigned int newFreq)
{
	oscStep = ((VIC20_SOUND_CLOCK) << PRECISION) / newFreq / 2;
	for (unsigned int i = 0; i < 4; i++)
		channel[i].oscCount = 0;
}

void Vicmem::vicSoundInit()
{
	soundReset();
	setSampleRate(sampleRate);
}

inline static int shiftRandom(int p)
{
	p ^= (p >> 3);
	p ^= (p >> 12);
	p ^= (p >> 14);
	p ^= (p >> 15);
	p ^= 1U;
	return p & 1U;
}

inline int Vicmem::doShiftReg(VicChannel &c)
{
	int shiftReg = c.shiftReg;
	shiftReg = (shiftReg << 1) | (((shiftReg ^ 0x80) & c.controlReg) >> 7);
	c.shiftReg = shiftReg;
	return shiftReg;
}

inline void Vicmem::soundDoOsc(int nr, int divisor)
{
	channel[nr].oscCount += divisor;
	// Oscillator
	if (channel[nr].oscCount >= RELOAD_VAL) {
		channel[nr].channelOutput = (doShiftReg(channel[nr]) & 1) ? SNDAMP : 0;
		// relatch counter
		channel[nr].oscCount += channel[nr].oscReload - RELOAD_VAL;
	}
}

void Vicmem::calcSamples(short* buffer, unsigned int nrsamples)
{
	unsigned int i = nrsamples;

	for (; i--;) {
		int divisor = oscStep;
		// Noise SR 4
		channel[3].oscCount += divisor;
		if (channel[3].oscCount >= RELOAD_VAL) {
			int noiseOut = channel[3].flipFlop;
			channel[3].flipFlop = (noiseOut << 1) | (shiftRandom(channel[3].flipFlop) & (channel[3].controlReg >> 7));
			if (!(noiseOut & 1) && (channel[3].flipFlop & 1)) {
				doShiftReg(channel[3]);
			}
			channel[3].channelOutput = (channel[3].shiftReg & 0x80) ? SNDAMP : 0;
			channel[3].oscCount += channel[3].oscReload - RELOAD_VAL;
		}
		// SR 3
		soundDoOsc(2, divisor >> 1);
		// SR 2
		soundDoOsc(1, divisor >> 2);
		// SR 1
		soundDoOsc(0, divisor >> 3);

		int result = channel[0].channelOutput + channel[1].channelOutput + channel[2].channelOutput + channel[3].channelOutput + 10;
		// apply pulse width modulation	but no filtering yet
		*buffer++ = (result * masterVol) >> 1;
	}
}

inline void Vicmem::setFreq(int chnl, int freq)
{
	switch (freq) {
		case 0x07F:
			freq = 0;
		default:
			channel[chnl].oscReload = ((freq + 0) & 0x7F) << PRECISION;
			break;
	}
}

inline void Vicmem::soundWrite(int reg, int value)
{
	flushBuffer(CycleCounter, VIC20_SOUND_CLOCK);

	switch (reg) {
		case 4:
			masterVol = value;
			break;
		default:
			setFreq(reg, value & 0x7F);
			channel[reg].controlReg = value & 0x80;
		break;
	}
}

void Vicmem::soundReset()
{
	masterVol = 0;
	memset(channel, 0, sizeof(channel));
}