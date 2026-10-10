// -----------------------------------------------------------------------------
// 6502/6510/7501/8501 CPU core - cycle exact bus behaviour
//
// Every call of process() executes exactly ONE bus cycle of the current
// instruction and performs the bus access of the real NMOS 6502 in that cycle:
//
//  - the opcode is fetched in cycle 1, the operand bytes in the cycles that
//    follow (nothing is prefetched), so the data bus carries the right byte
//    in every cycle
//  - read-modify-write instructions write the old value back first
//  - stopcycle() is the RDY-low variant of process(): read cycles stall
//    (nothing happens), write cycles proceed
//  - the "dummy" reads of the internal cycles (indexed / indirect addressing,
//    implied instructions, branches) are only done when CPU_DUMMY_READS is 1;
//    they cost time and only matter if the bus latch / I/O side effects of
//    those reads are emulated
//  - the stack page is accessed directly through the 'stack' pointer,
//    define CPU_STACK_VIA_BUS 1 to send stack cycles to memory
//
// Speed: the decoder is resolved at compile time. step<mode, operation, rdy>()
// is instantiated for each opcode and process() dispatches with one switch,
// so there is no run time table lookup or mode/operation decoding.
// -----------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include "cpu.h"

// one function per opcode (resolved at compile time) is faster out of line
#ifndef CPU_NOINLINE
#if defined(_MSC_VER)
#define CPU_NOINLINE __declspec(noinline)
#elif defined(__GNUC__)
#define CPU_NOINLINE __attribute__((noinline))
#else
#define CPU_NOINLINE
#endif
#endif

#ifndef CPU_DUMMY_READS
#define CPU_DUMMY_READS 0
#endif

bool CPU::bp_active = false;
bool CPU::bp_reached = false;
const unsigned int CPU::nr_of_bps = 11;
static unsigned int stats[256];
template<> unsigned int LinkedList<Debuggable>::count = 0;
template<> Debuggable* LinkedList<Debuggable>::root = 0;
template<> Debuggable* LinkedList<Debuggable>::last = 0;

CPU::CPU( MemoryHandler *memhandler, unsigned char *irqreg, unsigned char *cpustack) 
	: mem(memhandler), irq_register(irqreg), stack(cpustack)
{
	irq_sequence = 0;
	IRQcount = 0;
	cpu_jammed = false;
	PC = 0xFFFF;
	for(unsigned int i=0; i < nr_of_bps; i++) {
		bp[i].address = 0x10000;
		bp[i].enabled = false;
		bp[i].slot_free = true;
	}
	nr_activebps = 0;
	memset( stats, 0, sizeof(stats));
	irqVector = INTERRUPT_IRQ;
	nmiLevel = 0;
	setId("CPU0");
}

unsigned int CPU::getcins()
{
	// currins is set to 0x00 when in IRQ so we have to re-fetch the opcode
	// this is only used for debugging & co.
	return (currins==0) ?  mem->Read((PC-1)&0xFFFF) : currins&0xFF;
}

void CPU::dumpState()
{
	saveVar(&currins,sizeof(currins));
	saveVar(&nextins,sizeof(nextins));
	saveVar(&ptr,sizeof(ptr));
	saveVar(&PC,sizeof(PC));
	saveVar(&cycle,sizeof(cycle));
	saveVar(&SP,sizeof(SP));
	saveVar(&ST,sizeof(ST));
	saveVar(&AC,sizeof(AC));
	saveVar(&X,sizeof(X));
	saveVar(&Y,sizeof(Y));
	saveVar(&IRQcount, sizeof(IRQcount));
	saveVar(&irq_sequence, sizeof(irq_sequence));
	saveVar(&nmiLevel, sizeof(nmiLevel));
	saveVar(&irqVector, sizeof(irqVector));
	saveVar(&cpu_jammed, sizeof(cpu_jammed));
}

void CPU::readState()
{
	readVar(&currins,sizeof(currins));
	readVar(&nextins,sizeof(nextins));
	readVar(&ptr,sizeof(ptr));
	readVar(&PC,sizeof(PC));
	PC &= 0xFFFF;
	readVar(&cycle,sizeof(cycle));
	readVar(&SP,sizeof(SP));
	readVar(&ST,sizeof(ST));
	ST &= 0xFF;
	readVar(&AC,sizeof(AC));
	readVar(&X,sizeof(X));
	readVar(&Y,sizeof(Y));
	readVar(&IRQcount, sizeof(IRQcount));
	readVar(&irq_sequence, sizeof(irq_sequence));
	readVar(&nmiLevel, sizeof(nmiLevel));
	readVar(&irqVector, sizeof(irqVector));
	readVar(&cpu_jammed, sizeof(cpu_jammed));
}

// reset keeps all except for PC
void CPU::Reset()
{
	ST=0x24;
	softreset();
	IRQcount = cycle = 0;
	irq_sequence = 0;
}

void CPU::softreset()
{
	// clear the BRK flag
	ST&=0xEF;
	setPC(mem->Read(0xFFFC)|(mem->Read(0xFFFD)<<8));
}

