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
	via2_t2to_enable = false;
	devnr = (dev_num & 7) << 5;
	bus_state_change = false;
	serialPort[DeviceNr] = 0x85;
	ppIn = 0xFF;
	oldAtnIn = 1;
	Reset();
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
	// ATN 1->0
	newAtn &= 0x10;
	if (oldAtnIn && !newAtn) {
		//fprintf(stderr, "IEC irq: %i\n", devnr>>5);
		ATNlow(); // Set via interrupt on serial attention
	}
	oldAtnIn = newAtn;
}

void DRIVEMEM::Reset()
{
	// clears all 6522 internal registers to logic 0
	// (except T1 and T2 latches and counters and the shift register)
	via[0].pra = via[0].ddra = via[0].prb = via[0].ddrb = 0;
	via[1].pra = via[1].ddra = via[1].ddrb = 0;
	via[0].ifr = via[0].ier = via[1].ifr = via[1].ier =0;
	via[0].acr = via[0].pcr = via[1].acr = via[1].pcr = 0;

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
					| (((serial_state ^ 0x85) | devnr) & ~via[0].ddrb) ;// $1A  ddrb
			}
		case 0x1801:
		case 0x180F:
			via[0].ifr &= 0xFD;
			SetIRQflag( via[0].ier & via[0].ifr );
			return (via[0].pra & via[0].ddra) | (ppIn & ~via[0].ddra);
				// |0x01 1541C ROM check (track 0 sensor)
		case 0x1802:
			return via[0].ddrb;
		case 0x1803:
			return via[0].ddra;
		case 0x1804:
			via[0].ifr &= 0xBF;
			SetIRQflag( via[0].ier & via[0].ifr );
			return via[0].t1c&0xFF;
		case 0x1805:
			return via[0].t1c >> 8;
		case 0x1806:
			return via[0].t1l&0xFF;
		case 0x1807:
			return via[0].t1l >> 8;
		case 0x1808:
			via[0].ifr &= 0xDF;
			SetIRQflag( via[0].ier & via[0].ifr );
			return via[0].t2c&0xFF;
		case 0x1809:
			return via[0].t2c >> 8;
		case 0x180A:
			// the shift register IRQ is cleared on read
			via[0].ifr &= 0xFB;
			SetIRQflag( via[0].ier & via[0].ifr );
			return via[0].sr;
		case 0x180B:
			return via[0].acr;
		case 0x180C:
			return via[0].pcr;
		case 0x180D:
			// bit #7 will be read as 1 if any IRQ is active
			return via[0].ifr | (via[0].ifr & via[0].ier & 0x7F ? 0x80 : 0);
		case 0x180E:
			return via[0].ier | 0x80;

		// VIA 2
		case 0x1C00:
			return (via[1].prb & via[1].ddrb)
					| (fdc->WPState() | (fdc->SyncFound() & ~via[1].ddrb));
		case 0x1C01:
		case 0x1C0F:
			//fdc->ClearByteReady();
			return fdc->readGCRByte();
			//return (fdc->ReadGCRByte() & ~via[1].ddrb) | (via[1].prb & via[1].ddrb);
		case 0x1C02:
			return via[1].ddrb;
		case 0x1C03:
			return via[1].ddra;
		case 0x1C04:
			// Clear VIA2 timer1 IRQ
			via[1].ifr &= 0xBF;
			SetIRQflag( via[1].ier & via[1].ifr );
			return via[1].t1c&0xFF;
		case 0x1C05:
			return via[1].t1c >> 8;
		case 0x1C06:
			return via[1].t1l&0xFF;
		case 0x1C07:
			return via[1].t1l >> 8;
		case 0x1C08:
			// Clear VIA2 timer2 IRQ
			via[1].ifr &= 0xDF;
			SetIRQflag( via[1].ier & via[1].ifr );
			return via[1].t2c&0xFF;
		case 0x1C09:
			return via[1].t2c >> 8;
		case 0x1C0A:
			// the shift register IRQ is cleared on read
			via[1].ifr &= 0xFB;
			return via[1].sr;
		case 0x1C0B:
			return via[1].acr;
		case 0x1C0C:
			/*	Bit0     - CA1 Interrupt Control - Byte ready
				Bit1,2,3 - CA2 Interrupt Control - SOE - Set Overflow Enable for 6502
				Bit4     - CB1 Interrupt Control
				Bit5,6,7 - CB2 Interrupt Control
				bit #0 is the 'byte-ready' line, affects V flag also
				bit #1 is set OV enable
			*/
			//return (via[1].pcr&0xFE) | ((fdc->is_byteReady()>>7));
			return via[1].pcr;
		case 0x1C0D:
			// bit #7 will be read as 1 if any IRQ is active
			return via[1].ifr | (via[1].ifr & via[1].ier & 0x7F ? 0x80 : 0);
		case 0x1C0E:
			return via[1].ier | 0x80;

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
	else if (addr < 0x8000)
		// VIA 1
		switch (addr & 0x1C0F) {
			case 0x1800:
				if ( via[0].prb != value ) {
					via[0].prb = value;
					bus_state_change = true;
				}
				break;
			case 0x1801:
				// Acknowledge IEC IRQ otherwise unused
				via[0].ifr &= 0x7D;
				via[0].pra = value;
				SetIRQflag( via[0].ier & via[0].ifr );
				break;
			case 0x1802:
				if (via[0].ddrb != value) {
					via[0].ddrb = value;
					UpdateSerialPort();
				}
				break;
			case 0x1803:
				// Unused!
				via[0].ddra = value;
				break;
			case 0x1804:
			case 0x1806:
				via[0].t1l = (via[0].t1l & 0xFF00) | value;
				break;
			case 0x1805:
				via[0].t1l = (via[0].t1l & 0xFF) | (value << 8);
				via[0].ifr &= 0xBF;
				// FIXME!
				SetIRQflag(via[0].ier & via[0].ifr);
				via[0].t1c = via[0].t1l;
				break;
			case 0x1807:
				via[0].t1l = (via[0].t1l & 0xFF) | (value << 8);
				break;
			case 0x1808:
				via[0].t2l = (via[0].t2l & 0xFF00) | value;
				break;
			case 0x1809:
				via[0].t2l = (via[0].t2l & 0xFF) | (value << 8);
				via[0].ifr &= 0xDF;
				// FIXME!
				SetIRQflag(via[0].ier & via[0].ifr);
				via[0].t2c = via[0].t2l;
				break;
			case 0x180A:
				via[0].sr = value;
				break;
			case 0x180B:
				/* ACR bits:
				  7 = output enable
				  6 = free-run enable
				  5 = timer 2 control (0=timed interrupt,1=countdown with pulses)
				  1 = PB latching enabled
				  0 = PA latching enabled
				*/
				via[0].acr = value;
				break;
			case 0x180C:
				via[0].pcr = value;
				break;
			case 0x180D:
				via[0].ifr &= ~( value | 0x80 );
				SetIRQflag( via[0].ier & via[0].ifr );
				break;
			case 0x180E:
				if (value & 0x80)
					via[0].ier |= value & 0x7F;
				else
					via[0].ier &= ~value;
				SetIRQflag( via[0].ier & via[0].ifr );
				break;
			case 0x180F:
				// Unused
				break;

			// VIA 2
			case 0x1C00:
				// bits 0/1: Head stepper motor
				if ((via[1].prb ^ value) & 3) {
					if ((via[1].prb & 3) == ((value+1) & 3))
						fdc->moveHeadOut();
					else if ((via[1].prb & 3) == ((value-1) & 3))
						fdc->moveHeadIn();
                }
				// bit #3: Drive LED
//				if ((via[1].prb ^ value) & 8)
//					theLed->Update( (value << 2) & 0x20 );
				// bit #2: Drive motor on/off
				if ((via[1].prb ^ value) & 4)
					fdc->SetDriveMotor( value & 4);
				// Bit 5 and 6 density select
				if (( via[1].prb ^ value ) & 0x60 )
					fdc->SetDensity(value & 0x60);
				// Bit 7 is synch?
				via[1].prb = value & 0xEF; // was 0xEF
				break;
			case 0x1C01:
				fdc->WriteGCRByte( value );
			case 0x1C0F:
				via[1].pra = value;
				break;
			case 0x1C02:
				via[1].ddrb = value;
				break;
			case 0x1C03:
				via[1].ddra = value;
				break;
			case 0x1C04:
			case 0x1C06:
				via[1].t1l = (via[1].t1l & 0xFF00) | value;
				break;
			case 0x1C05:
				via[1].t1l = (via[1].t1l & 0xFF) | (value << 8);
				// Clear timer1 IRQ
				via[1].ifr &= 0xBF;
				SetIRQflag(via[1].ier & via[1].ifr);
				via[1].t1c = via[1].t1l;
				break;
			case 0x1C07:
				via[1].t1l = (via[1].t1l & 0xFF) | (value << 8);
				break;
			case 0x1C08:
				via[1].t2l = (via[1].t2l & 0xFF00) | value;
				break;
			case 0x1C09:
				via2_t2to_enable = true;
				via[1].t2l = (via[1].t2l & 0xFF) | (value << 8);
				// Clear timer2 IRQ
				via[1].ifr &= 0xDF;
				SetIRQflag(via[1].ier & via[1].ifr);
				via[1].t2c = via[1].t2l;
				break;
			case 0x1C0A:
				via[1].sr = value;
				break;
			case 0x1C0B:
				via[1].acr = value;
				break;
			case 0x1C0C:
				// bit #1 is the 'byte-ready' line, sets V flag to 1 also (SO enable?)
				// bit #5 controls the head's read/write mode. 1 is read, 0 is write.
				if ( value != via[1].pcr) {
					if ((value&0xC0) == 0xC0) {
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
			case 0x1C0D:
				via[1].ifr &= ~(value | 0x80);
				// FIXME! Is this OK?
				SetIRQflag( via[1].ier & via[1].ifr );
				break;
			case 0x1C0E:
				if (value & 0x80)
					via[1].ier |= value & 0x7F;
				else
					via[1].ier &= ~value;
				SetIRQflag( via[1].ier & via[1].ifr );
				break;
		}
}
