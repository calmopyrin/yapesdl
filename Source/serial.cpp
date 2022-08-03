#include "serial.h"
#include "iec.h"
#include "tedmem.h"
#include "tcbm.h"

enum {
	ST_OK = 0,				// No error
	ST_EOI = 0x40,			// Timeout 
	ST_NOT_FOUND = 0x80,	// File not found error
};

unsigned char CSerial::serialPort[16];
//unsigned char CSerial::Line[16];
class CSerial *CSerial::Devices[16];
unsigned int CSerial::NrOfDevicesAttached;
CSerial *CSerial::RootDevice = 0;
CSerial *CSerial::LastDevice = 0;

CSerial::CSerial()
{
	DeviceNr = 0;
}

void CSerial::InitPorts()
{
	for (int i=0; i<16; i++)
		serialPort[i] = 0xC0;
}

CSerial::CSerial(unsigned int DevNr) : DeviceNr(DevNr)
{
	NrOfDevicesAttached++;
	
	if (RootDevice == 0) {
	    PrevDevice = 0;
	    NextDevice = 0;
	    for (int i=0; i<4; i++)
	    	Devices[i] = 0;
	    RootDevice = this;
	    LastDevice = this;
	} else {
	    PrevDevice = LastDevice;
	    LastDevice->NextDevice = this;
	    LastDevice = this;
	    NextDevice = 0;
	}
	
	Devices[DevNr] = this;
	
	sprintf( Name, "Device #%u", DevNr);
	//cerr << Name << " with address " << this << " created." << endl;
}

CSerial::~CSerial()
{
	// don't do it for the machine
	if (DeviceNr) {
		if (!NrOfDevicesAttached)
			return;
		//cout << "Deleting device #" << DeviceNr << endl;
		if (!--NrOfDevicesAttached) {
			RootDevice = 0;
			LastDevice = 0;
		} else if (Devices[DeviceNr]->PrevDevice) {
			Devices[DeviceNr]->PrevDevice->NextDevice = Devices[DeviceNr]->NextDevice;
			if ( this == LastDevice) {
				LastDevice = PrevDevice;
			}
		}
		Devices[DeviceNr] = NULL;
	}
}

//-------------------

#define ATN_LO 0
#define CLK_LO 0
#define DATA_LO 0
#define ATN_HI 0x10
#define DATA_HI 0x80
#define CLK_HI 0x40

#define IEC_DEBUG 0

enum {
	IEC_STATE_IDLE = 0,
	IEC_STATE_ATN = 1,
	IEC_STATE_TALKING = 2,
	IEC_STATE_LISTENING = 4
};

// Serial bus mode states 
enum {
	SM_NONE,
	SM_WAITTIMEOUT,
	SM_WAITCLK0,
	SM_RDY_TO_SEND,
	SM_READY_FOR_DATA,
	SM_EOI,
	SM_EOI2,
	SM_BITTRANS,
	SM_BITTRANS2,
	SM_BYTEREADY,
	SM_BYTEREADY2,
	SM_FERROR0,
	SM_FERROR1,
 	SM_TEMP
};

IecFakeSerial::IecFakeSerial(unsigned int DevNr, CIECDevice *iecDev) : CSerial(DevNr), iecDevice(iecDev)
{
	atnInLine = ATN_HI;
	oldAtnLine = ATN_HI;
	state = IEC_STATE_IDLE;
	step = SM_NONE;
	eoi = 0;
	clkLine = CLK_HI;
	dataLine = DATA_HI;
	errorState = ST_OK;
	dev_nr = DevNr;
	dataTransfered = 0;
	bitCounter = 0;
	cycleCount = 0;

	namePtr = nameBuffer;
}

