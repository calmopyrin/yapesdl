#ifndef _CPU_H
#define _CPU_H

#define REG_AC	AC
#define REG_X	X
#define REG_Y	Y
#define REG_SP	SP
#define REG_PC	PC
#define REG_ST	ST
#define SETFLAGS_ZN(VALUE) ST = (ST&0x7D)|(((VALUE)==0)<<1)|((VALUE)&0x80)

#include "types.h"
#include "mem.h"
#include "SaveState.h"

class TED;

class CPU : public SaveState , public Debuggable {
	protected:
		unsigned char currins;
		unsigned char nextins;
		unsigned char farins;
		unsigned int  ptr;
		unsigned int  PC;
		unsigned char  SP;
						   // 76543210
		unsigned int  ST; // NV1BDIZC
		unsigned char  AC;
		unsigned char  X;
		unsigned char  Y;
		void ADC(const unsigned char value);
		void SBC(const unsigned char value);
		void DoCompare(unsigned char reg, unsigned char value);
		unsigned int cycle;
		unsigned int IRQcount;
		MemoryHandler *mem;
		unsigned char *irq_register;
		unsigned char *stack;
		unsigned char irq_sequence;
		virtual unsigned char CheckVFlag() { return (ST&0x40); };
		inline virtual void ClearVFlag() { ST&=0xBF; };
		inline void SetVFlag() { ST|=0x40; };
		enum {
		    INTERRUPT_NMI = 0xFFFA,
		    INTERRUPT_RESET = 0xFFFC,
		    INTERRUPT_IRQ = 0xFFFE,
		};
		unsigned short irqVector;
		unsigned int nmiLevel;

	public:
		CPU(MemoryHandler *memhandler, unsigned char *irqreg, unsigned char *cpustack);
		virtual ~CPU();
		void Reset(void);
		void softreset(void);
		void setPC(unsigned int addr);
		void process();
		void process(unsigned int clks);
		void stopcycle();
		virtual void step();

		unsigned int getPC() { return PC; };
		unsigned int getSP() { return SP; };
		unsigned int getST() { return ST; };
		unsigned int getAC() { return AC; };
		unsigned int getX() { return X; };
		unsigned int getY() { return Y; };
		unsigned int getnextins() { return nextins; };
		unsigned int getptr() { return ptr; };
		virtual unsigned int getcycle() { return cycle; };
		unsigned int getcins();
		unsigned int getRemainingCycles();
		void setST(unsigned int v) { ST = v; };

		virtual void dumpState();
		virtual void readState();
		// breakpoint variables
		static bool bp_active;
		static bool bp_reached;
		struct {
  			unsigned int address;
  			bool enabled;
  			bool slot_free;
		} bp[11];
		unsigned int nr_activebps;
		bool cpu_jammed;
		static const unsigned int nr_of_bps;
		virtual int disassemble(int pc, char *line);
		virtual unsigned int getProgramCounter() {
			return PC;
		}
		virtual void regDump(char *line, int rowcount);
		virtual MemoryHandler &getMem() { return *mem; };
		void setMem(MemoryHandler *_mem, unsigned char *_i, unsigned char *_s) {
			mem = _mem;
			irq_register = _i;
			stack = _s;
		}
		void triggerNmi() {
		    if (!nmiLevel) {
                irqVector = INTERRUPT_NMI;
                nmiLevel = 1;
                IRQcount = 1;
				irq_sequence = 0x10;
		    }
        }
        void clearNmi() {
            nmiLevel = 0;
        }
		virtual char *getName() {
			return "Main";
		}
};

inline void CPU::ADC(const unsigned char value)
{
	if (ST&0x08) {
		unsigned int bin_adc = AC + value + (ST&0x01);
		unsigned char AL, AH;

		AL=(AC&0x0F) + (value & 0x0F) + (ST&0x01);
		AH=(AC >> 4) + (value >> 4 ) ;
		// fix lower nybble
		if (AL>9) {
			AL+=6;
			AH++;
		}
		// zero bit depends on the normal ADC...
		(bin_adc)&0xFF ? ST&=0xFD : ST|=0x02;
		// negative flag also...
		( AH & 0x08 ) ? ST|=0x80 : ST&=0x7F;
		// V flag
		((((AH << 4) ^ AC) & 0x80) && !((AC ^ value) & 0x80)) ? SetVFlag() : ClearVFlag();
		// fix upper nybble
		if (AH>9)
			AH+=6;
		// set the Carry if the decimal add has an overflow
		(AH > 0x0f) ? ST|=0x01 : ST&=0xFE;
		// calculate new AC value
		AC = (AH<<4)|(AL & 0x0F);
	} else {
		unsigned int bin_adc = AC + value + (ST&0x01);
		(bin_adc>=0x100) ? ST|=0x01 : ST&=0xFE;
		(!((AC ^ value) & 0x80) && ((AC ^ bin_adc) & 0x80) ) ? SetVFlag() : ClearVFlag();
		AC=(unsigned char) bin_adc;
		SETFLAGS_ZN(AC);
	}
}

inline void CPU::SBC(const unsigned char value)
{
	if (ST&0x08) { // if in decimal mode
		unsigned int bin_sbc = (AC - value - (1-(ST&0x01)));
		unsigned int dec_sbc = (AC & 0x0F) - (value & 0x0F) - (1-(ST&0x01));
		// Calculate the upper nybble.
		// fix upper nybble
		if (dec_sbc&0x10)
			dec_sbc = ((dec_sbc-6)&0x0F) | ((AC&0xF0) - (value&0xF0) - 0x10);
		else
			dec_sbc = (dec_sbc&0x0F) | ((AC&0xF0) - (value&0xF0));

		if (dec_sbc&0x100)
			dec_sbc-= 0x60;

		// all flags depend on the normal SBC...
		(bin_sbc<0x100) ? ST|=0x01 : ST&=0xFE ; // carry flag
		SETFLAGS_ZN( bin_sbc & 0xFF );
		((AC^bin_sbc)&0x80 && (AC^value)&0x80 ) ? SetVFlag() : ClearVFlag(); // V flag

		AC=(unsigned char) dec_sbc;

	} else {
		unsigned int bin_sbc = (AC - value - (1-(ST&0x01)));
		(bin_sbc<0x100) ? ST|=0x01 : ST&=0xFE;
		(((AC ^ value) & 0x80) && ((AC ^ bin_sbc) & 0x80) ) ? SetVFlag() : ClearVFlag();
		AC=(unsigned char) bin_sbc;
		SETFLAGS_ZN(AC);
	}
}

class DRIVECPU : public CPU {

	protected:
		unsigned char *flag_v_pin_edge;
		unsigned char *is_so_enable;
		virtual unsigned char CheckVFlag();
		virtual void ClearVFlag();
	public:
		DRIVECPU(MemoryHandler *memhandler, unsigned char *irqreg, unsigned char *cpustack,
			unsigned char *vpin, unsigned char *so_enable, unsigned int id);
		virtual ~DRIVECPU() { };
		virtual char *getName() {
			return getId();
		}
};

#endif // _CPU_H
