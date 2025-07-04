#include "iec.h"

void CFakeIEC::Reset()
{
	state = STATE_IDLE;
	status = IEC_OK;
}

unsigned int CFakeIEC::Listen()
{ 
	state = STATE_LISTENING;
	return IEC_OK;
};

unsigned int CFakeIEC::Unlisten()
{ 
	if (state & STATE_LISTENING) {
		state = STATE_IDLE;
		if (prev_cmd == IEC_CMD_OPEN) {
			Device->setEoI(secondaryAddress);
			status = Device->Open(secondaryAddress);
		} else if (prev_cmd == IEC_CMD_DATA) {
			status = Device->Write(secondaryAddress, 0, IEC_CMD_DATA, true);
		}    
	} else
		status = IEC_OK;
	return status;
}

void CFakeIEC::Talk()
{ 
	state = STATE_TALKING;
}

void CFakeIEC::Untalk()
{ 
	state = STATE_IDLE;
}

unsigned int CFakeIEC::In(unsigned char *data)
{
	if ((state & STATE_TALKING) && received_cmd == IEC_CMD_DATA)
		return Device->Read(secondaryAddress, data);

	return IEC_ERROR;
}

unsigned int CFakeIEC::DispatchIECCmd(unsigned char cmd)
{
	switch (cmd&0xF0) {
		case IEC_CMD_LISTEN:
			return Listen();
		case IEC_CMD_UNLISTEN:
			return Unlisten();
		case IEC_CMD_TALK:
			Talk();
			return IEC_OK;
		case IEC_CMD_UNTALK:
			Untalk();
			return IEC_OK;
		default: // illegal command
			return IEC_ERROR;
	}
}
	
unsigned int CFakeIEC::OutCmd(unsigned char data)
{
	prev_cmd = received_cmd;
	received_cmd = data&0xF0;
	return status = DispatchIECCmd(data);
}

unsigned int CFakeIEC::Out(unsigned char data)
{
	if (state & STATE_LISTENING) {
		return Device->Write(secondaryAddress, data, received_cmd, false);
	}
	return IEC_ERROR;  
}

unsigned int CFakeIEC::OutSec(unsigned char data)
{
	prev_addr = secondaryAddress;
	secondaryAddress = data&0x0F;

	prev_cmd = received_cmd;
	received_cmd = data&0xF0;

	switch (state) {
		case STATE_IDLE:
			status = IEC_ERROR;
			break;        
		case STATE_LISTENING:
			switch (received_cmd) {
				case IEC_CMD_OPEN:	// Prepare for receiving the file name
					status = IEC_OK;
					break;

				case IEC_CMD_CLOSE: // Close channel
					status = Device->Close( secondaryAddress);
					break;
				
				case IEC_CMD_DATA: // Data comes
					break;
					
				default:
					status = IEC_ERROR;
					state = STATE_IDLE;
					break;
			}
			break;

		case STATE_TALKING:
			break;
	}
	return status;
}
