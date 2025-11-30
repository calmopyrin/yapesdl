#ifndef _MEM_H
#define _MEM_H

#include "types.h"

// Abstract Memory handler superclass for all memory handler objects
class MemoryHandler {
public:
	virtual ~MemoryHandler() {};
	virtual void Write(unsigned int addr, unsigned char data) = 0;
	virtual unsigned char Read(unsigned int addr) = 0;
	virtual void Reset() = 0;
	virtual void poke(unsigned int addr, unsigned char data) { Write(addr, data); }
//protected:
	unsigned char irqFlag;
};

class DRVMEM : public MemoryHandler {

	private:

	protected:
		unsigned int devnr;

	public:
		DRVMEM() { };
		virtual ~DRVMEM() { };
		virtual void Reset() = 0;
		virtual void EmulateTick() = 0;
		virtual unsigned char getLED() = 0;
		// generic varables for drives
		bool bus_state_change;
		virtual void setNewRom(unsigned char *newRom) = 0;
};

#endif // _MEM_H
