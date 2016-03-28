#include <stdio.h>
#include "keys64.h"

KEYS64::KEYS64()
{
}

KEYS64::~KEYS64()
{
}

unsigned char KEYS64::keyReadMatrixRow(unsigned int r)
{
	const Uint8 *kbstate = SDL_GetKeyboardState(NULL);
	unsigned char tmp;

	if (kbstate[SDL_SCANCODE_LALT])
		return 0xFF;
	//for(int i = 0; i < 256; i++)
	//	if (kbstate[i]) fprintf(stderr, "Key pressed (%02X)\n", i);

	switch (r) {
		default:
		case 0:
			tmp = ~
				((kbstate[SDL_SCANCODE_BACKSPACE]<<0)
				|(kbstate[SDL_SCANCODE_RETURN]<<1)
				|(kbstate[SDL_SCANCODE_KP_ENTER]<<1)
				|((kbstate[SDL_SCANCODE_RIGHT] | kbstate[SDL_SCANCODE_LEFT])<<2)
				|(kbstate[SDL_SCANCODE_F4]<<3)
				|(kbstate[SDL_SCANCODE_F1]<<4)
				|(kbstate[SDL_SCANCODE_F2]<<5)
				|(kbstate[SDL_SCANCODE_F3]<<6)
				|((kbstate[SDL_SCANCODE_DOWN]|kbstate[SDL_SCANCODE_UP])<<7));
			break;

		case 1:
			tmp = ~
				((kbstate[SDL_SCANCODE_3]<<0)
				|(kbstate[SDL_SCANCODE_W]<<1)
				|(kbstate[SDL_SCANCODE_A]<<2)
				|(kbstate[SDL_SCANCODE_4]<<3)
				|(kbstate[SDL_SCANCODE_Z]<<4)
				|(kbstate[SDL_SCANCODE_S]<<5)
				|(kbstate[SDL_SCANCODE_E]<<6)
				|(kbstate[SDL_SCANCODE_LSHIFT]<<7));
			break;

		case 2:
			tmp = ~
				((kbstate[SDL_SCANCODE_5]<<0)
				|(kbstate[SDL_SCANCODE_R]<<1)
				|(kbstate[SDL_SCANCODE_D]<<2)
				|(kbstate[SDL_SCANCODE_6]<<3)
				|(kbstate[SDL_SCANCODE_C]<<4)
				|(kbstate[SDL_SCANCODE_F]<<5)
				|(kbstate[SDL_SCANCODE_T]<<6)
				|(kbstate[SDL_SCANCODE_X]<<7));
			break;

		case 3:
			tmp = ~
				((kbstate[SDL_SCANCODE_7]<<0)
				|(kbstate[SDL_SCANCODE_Y]<<1)
				|(kbstate[SDL_SCANCODE_G]<<2)
				|(kbstate[SDL_SCANCODE_8]<<3)
				|(kbstate[SDL_SCANCODE_B]<<4)
				|(kbstate[SDL_SCANCODE_H]<<5)
				|(kbstate[SDL_SCANCODE_U]<<6)
				|(kbstate[SDL_SCANCODE_V]<<7));
			break;

		case 4:
			tmp = ~
				((kbstate[SDL_SCANCODE_9]<<0)
				|(kbstate[SDL_SCANCODE_I]<<1)
				|(kbstate[SDL_SCANCODE_J]<<2)
				|(kbstate[SDL_SCANCODE_0]<<3)
				|(kbstate[SDL_SCANCODE_M]<<4)
				|(kbstate[SDL_SCANCODE_K]<<5)
				|(kbstate[SDL_SCANCODE_O]<<6)
				|(kbstate[SDL_SCANCODE_N]<<7));
			break;

		case 5:
			tmp = ~
				((kbstate[SDL_SCANCODE_MINUS]<<0)
				|(kbstate[SDL_SCANCODE_KP_PLUS]<<0) /* numeric + */
				|(kbstate[SDL_SCANCODE_P]<<1)
				|(kbstate[SDL_SCANCODE_L]<<2)
				|(kbstate[SDL_SCANCODE_EQUALS]<<3)
				|(kbstate[SDL_SCANCODE_PERIOD]<<4)
				|(kbstate[SDL_SCANCODE_KP_PERIOD]<<4) /* numeric . */
				|(kbstate[SDL_SCANCODE_SEMICOLON]<<5)
				|(kbstate[SDL_SCANCODE_LEFTBRACKET]<<6)
				|(kbstate[SDL_SCANCODE_KP_MINUS]<<6) /* numeric - */
				|(kbstate[SDL_SCANCODE_COMMA]<<7));
			break;

		case 6:
			tmp = ~
				((kbstate[SDL_SCANCODE_END]<<0)
				|(kbstate[SDL_SCANCODE_BACKSLASH]<<1)
				|(kbstate[SDL_SCANCODE_KP_MULTIPLY]<<1)
				|(kbstate[SDL_SCANCODE_APOSTROPHE]<<2)
				|(kbstate[SDL_SCANCODE_HOME]<<3)
				|(kbstate[SDL_SCANCODE_RSHIFT]<<4)
				|(kbstate[SDL_SCANCODE_RIGHTBRACKET]<<5)
				|(kbstate[SDL_SCANCODE_PAGEUP]<<6)
				|(kbstate[SDL_SCANCODE_SLASH]<<7)
				|(kbstate[SDL_SCANCODE_KP_DIVIDE]<<7));  /* numeric / */
				if (kbstate[SDL_SCANCODE_LEFT] || kbstate[SDL_SCANCODE_UP])
					tmp &= ~0x10;
			break;

		case 7:
			tmp = ~
				((kbstate[SDL_SCANCODE_1]<<0)
				|(kbstate[SDL_SCANCODE_GRAVE]<<1)
				|(kbstate[SDL_SCANCODE_RCTRL]<<2)
				|(kbstate[SDL_SCANCODE_PRIOR]<<2)
				|(kbstate[SDL_SCANCODE_2]<<3)
				|(kbstate[SDL_SCANCODE_SPACE]<<4)
				|(kbstate[SDL_SCANCODE_LCTRL]<<5)
				|(kbstate[SDL_SCANCODE_Q]<<6)
				|(kbstate[SDL_SCANCODE_TAB]<<7));
			break;
	}
	return tmp | blockMask;
}

