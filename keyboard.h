/*
	YAPE - Yet Another Plus/4 Emulator

	The program emulates the Commodore 264 family of 8 bit microcomputers

	This program is free software, you are welcome to distribute it,
	and/or modify it under certain conditions. For more information,
	read 'Copying'.

	(c) 2000, 2001, 2015 Attila Grósz
*/
#ifndef _KEYBOARD_H
#define _KEYBOARD_H

#include "types.h"

#ifdef __WIIU__
#include "keybdwrapper.h"

#define SDL_A        SDL_CONTROLLER_BUTTON_A
#define SDL_B        SDL_CONTROLLER_BUTTON_B
#define SDL_X        SDL_CONTROLLER_BUTTON_X
#define SDL_Y        SDL_CONTROLLER_BUTTON_Y

#define SDL_PLUS     SDL_CONTROLLER_BUTTON_RIGHTSHOULDER
#define SDL_L        SDL_CONTROLLER_BUTTON_START
#define SDL_R        SDL_CONTROLLER_BUTTON_LEFTSTICK
#define SDL_ZL       SDL_CONTROLLER_BUTTON_RIGHTSTICK
#define SDL_ZR       SDL_CONTROLLER_BUTTON_LEFTSHOULDER
#define SDL_MINUS    SDL_CONTROLLER_BUTTON_DPAD_UP

#define SDL_UP        SDL_CONTROLLER_BUTTON_DPAD_LEFT
#define SDL_DOWN      SDL_CONTROLLER_BUTTON_MAX
#define SDL_LEFT      SDL_CONTROLLER_BUTTON_DPAD_DOWN
#define SDL_RIGHT     SDL_CONTROLLER_BUTTON_DPAD_RIGHT

#define SDL_L_THUMB   SDL_CONTROLLER_BUTTON_BACK
#define SDL_R_THUMB   SDL_CONTROLLER_BUTTON_GUIDE

#define SDL_L_LEFT   (SDL_GameControllerButton)16
#define SDL_L_UP     (SDL_GameControllerButton)17
#define SDL_L_RIGHT  (SDL_GameControllerButton)18
#define SDL_L_DOWN   (SDL_GameControllerButton)19

#define SDL_R_LEFT   (SDL_GameControllerButton)20
#define SDL_R_UP     (SDL_GameControllerButton)21
#define SDL_R_RIGHT  (SDL_GameControllerButton)22
#define SDL_R_DOWN   (SDL_GameControllerButton)23
#endif

class KEYS {
	protected:
		unsigned char joybuffer[256];
		unsigned char joy_trans(unsigned char r);
		//
		static unsigned int nrOfJoys;
#ifndef __WIIU__
		static SDL_GameController *sdlJoys[2];
		unsigned char getPcJoyState(unsigned int joyNr, unsigned int activeJoy);
#else
		static SDL_Joystick *sdlJoystick[2];
		unsigned char getPcJoystickState(unsigned int joystickNr, unsigned int activeJoystick);
#endif
		static unsigned int joystickScanCodes[][5];
		unsigned char latched;
		unsigned char keyReadMatrixRow(unsigned int r);
		unsigned char blockMask;
		virtual unsigned int fireButtonIndex(unsigned int a) const { return 6 + a; };

	public:
		KEYS();
		virtual ~KEYS();
		static void initPcJoys();
		static void closePcJoys();
		void latch(unsigned int keyrow, unsigned int joyrow);
		unsigned char feedkey(unsigned char latch);
		unsigned char feedjoy(unsigned char latch);
		void empty(void);
		static void swapjoy(void *none);
		static unsigned int activejoy;
		static unsigned int joystickScanCodeIndex;
		static const char *activeJoyTxt() {
			const char *txt[] = { "NONE", "PORT1", "PORT2", "BOTH" };
			return txt[activejoy];
		}
		static void swapKeyset(void *none);
		static const char *activeJoyKeyset() {
			const char *txt[] = { "NUMPAD24680", "ARROWS+SPACE", "WASD+RShift" };
			return txt[joystickScanCodeIndex];
		}
		unsigned char readLatch() { return latched | blockMask; };
		void block(bool isBlocked) { blockMask = isBlocked ? 0xFF : 0x00; };
#ifdef __WIIU__
		static KBWrapper* kbdwrapper;
#endif
};

extern rvar_t inputSettings[];

#endif // _KEYBOARD_H