void IecFakeSerial::interpretIecByte()
{
#if IEC_DEBUG >= 1
	fprintf(stderr, "Interpreting addresses %02X:%02X.\n", addr, secondaryAddress);
	fprintf(stderr, "Previous secondary address %02X.\n", secondaryAddress_prev);
#endif
	switch (addr&0x70) {
		case IEC_CMD_LISTEN:
			state |= IEC_STATE_LISTENING;
			state &= ~IEC_STATE_TALKING;
			step = SM_WAITCLK0;
			dataLine = DATA_LO;
			clkLine = CLK_HI;
			break;

		case IEC_CMD_TALK:
			state |= IEC_STATE_TALKING;
			state &= ~IEC_STATE_LISTENING;
			step = SM_WAITTIMEOUT;
			dataLine = DATA_LO;
			clkLine = CLK_HI;
			eoi = 0;
			break;
		
		case IEC_CMD_UNLISTEN:
			if ( state & IEC_STATE_LISTENING) {
				if ( (secondaryAddress_prev & 0xF0) == IEC_CMD_OPEN && dev_nr >= 8) {
					*namePtr = 0;
					//errorState = iecDevice->Open(secondaryAddress_prev & 0x0F, nameBuffer);
					errorState = iecDevice->Open(secondaryAddress_prev & 0x0F, 0);
				}
				state &= ~IEC_STATE_LISTENING;
			}
			break;
	
		case IEC_CMD_UNTALK:
			state &= ~IEC_STATE_TALKING;
			//state &= ~IEC_STATE_LISTENING;
			dataTransfered = 0;
			break;  	
	}
	
	switch (secondaryAddress&0xF0) {
		case IEC_CMD_OPEN:
			if (!dataTransfered && dev_nr >= 8) {
				namePtr = nameBuffer;
				nameLength = 0;
			}
			errorState = ST_OK;
  			break;	
		case IEC_CMD_CLOSE:
			if (dev_nr >= 8)
				iecDevice->Close(secondaryAddress & 0x0F);
			dataTransfered = 0;
			break;
		case IEC_CMD_DATA:
			dataTransfered = 1;
			break;
	}
	updateBus();
}