void CPU::setPC(unsigned int addr)
{
	PC=addr;
	cycle=0;
}

static unsigned int remained;

void CPU::process(unsigned int cycles)
{
	remained = cycles;
	while (remained-- && !bp_reached)
		process();
}

unsigned int CPU::getRemainingCycles()
{
	return remained;
}

inline void CPU::DoCompare(unsigned char reg, unsigned char value)
{
	ST = (ST & 0xFE) | ( value<=reg);
	SETFLAGS_ZN( reg - value);
}

// -----------------------------------------------------------------------------
// instruction decoder tables
// -----------------------------------------------------------------------------
enum {
	M_IMP, M_IMM, M_ZP, M_ZPX, M_ZPY, M_ABS, M_ABX, M_ABY, M_IZX, M_IZY, M_REL,
	M_BRK, M_JSR, M_RTI, M_RTS, M_PSH, M_PUL, M_JMP, M_JMPI, M_JAM
};

enum {
	// operations reading their operand
	O_ORA, O_AND, O_EOR, O_ADC, O_SBC, O_CMP, O_CPX, O_CPY, O_LDA, O_LDX, O_LDY,
	O_BIT, O_LAX, O_LAS, O_NOP, O_ANC, O_ALR, O_ARR, O_XAA, O_LXA, O_SBX,
	// operations storing a value
	O_STA, O_STX, O_STY, O_SAX, O_SHA, O_SHX, O_SHY, O_TAS,
	// read-modify-write operations
	O_ASL, O_ROL, O_LSR, O_ROR, O_INC, O_DEC, O_SLO, O_RLA, O_SRE, O_RRA, O_DCP, O_ISB,
	// implied operations
	O_ASLA, O_ROLA, O_LSRA, O_RORA, O_INX, O_INY, O_DEX, O_DEY, O_TAX, O_TXA, O_TAY, O_TYA,
	O_TSX, O_TXS, O_CLC, O_SEC, O_CLI, O_SEI, O_CLV, O_CLD, O_SED,
	// branches
	O_BPL, O_BMI, O_BVC, O_BVS, O_BCC, O_BCS, O_BNE, O_BEQ,
	// stack, jumps, interrupts
	O_PHA, O_PHP, O_PLA, O_PLP, O_BRK, O_JSR, O_RTI, O_RTS, O_JMP, O_JMPI, O_JAM
};