unsigned char KEYS64::feedkey(unsigned char rowselect)
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

	return tmp & getJoyState(0);
}

unsigned char KEYS64::feedKeyColumn(unsigned char column)
{
	unsigned char retval;
	unsigned char tmp = ~(keyReadMatrixRow(0) | column);
	retval = tmp ? 1 : 0;
	tmp = ~(keyReadMatrixRow(1) | column);
	retval |= tmp ? 2 : 0;
	tmp = ~(keyReadMatrixRow(2) | column); 
	retval |= tmp ? 4 : 0;
	tmp = ~(keyReadMatrixRow(3) | column); 
	retval |= tmp ?  8 : 0;
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

unsigned char KEYS64::feedjoy()
{
	const Uint8 *kbstate = SDL_GetKeyboardState(NULL);
	unsigned char tmp = ~
		((kbstate[joystickScanCodes[joystickScanCodeIndex][0]]<<0)
		|(kbstate[SDL_SCANCODE_KP_7]<<0)
		|(kbstate[SDL_SCANCODE_KP_9]<<0)
		|(kbstate[joystickScanCodes[joystickScanCodeIndex][2]]<<1)
		|(kbstate[SDL_SCANCODE_KP_1]<<1)
		|(kbstate[SDL_SCANCODE_KP_3]<<1)
		|(kbstate[joystickScanCodes[joystickScanCodeIndex][3]]<<2)
		|(kbstate[SDL_SCANCODE_KP_7]<<2)
		|(kbstate[SDL_SCANCODE_KP_1]<<2)
		|(kbstate[joystickScanCodes[joystickScanCodeIndex][1]]<<3)
		|(kbstate[SDL_SCANCODE_KP_3]<<3)
		|(kbstate[SDL_SCANCODE_KP_9]<<3)
		|(kbstate[joystickScanCodes[joystickScanCodeIndex][4]]<<4));
	return tmp;
}

unsigned char KEYS64::getJoyState(unsigned int j)
{
	unsigned char tmp = 0xFF;
	if (activejoy & (j + 1)) {
		tmp &= feedjoy();
	}
	const unsigned int pcJoyIx = activejoy >> 1;
	if (sdlJoys[1 ^ pcJoyIx ^ j]) // FIXME
		tmp &= getPcJoyState(1 ^ pcJoyIx ^ j, 0);
	return tmp;
}
