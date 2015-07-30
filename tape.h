/*
	YAPE - Yet Another Plus/4 Emulator

	The program emulates the Commodore 264 family of 8 bit microcomputers

	This program is free software, you are welcome to distribute it,
	and/or modify it under certain conditions. For more information,
	read 'Copying'.

	(c) 2000, 2001, 2007 Attila Grósz
*/
#ifndef _TAPE_H
#define _TAPE_H

#include "types.h"

class TAP {
	private:
		unsigned int TapeFileSize;
		unsigned char *TAPBuffer;
		ClockCycle tapeDelay, origTapeDelay;
		// indicates if we started the TAP process
		bool inwave;
		char buf;
		//
		ClockCycle lastCycle;
		unsigned char pio;
		unsigned char edge;
		bool motorOn;
		bool buttonPressed;
		void ConvTAPUnitsToCycles();
		void Advance(unsigned int);
		ClockCycle ReadNextTapDelay();
		unsigned int wholeWave;

	public:
		TAP();
		class TED *mem;
		char tapefilename[256];
		bool attach_tap();
		bool create_tap();
		bool detach_tap();
		void stop();
		void rewind();
		void changewave(bool wholewave);
		unsigned int TapeSoFar;
		//
		unsigned char ReadCSTIn(ClockCycle cycle);
		void WriteCSTOut(ClockCycle cycle, unsigned char value);
		void PressTapeButton(ClockCycle cycle);
		unsigned int IsButtonPressed() { 
			//fprintf(stderr,"Button state checked: %i\n", buttonPressed);
			return buttonPressed;
		};
		void SetTapeMotor(ClockCycle cycle, unsigned int on);
};


#endif // _TAPE_H