// opcode -> addressing mode, operation
#define OPCODE_LIST(X) \
	X(0x00,BRK,BRK) X(0x01,IZX,ORA) X(0x02,JAM,JAM) X(0x03,IZX,SLO) X(0x04,ZP,NOP) X(0x05,ZP,ORA) X(0x06,ZP,ASL) X(0x07,ZP,SLO) X(0x08,PSH,PHP) X(0x09,IMM,ORA) X(0x0A,IMP,ASLA) X(0x0B,IMM,ANC) X(0x0C,ABS,NOP) X(0x0D,ABS,ORA) X(0x0E,ABS,ASL) X(0x0F,ABS,SLO) \
	X(0x10,REL,BPL) X(0x11,IZY,ORA) X(0x12,JAM,JAM) X(0x13,IZY,SLO) X(0x14,ZPX,NOP) X(0x15,ZPX,ORA) X(0x16,ZPX,ASL) X(0x17,ZPX,SLO) X(0x18,IMP,CLC) X(0x19,ABY,ORA) X(0x1A,IMP,NOP) X(0x1B,ABY,SLO) X(0x1C,ABX,NOP) X(0x1D,ABX,ORA) X(0x1E,ABX,ASL) X(0x1F,ABX,SLO) \
	X(0x20,JSR,JSR) X(0x21,IZX,AND) X(0x22,JAM,JAM) X(0x23,IZX,RLA) X(0x24,ZP,BIT) X(0x25,ZP,AND) X(0x26,ZP,ROL) X(0x27,ZP,RLA) X(0x28,PUL,PLP) X(0x29,IMM,AND) X(0x2A,IMP,ROLA) X(0x2B,IMM,ANC) X(0x2C,ABS,BIT) X(0x2D,ABS,AND) X(0x2E,ABS,ROL) X(0x2F,ABS,RLA) \
	X(0x30,REL,BMI) X(0x31,IZY,AND) X(0x32,JAM,JAM) X(0x33,IZY,RLA) X(0x34,ZPX,NOP) X(0x35,ZPX,AND) X(0x36,ZPX,ROL) X(0x37,ZPX,RLA) X(0x38,IMP,SEC) X(0x39,ABY,AND) X(0x3A,IMP,NOP) X(0x3B,ABY,RLA) X(0x3C,ABX,NOP) X(0x3D,ABX,AND) X(0x3E,ABX,ROL) X(0x3F,ABX,RLA) \
	X(0x40,RTI,RTI) X(0x41,IZX,EOR) X(0x42,JAM,JAM) X(0x43,IZX,SRE) X(0x44,ZP,NOP) X(0x45,ZP,EOR) X(0x46,ZP,LSR) X(0x47,ZP,SRE) X(0x48,PSH,PHA) X(0x49,IMM,EOR) X(0x4A,IMP,LSRA) X(0x4B,IMM,ALR) X(0x4C,JMP,JMP) X(0x4D,ABS,EOR) X(0x4E,ABS,LSR) X(0x4F,ABS,SRE) \
	X(0x50,REL,BVC) X(0x51,IZY,EOR) X(0x52,JAM,JAM) X(0x53,IZY,SRE) X(0x54,ZPX,NOP) X(0x55,ZPX,EOR) X(0x56,ZPX,LSR) X(0x57,ZPX,SRE) X(0x58,IMP,CLI) X(0x59,ABY,EOR) X(0x5A,IMP,NOP) X(0x5B,ABY,SRE) X(0x5C,ABX,NOP) X(0x5D,ABX,EOR) X(0x5E,ABX,LSR) X(0x5F,ABX,SRE) \
	X(0x60,RTS,RTS) X(0x61,IZX,ADC) X(0x62,JAM,JAM) X(0x63,IZX,RRA) X(0x64,ZP,NOP) X(0x65,ZP,ADC) X(0x66,ZP,ROR) X(0x67,ZP,RRA) X(0x68,PUL,PLA) X(0x69,IMM,ADC) X(0x6A,IMP,RORA) X(0x6B,IMM,ARR) X(0x6C,JMPI,JMPI) X(0x6D,ABS,ADC) X(0x6E,ABS,ROR) X(0x6F,ABS,RRA) \
	X(0x70,REL,BVS) X(0x71,IZY,ADC) X(0x72,JAM,JAM) X(0x73,IZY,RRA) X(0x74,ZPX,NOP) X(0x75,ZPX,ADC) X(0x76,ZPX,ROR) X(0x77,ZPX,RRA) X(0x78,IMP,SEI) X(0x79,ABY,ADC) X(0x7A,IMP,NOP) X(0x7B,ABY,RRA) X(0x7C,ABX,NOP) X(0x7D,ABX,ADC) X(0x7E,ABX,ROR) X(0x7F,ABX,RRA) \
	X(0x80,IMM,NOP) X(0x81,IZX,STA) X(0x82,IMM,NOP) X(0x83,IZX,SAX) X(0x84,ZP,STY) X(0x85,ZP,STA) X(0x86,ZP,STX) X(0x87,ZP,SAX) X(0x88,IMP,DEY) X(0x89,IMM,NOP) X(0x8A,IMP,TXA) X(0x8B,IMM,XAA) X(0x8C,ABS,STY) X(0x8D,ABS,STA) X(0x8E,ABS,STX) X(0x8F,ABS,SAX) \
	X(0x90,REL,BCC) X(0x91,IZY,STA) X(0x92,JAM,JAM) X(0x93,IZY,SHA) X(0x94,ZPX,STY) X(0x95,ZPX,STA) X(0x96,ZPY,STX) X(0x97,ZPY,SAX) X(0x98,IMP,TYA) X(0x99,ABY,STA) X(0x9A,IMP,TXS) X(0x9B,ABY,TAS) X(0x9C,ABX,SHY) X(0x9D,ABX,STA) X(0x9E,ABY,SHX) X(0x9F,ABY,SHA) \
	X(0xA0,IMM,LDY) X(0xA1,IZX,LDA) X(0xA2,IMM,LDX) X(0xA3,IZX,LAX) X(0xA4,ZP,LDY) X(0xA5,ZP,LDA) X(0xA6,ZP,LDX) X(0xA7,ZP,LAX) X(0xA8,IMP,TAY) X(0xA9,IMM,LDA) X(0xAA,IMP,TAX) X(0xAB,IMM,LXA) X(0xAC,ABS,LDY) X(0xAD,ABS,LDA) X(0xAE,ABS,LDX) X(0xAF,ABS,LAX) \
	X(0xB0,REL,BCS) X(0xB1,IZY,LDA) X(0xB2,JAM,JAM) X(0xB3,IZY,LAX) X(0xB4,ZPX,LDY) X(0xB5,ZPX,LDA) X(0xB6,ZPY,LDX) X(0xB7,ZPY,LAX) X(0xB8,IMP,CLV) X(0xB9,ABY,LDA) X(0xBA,IMP,TSX) X(0xBB,ABY,LAS) X(0xBC,ABX,LDY) X(0xBD,ABX,LDA) X(0xBE,ABY,LDX) X(0xBF,ABY,LAX) \
	X(0xC0,IMM,CPY) X(0xC1,IZX,CMP) X(0xC2,IMM,NOP) X(0xC3,IZX,DCP) X(0xC4,ZP,CPY) X(0xC5,ZP,CMP) X(0xC6,ZP,DEC) X(0xC7,ZP,DCP) X(0xC8,IMP,INY) X(0xC9,IMM,CMP) X(0xCA,IMP,DEX) X(0xCB,IMM,SBX) X(0xCC,ABS,CPY) X(0xCD,ABS,CMP) X(0xCE,ABS,DEC) X(0xCF,ABS,DCP) \
	X(0xD0,REL,BNE) X(0xD1,IZY,CMP) X(0xD2,JAM,JAM) X(0xD3,IZY,DCP) X(0xD4,ZPX,NOP) X(0xD5,ZPX,CMP) X(0xD6,ZPX,DEC) X(0xD7,ZPX,DCP) X(0xD8,IMP,CLD) X(0xD9,ABY,CMP) X(0xDA,IMP,NOP) X(0xDB,ABY,DCP) X(0xDC,ABX,NOP) X(0xDD,ABX,CMP) X(0xDE,ABX,DEC) X(0xDF,ABX,DCP) \
	X(0xE0,IMM,CPX) X(0xE1,IZX,SBC) X(0xE2,IMM,NOP) X(0xE3,IZX,ISB) X(0xE4,ZP,CPX) X(0xE5,ZP,SBC) X(0xE6,ZP,INC) X(0xE7,ZP,ISB) X(0xE8,IMP,INX) X(0xE9,IMM,SBC) X(0xEA,IMP,NOP) X(0xEB,IMM,SBC) X(0xEC,ABS,CPX) X(0xED,ABS,SBC) X(0xEE,ABS,INC) X(0xEF,ABS,ISB) \
	X(0xF0,REL,BEQ) X(0xF1,IZY,SBC) X(0xF2,JAM,JAM) X(0xF3,IZY,ISB) X(0xF4,ZPX,NOP) X(0xF5,ZPX,SBC) X(0xF6,ZPX,INC) X(0xF7,ZPX,ISB) X(0xF8,IMP,SED) X(0xF9,ABY,SBC) X(0xFA,IMP,NOP) X(0xFB,ABY,ISB) X(0xFC,ABX,NOP) X(0xFD,ABX,SBC) X(0xFE,ABX,INC) X(0xFF,ABX,ISB)

