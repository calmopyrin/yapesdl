/*
	YAPE - Yet Another Plus/4 Emulator

	The program emulates the Commodore 264 family of 8 bit microcomputers

	This program is free software, you are welcome to distribute it,
	and/or modify it under certain conditions. For more information,
	read 'Copying'.

	(c) 2000, 2001, 2004, 2007, 2015 Attila Grósz
*/
#ifndef _TEDMEM_H
#define _TEDMEM_H

#include "types.h"
#include "mem.h"
#include "serial.h"
#include "sound.h"
#include "SaveState.h"

#define RAMSIZE 65536
#define ROMSIZE 16384
#define SCR_HSIZE 456
#define SCR_VSIZE 312
#define TED_CLOCK (312*114*500)
#define TED_REAL_CLOCK_M10 17734475
#define TED_SOUND_CLOCK (TED_CLOCK / 10 )
#define TED_REAL_SOUND_CLOCK (TED_REAL_CLOCK_M10 / 10 / 8)

#define TEXTMODE	0x00000000
#define MULTICOLOR	0x00000010
#define GRAPHMODE	0x00000020
#define EXTCOLOR	0x00000040
#define REVERSE		0x00000080
#define ILLEGAL		0x0000000F

#define DRAW_BORDER(OFFS,COL) { *((unsigned int *) (scrptr + OFFS)) = COL; };

class TED;
class CPU;
class KEYS;
class TAP;
class CTCBM;
class SIDsound;
struct Color;

typedef void (TED::*delayedEventCallback)();

class TED : public CSerial , public MemoryHandler, public SoundSource, public SaveState {
  public:
	TED();
	virtual ~TED();
	TAP	*tap;
	virtual KEYS *getKeys() { return keys; }
	virtual void UpdateSerialState(unsigned char portval);
	virtual void Reset(unsigned int resetLevel);
	virtual void Reset() { Reset(3); };
	void soundReset();
	// read memory through memory decoder
	virtual unsigned char Read(unsigned int addr);
	virtual void Write(unsigned int addr, unsigned char value);
	virtual unsigned char readOpenAddressSpace(unsigned int addr);
	// read memory directly
	unsigned char readDMA(unsigned int addr) { return Ram[addr]; }
	// same as above but with writing
	void wrtDMA(unsigned int addr, unsigned char value) { Ram[addr]=value; }
	// RAM size
	static void setRamMask(unsigned int value) { RAMMask=value;}
	static void flipRamMask(void *none);
	static unsigned int getRamMask(void) { return RAMMask;}
	// are the ROMs disabled?
	bool RAMenable;
	// RAM expansions
	static unsigned int reuSizeKb;
	static void flipRamExpansion(void* none);
	virtual void enableREU(unsigned int sizekb);
	// /ram/rom path/load variables
	virtual void loadroms(void);
	virtual void loadromfromfile(int nr, const char fname[512], unsigned int offset);
	static char romlopath[4][260];
	static char romhighpath[4][260];
	// this is for the FRE support
	virtual void dumpState();
	virtual void readState();
	// screen rendering
	// raster co-ordinates and boundaries
	unsigned int beamx, beamy;
	unsigned char *screen;
	bool render_ok;
	void texttoscreen(int x, int y, const char *scrtxt);
	void chrtoscreen(int x, int y, unsigned int scrchr);
	virtual void copyToKbBuffer(const char *text, unsigned int length = 0);
	// cursor stuff
	unsigned int crsrpos;
	int crsrphase;
	bool crsrblinkon;
	// CPU class pointer
	CPU	*cpuptr;
	// TED process (main loop of emulation)
	virtual void ted_process(const unsigned int continuous);

	unsigned char Ram[RAMSIZE];
	void ChangeMemBankSetup();

	
	// timer stuff
	bool t1on, t2on, t3on;
	unsigned int timer1, timer2, timer3, t1start;

