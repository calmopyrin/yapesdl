#ifndef _TCBM_H
#define _TCBM_H

#ifdef _WIN32
#include <SDL/SDL.h>
#else
#include <SDL2/SDL.h>
#endif
#include "mem.h"

class CTCBM : public MemoryHandler {
  protected:
	struct {
		unsigned char pra; // data line
		unsigned char prb; // status line
		unsigned char prc; // handshake line
		unsigned char ddra;
		unsigned char ddrb;
		unsigned char ddrc;
		// these are only present in a 6525...
		unsigned char cr;	// control register
		unsigned char air;	// active interrupt register
		unsigned int mode;  // in mode 1 DDRC and PRC behaves differently
	} tia;
  public:
    virtual ~CTCBM() {};
	virtual void Write(unsigned int addr, Uint8 value) = 0;
	virtual Uint8 Read(unsigned int) = 0;
	virtual void Reset() = 0;
};

#include "iec.h"

class CFakeTCBM : public CTCBM {
  protected:
	// possible TCBM states
	enum { IEC_IDLE = 0, IEC_OUTPUT, IEC_COMMAND, IEC_SECONDARY	};
	// TCBM statuses
	enum { TCBM_OK = 0, TCBM_ERROR = 0x03 };
	Uint8 Data;
	Uint8 Status;
	Uint8 HandShake;
	unsigned int State;
    class CIECInterface *iec;
  public:
  	void AddIECInterface(CIECInterface *pIEC) { iec = pIEC; };
  	virtual void Write(unsigned int addr, Uint8 data);
	virtual Uint8 Read(unsigned int);
	virtual void Reset();
};

class CRealTCBM : public CTCBM {
  protected:
	Uint8 Data;
	Uint8 Status;
	Uint8 HandShake;
	unsigned int State;
  public:
  	virtual void Write(unsigned int addr, Uint8 data);
	virtual Uint8 Read(unsigned int);
	virtual void Reset();
};

#endif // _TCBM_H

