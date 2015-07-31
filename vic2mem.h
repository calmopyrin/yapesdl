#ifndef VIC2MEM_H
#define VIC2MEM_H

#include "tedmem.h"

class KEYS64;
struct Color;

#define VIC_PIXELS_PER_ROW 504
#define VIC_REAL_CLOCK_M10 9852480 // 9852480 19704960
#define VIC_SOUND_CLOCK (VIC_REAL_CLOCK_M10 / 8 / 10)

class Vic2mem : public TED
{
    public:
        Vic2mem();
        virtual ~Vic2mem();
		virtual KEYS *getKeys() { return (KEYS*) keys64; }
		virtual void loadroms();
        virtual void Reset(bool clearmem);
        virtual void soundReset();
        // read memory through memory decoder
        virtual unsigned char Read(unsigned int addr);
        virtual void Write(unsigned int addr, unsigned char value);
		virtual void poke(unsigned int addr, unsigned char data) { Ram[addr & 0xffff] = data; }
        virtual void ted_process(const unsigned int continuous);
		//virtual unsigned int getColorCount() { return 256; };
		virtual Color getColor(unsigned int ix);
		virtual unsigned int getCyclesPerRow() const { return 504; }
		virtual unsigned char *getIrqReg() { return &irqFlag ; }
		virtual void copyToKbBuffer(const char *text, unsigned int length = 0);
		virtual unsigned int getSoundClock() { return VIC_SOUND_CLOCK; }
		virtual unsigned int getEmulationLevel() { return 2; }

    protected:
		void doHRetrace();
		void newLine();
		void changeCharsetBank();
		void checkIRQflag();
		void doDelayedDMA();
		unsigned char vicReg[0x40];
		//
		struct Mob {
			bool enabled;
			bool dmaOn;
			bool multicolor;
			bool priority;
			bool expandX;
			bool expandY;
			unsigned char y;
			unsigned short x;
			unsigned char color;
			unsigned char data[63];
			unsigned int linesLeft;
			unsigned int dataCount;
			unsigned char *address;
		} mob[8];
		unsigned char mobExtCol[2];
		//
		struct CIA {
			unsigned char pra;
			unsigned char prb;
			unsigned char prbTimerToggle;
			unsigned char prbTimerOut;
			unsigned char ddra;
			unsigned char ddrb;
			unsigned short ta;
			unsigned short tb;
			unsigned short latcha;
			unsigned short latchb;
			unsigned char cra;
			unsigned char crb;
			unsigned char sdr;
			unsigned int sdrShiftCnt;
			unsigned char icr;
			unsigned char irq_mask;
			struct TOD {
                unsigned int sec;
                unsigned int min;
                unsigned int tenths;
                unsigned int halt;
                unsigned int hr;
				unsigned int ampm;
                bool latched;
			} alm, tod, todLatch;
			unsigned int todIn;
			unsigned char reg[16];
			void reset();
			void write(unsigned int addr, unsigned char value);
			unsigned char read(unsigned int addr);
			void setIRQflag(unsigned int mask);
			void countTimers();
			void todUpdate();
			static unsigned int bcd2hex(unsigned int bcd);
			static unsigned int hex2bcd(unsigned int hex);
			static unsigned int tod2frames(TOD &todin);
			static void frames2tod(unsigned int frames, TOD &todout, unsigned int frq);
			unsigned int todCount, alarmCount;
		} cia[2];
		unsigned char *vicBase;
		void	hi_text();
		void	mc_text();
		void	ec_text();
		void	mcec();
		void	hi_bitmap();
		void	mc_bitmap();
		void render();
		unsigned char colorRAM[0x0400];
		KEYS64 *keys64;
    private:
};

#endif // VIC2MEM_H