	virtual void setCpuPtr(CPU *cpu) { cpuptr = cpu; };
	void HookTCBM(CTCBM *pTcbmbus) { tcbmbus = pTcbmbus; };
	ClockCycle GetClockCount();
	void log(unsigned int addr, unsigned int value);
	static TED *instance() { return instance_; };
	unsigned char *getScreenData() { return screen; };
	bool enableSidCard(bool enable, unsigned int disableMask);
	static void toggleSidCard(void *v) {
		sidCardEnabled = !sidCardEnabled;
	}
	SIDsound *getSidCard();
	static void writeSoundReg(ClockCycle cycle, unsigned int reg, unsigned char value);
	void tedSoundInit(unsigned int mixingFreq);
	static size_t usec2cycles(unsigned long usecs) {
		return (unsigned long) (((double) masterClock) / 10000000.0f * (double) usecs);
	}
	//
	void showled(int x, int y, unsigned char onoff);
	virtual unsigned int getColorCount() { return 128; };
	virtual Color getColor(unsigned int ix);
	virtual unsigned int getCyclesPerRow() const { return SCR_HSIZE; }
	virtual unsigned int getCanvasX() const { return SCR_HSIZE; }
	virtual unsigned char *getIrqReg() { return &irqFlag; }
	virtual unsigned int getSoundClock() { return TED_SOUND_CLOCK; }
	virtual unsigned int getRealSlowClock() { return TED_REAL_CLOCK_M10 / clockDivisor / 2; }
	virtual unsigned int getEmulationLevel() { return 0; }
	virtual unsigned int getAutostartDelay() { return 70; }
	virtual unsigned int getHorizontalCount() { return ((98 + beamx) << 1) % 228; }
	virtual unsigned int getVerticalCount() { return beamy; }
	virtual unsigned short getEndLoadAddressPtr() { return 0x9D; };
	virtual unsigned short getKbBufferSizePtr() { return 0xEF; };
	virtual unsigned short getRamSize() { return (RAMMask + 1) / 1024; };
	virtual void setRamSize(unsigned short) { };
	virtual void calcSamples(short *buffer, unsigned int nrsamples);
	virtual void setFrequency(unsigned int sid_frequency);
	virtual void setSampleRate(unsigned int sampleRate_);
	virtual void triggerNMI() { /* no NMI */ };
	void setClockStep(unsigned int originalFreq, unsigned int samplingFreq);
	//
	static unsigned int sidCardEnabled;
	static rvar_t tedSettings[];

private:
	  KEYS *keys;

protected:
	static TED *instance_;
	CTCBM *tcbmbus;
	unsigned int loop_continuous;
	// memory variables
	unsigned char rom[4][ROMSIZE * 2];
	unsigned char *actromlo, *actromhi;
	unsigned char *mem_8000_bfff, *mem_c000_ffff, *mem_fc00_fcff;
	static unsigned int RAMMask;
	unsigned char *actram;
	unsigned char* actramBelow4000;
	// Ram expansion
	unsigned char* ramExt;
	unsigned int reuBank;
	unsigned int reuMemMask;
	void reuWrite(unsigned char value);
	// I/O ports
	unsigned char prp, prddr;
	unsigned char pio1;
	// indicates if screen blank is off
	bool scrblank;
	// for vertical/horizontal smooth scroll
	unsigned int hshift, vshift;
	unsigned int nrwscr, fltscr;
	// char/color buffers
	unsigned char DMAbuf[64*3];
	unsigned char *chrbuf, *clrbuf, *tmpClrbuf;
	// rendering functions
	void	(TED::*scrmode)();
	inline void	hi_text();
	void	mc_text();
	void 	mc_text_rvs();
	void	ec_text();
	void	mcec();
	void	rv_text();
	void	hi_bitmap();
	void	mc_bitmap();
	void	illegalbank();
	void	render();
	bool	charrom;
	int		rvsmode, grmode, ecmode;
	int		scrattr, charbank;
	void	changeCharsetBank();

	// border color
	unsigned int framecol;
	// various memory pointers
	unsigned char *charrombank, *charrambank;
	unsigned char *grbank;
	unsigned char *scrptr, *endptr;
	unsigned int fastmode, irqline;
	unsigned char hcol[2], mcol[4], ecol[4], bmmcol[4], *cset;
	//
	static bool				vertSubIncrAllowed;
	static unsigned int		vertSubCount;
	static int				x;
	static unsigned char	*VideoBase;

	static unsigned int masterClock;
	static ClockCycle CycleCounter;
	static bool ScreenOn, attribFetch, dmaAllowed, externalFetchWindow;
	static bool SideBorderFlipFlop, CharacterWindow;
	static unsigned int BadLine;
	static unsigned int	clockingState;
	static unsigned int	CharacterCount, CharacterCountReload, dmaFetchCountStart;
	static bool VertSubActive;
	static unsigned int	CharacterPosition;
	static unsigned int	CharacterPositionReload;
	//static unsigned int	CharacterPositionCount;
	static unsigned int	TVScanLineCounter;
	static bool HBlanking;
	static bool VBlanking;
	static bool aligned_write;
	static unsigned char *aw_addr_ptr;
	static unsigned char aw_value;
	static unsigned int ff1d_latch;
	static bool charPosLatchFlag;
	static bool endOfScreen;
	static bool delayedDMA;
	static bool displayEnable;
	static unsigned int retraceScanLine;
	//
	void doDMA( unsigned char *Buf, unsigned int Offset  );
	SIDsound *sidCard;
	//
	void doHRetrace();
	void doVRetrace();
	void newLine();
	void setAlignedWrite(unsigned char* addr, unsigned char value) {
		aligned_write = true;
		aw_addr_ptr = addr;
		aw_value = value;
	}
	unsigned int clockDivisor;
	//
	void updateSerialDevices(unsigned char newAtn);
	//
	static const int hue(unsigned int i)
	{
		const int HUE[16] = { -2, -1,
		/*RED*/	103, /*CYAN	*/ 283,
		/*MAGENTA*/	53,/*GREEN*/ 241, /*BLUE*/347,
		/*YELLOW*/ 167,/*ORANGE*/123, /*BROWN*/	148,
		/*YLLW-GRN*/ 195, /*PINK*/ 83, /*BLU-GRN*/ 265,
		/*LT-BLU*/ 323, /*DK-BLU*/ /*23 original, but wrong...*/ 355, /*LT-GRN	*/ 213};
		return HUE[i];
	}
	static const double luma(unsigned int i)
	{
		/*
			Luminance Voltages
		*/
		const double luma[9] = { 2.00, 2.4, 2.55, 2.7, 2.9, 3.3, 3.6, 4.1, 4.95 };
		return luma[i + 1];
	}
};

class TEDFAST : public TED {
public:
	TEDFAST();
	virtual ~TEDFAST() {};
	virtual void ted_process(const unsigned int continuous);
	virtual void process_debug(unsigned int continuous);
	virtual unsigned int getEmulationLevel() { return 1; }
protected:
	virtual unsigned int getHorizontalCount();
private:
	bool endOfDMA;
	inline void countTimers(unsigned int clocks);
	inline void dmaLineBased();
	inline void renderLine();
};

#define theTed TED::instance()

#endif //_TEDMEM_H
