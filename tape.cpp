/*
	YAPE - Yet Another Plus/4 Emulator

	The program emulates the Commodore 264 family of 8 bit microcomputers

	This program is free software, you are welcome to distribute it,
	and/or modify it under certain conditions. For more information,
	read 'Copying'.

	(c) 2000, 2001, 2005 Attila Grósz
*/

#include "tape.h"
#include "tedmem.h"
#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned char tap_header[]={
	'C','1','6','-','T','A','P','E','-','R','A','W',
	0x02, // version -> 1 - whole wave 2 - wholeWave
	0x02, // 0 - C64, 1 - VIC20, 2 - C16/+4
	0x00, // Video standard ( 0= PAL, 1 =NTSC, 2 = NTSC2
	0x00, // empty
	// data length (4 byte) file size!!!!
	// data
};

typedef struct _MTAP {
	char string[12];
	unsigned char version;
	unsigned char platform;
	unsigned char video;
	unsigned char reserved;
} MTATAPBuffer , * pMTATAPBuffer;

static FILE *tapfile;
static MTATAPBuffer tap_header_read;

TAP::TAP()
{
	TapeSoFar=0;
	TapeFileSize=0;
	TAPBuffer=NULL;
	buttonPressed = motorOn = false;
	lastCycle = 0;
	edge = 0;
}

bool TAP::attach_tap()
{
	char tmpname[14];

	if ((tapfile=fopen(tapefilename,"rb"))) {
		// load TAP file into buffer
		fseek(tapfile, 0L, SEEK_END);
		TapeFileSize=ftell(tapfile);
		fseek(tapfile, 0L, SEEK_SET);
		// allocate and load file
		TAPBuffer=(unsigned char *) malloc(TapeFileSize);
		fread(TAPBuffer,TapeFileSize,1,tapfile);

		// initialise TAP image
		inwave=false;

		// setting platform
		tap_header_read.version=TAPBuffer[0x0C];
		// allocating space for type...
		strncpy(tmpname,(const char *) TAPBuffer,12);
		// determine the tYpe of tape file attached
		if (!strncmp(tmpname,"C16-TAPE-RAW",12)) {
			TapeSoFar=0x14; // offset to beginning of wave data
			if (tap_header_read.version==1) {
				wholeWave = 1;
			} else {
				wholeWave = 0;
			}
		} else { // if no match for MTAP, assume it is a sample
		}
		motorOn = buttonPressed = false;
		// close the file, it's in the memory now...
		fclose(tapfile);
		return true;
	}
	else
		return false;
}

bool TAP::create_tap()
{
	int filesize = 0x00;

	if (strlen(tapefilename)<5)
		return false;
	// initialise TAP image
	if ((tapfile=fopen(tapefilename,"wb"))) {
		// write the header
		fwrite(&tap_header,sizeof(tap_header),1,(FILE *)tapfile);
		fwrite(&filesize,4,1,(FILE *)tapfile);
		return true;
	}
	else
		return false;
}

bool TAP::detach_tap()
{
	if ( tapfile!=NULL && TapeSoFar>0) {
		fseek(tapfile, 0x10L, SEEK_SET);
		fwrite(&TapeSoFar,sizeof(TapeSoFar),1,tapfile);
		fseek(tapfile, 0L, SEEK_END);
	}
	if (TAPBuffer!=NULL) { // just to be sure....
		free(TAPBuffer);
		TAPBuffer=NULL;
	}
	if (tapfile!=NULL) {
		fclose(tapfile);
		tapfile=NULL;
	}

	TapeSoFar=0;
	return false;
}

void TAP::rewind()
{
	inwave=false;
	TapeSoFar=0x14;
}

void TAP::changewave(bool wholewave)
{
	if (!wholewave) {
		tap_header[12]=2;
	} else {
		tap_header[12]=1;
	}
}

inline void TAP::Advance(unsigned int elapsed)
{
	while (elapsed--) {
		if (inwave) {
			if (tapeDelay-- == 0) {
				//fprintf( stderr, "TAP unit timeout in cycle: %u\n", cycle + elapsed);
				if (edge == 0x10)
					ConvTAPUnitsToCycles();
				else
					tapeDelay = origTapeDelay;
				edge ^= 0x10;
			}
		} else {
			inwave=true;
			ConvTAPUnitsToCycles();
			edge = 0x00;
		}
	}
}

unsigned char TAP::ReadCSTIn(ClockCycle cycle)
{
	int elapsed = int(cycle - lastCycle);

	if (motorOn && TAPBuffer) {
		Advance(elapsed);
		lastCycle = cycle;
	}

	return edge;
}

inline ClockCycle TAP::ReadNextTapDelay()
{
	if (!TAPBuffer)
		return 0;

	ClockCycle delay=TAPBuffer[TapeSoFar];

	/* byte $00 is a pilot byte */
	if (delay==0) {
		delay=TAPBuffer[TapeSoFar+1];
		delay+=TAPBuffer[TapeSoFar+2]<<8;
		delay+=TAPBuffer[TapeSoFar+3]<<16;
		TapeSoFar+=4;
		/*fprintf( stderr, "Pilot byte %i.\n", delay);
		fprintf( stderr, "Pilot offset: %i.\n", TapeSoFar);*/
	} else {
		delay <<= 3;
		TapeSoFar+=1;
	}

	if (TapeSoFar>=TapeFileSize) {
		motorOn = buttonPressed = false;
	}

	/* TAP units are measured in slow clock cycles */
	return delay * 2;
}

void TAP::ConvTAPUnitsToCycles()
{
	if (wholeWave) {
		origTapeDelay = ReadNextTapDelay();
		tapeDelay = origTapeDelay >> 1;
		origTapeDelay -= tapeDelay;
	} else {
		tapeDelay = ReadNextTapDelay();
		if (buttonPressed)
			origTapeDelay = ReadNextTapDelay();
		else
			origTapeDelay = tapeDelay;
	}
	// printf( stderr, "New TAP delay value: %u\n", origTapeDelay);
}

void TAP::PressTapeButton(ClockCycle cycle)
{
	if (!buttonPressed) {
		lastCycle = cycle;
		motorOn = buttonPressed = true;
	}
}

void TAP::SetTapeMotor(ClockCycle cycle, unsigned int on)
{
	int elapsed = int(cycle - lastCycle);

	if (!on && motorOn)
		Advance(elapsed);

	lastCycle = cycle;

	motorOn = (on != 0);
	//fprintf( stderr, "Motor state: %i, in cycle %u\n", motorOn, cycle);
};
