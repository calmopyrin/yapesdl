/*
	YAPE - Yet Another Plus/4 Emulator

	The program emulates the Commodore 264 family of 8 bit microcomputers

	This program is free software, you are welcome to distribute it,
	and/or modify it under certain conditions. For more information,
	read 'Copying'.

	(c) 2000, 2001, 2007, 2016 Attila Grósz
*/
#ifndef _TAPE_H
#define _TAPE_H

#include "types.h"

#define C64PALFREQ  123156	/*  985248 / 8 */
#define C64NTSCFREQ 127841	/* 1022727 / 8 */
#define VICPALFREQ  138551	/* 1108405 / 8 */
#define VICNTSCFREQ 127841	/* 1022727 / 8 */
#define C16PALFREQ  110840	/*  886724 / 8 */
#define C16NTSCFREQ 111860	/*  894886 / 8 */

enum TapeFormat {
	TAPE_FORMAT_MTAP1 = 0,
	TAPE_FORMAT_MTAP2,
	TAPE_FORMAT_PCM8,
	TAPE_FORMAT_PCM16,
	TAPE_FORMAT_NONE
};

class TAP {
	private:
		char tapefilename[260];
		unsigned int tapeFileSize;
		unsigned char *tapeBuffer;
		unsigned int tapeDelay, origTapeDelay;
		//
		ClockCycle lastCycle;
		unsigned char edge;
		bool motorOn;
		unsigned int buttonPressed;
		void convTAPUnitsToCycles();
		void readMtapData(unsigned int);
		void readWavData(unsigned int elapsed);
		unsigned int readNextTapDelay();
		TapeFormat tapeFormat;
		bool fallingEdge;
		unsigned char *tapeHeaderRead;
		unsigned int tapeImageHeaderSize;
		unsigned int tapeImageSampleRate;

	public:
		TAP();
		class TED *mem;
		bool attachTape(const char *fname);
		bool createTape(const char *fname);
		bool detachTape();
		void rewind();
		void changewave(bool wholewave);
		unsigned int tapeSoFar;
		//
		unsigned char readCSTIn(ClockCycle cycle);
		void writeCSTOut(ClockCycle cycle, unsigned char value);
		void pressTapeButton(ClockCycle cycle, unsigned int);
		unsigned int IsButtonPressed() { 
			//fprintf(stderr,"Button state checked: %i\n", buttonPressed);
			return buttonPressed;
		}
		void setTapeMotor(ClockCycle cycle, unsigned int on);
		inline bool isMotorOn() { 
			return motorOn;
		}
		bool getFallingEdgeState(ClockCycle clk) {
			readCSTIn(clk);
			return fallingEdge;
		}
		void resetFallingEdge(ClockCycle clk) {
			readCSTIn(clk);
			fallingEdge = false;
		}
};

#endif // _TAPE_H