#define IS_STORE_OP(o)	((o) >= O_STA && (o) <= O_TAS)
#define IS_RMW_OP(o)	((o) >= O_ASL && (o) <= O_ISB)

#define BUS_READ(a)		(mem->Read((unsigned int)(a) & 0xFFFF))
#define BUS_WRITE(a,v)	(mem->Write((unsigned int)(a) & 0xFFFF, (unsigned char)(v)))
// the stack page is accessed directly (as the previous core did) unless
// CPU_STACK_VIA_BUS is set, in which case the stack cycles go through mem
#ifndef CPU_STACK_VIA_BUS
#define CPU_STACK_VIA_BUS 0
#endif
#if CPU_STACK_VIA_BUS
#define STACK_RD(sp)	BUS_READ(0x100 | (sp))
#define STACK_WR(sp,v)	BUS_WRITE(0x100 | (sp), v)
#else
#define STACK_RD(sp)	(stack[(sp) & 0xFF])
#define STACK_WR(sp,v)	(stack[(sp) & 0xFF] = (unsigned char)(v))
#endif
#if CPU_DUMMY_READS
#define DUMMY_READ(a)	((void)BUS_READ(a))
#else
#define DUMMY_READ(a)	((void)0)
#endif

// -----------------------------------------------------------------------------
// one bus cycle of an instruction (not the opcode fetch)
//
// 'cycle' is the number of cycles of the current instruction that are already
// done. While an instruction is in progress 'nextins', 'farins' and 'ptr' hold
// the intermediate address and data bytes.
// The function is instantiated for every opcode (M = addressing mode, O =
// operation) so the compiler resolves the decoding at compile time. H is true
// when the RDY line is low: read cycles stall, write cycles go ahead.
// -----------------------------------------------------------------------------
template<unsigned int M, unsigned int O, bool H>
CPU_NOINLINE void CPU::step()
{
	const unsigned int c = cycle;				// the cycle executed now (1 = 2nd cycle of the instruction)
	const bool halted = H;

	#define FETCH(var)	do { var = BUS_READ(PC); PC = (PC+1)&0xFFFF; } while (0)
	#define STALL()		do { if (halted) return; } while (0)	// read cycles stall when RDY is low
	#define NEXT()		do { cycle = c + 1; return; } while (0)
	#define DONE()		do { cycle = 0; return; } while (0)
	#define SET_C(f)	do { if (f) ST |= 0x01; else ST &= 0xFE; } while (0)

	// ---- operations ------------------------------------------------------
	// operations reading their operand (also used for immediate operands)
	auto execRead = [&](unsigned char v) {
		unsigned char t;
		switch (O) {
			case O_ORA: AC |= v; t = AC; SETFLAGS_ZN(t); break;
			case O_AND: AC &= v; t = AC; SETFLAGS_ZN(t); break;
			case O_EOR: AC ^= v; t = AC; SETFLAGS_ZN(t); break;
			case O_ADC: ADC(v); break;
			case O_SBC: SBC(v); break;
			case O_CMP: DoCompare(AC, v); break;
			case O_CPX: DoCompare(X, v); break;
			case O_CPY: DoCompare(Y, v); break;
			case O_LDA: AC = v; t = AC; SETFLAGS_ZN(t); break;
			case O_LDX: X = v; t = X; SETFLAGS_ZN(t); break;
			case O_LDY: Y = v; t = Y; SETFLAGS_ZN(t); break;
			case O_BIT:
				ST = (ST&0x3D) | (v&0xC0) | (((AC&v)==0)<<1);
				break;
			case O_LAX: AC = X = v; t = AC; SETFLAGS_ZN(t); break;
			case O_LAS: SP &= v; AC = X = SP; t = X; SETFLAGS_ZN(t); break;
			case O_ANC:
				AC &= v;
				SET_C(AC & 0x80);
				t = AC; SETFLAGS_ZN(t);
				break;
			case O_ALR:
				AC &= v;
				SET_C(AC & 0x01);
				AC >>= 1;
				t = AC; SETFLAGS_ZN(t);
				break;
			case O_ARR:
			{
				unsigned int tmp;
				AC &= v;
				tmp = AC;
				tmp |= (ST&1)<<8;
				tmp >>= 1;
				if (ST&0x08) {
					// Set sign flag state to that of carry
					ST = (ST & 0x7F)|((ST & 1) << 7);
					// Set the zero flag
					ST = (ST & 0xFD) | (tmp == 0 ? 2 : 0);
					// Set the overflow flag
					ST = (ST & 0xBF) | ((tmp ^ AC) & 0x40);
					if (((AC & 0x0F) + (AC & 0x01)) > 0x05)
						tmp = (tmp & 0xF0) | ((tmp + 0x06) & 0x0F);
					if (((AC & 0xF0) + (AC & 0x10)) > 0x50) {
						tmp = (tmp & 0x0F) | ((tmp + 0x60) & 0xF0);
						ST |= 1;
					} else
						ST &= 0xFE;
				} else {
					t = (unsigned char)tmp;
					SETFLAGS_ZN(t);
					SET_C(tmp & 0x40);
					if ((tmp & 0x40)^(((tmp & 0x20)<<1) & 0x40)) SetVFlag(); else ClearVFlag();
				}
				AC = tmp & 0xFF;
				break;
			}
			case O_XAA:
				AC = (X&v&(AC|0xEE)) | ((X&v)&((AC<<1)&0x10));
				t = AC; SETFLAGS_ZN(t);
				break;
			case O_LXA:
				X = AC = (v&(AC|0xEE));
				t = AC; SETFLAGS_ZN(t);
				break;
			case O_SBX:
				t = X & AC;
				SET_C(t >= v);
				X = t - v;
				t = X; SETFLAGS_ZN(t);
				break;
			default:				// NOP and its relatives: the operand is just read
				break;
		}
	};

	// read-modify-write: the new value for the memory location, flags and
	// accumulator are updated as well
	auto execRmw = [&](unsigned char old) -> unsigned char {
		unsigned char n;
		switch (O) {
			case O_ASL: SET_C(old & 0x80); n = old << 1; break;
			case O_ROL: n = (old << 1) | (ST & 0x01); SET_C(old & 0x80); break;
			case O_LSR: SET_C(old & 0x01); n = old >> 1; break;
			case O_ROR: n = (old >> 1) | ((ST & 0x01) << 7); SET_C(old & 0x01); break;
			case O_INC: n = old + 1; break;
			case O_DEC: n = old - 1; break;
			case O_SLO: SET_C(old & 0x80); n = old << 1; AC |= n; break;
			case O_RLA: n = (old << 1) | (ST & 0x01); SET_C(old & 0x80); AC &= n; break;
			case O_SRE: SET_C(old & 0x01); n = old >> 1; AC ^= n; break;
			case O_RRA: n = (old >> 1) | ((ST & 0x01) << 7); SET_C(old & 0x01); ADC(n); break;
			case O_DCP: n = old - 1; DoCompare(AC, n); break;
			case O_ISB: n = old + 1; SBC(n); break;
			default:    n = old; break;
		}
		unsigned char t;
		switch (O) {
			case O_SLO: case O_RLA: case O_SRE:
				t = AC; SETFLAGS_ZN(t);
				break;
			case O_RRA: case O_DCP: case O_ISB:
				break;				// flags are set by ADC, SBC and the compare
			default:
				t = n; SETFLAGS_ZN(t);
		}
		return n;
	};

	// stores
	auto doStore = [&]() {
		unsigned int addr = ptr;
		unsigned char v;
		switch (O) {
			case O_STA: v = AC; break;
			case O_STX: v = X; break;
			case O_STY: v = Y; break;
			case O_SAX: v = AC & X; break;
			default:
			{
				// the 'unstable' stores: the value is ANDed with the high byte of
				// the base address + 1; when the indexing crosses a page the high
				// byte of the address becomes that value
				unsigned int idx = (M == M_ABX) ? X : Y;
				unsigned int baseHi = ((ptr - idx) >> 8) & 0xFF;
				unsigned char reg;
				switch (O) {
					case O_SHA: reg = AC & X; break;
					case O_SHX: reg = X; break;
					case O_SHY: reg = Y; break;
					default:	// TAS
						SP = AC & X;
						reg = SP;
				}
				v = reg & (baseHi + 1);
				if (((ptr >> 8) & 0xFF) != baseHi)
					addr = (v << 8) | (ptr & 0xFF);
			}
		}
		BUS_WRITE(addr, v);
	};

	auto push = [&](unsigned char v) { STACK_WR(SP, v); SP = (SP-1) & 0xFF; };

	// ---- instructions with their own sequence ---------------------------
	switch (M) {

		case M_IMP:			// implied and accumulator: dummy read of the next byte
		{
			STALL();
			DUMMY_READ(PC);
			unsigned char t;
			switch (O) {
				case O_ASLA: SET_C(AC & 0x80); AC <<= 1; t = AC; SETFLAGS_ZN(t); break;
				case O_ROLA: t = (AC << 1) | (ST & 0x01); SET_C(AC & 0x80); AC = t; SETFLAGS_ZN(t); break;
				case O_LSRA: SET_C(AC & 0x01); AC >>= 1; t = AC; SETFLAGS_ZN(t); break;
				case O_RORA: t = (AC >> 1) | ((ST & 0x01) << 7); SET_C(AC & 0x01); AC = t; SETFLAGS_ZN(t); break;
				case O_INX: ++X; t = X; SETFLAGS_ZN(t); break;
				case O_INY: ++Y; t = Y; SETFLAGS_ZN(t); break;
				case O_DEX: --X; t = X; SETFLAGS_ZN(t); break;
				case O_DEY: --Y; t = Y; SETFLAGS_ZN(t); break;
				case O_TAX: X = AC; t = X; SETFLAGS_ZN(t); break;
				case O_TXA: AC = X; t = AC; SETFLAGS_ZN(t); break;
				case O_TAY: Y = AC; t = Y; SETFLAGS_ZN(t); break;
				case O_TYA: AC = Y; t = AC; SETFLAGS_ZN(t); break;
				case O_TSX: X = SP; t = X; SETFLAGS_ZN(t); break;
				case O_TXS: SP = X; break;
				case O_CLC: ST &= 0xFE; break;
				case O_SEC: ST |= 0x01; break;
				case O_CLI: ST &= 0xFB; break;
				case O_SEI: ST |= 0x04;
					// actually, IRQ can still occur right after the SEI instruction
					// some programs (like Castle Master) rely on it.
					break;
				case O_CLV: ClearVFlag(); break;
				case O_CLD: ST &= 0xF7; break;
				case O_SED: ST |= 0x08; break;
				default: break;		// NOP
			}
			DONE();
		}

		case M_IMM:			// the operand is fetched in the 2nd cycle
		{
			STALL();
			unsigned char v;
			FETCH(v);
			execRead(v);
			DONE();
		}

		case M_REL:			// conditional branches
		{
			STALL();
			if (c == 1) {
				FETCH(nextins);
				bool taken = false;
				switch (O) {
					case O_BPL: taken = !(ST & 0x80); break;
					case O_BMI: taken =  (ST & 0x80) != 0; break;
					case O_BVC: taken = !CheckVFlag(); break;
					case O_BVS: taken =  CheckVFlag() != 0; break;
					case O_BCC: taken = !(ST & 0x01); break;
					case O_BCS: taken =  (ST & 0x01) != 0; break;
					case O_BNE: taken = !(ST & 0x02); break;
					case O_BEQ: taken =  (ST & 0x02) != 0; break;
					default: break;
				}
				if (!taken)
					DONE();
				NEXT();
			}
			unsigned int target = (PC + (signed char)nextins) & 0xFFFF;
			if (c == 2) {
				DUMMY_READ(PC);
				if (!((target ^ PC) & 0xFF00)) {	// same page
					PC = target;
					DONE();
				}
				NEXT();
			}
			// page crossing: the high byte of PC is fixed in the extra cycle
			DUMMY_READ((PC & 0xFF00) | (target & 0xFF));
			PC = target;
			DONE();
		}

		case M_PSH:			// PHA, PHP
			if (c == 1) {
				STALL();
				DUMMY_READ(PC);
				NEXT();
			}
			push(O == O_PHA ? AC : (ST | 0x30));
			DONE();

		case M_PUL:			// PLA, PLP
		{
			STALL();
			if (c == 1) {
				DUMMY_READ(PC);
				NEXT();
			}
			if (c == 2) {
				((void)STACK_RD(SP));
				SP = (SP+1) & 0xFF;
				NEXT();
			}
			unsigned char v = STACK_RD(SP);
			if (O == O_PLA) {
				AC = v;
				SETFLAGS_ZN(v);
			} else
				ST = v;
			DONE();
		}

		case M_JMP:
			STALL();
			if (c == 1) {
				FETCH(nextins);
				NEXT();
			}
			PC = nextins | (BUS_READ(PC) << 8);
			DONE();

		case M_JMPI:
			STALL();
			if (c == 1) {
				FETCH(nextins);
				NEXT();
			}
			if (c == 2) {
				ptr = nextins | (BUS_READ(PC) << 8);
				PC = (PC+1) & 0xFFFF;
				NEXT();
			}
			if (c == 3) {
				farins = BUS_READ(ptr);
				NEXT();
			}
			// instruction does not handle page crossing
			PC = farins | (BUS_READ((ptr & 0xFF00) | ((ptr+1) & 0xFF)) << 8);
			DONE();

		case M_JSR:
			if (c == 1) {
				STALL();
				FETCH(nextins);
				NEXT();
			}
			if (c == 2) {
				STALL();
				((void)STACK_RD(SP));		// internal operation on the stack pointer
				NEXT();
			}
			if (c == 3) {
				push(PC >> 8);
				NEXT();
			}
			if (c == 4) {
				push(PC & 0xFF);
				NEXT();
			}
			STALL();
			PC = ptr = nextins | (BUS_READ(PC) << 8);
			DONE();

		case M_RTS:
		{
			STALL();
			if (c == 1) {
				DUMMY_READ(PC);
				NEXT();
			}
			if (c == 2) {
				((void)STACK_RD(SP));
				SP = (SP+1) & 0xFF;
				NEXT();
			}
			if (c == 3) {
				ptr = STACK_RD(SP);
				SP = (SP+1) & 0xFF;
				NEXT();
			}
			if (c == 4) {
				ptr |= STACK_RD(SP) << 8;
				PC = ptr;
				NEXT();
			}
			DUMMY_READ(PC);
			PC = (PC+1) & 0xFFFF;
			DONE();
		}

		case M_RTI:
		{
			STALL();
			if (c == 1) {
				DUMMY_READ(PC);
				NEXT();
			}
			if (c == 2) {
				((void)STACK_RD(SP));
				SP = (SP+1) & 0xFF;
				NEXT();
			}
			if (c == 3) {
				ST = STACK_RD(SP);
				SP = (SP+1) & 0xFF;
				NEXT();
			}
			if (c == 4) {
				ptr = STACK_RD(SP);
				SP = (SP+1) & 0xFF;
				NEXT();
			}
			ptr |= STACK_RD(SP) << 8;
			PC = ptr;
			DONE();
		}

		case M_BRK:			// BRK and the interrupt sequence
			switch (c) {
				case 1:
					STALL();
					DUMMY_READ(PC);
					if (!irq_sequence)
						PC = (PC+1) & 0xFFFF;
					NEXT();
				case 2:
					push(PC >> 8);
					NEXT();
				case 3:
					push(PC & 0xFF);
					NEXT();
				case 4:
					// BRK pushes the status register to the stack with the B bit set
					// IRQ can still occur on BRK before saving the flags
					push((ST | 0x30) & ~irq_sequence);
					// and then masks the interrupt bit (even when NMI)
					ST |= 0x04;
					NEXT();
				case 5:
					STALL();
					ptr = BUS_READ(irqVector);
					NEXT();
				default:
					STALL();
					ptr |= BUS_READ(irqVector | 1) << 8;
					PC = ptr;
					if (irqVector != INTERRUPT_IRQ)
						irqVector = INTERRUPT_IRQ;
					irq_sequence = 0x0;
					DONE();
			}

		case M_JAM:			// KIL, JAM or whatever: jams the machine
			STALL();
			DUMMY_READ(PC);
			cycle = 0;
			PC = (PC-1) & 0xFFFF;
			IRQcount = 0;	// don't allow IRQ
			bp_reached = bp_active = cpu_jammed = true;
			return;

		default:
			break;
	}

	// ---- memory operands: address calculation ---------------------------
	// when the effective address is known 's' counts the data cycles
	// (0 = the first access to the operand)
	const bool store = IS_STORE_OP(O);
	const bool rmw = IS_RMW_OP(O);
	unsigned int s;

	switch (M) {

		case M_ZP:				// $00
			if (c == 1) {
				STALL();
				FETCH(nextins);
				ptr = nextins;
				NEXT();
			}
			s = c - 2;
			break;

		case M_ZPX:				// $00,X
		case M_ZPY:				// $00,Y
			if (c == 1) {
				STALL();
				FETCH(nextins);
				NEXT();
			}
			if (c == 2) {
				STALL();
				DUMMY_READ(nextins);
				ptr = (nextins + (M == M_ZPX ? X : Y)) & 0xFF;
				NEXT();
			}
			s = c - 3;
			break;

		case M_ABS:				// $0000
			if (c == 1) {
				STALL();
				FETCH(nextins);
				NEXT();
			}
			if (c == 2) {
				STALL();
				ptr = nextins | (BUS_READ(PC) << 8);
				PC = (PC+1) & 0xFFFF;
				NEXT();
			}
			s = c - 3;
			break;

		case M_ABX:				// $0000,X
		case M_ABY:				// $0000,Y
		{
			const unsigned int idx = (M == M_ABX) ? X : Y;
			if (c == 1) {
				STALL();
				FETCH(nextins);
				NEXT();
			}
			if (c == 2) {
				STALL();
				ptr = (nextins | (BUS_READ(PC) << 8)) + idx;
				ptr &= 0xFFFF;
				PC = (PC+1) & 0xFFFF;
				NEXT();
			}
			if (c == 3) {
				if (!store && !rmw && (nextins + idx) < 0x100) {
					s = 0;		// no page crossing: the read is done in this cycle
					break;
				}
				// dummy read from the address with the uncorrected high byte
				STALL();
				DUMMY_READ((ptr & 0xFF) | ((ptr - idx) & 0xFF00));
				NEXT();
			}
			s = c - 4;
			break;
		}

		case M_IZX:				// ($00,X)
			if (c == 1) {
				STALL();
				FETCH(nextins);
				NEXT();
			}
			if (c == 2) {
				STALL();
				DUMMY_READ(nextins);
				nextins = (nextins + X) & 0xFF;
				NEXT();
			}
			if (c == 3) {
				STALL();
				ptr = BUS_READ(nextins);
				NEXT();
			}
			if (c == 4) {
				STALL();
				ptr |= BUS_READ((nextins + 1) & 0xFF) << 8;
				NEXT();
			}
			s = c - 5;
			break;

		case M_IZY:				// ($00),Y
			if (c == 1) {
				STALL();
				FETCH(nextins);
				NEXT();
			}
			if (c == 2) {
				STALL();
				farins = BUS_READ(nextins);				// low byte of the base address
				NEXT();
			}
			if (c == 3) {
				STALL();
				ptr = ((farins | (BUS_READ((nextins + 1) & 0xFF) << 8)) + Y) & 0xFFFF;
				NEXT();
			}
			if (c == 4) {
				if (!store && !rmw && (farins + Y) < 0x100) {
					s = 0;		// no page crossing: the read is done in this cycle
					break;
				}
				STALL();
				DUMMY_READ((ptr & 0xFF) | ((ptr - Y) & 0xFF00));
				NEXT();
			}
			s = c - 5;
			break;

		default:				// can't happen
			cycle = 0;
			return;
	}

	// ---- memory operands: data cycles -----------------------------------
	if (store) {
		doStore();				// write cycles are never stalled
		DONE();
	}
	if (!rmw) {
		STALL();
		execRead(BUS_READ(ptr));
		DONE();
	}
	// read-modify-write: read, write the old value back, write the new value
	if (s == 0) {
		STALL();
		farins = BUS_READ(ptr);
		NEXT();
	}
	if (s == 1) {
		BUS_WRITE(ptr, farins);
		farins = execRmw(farins);
		NEXT();
	}
	BUS_WRITE(ptr, farins);
	DONE();

	#undef FETCH
	#undef STALL
	#undef NEXT
	#undef DONE
	#undef SET_C
}

