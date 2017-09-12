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

static unsigned int tapFrqs[] = {
	C64PALFREQ, C64NTSCFREQ,
	VICPALFREQ, VICNTSCFREQ,
	C16PALFREQ, C16NTSCFREQ
};
#define NROFFRQS (sizeof(tapFrqs)/sizeof(tapFrqs[0]))

enum {
	MTAP_IDSTRING = 0,
	MTAP_VERSION = 12,
	MTAP_PLATFORM,
	MTAP_VIDEOFORMAT,
	MTAP_RESERVED,
	MTAP_DATASIZE
};

static unsigned char mtap_header_default[]={
	'C','1','6','-','T','A','P','E','-','R','A','W',
	0x02, // version -> 1 - whole wave 2 - wholeWave
	0x02, // 0 - C64, 1 - VIC20, 2 - C16/+4
	0x00, // Video standard ( 0= PAL, 1 =NTSC, 2 = NTSC2
	0x00, // empty
	// data length (4 byte) file size!!!!
	0x00, 0x00, 0x00, 0x00
	// data
};

const char tapeFormatStr[][32] = {
	"MTAP1", "MTAP2", "PCM WAV 8-bit", "PCM WAV 16-bit", "Unknown"
};

TAP::TAP() : tapeSoFar(0), tapeFileSize(0), tapeBuffer(NULL), lastCycle(0), edge(0), buttonPressed(0)
{
	buttonPressed = motorOn = false;
}

bool TAP::attachTape(const char *fname)
{
	FILE *tapfile;

	tapeFormat = TAPE_FORMAT_NONE;
	if ((tapfile = fopen(fname,"rb"))) {
		strcpy(tapefilename, fname);
		// load TAP file into buffer
		fseek(tapfile, 0L, SEEK_END);
		tapeFileSize = ftell(tapfile);
		fseek(tapfile, 0L, SEEK_SET);
		// allocate and load file
		tapeBuffer = new unsigned char[tapeFileSize];
		if (fread(tapeBuffer, 1, tapeFileSize, tapfile) < 4)
			return false;
		tapeHeaderRead = tapeBuffer;

		// determine the type of tape file attached
		// MTAP?
		if (!strncmp((const char *) tapeBuffer + 3,"-TAPE-RAW", 9)) {
			tapeImageHeaderSize = tapeSoFar = sizeof(mtap_header_default); // offset to beginning of wave data
			tapeFormat = (tapeHeaderRead[MTAP_VERSION] == 2) ? TAPE_FORMAT_MTAP2 : TAPE_FORMAT_MTAP1;
			// some sanity checks for crappy TAPs
			if (tapeHeaderRead[MTAP_PLATFORM] > 2)
				tapeHeaderRead[MTAP_PLATFORM] = 0;
			if (tapeHeaderRead[MTAP_VIDEOFORMAT] > 1)
				tapeHeaderRead[MTAP_VIDEOFORMAT] = 0;
			// set the MTAP frequency based on header
			unsigned int index = tapeHeaderRead[MTAP_PLATFORM] * 2 + tapeHeaderRead[MTAP_VIDEOFORMAT];
			tapeImageSampleRate = tapFrqs[index];
			// trigger reading first pulse by starting high and no initial delay
			edge = 0x10;
			tapeDelay = 0;
		// PCM WAV?
		} else if (!memcmp(tapeBuffer, "RIFF", 4) && !memcmp(tapeBuffer + 8, "WAVEfmt ", 8)) {
			tapeImageHeaderSize = tapeSoFar = sizeof(wav_header_t);
			wav_header_t *wavh = (wav_header_t *)tapeHeaderRead;
			tapeImageSampleRate = wavh->nSamplesPerSec;
			// mono?
			if (wavh->nChannels != 1)
				return false;
			tapeFormat = wavh->nBitsPerSample == 8 ? TAPE_FORMAT_PCM8 : TAPE_FORMAT_PCM16;
		// if no match, assume it is a 44.1 kHz sample
		} else {
			tapeImageHeaderSize = tapeSoFar = 0;
			tapeImageSampleRate = 44100;
			tapeFormat = TAPE_FORMAT_PCM8;
		}
		motorOn = buttonPressed = false;
		// close the file, it's in the memory now...
		fclose(tapfile);
		fprintf(stderr, "Tape attached    : %s\n", tapefilename);
		fprintf(stderr, "Tape format      : %s\n", tapeFormatStr[(unsigned int)tapeFormat]);
		fprintf(stderr, "Tape data size   : %3.1f kBytes\n", double(tapeFileSize) / 1024.0);
		fprintf(stderr, "Tape sample rate : %u Hz\n", tapeImageSampleRate);
		return true;
	}
	return false;
}

bool TAP::createTape(const char *fname)
{
	FILE *tapfile;
	unsigned int filesize = 0;

	if (strlen(fname) < 5)
		return false;
	// initialise TAP image
	if ((tapfile = fopen(tapefilename,"wb"))) {
		strcpy(tapefilename, fname);
		// write the header
		fwrite(mtap_header_default, sizeof(mtap_header_default), 1, tapfile);
		fwrite(&filesize, 4, 1, tapfile);
		return true;
	}
	else
		return false;
}

