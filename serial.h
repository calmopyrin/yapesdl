#ifndef _SERIAL_H
#define _SERIAL_H

#define LOG_SERIAL 0

class CSerial {

protected:
	static unsigned int NrOfDevicesAttached;
    static CSerial *RootDevice;
    static CSerial *LastDevice;
	CSerial *PrevDevice;
    CSerial *NextDevice;
	char Name[16];
	unsigned int DeviceNr;

public:
	CSerial();
	~CSerial();
	CSerial(unsigned int DevNr);
	CSerial *getRoot() { return RootDevice; };
	CSerial *getNext() { return NextDevice; };
	unsigned int getDeviceNumber() { return DeviceNr; };
	// State of IEC lines (bit 7 - DATA, bit 6 - CLK, bit 4 - ATN)
	static unsigned char serialPort[16];
	static void InitPorts();
	static unsigned char readBus();
	virtual void UpdateSerialState(unsigned char ) { };
	static class CSerial *Devices[16];

	friend class TED;
};

// class for not real devices (printer)
class CTrueSerial : public CSerial {
public:
	CTrueSerial(unsigned int DevNr) : CSerial(DevNr) { };
};

// class for not real devices (printer)
class CRealSerialIEC : public CSerial {
	virtual void NewSerialState(unsigned char Clk);
};

class IEC;

// class for not real devices (printer)
class CFakeSerialIEC : public CSerial {
	virtual void NewSerialState(unsigned char Clk);
	IEC *iec;
};

// class for true serial drive emulation
class CTrueSerialIEC : public CSerial {
	virtual void NewSerialState(unsigned int Clk);
};

#endif // _SERIAL_H

