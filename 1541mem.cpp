/*

	This is the memory controller and VIA-6522 chip emulation of the 1541 drive,
	necessary for true-drive emulation.

    1541 memory map:

       $0000-$07FF RAM (2KB)
       $0800-$0FFF RAM mirror
       $1000-$17FF unconnected address space
       $1800-$1BFF VIA1 mirrored each 16 bytes
       $1C00-$1FFF VIA2 mirrored each 16 bytes
       $2000-$7FFF unconnected address space
	   $8000-$BFFF ROM mirror
       $C000-$FFFF ROM (16KB)
*/

#include "1541mem.h"
#include "FdcGcr.h"

DRIVEMEM::DRIVEMEM(FdcGcr *_fdc, unsigned char *Ramptr, unsigned char *Rom, unsigned int dev_num)
 : CTrueSerial(dev_num), Ram(Ramptr), rom(Rom), fdc(_fdc)
{
	char id[8];
	sprintf(id, "M41%u", dev_num);
	setId(id);
	devnr = (dev_num & 7) << 5;
	bus_state_change = false;
	serialPort[DeviceNr] = 0x85;
	ppIn = 0xFF;
	oldAtnIn = 1;
	Reset();
	// set VIA interrupt callbacks
	via[0].setCheckIrqCallback(this, checkIrq);
	via[1].setCheckIrqCallback(this, checkIrq);
}

void DRIVEMEM::dumpState()
{
	saveVar(Ram, 0x4000);
	saveVar(&irqFlag, sizeof(irqFlag));
	saveVar(&oldAtnIn, sizeof(oldAtnIn));
	saveVar(&bus_state_change, sizeof(bus_state_change));
	saveVar(&serialPort[DeviceNr], sizeof(serialPort[DeviceNr]));
	saveVar(&via[0].reg, sizeof(via[0].reg) / sizeof(via[0].reg[0]));
	saveVar(&via[1].reg, sizeof(via[1].reg) / sizeof(via[1].reg[1]));
}

void DRIVEMEM::readState()
{
	readVar(Ram, 0x4000);
	readVar(&irqFlag, sizeof(irqFlag));
	readVar(&oldAtnIn, sizeof(oldAtnIn));
	readVar(&bus_state_change, sizeof(bus_state_change));
	readVar(&serialPort[DeviceNr], sizeof(serialPort[DeviceNr]));
	readVar(&via[0].reg, sizeof(via[0].reg) / sizeof(via[0].reg[0]));
	readVar(&via[1].reg, sizeof(via[1].reg) / sizeof(via[1].reg[1]));
	//
	for (unsigned int i = 0; i < 16; i++) {
		Write(0x1800 + i, via[0].reg[i]);
		Write(0x1C00 + i, via[1].reg[i]);
	}
}

/*
   Control emulating one clock tick from here....
*/
void DRIVEMEM::EmulateTick()
{
	fdc->SpinMotor();
	CountTimers();
	if (bus_state_change) {
		UpdateSerialPort();
	}
}

inline void DRIVEMEM::CountTimers()
{
#if 0
	if (!via[0].t1c--) {
		if (via[0].acr & 0x40)	// free-run mode?
			via[0].t1c = via[0].t1l; // reload from latch
		via[0].ifr |= 0x40;
		/*if (via[0].ier & 0x40)
			irq_flag |= 0xC0;*/
	}

	if (!(via[0].acr & 0x20)) {	// one-shot mode?
		if (!via[0].t2c--)
			via[0].ifr |= 0x20;
	}

	if (!via[1].t1c--) {
		if (via[1].acr & 0x40)	// free-run mode?
			via[1].t1c = via[1].t1l; // reload from latch
		via[1].ifr |= 0x40;
		// Set VIA2 timer 1 IRQ
		if (via[1].ier & 0x40)
			irqFlag |= 0xC0;
	}

	if (!(via[1].acr & 0x20)) {	// one-shot mode?
		if (!via[1].t2c--) {
			if (via2_t2to_enable) {
				via2_t2to_enable = false;
				via[1].ifr |= 0x20;
				// Set VIA2 timer 2 IRQ, set only when t2 hi is re-written
				/*if (via[1].ier & 0x20)
					irq_flag |= 0xA0;*/
			}
		}
	}
#else
	via[0].countTimers();
	via[1].countTimers();
#endif
}