bool TAP::detachTape()
{
	//if (tapfile != NULL && TapeSoFar > 0) {
	//	fseek(tapfile, 0x10L, SEEK_SET);
	//	fwrite(&TapeSoFar, sizeof(TapeSoFar), 1, tapfile);
	//	fseek(tapfile, 0L, SEEK_END);
	//  fclose(tapfile);
	//  tapfile = NULL;
	//}
	if (tapeBuffer != NULL) { // just to be sure....
		delete [] tapeBuffer;
		tapeBuffer = NULL;
	}
	tapeSoFar = 0;
	return false;
}

void TAP::rewind()
{
	tapeSoFar = tapeImageHeaderSize;
	if (tapeFormat <= TAPE_FORMAT_MTAP2) {
		// start high since XOR is triggered
		edge = 0x10;
		tapeDelay = 0;
	} else
		edge = 0x00;
}

void TAP::changewave(bool wholewave)
{
	mtap_header_default[MTAP_VERSION] = wholewave ? 1 : 2;
}

inline void TAP::readMtapData(unsigned int elapsed)
{
	while (elapsed--) {
		if (!tapeDelay--) {
			//fprintf( stderr, "TAP unit timeout in cycle: %u\n", elapsed);
			if (edge == 0x10) {
				convTAPUnitsToCycles();
				fallingEdge = true;
			} else {
				tapeDelay = origTapeDelay;
			}
			edge ^= 0x10;
		}
	}
}

inline void TAP::readWavData(unsigned int elapsed)
{
	const unsigned int fastClockFreq = mem->getRealSlowClock() << 1;
	static int prevSample = 0;

	while (elapsed--) {
		if (tapeSoFar >= tapeFileSize)
			return;
		if ((tapeDelay += tapeImageSampleRate) >= fastClockFreq) {
			int sample = tapeBuffer[tapeSoFar++];

			if (tapeFormat == TAPE_FORMAT_PCM16) {
				short in = short(sample | (tapeBuffer[tapeSoFar++] << 8));
				sample += tapeBuffer[tapeSoFar++] << 8;
				int change = short(sample) - prevSample;
				if (sample > 0 && change > 0) {
					edge = 0x10;
				} else if (sample <= 0 && change < 0) {
					edge = 0x00;
					fallingEdge = true;
				}
			} else {
				int change = sample - prevSample;
				if (sample > 0x80 && change > 0) {
					edge = 0x10;
				} else if (sample <= 0x7F && change < 0) {
					edge = 0x00;
					fallingEdge = true;
				}
			}
			prevSample = sample;
			tapeDelay -= fastClockFreq;
		}
	}
}

unsigned char TAP::readCSTIn(ClockCycle cycle)
{
	int elapsed = int(cycle - lastCycle);

	if (motorOn && tapeBuffer) {
		switch (tapeFormat) {
			case TAPE_FORMAT_MTAP1:
			case TAPE_FORMAT_MTAP2:
				readMtapData(elapsed);
				break;
			case TAPE_FORMAT_PCM8:
				readWavData(elapsed);
				break;
			case TAPE_FORMAT_NONE:
			default:
				break;
		}
		lastCycle = cycle;
	}
	return edge;
}

inline unsigned int TAP::readNextTapDelay()
{
	unsigned int delay = tapeBuffer[tapeSoFar++];

	/* byte $00 is a pilot byte */
	if (!delay) {
		delay = tapeBuffer[tapeSoFar++];
		delay += tapeBuffer[tapeSoFar++] << 8;
		delay += tapeBuffer[tapeSoFar++] << 16;
		/*fprintf( stderr, "Pilot byte %i.\n", delay);
		fprintf( stderr, "Pilot offset: %i.\n", TapeSoFar);*/
	} else {
		delay <<= 3;
	}
	unsigned int tapClockFreq = mem->getRealSlowClock() >> 4;
	// machine clock frequency different from MTAP one? then adjust...
	if (tapeImageSampleRate != tapClockFreq) {
		const double frqMult = double(tapClockFreq) / double(tapeImageSampleRate);
		delay = (unsigned int)(double(delay) * frqMult + 0.5);
	}
	if (tapeSoFar >= tapeFileSize) {
		motorOn = buttonPressed = false;
	}
	return delay << 1;
}

void TAP::convTAPUnitsToCycles()
{
	if (tapeFormat == TAPE_FORMAT_MTAP1) {
		origTapeDelay = readNextTapDelay();
		tapeDelay = origTapeDelay >> 1;
		origTapeDelay -= tapeDelay;
	} else {
		tapeDelay = readNextTapDelay();
		origTapeDelay = readNextTapDelay();
	}
	//fprintf(stderr, "New TAP delay value: %u\n", tapeDelay);
}

void TAP::pressTapeButton(ClockCycle cycle, unsigned int pressed)
{
	if (pressed) {
		buttonPressed = 1;
	} else {
		buttonPressed = 0;
		motorOn = false;
	}
}

void TAP::setTapeMotor(ClockCycle cycle, unsigned int on)
{
	if (!on && motorOn)
		readCSTIn(cycle);
	else if (on && !motorOn)
		lastCycle = cycle;
	motorOn = (on != 0);
	//fprintf( stderr, "Motor state: %i, in cycle %u\n", motorOn, cycle);
}
