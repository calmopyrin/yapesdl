#pragma once

#include "mem.h"
#include "cpu.h"

template <typename T>
class StaticList
{
public:
    static const unsigned int maxSlots = 16;
    StaticList(unsigned int dev, T* instance) {
		if (!refCount) {
			for(unsigned int i = 0; i < maxSlots; i++) {
				itemHeap[i] = item[i] = 0;
			}
		}
		if (dev < maxSlots) {
            item[dev] = instance;
        }
        slotNr = dev;
		itemHeap[refCount] = instance;
		refCount++;
    }
    ~StaticList() {
        item[slotNr] = NULL;
		refCount--;
		itemHeap[refCount] = NULL;
    }
	static T *itemHeap[maxSlots];
protected:
	static unsigned int refCount;
    static T *item[maxSlots];
    unsigned int slotNr;

	friend class TED;
};

template <typename T> unsigned int StaticList<T>::refCount = 0;
template <typename T> T *StaticList<T>::item[StaticList::maxSlots];
template <typename T> T *StaticList<T>::itemHeap[StaticList::maxSlots];

class DRVMEM;
class CPU;
class Mem;
class Cpu;

class Clockable : public StaticList<Clockable>
{
public:
	Clockable(unsigned int dn) : StaticList<Clockable>(dn, this), devNr_(dn) {
	}
	virtual ~Clockable() {}
	// Clock the drive (1 cycle)
	//virtual
		void Clock();
	// Clock the drive (n cycles)
	//virtual
		void Clock(unsigned int n);
	inline unsigned int getDevNr() {
		return devNr_;
	}
	inline unsigned int getClockRate() {
		return ClockRate;
	}
	inline unsigned long getClockCount() {
		return ClockCount;
	}
	DRVMEM *getDriveMemHandler() {
		return static_cast<DRVMEM*>(Mem);
	};
	// Obtain pointer to the CPU object
	CPU *getDriveCpu() {
		return static_cast<CPU*>(Cpu);
	};
	unsigned long ClockRate;
	unsigned long ClockCount;

protected:
	CPU *Cpu;
	DRVMEM *Mem;
	unsigned int devNr_;
};

inline void Clockable::Clock(unsigned int n)
{
	do {
		Mem->EmulateTick();
		Cpu->process();
	} while (--n);
}

inline void Clockable::Clock()
{
	Mem->EmulateTick();
	Cpu->process();
}
