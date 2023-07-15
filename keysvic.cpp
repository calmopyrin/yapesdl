#include <stdio.h>
#include "keysvic.h"

/*
	Write to Port B($9120) column
	Read from Port A($9121) row

	 7   6   5   4   3   2   1   0
	--------------------------------
  7| F7  F5  F3  F1  CDN CRT RET DEL    CRT=Cursor-Right, CDN=Cursor-Down
   |
  6| HOM UA  =   RSH /   ;   *   BP     BP=British Pound, RSH=Should be Right-SHIFT,
   |                                    UA=Up Arrow
  5| -   @   :   .   ,   L   P   +
   |
  4| 0   O   K   M   N   J   I   9
   |
  3| 8   U   H   B   V   G   Y   7
   |
  2| 6   T   F   C   X   D   R   5
   |
  1| 4   E   S   Z   LSH A   W   3      LSH=Should be Left-SHIFT
   |
  0| 2   Q   CBM SPC STP CTL LA  1      LA=Left Arrow, CTL=Should be CTRL, STP=RUN/STOP
   |                                    CBM=Commodore key
*/

inline unsigned char KEYSVIC::keyReadMatrixRow(unsigned int r)
{
	const unsigned char *kbstate = SDL_GetKeyboardState(NULL);
	unsigned char tmp;

	if (kbstate[SDL_SCANCODE_LALT])
		return 0xFF;
	//for(int i = 0; i < 256; i++)
	//	if (kbstate[i]) fprintf(stderr, "Key pressed (%02X)\n", i);

	switch (r) {
		default:
		case 0:
			tmp = ~
				((kbstate[SDL_SCANCODE_1]<<0)
				|(kbstate[SDL_SCANCODE_3]<<1)
				|(kbstate[SDL_SCANCODE_5]<<2)
				|(kbstate[SDL_SCANCODE_7]<<3)
				|(kbstate[SDL_SCANCODE_9]<<4)
				|((kbstate[SDL_SCANCODE_KP_PLUS] | kbstate[SDL_SCANCODE_MINUS] ) <<5)
				|(kbstate[SDL_SCANCODE_INSERT]<<6)
				|(kbstate[SDL_SCANCODE_BACKSPACE]<<7));
			break;

		case 1:
			tmp = ~
				((kbstate[SDL_SCANCODE_GRAVE]<<0)
				|(kbstate[SDL_SCANCODE_W]<<1)
				|(kbstate[SDL_SCANCODE_R]<<2)
				|(kbstate[SDL_SCANCODE_Y]<<3)
				|(kbstate[SDL_SCANCODE_I]<<4)
				|(kbstate[SDL_SCANCODE_P]<<5)
				|((kbstate[SDL_SCANCODE_RIGHTBRACKET] | kbstate[SDL_SCANCODE_KP_MULTIPLY])<<6)
				|((kbstate[SDL_SCANCODE_RETURN] | kbstate[SDL_SCANCODE_KP_ENTER]) <<7));
			break;

		case 2:
			tmp = ~
				((kbstate[SDL_SCANCODE_RCTRL]<<0)
				|(kbstate[SDL_SCANCODE_A]<<1)
				|(kbstate[SDL_SCANCODE_D]<<2)
				|(kbstate[SDL_SCANCODE_G]<<3)
				|(kbstate[SDL_SCANCODE_J]<<4)
				|(kbstate[SDL_SCANCODE_L]<<5)
				|(kbstate[SDL_SCANCODE_APOSTROPHE]<<6)
				|((kbstate[SDL_SCANCODE_RIGHT]|kbstate[SDL_SCANCODE_LEFT])<<7));
			break;

		case 3:
			tmp = ~
				((kbstate[SDL_SCANCODE_TAB]<<0)
				|(kbstate[SDL_SCANCODE_LSHIFT]<<1)
				|(kbstate[SDL_SCANCODE_X]<<2)
				|(kbstate[SDL_SCANCODE_V]<<3)
				|(kbstate[SDL_SCANCODE_N]<<4)
				|(kbstate[SDL_SCANCODE_COMMA]<<5)
				|(kbstate[SDL_SCANCODE_SLASH]<<6)
				|(kbstate[SDL_SCANCODE_DOWN]<<7));
				if (kbstate[SDL_SCANCODE_LEFT])
					tmp &= ~0x02;
				else if (kbstate[SDL_SCANCODE_UP])
					tmp &= ~0x82;
			break;

		case 4:
			tmp = ~
				((kbstate[SDL_SCANCODE_SPACE]<<0)
				|(kbstate[SDL_SCANCODE_Z]<<1)
				|(kbstate[SDL_SCANCODE_C]<<2)
				|(kbstate[SDL_SCANCODE_B]<<3)
				|(kbstate[SDL_SCANCODE_M]<<4)
				|(kbstate[SDL_SCANCODE_PERIOD]<<5)
				|(kbstate[SDL_SCANCODE_RSHIFT]<<6)
				|(kbstate[SDL_SCANCODE_F1]<<7));
				/*if (kbstate[SDL_SCANCODE_LEFT] || kbstate[SDL_SCANCODE_UP])
					tmp &= ~0x40;*/
			break;

		case 5:
			tmp = ~
				((kbstate[SDL_SCANCODE_LCTRL]<<0) // MINUS
				|(kbstate[SDL_SCANCODE_S]<<1)
				|(kbstate[SDL_SCANCODE_F]<<2)
				|(kbstate[SDL_SCANCODE_H]<<3) // EQUALS
				|(kbstate[SDL_SCANCODE_K]<<4)
				|(kbstate[SDL_SCANCODE_SEMICOLON]<<5) /* numeric . KP_PERIOD */
				|(kbstate[SDL_SCANCODE_BACKSLASH]<<6)
				|(kbstate[SDL_SCANCODE_F2]<<7));
			break;

		case 6:
			tmp = ~
				((kbstate[SDL_SCANCODE_Q]<<0)
				|(kbstate[SDL_SCANCODE_E] <<1) //
				|(kbstate[SDL_SCANCODE_T]<<2) // APOSTROPHE
				|(kbstate[SDL_SCANCODE_U]<<3)
				|(kbstate[SDL_SCANCODE_O]<<4)
				|(kbstate[SDL_SCANCODE_LEFTBRACKET]<<5)
				|(kbstate[SDL_SCANCODE_END]<<6) // 
				|(kbstate[SDL_SCANCODE_F3]<<7));  // numeric SDL_SCANCODE_KP_DIVIDE SLASH
			break;

		case 7:
			tmp = ~
				((kbstate[SDL_SCANCODE_2]<<0)
				|(kbstate[SDL_SCANCODE_4]<<1) // GRAVE
				|(kbstate[SDL_SCANCODE_6]<<2)
				|(kbstate[SDL_SCANCODE_8]<<3) //PRIOR
				|(kbstate[SDL_SCANCODE_0]<<4)
				|((kbstate[SDL_SCANCODE_EQUALS]|kbstate[SDL_SCANCODE_KP_MINUS])<<5)
				|(kbstate[SDL_SCANCODE_HOME]<<6)
				|(kbstate[SDL_SCANCODE_F4]<<7));
			break;
	}
	return tmp | blockMask;
}

