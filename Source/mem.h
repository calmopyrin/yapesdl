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

class RomHandler : public MemoryHandler {
private:
protected:
	unsigned char *mem;
	unsigned int mask;
	bool allocate;
public:
	RomHandler(const unsigned int size) : allocate(true) {
		mem = new unsigned char[size];
		mask = size - 1;
	}
	RomHandler(unsigned char *ram_, const unsigned int size) : allocate(false) {
		mem = ram_;
		mask = size - 1;
	}
	virtual ~RomHandler() {
		if (allocate)
			delete[] mem;
	};
	virtual void Write(unsigned int addr, unsigned char data) { };
	virtual unsigned char Read(unsigned int addr) {
		return mem[addr & mask];
	}
};

class RamHandler : public RomHandler {
private:
protected:
public:
	virtual void Write(unsigned int addr, unsigned char data) {
		mem[addr & mask] = data;
	}
};

class CartMem : public MemoryHandler {
private:
	unsigned char *romBank[4];
	unsigned int activeBank;
protected:
public:
	CartMem(const unsigned int nofbanks) : activeBank(0) {
		*romBank = new unsigned char[16384];
	};
	virtual ~CartMem() {
		delete[] * romBank;
	};
};

#endif // _MEM_H
