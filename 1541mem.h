#ifndef _1541MEM_H
#define _1541MEM_H

#include "mem.h"
#include "serial.h"
#include "SaveState.h"

class FdcGcr;

class DRIVEMEM : public DRVMEM, public CTrueSerial, public SaveState {

public:
	DRIVEMEM( FdcGcr *_fdc,
		unsigned char *Ramptr, unsigned char *Rom, unsigned int dev_num);
	//~DRIVEMEM();
	// all the virtual functions from class MEMORY
	virtual unsigned char Read(unsigned int addr);
	virtual void Write(unsigned int addr, unsigned char value);
	virtual unsigned char read_zp(unsigned int addr) { return Ram[addr]; };
	virtual void wrt_zp(unsigned int addr, unsigned char value) { Ram[addr] = value; };
	// virtual functions from class DRVMEM
	virtual void Reset();
	virtual void EmulateTick();
	virtual void setNewRom(unsigned char *newRom) {
		rom = newRom;
	};
	// virtual functions from class CSerial
	// Update serial lines after change
	virtual void UpdateSerialState(unsigned char newAtn);
	// This is for parallelized 1541 drives
	virtual void UpdateParallelState(unsigned char value) {};
	virtual unsigned char ReadParallelState() { return 0xFF; };
	// Set DATA after ATN low
	void ATNlow();

	inline unsigned char *get_via2pcr() { return &(via[1].pcr); };
	virtual unsigned char getLED() { return (via[1].prb&0x0C); };
	// this is for the FRE support
	virtual void dumpState();
	virtual void readState();

protected:

	unsigned char ReadVIA(unsigned int adr);
	void SetIRQflag( unsigned int mask );
	void CountTimers();
	void UpdateSerialPort();
	// Pointer to RAM
	unsigned char *Ram;
	// Pointer to ROM
	unsigned char *rom;
	FdcGcr *fdc;		// Pointer to drive _fdc object

	struct VIA {
		unsigned char pra, ddra, prb, ddrb;
		unsigned short t1c, t1l, t2c, t2l;
		unsigned char sr, acr, pcr, ifr, ier;
		unsigned char reg[16];
	} via[2];
	bool via2_t2to_enable;		// VIA 2 timer 2 timeout IRQ enable
	unsigned char oldAtnIn;
	unsigned char ppIn; // Parallel cable input
};

/*
   Set the IRQ flag according to the masks provided
*/
inline void DRIVEMEM::SetIRQflag( unsigned int mask )
{
	mask & 0x7F ? irqFlag = 1 : irqFlag = 0;
}

/*
   Interrupt on 1 -> 0 transition (negative edge) of ATN on serial IEC bus
*/
inline void DRIVEMEM::ATNlow()
{
	via[0].ifr |= 0x02;
	SetIRQflag(via[0].ifr & via[0].ier);
}

class MEM1541P :
	public DRIVEMEM
{
public:
	MEM1541P( FdcGcr *_fdc, unsigned char *Ramptr, unsigned char *Rom, unsigned int dev_num)
		: DRIVEMEM(_fdc, Ramptr, Rom, dev_num) {}
	// This is for parallel 1541 drives
	virtual void UpdateParallelState(unsigned char value) {
		ppIn = value;
	};
	virtual unsigned char ReadParallelState() {
		return via[0].pra;
	};
};

#endif // _1541MEM_H
