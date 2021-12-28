#ifndef _IEC_H
#define _IEC_H

#include "types.h"

enum {
	// IEC command codes normally sent under ATN low
	IEC_CMD_LISTEN = 0x20,
	IEC_CMD_UNLISTEN = 0x30,
	IEC_CMD_TALK = 0x40,
	IEC_CMD_UNTALK = 0x50,
	// IEC command codes
	IEC_CMD_DATA = 0x60,	// Data transfer
	IEC_CMD_CLOSE = 0xE0,	// Close channel
	IEC_CMD_OPEN = 0xF0		// Open channel
};

class CIECInterface {
  public:
    virtual ~CIECInterface() {};
    // IEC command & control codes
	enum {
		CMD_LISTEN = 0x20,
		CMD_UNLISTEN = 0x30,
		CMD_TALK = 0x40,
		CMD_UNTALK = 0x50,
		CMD_DATA = 0x60,	// Data transfer
		CMD_CLOSE = 0xe0,	// Close channel
		CMD_OPEN = 0xf0		// Open channel
	};
    virtual void Reset() = 0;
    virtual unsigned int Listen() = 0;
    virtual unsigned int Unlisten() = 0;
    virtual void Talk() = 0;
    virtual void Untalk() = 0;
    virtual unsigned int In(unsigned char *data) = 0;
    virtual unsigned int Out(unsigned char data) = 0;
    virtual unsigned int OutCmd(unsigned char data) = 0;
	virtual unsigned int OutSec(unsigned char data) = 0;
    virtual unsigned char Status() = 0;
protected:
	unsigned char nameBuffer[512];	// Buffer for file names and command strings
	unsigned char *namePtr;
	unsigned int nameLength;
};

#include "device.h"

class CFakeIEC : public CIECInterface {
  protected:
  	enum { STATE_IDLE = 0, STATE_TALKING, STATE_LISTENING };
  	enum { IEC_OK = 0, IEC_EOF = 0x40, IEC_ERROR = 0x80 };
	unsigned int	state;
	unsigned char	status;
	unsigned int received_cmd;
	unsigned int	prev_cmd;
	unsigned int	secondaryAddress;
	unsigned int	prev_addr;
	unsigned int dev_nr;
	CIECDevice *Device;
	unsigned int DispatchIECCmd(unsigned char cmd);
  public:
//	CFakeIEC() {}
    CFakeIEC(unsigned int dn) { dev_nr = dn; };
    virtual void Reset();
    virtual unsigned int Listen();
    virtual unsigned int Unlisten();
    virtual void Talk();
    virtual void Untalk();
    virtual unsigned int	In(unsigned char *data);
    virtual unsigned int Out(unsigned char data);
    virtual unsigned int OutCmd(unsigned char data);
	virtual unsigned int OutSec(unsigned char data);
    virtual unsigned char Status() { return status; };
    void AddIECDevice(CIECDevice *dev) { Device = dev; };
};

// opencbm
class CRealIEC : public CFakeIEC {
  public:
	CRealIEC();
	CRealIEC(unsigned int dn) : CFakeIEC(dn) { dev_nr = dn; };
	unsigned int Init();
	unsigned int RawRead(unsigned int secondaryAddress, unsigned char *data);
	unsigned int RawWrite(unsigned int secondaryAddress, unsigned char data);
    virtual void Reset();
    virtual unsigned int Listen();
    virtual unsigned int Unlisten();
    virtual void Talk();
    virtual void Untalk();
    virtual unsigned int In(unsigned char *data);
    virtual unsigned int Out(unsigned char data);
	virtual unsigned int OutCmd(unsigned char data);
	virtual unsigned int OutSec(unsigned char data);
  protected:
	unsigned char	cmd_buffer[255];
	int		cmd_len;
};

#endif // _IEC_H