unsigned char KEYSVIC::feedkey(unsigned char rowselect)
{
	unsigned char tmp = 0xFF;

	if ((rowselect & 0x01)==0) tmp &= keyReadMatrixRow(0);
	if ((rowselect & 0x02)==0) tmp &= keyReadMatrixRow(1);
	if ((rowselect & 0x04)==0) tmp &= keyReadMatrixRow(2);
	if ((rowselect & 0x08)==0) tmp &= keyReadMatrixRow(3);
	if ((rowselect & 0x10)==0) tmp &= keyReadMatrixRow(4);
	if ((rowselect & 0x20)==0) tmp &= keyReadMatrixRow(5);
	if ((rowselect & 0x40)==0) tmp &= keyReadMatrixRow(6);
	if ((rowselect & 0x80)==0) tmp &= keyReadMatrixRow(7);

	return tmp;
}

unsigned char KEYSVIC::feedKeyColumn(unsigned char column)
{
	unsigned char retval;
	unsigned char tmp = ~(keyReadMatrixRow(0) | column);
	retval = tmp ? 1 : 0;
	tmp = ~(keyReadMatrixRow(1) | column);
	retval |= tmp ? 2 : 0;
	tmp = ~(keyReadMatrixRow(2) | column);
	retval |= tmp ? 4 : 0;
	tmp = ~(keyReadMatrixRow(3) | column);
	retval |= tmp ? 8 : 0;
	tmp = ~(keyReadMatrixRow(4) | column);
	retval |= tmp ? 0x10 : 0;
	tmp |= ~(keyReadMatrixRow(5) | column);
	retval |= tmp ? 0x20 : 0;
	tmp |= ~(keyReadMatrixRow(6) | column);
	retval |= tmp ? 0x40 : 0;
	tmp |= ~(keyReadMatrixRow(7) | column);
	retval |= tmp ? 0x80 : 0;

	return retval ^ 0xff;
}

unsigned char KEYSVIC::feedjoy()
{
	const Uint8 *kbstate = SDL_GetKeyboardState(NULL);
	unsigned char tmp = ~
		((kbstate[joystickScanCodes[joystickScanCodeIndex][0]]<<2)
		|(kbstate[SDL_SCANCODE_KP_7]<<0)
		|(kbstate[SDL_SCANCODE_KP_9]<<0)
		|(kbstate[joystickScanCodes[joystickScanCodeIndex][2]]<<3)
		|(kbstate[SDL_SCANCODE_KP_1]<<1)
		|(kbstate[SDL_SCANCODE_KP_3]<<1)
		|(kbstate[joystickScanCodes[joystickScanCodeIndex][3]]<<4)
		|(kbstate[SDL_SCANCODE_KP_7]<<2)
		|(kbstate[SDL_SCANCODE_KP_1]<<2)
		|(kbstate[joystickScanCodes[joystickScanCodeIndex][4]]<<5)
		|(kbstate[SDL_SCANCODE_KP_3]<<3)
		|(kbstate[SDL_SCANCODE_KP_9]<<3)
		|(kbstate[joystickScanCodes[joystickScanCodeIndex][1]]<<7));
	return tmp;
}

unsigned char KEYSVIC::getJoyState(unsigned int j)
{
	unsigned char tmp = 0xFF;
	if (activejoy) {
		tmp &= feedjoy();
	}
	const unsigned int pcJoyIx = activejoy >> 1;
	if (sdlJoys[0]) // FIXME
		tmp &= getPcJoyState(0, 0) << 2;
	return tmp;
}
