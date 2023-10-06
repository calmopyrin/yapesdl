#pragma once

#include "tedmem.h"
#include "Via.h"

class KEYSVIC;
struct Color;

#define FAST_BOOT 1
#define VIC20_PIXELS_PER_ROW 568
#define VIC20_REAL_CLOCK_M10 11084080
#define VIC20_REAL_CLOCK_NTSC_M10 10227272
#define VIC20_SOUND_CLOCK (312*71*50)
#define VIC20_REAL_SOUND_CLOCK (VIC_REAL_CLOCK_M10 / 8 / 10)

class Vicmem : public TED
{
	public:
		Vicmem();
		virtual ~Vicmem();
		virtual KEYS *getKeys() { return (KEYS*) keysvic; }
		virtual void loadroms();
		virtual void Reset(unsigned int resetLevel);
		virtual void soundReset();
		// read memory through memory decoder
		virtual unsigned char Read(unsigned int addr);
		virtual void Write(unsigned int addr, unsigned char value);
		virtual void poke(unsigned int addr, unsigned char data) { Ram[addr & 0xffff] = data; }
		virtual void ted_process(const unsigned int continuous);
		virtual unsigned int getColorCount() { return 128; };
		virtual Color getColor(unsigned int ix);
		virtual unsigned int getCyclesPerRow() const { return clocks_per_line * 8; }
		virtual unsigned int getCanvasX() const { return 580; }
		virtual void copyToKbBuffer(const char *text, unsigned int length = 0);
		virtual unsigned short getKbBufferSizePtr() { return 0xC6; };
		virtual unsigned short getRamSize() { return 3 + ramExpSizeKb; };
		virtual void setRamSize(unsigned short s) { ramExpSizeKb = s >= 3 ? s - 3 : 3; };
		virtual void calcSamples(short* buffer, unsigned int nrsamples);
		virtual void setSampleRate(unsigned int newFreq);
		virtual unsigned int getSoundClock() { return VIC20_SOUND_CLOCK; }
		virtual unsigned int getRealSlowClock() { return VIC20_REAL_CLOCK_M10 / 5 / 2; }
		virtual unsigned int getEmulationLevel() { return 3; }
#if !FAST_BOOT
		virtual unsigned int getAutostartDelay() { return 175; }
#else
		virtual unsigned int getAutostartDelay() { 
			switch (ramExpSizeKb) {
			default:
			case 0:
				return 125;
			case 3:
				return 125;
			case 8:
				return 125;
			case 16:
				return 125;
			case 24:
				return 140;
			}
		}
#endif
		virtual unsigned short getEndLoadAddressPtr() { return 0xAE; };
		virtual unsigned int getHorizontalCount() { return beamx; }
		// this is for the savestate support
		virtual void dumpState();
		virtual void readState();
		void cartRemove();
		void cartDetach(bool emptyUpper);
		virtual void loadromfromfile(int nr, const char fname[512], unsigned int offset);
		virtual void triggerNMI();
		//virtual void enableREU(unsigned int sizekb);
		//
		void vic6560write(unsigned int addr, unsigned char value);
		unsigned char vic6560read(unsigned int addr);
		unsigned char via1read(unsigned int addr);
		unsigned char via2read(unsigned int addr);
		void via1write(int addr, int value);
		void via2write(int addr, int value);
		void fetchMatrixData(unsigned int);
		void fetchBitmapData(unsigned int d);
		void renderBitmap(unsigned char* scr, unsigned int d, const unsigned int half);
		unsigned char fetchCharBitmap(unsigned int cpos);
		unsigned char* changeVideoBase(int address);
		delayedEventCallback _delayedEventCallBack;
		void updateColorRegs();
		void updateFirstPixelColor();
		unsigned char firstmcol[4];
		unsigned int framecol2;
		//
		static rvar_t vicSettings[];
		static unsigned int ramExpSizeKb;

	protected:
		Via via[2];
		KEYSVIC *keysvic;
		static void checkIrq(void* cptr, unsigned char m)
		{
			Vicmem* mh = reinterpret_cast<Vicmem*>(cptr);
			mh->setIRQflag(m);
		}
		static void checkNmi(void* cptr, unsigned char m);
		void setIRQflag(unsigned char m) {
			if (m)
				irqFlag |= 0x80;
			else
				irqFlag &= ~0x80;
		}
		//
		void newFrame();
		void incrementVerticalSub();
		void doHRetrace();
		void changeCharsetPtr(int value);
		unsigned char readUnconnected(unsigned int addr, bool isCpu = true);
		void render4px(unsigned char* scr, const unsigned int x, const unsigned int half);
		// Colours
		enum { SCRN = 0, BORDER, CHARS, AUX };
		//
		// VIC registers
		//
		unsigned char vicReg[16];
		unsigned int interlaceMode;
		unsigned int screenOriginX;
		unsigned int screenOriginY;
		unsigned int nrOfColumns;
		unsigned int nrOfRows;
		unsigned int charHeight;
		unsigned int scrclr_bit9;
		// video parameters
		unsigned int clocks_per_line;
		// Video rendering
		unsigned char* colorRam_;
		unsigned int vicCharsetBase;
		unsigned int videoBaseValue;
		unsigned int csetmask;
		unsigned int rowCount;
		unsigned int latchedColumns;
		unsigned int latchedRows;
		// States
		unsigned int reverseMode;
		unsigned char dataRead[3];
		unsigned int videoLatchPhase;
		bool cartAttached;
		// Sound
		struct VicChannel {
			int oscReload;
			int oscCount;
			int shiftReg;
			int flipFlop;
			int controlReg;
			int channelOutput;
		} channel[4];
		int oscStep;
		int masterVol;
		void soundWrite(int reg, int value);
		void soundDoOsc(int nr, int divisor);
		void setFreq(int chnl, int freq);
		int doShiftReg(VicChannel& c);
		void vicSoundInit();
		//
		static void flipVicramExpansion(void* none);
};