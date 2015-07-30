#include <stdio.h>
#include "serial.h"

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

unsigned char CSerial::readBus()
{
#if LOG_SERIAL
	{
		static unsigned char prev = 0xFF;
		unsigned char retval =
			serialPort[0]
					&serialPort[4]&serialPort[5] // printers
					&serialPort[8]&serialPort[9]&serialPort[10]&serialPort[11];
		if (retval ^ prev) {
			fprintf(stderr, "Serial read: %02X\n", retval);
			prev = retval;
		}
	}
#endif
    return
        serialPort[0]
        &serialPort[4]&serialPort[5] // printers
        &serialPort[8]&serialPort[9]&serialPort[10]&serialPort[11]; // drives
}

