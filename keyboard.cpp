/*
	YAPE - Yet Another Plus/4 Emulator

	The program emulates the Commodore 264 family of 8 bit microcomputers

	This program is free software, you are welcome to distribute it,
	and/or modify it under certain conditions. For more information,
	read 'Copying'.

	(c) 2000, 2001, 2004, 2015 Attila Grósz
*/
#include <memory.h>
#include <stdio.h>
#include "keyboard.h"

enum {
  	P4K_INS = 0, P4K_RETURN, P4K_POUND, P4K_HELP,  P4K_F1, P4K_F2, P4K_F3, P4K_AT,
  	P4K_3, P4K_W, P4K_A, P4K_4,  P4K_Z, P4K_S, P4K_E, P4K_SHIFT,
  	P4K_5, P4K_R, P4K_D, P4K_6,  P4K_C, P4K_F, P4K_T, P4K_X,
  	P4K_7, P4K_Y, P4K_G, P4K_8,  P4K_B, P4K_H, P4K_U, P4K_V,
  	
  	P4K_9, P4K_I, P4K_J, P4K_0,  P4K_M, P4K_K, P4K_O, P4K_N,
  	P4K_DOWN, P4K_P, P4K_L, P4K_UP,  P4K_PERIOD, P4K_DPOINT, P4K_MINUS, P4K_COMMA,
  	P4K_LEFT, P4K_MULTIPLY, P4K_SEMICOLON, P4K_RIGHT,  P4K_ESC, P4K_EQUAL, P4K_PLUS, P4K_SLASH,
  	P4K_1, P4K_HOME, P4K_CTRL, P4K_2,  P4K_SPACE, P4K_COMMIE, P4K_Q, P4K_STOP
};

unsigned int KEYS::joystickScanCodes[5] = {
#ifdef __EMSCRIPTEN__
	SDL_SCANCODE_UP, SDL_SCANCODE_RIGHT, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_SPACE
#else
	SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_6, SDL_SCANCODE_KP_2, SDL_SCANCODE_KP_4, SDL_SCANCODE_KP_0 
#endif
}; // PC keycodes up, right, down, left and fire

static const unsigned int joykeys[2][5]=
	{ {P4K_5, P4K_6, P4K_R, P4K_D, P4K_T }, // keys for joystick 1 and 2 up, right, down, left and fire
	{P4K_3, P4K_4, P4K_W, P4K_A, P4K_SHIFT } };

static const unsigned int origkeys[5]={
	P4K_UP, P4K_RIGHT, P4K_DOWN, P4K_LEFT, 255};

//--------------------------------------------------------------
unsigned int KEYS::nrOfJoys;
SDL_GameController *KEYS::sdlJoys[2];
unsigned int KEYS::activejoy = 0;

KEYS::KEYS() 
{
	empty();
	block(false);
}

void KEYS::initPcJoys()
{
	nrOfJoys = SDL_NumJoysticks();
	SDL_JoystickEventState(SDL_ENABLE);
	sdlJoys[0] = sdlJoys[1] = 0;
	unsigned int i = nrOfJoys + 1;
	if (i > 2) i = 2;
	while (i) {
		i -= 1;
		sdlJoys[i] = SDL_GameControllerOpen(i);
	}
	
	fprintf(stderr, "Found %i joysticks.\n", nrOfJoys);
	for( i=0; i < nrOfJoys; i++ ) {
	  printf("    %s\n", SDL_JoystickNameForIndex(i));
	}
}

void KEYS::empty(void)
{
	memset(joybuffer,0,256);
	latched = 0xff;
}

void KEYS::latch(unsigned int keyrow, unsigned int joyrow)
{
	latched = feedkey(keyrow) & feedjoy(joyrow);
	//fprintf(stderr, "Key latch (%02X): %02X\n", keyrow, latched);
}