/*
   Serial ports have changed, recalculate IEC bus state

	$1800 - VIA 1 port B

	bit 0 : DATA IN
	bit 1 : DATA OUT
	bit 2 : CLK IN
	bit 3 : CLK OUT
	bit 4 : ATN acknowledge OUT
	bit 5,6 : Device address preset switches
	bit 7 : ATN IN
*/
inline void DRIVEMEM::UpdateSerialPort()
{
	unsigned char byte = ~via[0].prb & via[0].ddrb;

	// DATA (including ATN acknowledge)
	serialPort[DeviceNr] = ((byte << 6) & ((~byte ^ serialPort[0]) << 3) & 0x80)	// DATA+ATN
				  |((byte << 3) & 0x40); // CLK
	bus_state_change = false;
#if LOG_SERIAL
	fprintf(stderr, "1541: serial write : %02X\n", via[0].prb);
	fprintf(stderr, "1541: serial written: %02X.\n", serialPort[DeviceNr]);
#endif
}

void DRIVEMEM::UpdateSerialState(unsigned char newAtn)
{
	UpdateSerialPort();
	newAtn &= 0x10;
	//   Interrupt on 1 -> 0 transition (negative edge) of ATN on serial IEC bus
	if (oldAtnIn && !newAtn) {
		//fprintf(stderr, "IEC irq: %i\n", devnr>>5);
		// Set via interrupt on serial attention
		via[0].ifr |= (via[0].pcr << 1);
		SetIRQflag(via[0].ifr & via[0].ier);
	}
	oldAtnIn = newAtn;
}

void DRIVEMEM::Reset()
{
	// clears all 6522 internal registers to logic 0
	// (except T1 and T2 latches and counters and the shift register)
	via[0].reset();
	via[1].reset();

	// motor is on after reset
	fdc->SetDriveMotor(via[1].prb = 0x0C);
	irqFlag = 0;
}

inline unsigned char DRIVEMEM::ReadVIA(unsigned int adr)
{
	// VIA 1
	switch (adr & 0x1C0F) {
		case 0x1800:
			{
				unsigned char serial_bus = readBus();
				unsigned char serial_state =  (serial_bus >> 7)		// DATA
											|((serial_bus >> 4) & 0x04)	// CLK
											|((serialPort[0] << 3) & 0x80); // ATN OUT -> DATA

				//Log::write( "#%i, $1800 read : %02X\n", devnr>>5, serial_bus);
				// FIXME! bit 5 and 6 gives the device number thru 2 jumpers
				//return (via[0].prb & 0x1A | serial_state | devnr) ^ 0x85;// $1A  ddrb
				return (via[0].prb & via[0].ddrb)
					| (((serial_state ^ 0x85) | devnr | 0x1A | ((via[0].pb7 ^ 0x80) & via[0].acr & 0x80)) & ~via[0].ddrb) ;// $1A  ddrb
			}
		case 0x1801:
		case 0x180F:
			via[0].ifr &= 0xFD;
			SetIRQflag( via[0].ier & via[0].ifr );
			return (via[0].pra & via[0].ddra) | (ppIn & ~via[0].ddra);
				// |0x01 1541C ROM check (track 0 sensor)
		case 0x1804:
		case 0x1808:
		case 0x180A:
			{
				unsigned char retval = via[0].read(adr);
				//SetIRQflag(via[0].ier & via[0].ifr);
				//fprintf(stderr, "%02X read: %02X\n", adr, retval);
				return retval;
			}
		case 0x1802:
		case 0x1803:
		case 0x1805:
		case 0x1806:
		case 0x1807:
		case 0x1809:
		case 0x180B:
		case 0x180C:
		case 0x180D:
		case 0x180E:
			return via[0].read(adr);

		// VIA 2
		case 0x1C00:
			return (via[1].prb & via[1].ddrb)
					| ((fdc->WPState() | fdc->SyncFound()) & ~via[1].ddrb);
		case 0x1C01:
		case 0x1C0F:
			//fdc->ClearByteReady();
			return fdc->readGCRByte();
			//return (fdc->ReadGCRByte() & ~via[1].ddrb) | (via[1].prb & via[1].ddrb);
		case 0x1C04:
		case 0x1C08:
		case 0x1C0A:
			{
				unsigned char retval = via[1].read(adr);
				SetIRQflag(via[1].ier & via[1].ifr);
				return retval;
			}
		case 0x1C02:
		case 0x1C03:
		case 0x1C05:
		case 0x1C06:
		case 0x1C07:
		case 0x1C09:
		case 0x1C0B:
		case 0x1C0C:
		case 0x1C0D:
		case 0x1C0E:
			return via[1].read(adr);

		// open address space
		default:
			return adr >> 8;
	}
}

