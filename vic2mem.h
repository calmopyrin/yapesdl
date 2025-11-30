#ifndef VIC2MEM_H
#define VIC2MEM_H

#include "tedmem.h"
#include "Cia.h"

class KEYS64;
struct Color;

#define FAST_BOOT 1
#define VIC_PIXELS_PER_ROW 504
#define VIC_REAL_CLOCK_M10 9852480 // 9852480 19704960
#define VIC_SOUND_CLOCK (312*63*50)
#define VIC_REAL_SOUND_CLOCK (VIC_REAL_CLOCK_M10 / 8 / 10)

class Vic2mem : public TED
{
	public:
		Vic2mem();
		virtual ~Vic2mem();
		virtual KEYS *getKeys() { return (KEYS*) keys64; }
		virtual void loadroms();
		virtual void Reset(unsigned int resetLevel);
		virtual void soundReset();
		// read memory through memory decoder
		virtual unsigned char Read(unsigned int addr);
		virtual void Write(unsigned int addr, unsigned char value);
		virtual void poke(unsigned int addr, unsigned char data) { Ram[addr & 0xffff] = data; }
		virtual void ted_process(const unsigned int continuous);
		virtual void setCpuPtr(CPU *cpu);
		//virtual unsigned int getColorCount() { return 256; };
		virtual Color getColor(unsigned int ix);
		virtual unsigned int getCyclesPerRow() const { return 504; }
		virtual unsigned short getKbBufferSizePtr() { return 0xC6; };
		virtual unsigned short getRamSize() { return 64; };
		virtual unsigned int getCanvasX() const { return 580; }
		void latchCounters();
		virtual void copyToKbBuffer(const char *text, unsigned int length = 0);
		virtual unsigned int getSoundClock() { return VIC_SOUND_CLOCK; }
		virtual unsigned int getRealSlowClock() { return VIC_REAL_CLOCK_M10 / 10 / 2; }
		virtual unsigned int getEmulationLevel() { return 2; }
#if !FAST_BOOT
		virtual unsigned int getAutostartDelay() { return 175; }
#else
		virtual unsigned int getAutostartDelay() { return 50; }
#endif
		virtual unsigned short getEndLoadAddressPtr() { return 0xAE; };
		virtual unsigned int getHorizontalCount() { return beamx; }
		// this is for the savestate support
		virtual void dumpState();
		virtual void readState();
		virtual void loadromfromfile(int nr, const char fname[512], unsigned int offset);
		virtual void enableREU(unsigned int sizekb);
		virtual void triggerNMI();
		virtual void setNtscMode(bool on) {};
		virtual bool isNtscMode() { return false; };

	protected:
		void doHRetrace();
		void newLine();
		void changeCharsetBank();
		void checkIRQflag();
		void doDelayedDMA();
		void UpdateSerialState(unsigned char newPort);
		unsigned char vicReg[0x40];
		//
		struct Mob {
			bool enabled;
			bool multicolor;
			bool priority;
			unsigned int expandX;
			unsigned int expandY;
			unsigned int y;
			unsigned int x;
			unsigned char color;
			unsigned int dataCount;
			unsigned int dataCountReload;
			unsigned int dataAddress;
			unsigned int reloadFlipFlop;
			bool dmaState;
			bool rendering;
			bool finished;
			union SpriteDma {
				unsigned char shiftRegBuf[4];
				unsigned int dwSrDmaBuf;
			} sdb[2];
		} mob[8];
		unsigned char spriteCollisionReg;
		unsigned char spriteBckgCollReg;
		unsigned char spriteCollisions[512 + 48];
		unsigned char spriteBckgColl[512 + 48];
		unsigned char collisionLookup[256];
		unsigned char mobExtCol[4];
		void renderSprite(unsigned char *in, unsigned char *out, Mob &m, unsigned int cx, const unsigned int six);
		void drawSpritesPerLine(unsigned char* lineBuf);
		bool checkSpriteDMA(unsigned int i);
		//
		Cia cia[2];
		static void setCiaIrq(void *param);
		static void setCiaNmi(void *param);
		unsigned char *vicBase;
		void	hi_text();
		void	mc_text();
		void	ec_text();
		void	mcec();
		void	hi_bmec();
		void	mc_bmec();
		void	hi_bitmap();
		void	mc_bitmap();
		void render();
		unsigned char *colorRAM;
		KEYS64 *keys64;
	private:
		unsigned char portState;
		unsigned int dmaCount;
		ClockCycle vicBusAccessCycleStart;
		unsigned int spriteDMAmask;
		void doXscrollChange(unsigned int oldXscr, unsigned int newXscr);
		unsigned char readFloatingBus(unsigned int adr);
		void checkSpriteEnable();
		void stopSpriteDMA();
		void spriteReloadCounters();
		unsigned int lpLatchX, lpLatchY;
		bool lpLatched;
		unsigned int gamepin, exrom;
		void changeMemoryBank(unsigned int port, unsigned int ex, unsigned int game);
		unsigned char *mem_8000_9fff;
		unsigned char *mem_1000_3fff; // for Ultimax mode
		unsigned int getVicBaseAddress();
		unsigned int crtType;
		// REU
		class REU : public MemoryHandler, public SaveState {
			REU(unsigned int sizekb, TED *machine_);
			~REU();
			void initRam(unsigned int sizekb);
			unsigned int sizeKb;
			unsigned int sizeInBytes;
			unsigned int memMask;
			unsigned char status;
			unsigned char command;
			unsigned int xferReady;
			unsigned int bank;
			unsigned int bankMask;
			unsigned int baseAddress;
			unsigned int machineBaseAddress;
			unsigned int transferLen;
			unsigned char imr;
			unsigned char acr;
			unsigned char regs[16];
			unsigned char* ram;
			void doCommand(unsigned int cmd);
			void doTransfer(unsigned int type);
			virtual void Write(unsigned int addr, unsigned char data);
			virtual unsigned char Read(unsigned int addr);
			virtual void Reset();
			// inherited from SaveState
			virtual void dumpState();
			virtual void readState();
			//
			void startDMA();
			TED *machine;
			
			friend Vic2mem;
		};
		REU* reu;
};

#endif // VIC2MEM_H