void IecFakeSerial::update()
{
#if IEC_DEBUG >= 6
	fprintf(stderr, "Device #%i.\n", dev_nr);
#endif
	cycleCount = (size_t) TED::instance()->GetClockCount();

	if ( !atnInLine && (oldAtnLine&ATN_HI) && !(state & IEC_STATE_ATN)) { // ATN 1 -> 0 

	    dataLine = DATA_LO; // device present 
		step = SM_WAITTIMEOUT;
		addr = 0;
		secondaryAddress_prev = secondaryAddress;
		secondaryAddress = 0;
		state |= IEC_STATE_ATN;
		timeout = cycleCount + TED::usec2cycles(100);
		updateBus();
		return;

	} else if (atnInLine && (state & IEC_STATE_ATN)) { // ATN 0 -> 1 

		state &= ~IEC_STATE_ATN;
		eoi = 0;
		if ( addr == (IEC_CMD_LISTEN | dev_nr) || addr == (IEC_CMD_TALK | dev_nr)) {
			interpretIecByte();
			updateBus();
		} else if (addr == 0x3F || addr == 0x5F) {
			interpretIecByte();
			if ( !(state & (IEC_STATE_TALKING | IEC_STATE_LISTENING)) ) {
				dataLine = DATA_HI;
				clkLine = CLK_HI;
			}
#if IEC_DEBUG >= 2
			fprintf(stderr, "ATN 0 -> 1.\n");
#endif
			updateBus();
		}
		return;
	}

	if (state & (IEC_STATE_ATN | IEC_STATE_LISTENING) ) {
		
  		switch (step) {

			case SM_NONE:
#if IEC_DEBUG >= 2
	fprintf(stderr, "Idling.\n");
#endif				
				break;
			
			case SM_WAITTIMEOUT:
				if (cycleCount >= timeout) {
					step = SM_WAITCLK0;
#if IEC_DEBUG >= 1
			fprintf(stderr, "Resetting bus state\n");
#endif	
				}
				break;
			
			case SM_WAITCLK0:
				if (!(readBusWithoutUpdate() & CLK_HI)) {
					step = SM_RDY_TO_SEND;
#if IEC_DEBUG >= 2
					fprintf(stderr, "Ready to send.\n");
#endif					
				}
				break;
			
			case SM_RDY_TO_SEND:
				if (readBusWithoutUpdate() & CLK_HI) {
					dataLine = DATA_HI;
					clkLine = CLK_HI;
					step = SM_READY_FOR_DATA;
					timeout = cycleCount + TED::usec2cycles(60); // 200 max, 60 avg
#if IEC_DEBUG >= 2
					fprintf(stderr, "DATA -> 1, ready for data, mainClock: %i, timeout:%i.\n",
      						cycleCount, timeout);
#endif
				}
				break;
			
			case SM_READY_FOR_DATA:
				if (!(readBusWithoutUpdate() & CLK_HI)) {
					step = SM_BITTRANS;
					bitCounter = 0;
					io_byte = 0; // Call function here
#if IEC_DEBUG >= 2
					fprintf(stderr, "Waiting on CLK=0.\n");
#endif
				} else if (cycleCount > (timeout + TED::usec2cycles(140)) && atnInLine) { // EOI! timeout > 200 us
					dataLine = DATA_LO;
					clkLine = CLK_HI;
					step = SM_EOI;
					timeout = cycleCount + TED::usec2cycles(60);
					eoi = 1;
#if IEC_DEBUG >= 1
					fprintf(stderr, "EOI!\n");
					fprintf(stderr, "Bus step:%i.\n", step);
#endif
				} 	
				break;
			
			case SM_BITTRANS:
				if (readBusWithoutUpdate() & CLK_HI) {
					const unsigned int level = readBusWithoutUpdate() & DATA_HI; // 0x80 / 0x00
					io_byte >>= 1;				
   					io_byte |= level;
#if IEC_DEBUG >= 2
					fprintf(stderr, "Transfering bit #%i:%i.\n", bitCounter, level ? 1 : 0);
#endif
					if (bitCounter++ >= 7) {
						bitCounter = 0;
						step = SM_BYTEREADY;
					} else
						step = SM_BITTRANS2;
				}
				break;
			
			case SM_BITTRANS2:
				if (!(readBusWithoutUpdate() & CLK_HI)) {
					step = SM_BITTRANS;	
    			}
				break;

			case SM_BYTEREADY:
				if (!(readBusWithoutUpdate() & CLK_HI)) {
					if (!atnInLine) {
						if (!addr)
							addr = io_byte;
						else if (!secondaryAddress) {
							secondaryAddress = io_byte;
							// !!! printer only
							if (dev_nr < 8)
								errorState = iecDevice->Open((secondaryAddress) & 0x0F, 0);
						}
						// Check device number sent if ours 
						if (!(addr&0x10) && ((addr&0xF)!=dev_nr) ) {
							step = SM_NONE;
							dataLine = DATA_HI;
							clkLine = CLK_HI;							
						} else {
							dataLine = DATA_LO;
							clkLine = CLK_HI;
							step = SM_RDY_TO_SEND;
						}
					} else if ( state & (IEC_STATE_LISTENING| IEC_STATE_ATN)) {
						// send byte to higher abstraction layer
						dataLine = DATA_LO;
						clkLine = CLK_HI;
						step = SM_RDY_TO_SEND;
						//if ( dataTransfered ) 
						if (state & (IEC_STATE_LISTENING))
						{
							unsigned int command = dataTransfered ? CIECInterface::CMD_DATA : CIECInterface::CMD_OPEN;
							switch (dev_nr) {
								case 8:
								case 9:
								case 10:
								case 11:
									errorState = iecDevice->Write( secondaryAddress & 0x0F, io_byte, command, eoi != 0);
									break;
								case 4:
								case 5:
									errorState = iecDevice->Write( secondaryAddress & 0x0F, io_byte, command, eoi != 0);
									//errorState = printer_send_byte( io_byte, eoi)
									;
							}
							
						}
						eoi = 0;
					}
#if IEC_DEBUG >= 1
					fprintf(stderr, "Byte finished: %02X %c.\n", io_byte, (char) io_byte);
#endif
				}
				break;

			case SM_EOI:
				if (cycleCount >= timeout) {
					dataLine = DATA_HI;
					clkLine = CLK_HI;
					step = SM_EOI2;
#if IEC_DEBUG >= 2
					fprintf(stderr, "EOI timed out, setting DATA -> 0.\n");
#endif					
     			}		
				break;
			
			case SM_EOI2:
				if (!(readBusWithoutUpdate() & CLK_HI)) {
					step = SM_BITTRANS;		
#if IEC_DEBUG >= 2
					fprintf(stderr, "Starting bit transfer with ATN hi.\n");
#endif					
     			}		
				break;
		}
	
	} else if (state & (IEC_STATE_TALKING)) {
		
		// Device is commanded to TALK  

		switch( step) {
		
			case SM_WAITTIMEOUT: // LISTEN <-> TALK turnaround 
				if (readBusWithoutUpdate() & CLK_HI) {
					step = SM_WAITCLK0;
					// At this time, both the Clock line and the Data line are being held down to the true state
					// the talker is holding the Clock line true and
					// the listener is holding the Data line true.
					clkLine = CLK_LO;
					// FILE NOT FOUND ERROR?
					if (errorState == ST_NOT_FOUND) {
						dataLine = DATA_LO;
					} else {
						dataLine = DATA_HI;
					}
					timeout = cycleCount + TED::usec2cycles(80); // 80
#if IEC_DEBUG >= 1
					fprintf(stderr, "Turnaround.\n");
#endif	
				}
				break;
			
			case SM_WAITCLK0:
				if (cycleCount >= timeout) {
					step = SM_RDY_TO_SEND;
					// When ready to go, release the Clock line to false
					clkLine = CLK_HI;
#if IEC_DEBUG >= 1
					fprintf(stderr, "Clock: %llu, Timeout:%llu.\n", cycleCount, timeout);
					fprintf(stderr, "Ready to send.\n");
#endif					
				}
				break;
			
			case SM_RDY_TO_SEND:
				// When the listener is ready to listen, it releases the Data line to false
				// wait endlessly
				if (readBusWithoutUpdate() & DATA_HI) { // Listener ready for data 
					// get byte from higher abstraction layer
					if (dataTransfered && dev_nr >= 8) {
						errorState = iecDevice->Read(secondaryAddress & 0x0F, &io_byte);
						if (errorState == ST_EOI) {
							eoi = 1;
#if IEC_DEBUG >= 1
							fprintf(stderr, "EOI coming.\n");
#endif
						}
					}
					// the talker will pull the Clock line back to true in less than 200 microseconds
					// Non-EOI Response to RFD typ: 40 max: 200
					timeout = cycleCount + TED::usec2cycles(eoi ? 350 : 40);
					step = SM_READY_FOR_DATA;
#if IEC_DEBUG >= 1
					fprintf(stderr, "Ready for data.\n");
#endif	
				}
				break;

			case SM_READY_FOR_DATA:
				if (cycleCount >= timeout) {
					bitCounter = 0;
					if (eoi) {
						step = SM_EOI;
						// EOI-Timeout Handshake 200-250 us
						timeout = cycleCount + TED::usec2cycles(250);
					} else {
						step = SM_BITTRANS;
						// the talker controls both lines, Clock and Data.  At the beginning of the sequence, 
						// it is holding the Clock true, while the Data line is released to false
						clkLine = CLK_LO;
						dataLine = DATA_HI;
						timeout = cycleCount + TED::usec2cycles(60);
#if IEC_DEBUG >= 1
						fprintf(stderr, "Transfering byte: %02X (%c).\n", io_byte, (char) io_byte);
#endif
					}
				}
				break;

/*
 ___	_____________________________________________________________________
 ATN
 ___	    _________ ___ ___ ___ ___ ___ ___ ___ ___       ________ ___ ___
 CLK	____|       |_| |_| |_| |_| |_| |_| |_| |_| |_______|      |_| |_| |_
	    :       :				    :       :      :
	    :Th :Tne:                               :Tf :Tbb:Th:Tne:
 ____	    :   :___:___________________________________:      :_____________
 DATA	________|   :|__||__||__||__||__||__||__||__|   |______|
	    :   :   : 0   1   2   3   4   5   6   7     :
	    :   :   :LSB                         MSB    :
	    :	:   :					:
	    :	:   : TALKER SENDING		Listener: Data Accepted
	    :	: LISTENER READY-FOR-DATA
	    : TALKER READY-TO-SEND

  Serial Bus Timing


	Description			        Symbol	 Min	 Typ	 Max

	ATN Response (required) 1)	 Tat      -	     -	    1000us
	Listener Hold-Off		     Th	      0	     -	    oo
	Non-EOI Response to RFD 2)	 Tne	  -	     40us	200us
	Bit Set-Up Talker  4)		 Ts	      20us	 70us	  -
	Data Valid			         Tv	      20us   20us	  -
	Frame Handshake  3)		     Tf	      0	     20	    1000us
	Frame to Release of ATN		 Tr	      20us	  -	    -
	Between Bytes Time		     Tbb	 100us	  -	    -
*/

			case SM_BITTRANS:
				if (cycleCount >= timeout) {
#if IEC_DEBUG >= 2
					fprintf(stderr, "Transfering bit #%i.\n", bitCounter);
#endif
					// When the talker figures the data has been held for a sufficient length of time, it pulls the Clock line true and  
					// releases the Data line to false. Then it starts to prepare the next bit
					clkLine = CLK_LO;
					dataLine = DATA_HI;
					step = SM_BITTRANS2;
					// bit setup time 70 us
					timeout = cycleCount + TED::usec2cycles(70);
				}
				break;

			case SM_BITTRANS2:
				if (cycleCount >= timeout) {
					// As soon as data line is set, the Clock line is released to false, signalling "data ready."  
					// The talker will typically have a bit in  place  and  be  signalling  ready in 70 microseconds or less
					dataLine = ((io_byte >> (bitCounter++)) & 1) << 7;
					clkLine = CLK_HI;
					// FIXME? Bit Valid time is 20 us
     				timeout = cycleCount + TED::usec2cycles(60);
					if (bitCounter >= 8) {
						step = SM_BYTEREADY;
#if IEC_DEBUG >= 1
						fprintf(stderr, "Byte finished: %02X (%c) (EOI:%i).\n", io_byte, (char) io_byte, eoi);
#endif
					} else     				
         				step = SM_BITTRANS;				
				}
				break;
			
			case SM_BYTEREADY:
				if (cycleCount >= timeout) {
					clkLine = CLK_LO;
					dataLine = DATA_HI;
     				// The talker is watching the Data line. The listener should pull the Data line true within one millisecond (typ: 20)
					timeout = cycleCount + TED::usec2cycles(20);
         			step = SM_BYTEREADY2;
				}   			
				break;
			
			case SM_BYTEREADY2:
				if (!(readBusWithoutUpdate() & DATA_HI)) {
					if (ST_EOI == errorState) { // EOI
						eoi = 1;
						state &= ~IEC_STATE_TALKING;
						errorState = 0;
						clkLine = CLK_HI;
						dataLine = DATA_HI;
					} else {
						timeout = cycleCount;
						step = SM_WAITCLK0;
					}
#if IEC_DEBUG >= 1
					fprintf(stderr, "Byte acknowledged.\n");
#endif
				} else if (cycleCount >= timeout + TED::usec2cycles(1000)) {
					dataLine = DATA_HI;
					clkLine = CLK_HI; // ???
					step = SM_RDY_TO_SEND;
					timeout = cycleCount + TED::usec2cycles(100);
#if IEC_DEBUG >= 0
					fprintf(stderr, "Timeout, no acknowledge, frame error!\n");
#endif
				}
				break;

			case SM_EOI:
				if (cycleCount >= timeout) {
					// Listener is pulling the Data line true for at least 60 microseconds...
					if (!(readBusWithoutUpdate() & DATA_HI)) {
						step = SM_EOI2;
						timeout = cycleCount + TED::usec2cycles(60);
     				} else {
						// Listener missed the EOI or did not care...
	#if IEC_DEBUG >= 1
						fprintf(stderr, "EOI _not_ acknowledged.\n");
	#endif
						step = SM_BITTRANS;
						timeout = cycleCount;
					}
				}
				break;
			
			case SM_EOI2:
				// ... and then releasing it.
				if (readBusWithoutUpdate() & DATA_HI) {
					if (cycleCount >= timeout + TED::usec2cycles(60)) {
#if IEC_DEBUG >= 1
						fprintf(stderr, "EOI acknowledged.\n");
#endif
						// within 60 microseconds it will pull the Clock line true
						timeout = cycleCount + TED::usec2cycles(30);
						// back to business
						step = SM_BITTRANS;
					}
#if IEC_DEBUG >= 1
					 else
						fprintf(stderr, "EOI _not_ acknowledged.\n");
#endif
     			}
				break;
			
			case SM_FERROR0:
				if (cycleCount >= timeout) {
					clkLine = CLK_LO;
					dataLine = DATA_HI;
					step = SM_FERROR1;
				}
				break;
			
			case SM_FERROR1:
				if ( !(readBusWithoutUpdate()&DATA_HI) ) {
					step = SM_WAITCLK0;
					timeout = cycleCount;
				}
				break;			
		}
	}
	updateBus();
}

void IecFakeSerial::writeBus(unsigned char newLines)
{
	oldAtnLine = atnInLine;
	atnInLine = newLines & ATN_HI;

	update();

#if IEC_DEBUG >= 2
	fprintf(stderr, "Serial write: DATA -> %i, CLK -> %i, ATN ->%i\n", (newLines&0x10)>0, 
			(newLines&0x20)>0, atnInLine>0);
#endif

}
