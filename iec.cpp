#include "iec.h"

void CFakeIEC::Reset()
{
    state = STATE_IDLE;
	status = ST_OK;
}

unsigned int CFakeIEC::Listen()
{ 
	state = STATE_LISTENING;
	return ST_OK;
};

unsigned int CFakeIEC::Unlisten()
{ 
    if (state & STATE_LISTENING) {
        state = STATE_IDLE;
        if (prev_cmd == CMD_OPEN) {
			status = Device->Write(secondaryAddress, 0, CMD_OPEN, true);
            status = Device->Open(secondaryAddress);
        } else if (prev_cmd == CMD_DATA) {
            status = Device->Write(secondaryAddress, 0, CMD_DATA, true);
        }    
    } else
    	status = ST_OK;
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
	if ((state&STATE_TALKING) && (received_cmd == CMD_DATA))
		return Device->Read(secondaryAddress, data);

	return ST_ERROR;   
}

unsigned int CFakeIEC::DispatchIECCmd(unsigned char cmd)
{
	switch (cmd&0xF0) {
		case CMD_LISTEN:
			return Listen();
		case CMD_UNLISTEN:
			return Unlisten();
		case CMD_TALK:
			Talk();
			return ST_OK;
		case CMD_UNTALK:
			Untalk();
			return ST_OK;
		default: // illegal command
		    return ST_ERROR;
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
	if ((state&STATE_LISTENING) /*&& (received_cmd == CMD_DATA)*/) {
		status = Device->Write( secondaryAddress, data, received_cmd, false);
		return status;
	}
	return ST_ERROR;  
}

unsigned int CFakeIEC::OutSec(unsigned char data)
{
	prev_addr = secondaryAddress;
    secondaryAddress = data&0x0F;

	prev_cmd = received_cmd;
	received_cmd = data&0xF0;

    switch (state) {
        case STATE_IDLE:
			status = ST_ERROR;
            break;        
        case STATE_LISTENING:
			switch (received_cmd) {
				case CMD_OPEN:	// Prepare for receiving the file name
					status = ST_OK;
					break;

				case CMD_CLOSE: // Close channel
					status = Device->Close( secondaryAddress);
					break;
				
				case CMD_DATA: // Data comes
					break;
				    
				default:
				    status = ST_ERROR;
				    state = STATE_IDLE;
				    break;
			}
            break;

        case STATE_TALKING:
            break;
    }
    return status;
}


