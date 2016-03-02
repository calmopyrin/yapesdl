#ifndef _SERIAL_H
#define _SERIAL_H

#include <stdio.h>

#define LOG_SERIAL 0

class CSerial {

protected:
	CSerial *PrevDevice;
    CSerial *NextDevice;
	char Name[16];
	unsigned int DeviceNr;
	static unsigned int NrOfDevicesAttached;
    static CSerial *RootDevice;
    static CSerial *LastDevice;

public:
	CSerial();
	virtual ~CSerial();
	CSerial(unsigned int DevNr);
	CSerial *getNext() { return NextDevice; };
	unsigned int getDeviceNumber() { return DeviceNr; };
	//
	virtual void update() {};
	virtual void UpdateSerialState(unsigned char ) { };
	virtual unsigned char readBusWithUpdate() {
		return readBus();
	}
	// State of IEC lines (bit 7 - DATA, bit 6 - CLK, bit 4 - ATN)
	static unsigned char serialPort[16];
	static void InitPorts();
	static unsigned char readBus();
	static class CSerial *Devices[16];
	static CSerial *getRoot() { return RootDevice; };
	//
	friend class TED;
};

inline unsigned char CSerial::readBus()
{
	CSerial *sDevPtr = CSerial::getRoot();
	while (sDevPtr) {
		sDevPtr->update();
		sDevPtr = sDevPtr->getNext();
	}
	// the bus is using open-collectors so do an AND with all devices attached
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
		return retval;
	}
#else
	unsigned char retVal = serialPort[0]
		&serialPort[4]&serialPort[5] // printers
		&serialPort[8]&serialPort[9]&serialPort[10]&serialPort[11]; // drives
	return retVal;
#endif
}

class CTrueSerial : public CSerial {
public:
	CTrueSerial(unsigned int DevNr) : CSerial(DevNr) { };
};

class IEC;
class CIECDevice;

class IecFakeSerial : public CSerial
{
public:
	IecFakeSerial(unsigned int DevNr, CIECDevice *iecDev);
	virtual ~IecFakeSerial() {};
	virtual void UpdateSerialState() {
		update();
	};
	virtual void UpdateSerialState(unsigned char newAtn) {
		update();
		writeBus(newAtn);
	};
	virtual unsigned char readBusWithUpdate() {
#if 0
		update();
		return serialPort[dev_nr];
#else
		return readBus();
#endif
	}
	virtual unsigned char readBusWithoutUpdate() {
		return serialPort[0]
			& serialPort[4]
			& serialPort[8] & serialPort[9] & serialPort[10] & serialPort[11];
	}
	void writeBus(unsigned char newLines);
	inline void updateBus() {
		serialPort[dev_nr] = dataLine | clkLine;
	}
	virtual void update();
private:
	IecFakeSerial();
protected:
	void interpretIecByte();
	CIECDevice *iecDevice;
	unsigned int state;	
	unsigned int step;		
	unsigned int addr;	
	unsigned int secondaryAddress;
	unsigned int secondaryAddress_prev;	
	unsigned char dev_nr;
	size_t timeout;
	size_t cycleCount;
	unsigned char errorState;
	unsigned int eoi;
	unsigned int clkLine;
	unsigned int dataLine;
	unsigned char io_byte;
	unsigned int bitCounter;
	unsigned int dataTransfered;
	unsigned char atnInLine;
	unsigned char oldAtnLine;
	unsigned long clock_rate;
	//
	char nameBuffer[16];	// Buffer for file names and command strings
	char *namePtr;	// Pointer for reception of file name
	int nameLength;	// Received length of file name
};

#endif // _SERIAL_H