// opcode dispatch
#define CASE_RUN(code, m, o)	case code: step<M_##m, O_##o, false>(); break;
#define CASE_HALT(code, m, o)	case code: step<M_##m, O_##o, true>(); break;
// The IRQ line is level sensitive: when its source is released again before the
// interrupt sequence has started the pending interrupt is dropped. 
// NMI is edge triggered and stays latched until it is serviced.
#define SAMPLE_IRQ() \
	if (IRQcount | *irq_register) { \
		if (!*irq_register && irqVector != INTERRUPT_NMI) \
			IRQcount = 0; \
		else if (IRQcount || (!irq_sequence && !(ST & 0x04))) \
			IRQcount++; \
}

void CPU::process()
{
	SAMPLE_IRQ();

	if (cycle) {
		switch (currins & 0xFF) {
			OPCODE_LIST(CASE_RUN)
		}
		return;
	}

	// first cycle: fetch the opcode and nothing else
	if (IRQcount>=3 && currins != 0x58) {
		IRQcount = 0;
		if (!(ST&0x04) || currins == 0x78 || currins == 0x28 || irqVector == INTERRUPT_NMI) { //
			irq_sequence = 0x10;	// this is 0x10 to match the B flag later
			currins = 0x00;			// the BRK sequence is used for the interrupt
			cycle = 1;
			(void)BUS_READ(PC);		// the opcode fetch is thrown away
			return;
		}
	}
	currins = BUS_READ(PC);
	PC = (PC+1)&0xFFFF;
	cycle = 1;
#ifdef CPUS_STATS
	stats[currins & 0xFF]++;
#endif
}

// in these cycles, only write operations are allowed for the CPU
// (the RDY line of the CPU is low, read cycles are stalled)
void CPU::stopcycle()
{
	SAMPLE_IRQ();

	if (!cycle)
		return;					// the opcode fetch is a read cycle
	switch (currins & 0xFF) {
		OPCODE_LIST(CASE_HALT)
	}
}

CPU::~CPU()
{
}

// Drive CPU class overrides

DRIVECPU::DRIVECPU(MemoryHandler *memhandler, unsigned char *irqreg, unsigned char *cpustack,
				   unsigned char *so_enable, unsigned int id)
				   : CPU(memhandler, irqreg, cpustack)
{
	char cpuid[8];
	sprintf(cpuid, "CPU%u", id);
	setId(cpuid);

	irq_sequence = 0;
	IRQcount = 0;

	is_so_enable = so_enable;
}

void DRIVECPU::setVFlagWithCheckSOE()
{
	if (((*is_so_enable) & 0x0E) == 0x0E)
		SetVFlag();
}

void DRIVECPU::setVFlagCallback(void* clptr)
{
	DRIVECPU* dm = static_cast<DRIVECPU*>(clptr);
	dm->setVFlagWithCheckSOE();
}