unsigned char DRIVEMEM::Read(unsigned int addr)
{
	addr &= 0xFFFF;
	// 1541 ROM is shadowed between $8000-$BFFF except rev B. boards
	if (addr >= 0x8000)
		return rom[addr & 0x3FFF];
	else if (addr & 0x1800)
		return ReadVIA(addr);
	else
		return Ram[addr & 0x07FF];
}

void DRIVEMEM::Write(unsigned int addr, unsigned char value)
{
	if (!(addr & 0x1800))
		Ram[addr & 0x07FF] = value;
	else if (addr < 0x8000) {
		unsigned int viaIndex = (addr & 0x0400) ? 1 : 0;
		via[viaIndex].reg[addr & 0x0F] = value;
		// VIA 1
		switch (addr & 0x1C0F) {
			case 0x1800:
				if (via[0].prb != value) {
					via[0].prb = value;
					bus_state_change = true;
				}
				break;
			case 0x1802:
				if (via[0].ddrb != value) {
					via[0].ddrb = value;
					UpdateSerialPort();
				}
				break;
			case 0x1801:
				// Acknowledge IEC IRQ otherwise unused
			case 0x1805:
			case 0x1807:
			case 0x1809:
			case 0x180D:
			case 0x180E:
				via[0].write(addr, value);
				SetIRQflag(via[0].ier & via[0].ifr);
				break;
			case 0x1803:
			case 0x1804:
			case 0x1806:
			case 0x1808:
			case 0x180A:
			case 0x180B:
			case 0x180C:				
			case 0x180F: // Unused
				via[0].write(addr, value);
				break;

				// VIA 2
			case 0x1C00:
				// bits 0/1: Head stepper motor
				if ((via[1].prb ^ value) & 3) {
					if ((via[1].prb & 3) == ((value + 1) & 3))
						fdc->moveHeadOut();
					else if ((via[1].prb & 3) == ((value - 1) & 3))
						fdc->moveHeadIn();
				}
				// bit #3: Drive LED
	//				if ((via[1].prb ^ value) & 8)
	//					theLed->Update( (value << 2) & 0x20 );
					// bit #2: Drive motor on/off
				if ((via[1].prb ^ value) & 4)
					fdc->SetDriveMotor(value & 4);
				// Bit 5 and 6 density select
				if ((via[1].prb ^ value) & 0x60)
					fdc->SetDensity(value & 0x60);
				// Bit 7 is synch?
				via[1].prb = value & 0xEF; // was 0xEF
				break;
			case 0x1C01:
				fdc->WriteGCRByte(value);
				via[1].pra = value;
				break;
			case 0x1C0C:
				// bit #1 is the 'byte-ready' line, sets V flag to 1 also (SO enable?)
				// bit #5 controls the head's read/write mode. 1 is read, 0 is write.
				if (value != via[1].pcr) {
					if ((value & 0xC0) == 0xC0) {
						unsigned char pinCB2;
						pinCB2 = value & 0x20;
						fdc->SetRWMode(pinCB2);
					}
					//					if ((value&0x0C) == 0x0C) {
					//						unsigned char pinCA2;
					//						pinCA2 = value & 0x02;
					//						/*if ( fdc->is_byteReady() && pinCA2 ) {
					//							unsigned char *bre = fdc->get_byteReady_edge();
					//							*bre = 1;
					//						}*/
					//					}
					via[1].pcr = value;
				}
				break;
			case 0x1C09:
			case 0x1C05:
			case 0x1C07:
			case 0x1C0D:
			case 0x1C0E:
				via[1].write(addr, value);
				SetIRQflag(via[1].ier & via[1].ifr);
				break;

			case 0x1C0F:
			case 0x1C02:
			case 0x1C03:
			case 0x1C04:
			case 0x1C06:
			case 0x1C08:
			case 0x1C0A:
			case 0x1C0B:
				via[1].write(addr, value);
				break;
		}
	}
}