unsigned char KEYS::keyReadMatrixRow(unsigned int r)
{
	const Uint8 *kbstate = SDL_GetKeyboardState(NULL);
	unsigned char tmp;

	//for(int i = 0; i < 256; i++)
	//	if (kbstate[i]) fprintf(stdout, "Key pressed (%02X)\n", i);
	if (kbstate[SDL_SCANCODE_LALT])
		return 0xFF;

	switch (r) {
		default:
		case 0:
			tmp = ~
				((kbstate[SDL_SCANCODE_BACKSPACE]<<0)
				|(kbstate[SDL_SCANCODE_RETURN]<<1)
				|(kbstate[SDL_SCANCODE_KP_ENTER]<<1)
				|(kbstate[SDL_SCANCODE_END]<<2)
				|(kbstate[SDL_SCANCODE_F4]<<3)
				|(kbstate[SDL_SCANCODE_F1]<<4)
				|(kbstate[SDL_SCANCODE_F2]<<5)
				|(kbstate[SDL_SCANCODE_F3]<<6)
				|(kbstate[SDL_SCANCODE_LEFTBRACKET]<<7)); // '@'
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
				|(kbstate[SDL_SCANCODE_RSHIFT]<<7)
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
				//((kbstate[joyset_dinp[2 - (joy_setup_code[0] == 2)][2]]<<0)
				((kbstate[SDL_SCANCODE_P]<<1)
				|(kbstate[SDL_SCANCODE_L]<<2)
				//|(kbstate[joyset_dinp[2 - (joy_setup_code[0] == 2)][0]]<<3)
				|(kbstate[SDL_SCANCODE_PERIOD]<<4)
				|(kbstate[SDL_SCANCODE_KP_PERIOD]<<4) /* numeric . */
				|(kbstate[SDL_SCANCODE_SEMICOLON]<<5)
				|(kbstate[SDL_SCANCODE_EQUALS]<<6)
				|(kbstate[SDL_SCANCODE_KP_MINUS]<<6) /* numeric - */
				|(kbstate[SDL_SCANCODE_COMMA]<<7));
			//if (!activejoy)
				tmp &= ~((kbstate[SDL_SCANCODE_DOWN]<<0)|(kbstate[SDL_SCANCODE_UP]<<3));
			//else
			//	tmp &= ~((kbstate[SDL_SCANCODE_KP_2]<<0)|(kbstate[SDL_SCANCODE_KP_8]<<3));
			break;

		case 6:
			tmp = ~
				((kbstate[SDL_SCANCODE_BACKSLASH]<<1)
				|(kbstate[SDL_SCANCODE_KP_MULTIPLY]<<1)
				|(kbstate[SDL_SCANCODE_APOSTROPHE]<<2)
				|(kbstate[SDL_SCANCODE_GRAVE]<<4)
				|(kbstate[SDL_SCANCODE_RIGHTBRACKET]<<5)
				|(kbstate[SDL_SCANCODE_MINUS]<<6)
				|(kbstate[SDL_SCANCODE_KP_PLUS]<<6) /* numeric + */
				|(kbstate[SDL_SCANCODE_SLASH]<<7)
				|(kbstate[SDL_SCANCODE_KP_DIVIDE]<<7));  /* numeric / */
			//if (!activejoy)
				tmp &= ~((kbstate[SDL_SCANCODE_LEFT]<<0)|(kbstate[SDL_SCANCODE_RIGHT]<<3));
			//else
			//	tmp &= ~((kbstate[SDL_SCANCODE_KP_4]<<0)|(kbstate[SDL_SCANCODE_KP_6]<<3));
			break;

		case 7:
			tmp = ~
				((kbstate[SDL_SCANCODE_1]<<0)
				|(kbstate[SDL_SCANCODE_HOME]<<1)
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

unsigned char KEYS::feedkey(unsigned char latch)
{
	static unsigned char tmp;

	tmp = 0xFF;

	if ((latch&0x01)==0) tmp&=keyReadMatrixRow(0);
	if ((latch&0x02)==0) tmp&=keyReadMatrixRow(1);
	if ((latch&0x04)==0) tmp&=keyReadMatrixRow(2);
	if ((latch&0x08)==0) tmp&=keyReadMatrixRow(3);
	if ((latch&0x10)==0) tmp&=keyReadMatrixRow(4);
	if ((latch&0x20)==0) tmp&=keyReadMatrixRow(5);
	if ((latch&0x40)==0) tmp&=keyReadMatrixRow(6);
	if ((latch&0x80)==0) tmp&=keyReadMatrixRow(7);

	return tmp;
}

unsigned char KEYS::joy_trans(unsigned char r)
{
	const Uint8 *kbstate = SDL_GetKeyboardState(NULL);
	unsigned char tmp;

	tmp = ~
		((kbstate[joystickScanCodes[0]]<<0)
		|(kbstate[joystickScanCodes[2]]<<1)
		|(kbstate[joystickScanCodes[3]]<<2)
		|(kbstate[joystickScanCodes[1]]<<3));

	if (activejoy & 1)
		tmp &= ~(kbstate[joystickScanCodes[4]] << 6);
	if (activejoy & 2)
		tmp &= ~(kbstate[joystickScanCodes[4]] << 7);

	return tmp;
}

unsigned char KEYS::getPcJoyState(unsigned int joyNr, unsigned int activeJoy)
{
	const Sint16 deadZone = 32767 / 5;
	unsigned char state;
	Sint16 x_move, y_move;
	SDL_GameController *thisController = sdlJoys[joyNr];

	state = SDL_GameControllerGetButton(thisController, SDL_CONTROLLER_BUTTON_A);
	state |= SDL_GameControllerGetButton(thisController, SDL_CONTROLLER_BUTTON_RIGHTSTICK);
	state <<= fireButtonIndex(activeJoy);
	// if (state)
// 	  fprintf(stderr,"Joy(%i) state: %X ", joyNr, state);
	x_move = SDL_GameControllerGetAxis(thisController, SDL_CONTROLLER_AXIS_LEFTX);
	y_move = SDL_GameControllerGetAxis(thisController, SDL_CONTROLLER_AXIS_LEFTY);
	if (x_move >= deadZone || SDL_GameControllerGetButton(thisController, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) {
		state |= 8;
	} else if (x_move <= -deadZone || SDL_GameControllerGetButton(thisController, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) {
		state |= 4;
	}
	if (y_move >= deadZone || SDL_GameControllerGetButton(thisController, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) {
		state |= 2;
	} else if (y_move <= -deadZone || SDL_GameControllerGetButton(thisController, SDL_CONTROLLER_BUTTON_DPAD_UP)) {
		state |= 1;
	}
	return state ^ 0xFF;
}

unsigned char KEYS::feedjoy(unsigned char latch)
{
	unsigned char tmp = 0xFF;

	if ((latch & 0x04) == 0) {
		const unsigned int joy1ix = activejoy & 1;
		const unsigned int activePcJoy1ix = (joy1ix || sdlJoys[1]) ? 1 : 0;
		if (joy1ix)
			tmp &= joy_trans(1);
		if (sdlJoys[activePcJoy1ix])
			tmp &= getPcJoyState(activePcJoy1ix, 0);
	}
	if ((latch & 0x02) == 0) {
		const unsigned int joy2ix = activejoy & 2;
		const unsigned int activePcJoy2ix = joy2ix ? 1 : 0;
		if (joy2ix)
			tmp &= joy_trans(2);
		if (sdlJoys[activePcJoy2ix])
			tmp &= getPcJoyState(activePcJoy2ix, 1);
	}
	return tmp;
}

unsigned int KEYS::swapjoy()
{
	return (activejoy = (activejoy + 1) & 3);
}

void KEYS::closePcJoys()
{
	int i = nrOfJoys;
	while (i) {
		i -= 1;
		SDL_GameControllerClose(sdlJoys[i]);
	}
}

KEYS::~KEYS() 
{

}
