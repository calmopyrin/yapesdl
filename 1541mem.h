#ifndef _1541MEM_H
#define _1541MEM_H

#include "mem.h"
#include "Via.h"
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
	Via via[2];
	unsigned char oldAtnIn;
	unsigned char ppIn; // Parallel cable input
	static void checkIrq(void* cptr, unsigned char m)
	{
		DRIVEMEM* mh = reinterpret_cast<DRIVEMEM*>(cptr);
		mh->SetIRQflag(m);
	}
};

/*
   Set the IRQ flag according to the masks provided
*/
inline void DRIVEMEM::SetIRQflag( unsigned int mask )
{
	irqFlag = (mask & 0x7F) ? 1 : 0;
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
