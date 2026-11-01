#ifndef _FDCGCR_H
#define _FDCGCR_H

#include <stdio.h>
#include <stdlib.h>
#include "types.h"
#include "SaveState.h"

enum {
	DISK_D64, DISK_G64, DISK_D81
};

//#define FDC_CYCLE_BASED

// Number of tracks/sectors
static const unsigned int MAX_NUM_TRACKS = 42; // 35 41 42
static const unsigned int MAX_G64_TRACKS = MAX_NUM_TRACKS * 2;
static const unsigned int MIN_d64NumOfSectors = 683;
static const unsigned int MAX_d64NumOfSectors = 768; // This copes with max 40 tracks size d64 images

typedef struct _G64HEADER {
	char headerstring[8];
	unsigned char version; // 0
	unsigned char no_of_tracks;
	unsigned short size_of_tracks;
	unsigned int track_offset_in_file[MAX_G64_TRACKS]; // max is 84
	unsigned int speedZone[MAX_G64_TRACKS];
} g64_header_t;

class FdcGcr : public SaveState {
public:
	FdcGcr();
	virtual ~FdcGcr();

	virtual void openDiskImage(const char *filepath);
	void moveHeadOut();
	void moveHeadIn();
	unsigned char SyncFound();
	void SetRWMode(unsigned int rwmode);
	unsigned char readGCRByte();
	void WriteGCRByte(unsigned char byte);
	unsigned char WPState();
	void SetDriveMotor(unsigned char motoron);
	void setSyncEnable(bool enabled) { isSyncEnabled = enabled; };
	void clockCycles();
	//
	unsigned char *getSectorHeaderOffset(unsigned int track, unsigned int sector,
										unsigned char *trackStart, unsigned char *trackEnd);

	// this is 2 for 1541 drives and 4 for 1551 drives
	void setSpinFactor(unsigned int f) { spinFactor = f; };
	void SetDensity( unsigned int ds);
	virtual void closeDiskImage();
	virtual void trackSector(unsigned int &track, unsigned int &sector);
	virtual void reset();
	virtual bool isMFM() {
		return false;
	}
	unsigned char getMotorState();
	// this is for the FRE support
	virtual void dumpState();
	virtual void readState();
	void setByteReadEdgeCallback(CallBackFunctor cbptr, void* param) {
		byteReadEdgeCallback = cbptr;
		byteReadEdgeCallbackParam = param;
	}
	void setVFlagCallback(CallBackFunctor cbptr, void* param) {
		vFlagCallback = cbptr;
		vFlagCallbackParam = param;
	}
	static void dummyCallback(void* param) {
		(void)(param);
	}

private:

	void attachDiskImage(const char *filepath);
	bool readSector(int track, int sector, unsigned char *buffer);
	bool writeSector(int track, int sector, unsigned char *buffer);
	unsigned int secnumFromTS(unsigned int track, unsigned int sector);
	int offsetFromTS(unsigned int track, unsigned int sector);
	void gcrConv4bytesTo5(unsigned char *from, unsigned char *to);
	void gcrConv5bytesTo4(unsigned char *buffer, unsigned char *ptr);
	void sector2gcr(int track, int sector);
	void gcr2sector(unsigned char *buffer, unsigned char *p, unsigned char *trackStart, unsigned char *trackEnd);
	void disk2gcr();
	void gcr2disk();
	void updateTrackPointers(unsigned int newHalfTrack);
	void feedOneBit();
	void dumpGcr(unsigned char *p);
	FILE *diskImageHandle;
	char imageName[266];
	int imageType;
	unsigned int diskImageHeaderSize;		// Length of D64/x64 file header (if any)
	unsigned char id1, id2;			// Disk IDs
	unsigned char diskErrorInfo[MAX_d64NumOfSectors];	// sector error info (1 byte/sector)
	static unsigned int sectorSize[MAX_NUM_TRACKS+1];
	unsigned int NrOfTracks;
	unsigned int NrOfSectors;

	unsigned int gcrCurrentBitcount;// number of bits rotated*4 (wraps around at 32)
	unsigned int gcrCurrentBitRate;	// current bite rate / speed zone (13-16)
	unsigned int currentHalfTrack;	// current halftrack (2-70)
	unsigned char *gcrData;			// pointer to GCR disk buffer
	unsigned char *gcrPtr;			// GCR data right under the drive head
	unsigned char *gcrTrackBegin;	// pointer to start of GCR data of actual track
	unsigned char *gcrTrackEnd;		// pointer to end of GCR data of actual track
	unsigned char byteLatched;		// GCR byte latched in from the disk surface
	unsigned char byteWritten;		// GCR byte to be written
	bool motorSpinning;		// Flag: Disk motor is on/off
	bool isDiskInserted;		// Flag: Disk inserted
	bool isImageWriteProtected;	// Flag: Disk write-protected
	bool isDiskSwapped;		// Flag: Disk changed (WP sensor strobe control)
	bool isDiskCorrupted;
	bool isImageChanged;		// Flag: D64 image changed
	unsigned int writeMode;		// Flag: R/W mode flag
	unsigned int spinFactor;		// used for synching rotation speed with drive speed
	unsigned short g64TrackSize[MAX_G64_TRACKS];
	unsigned int gcrBitsPerClock[MAX_G64_TRACKS];
	bool isSyncEnabled; // Flag: sync enabled
	CallBackFunctor byteReadEdgeCallback = dummyCallback;
	void* byteReadEdgeCallbackParam = NULL;
	CallBackFunctor vFlagCallback = dummyCallback;
	void* vFlagCallbackParam = NULL;
	unsigned int rngState = 0x2545F491;
	unsigned int nextRandom() {
		rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5;
		return rngState;
	}
#ifdef FDC_CYCLE_BASED
	// --- write path (byte/bit-clocked at the density rate) ---
	unsigned int bitAccumulator = 0;
	unsigned int currentSpeedZoneTarget = 4000;
	unsigned int bitOffset = 0;
	// --- read circuit model: the disk rotates at a fixed 300 rpm (bit rate follows the
	// track length), UE7/UF4 (data separator) are set by the density bits only ---
	bool inSync = false;
	unsigned int diskPhase = 0;			// disk bit position accumulator (16 MHz ticks * track bits)
	unsigned int ue7Counter = 0;		// 4-bit divider, counts up to 16
	unsigned int ue7Dcba = 0;			// UE7 preset = density bits (0..3)
	unsigned int uf4Counter = 0;		// bit cell phase counter, cleared by flux reversals
	unsigned int lastReadData = 0;		// 10 bit read shifter
	unsigned int bitCounter = 0;		// bits since last sync
	int frRandCount = 289;				// ticks until a random flux reversal (no-flux areas)
	unsigned int cycleTick = 0;			// 16 MHz phase within a drive CPU cycle
	unsigned int soDelay = 0;			// ticks until the byte ready edge reaches CA1/SO
	void readCircuit(unsigned int ticks);
	void fluxReversal(bool isRandom);
	void clockShifter();
#endif
};

/*
   Returns if drive head is over a sync area (10 consecutive '1' bits)
*/
inline unsigned char FdcGcr::SyncFound(void)
{
#ifndef FDC_CYCLE_BASED
	// seems like byte ready also set when sync happened
	// but latter has priority...
	// Synch is found after the 10th consecutive 1 bits
	unsigned char currbyte = *gcrPtr;
	if (currbyte != 0xFF || !isSyncEnabled || writeMode)
		return 0x80;
	else {
		unsigned char *prev_gcr_byte_ptr = (gcrPtr == gcrTrackBegin) ? (gcrTrackEnd - 1) : gcrPtr - 1;
		unsigned char prevbyte = *prev_gcr_byte_ptr;
		if ((prevbyte & 3) == 3)
			return 0x00;

		unsigned int shiftReg = gcrCurrentBitcount * 8 / (100000 * spinFactor);
		unsigned char* next_gcr_byte_ptr = (gcrPtr == gcrTrackEnd - 1) ? (gcrTrackBegin) : gcrPtr + 1;
		unsigned char nextbyte = *next_gcr_byte_ptr;
		unsigned int syncWindow = (currbyte << shiftReg) | (nextbyte >> (8 - shiftReg));
		
		if (((syncWindow & 0x3FF) == 0x3FF) || ((prevbyte & 1) && (syncWindow & 1)))
			return 0x00;

		return 0x80;
	}
#else 
	return (inSync || writeMode) ? 0x00 : 0x80;
#endif //FDC_CYCLE_BASED
}

/*
	Toggle the R/W head
*/
inline void FdcGcr::SetRWMode(unsigned int rwmode)
{
	writeMode = (rwmode==0);
	if ( writeMode && isDiskInserted && !isImageWriteProtected) {
		isImageChanged = true;
	}
}

/*
   Write one GCR byte to disk
*/
inline void FdcGcr::WriteGCRByte(unsigned char byte)
{
	byteWritten = byte;
}

/*
   Read a GCR encoded byte from the disk surface, clear byte ready (?)
*/
inline unsigned char FdcGcr::readGCRByte()
{
	return byteLatched;
}


/*
   Returns state of WP sensor strobe (read-only also if no disk present)
*/
inline unsigned char FdcGcr::WPState(void)
{
	if (!isDiskInserted)
		return 0;
	else if (isDiskSwapped) {
		// Disk change -> WP sensor strobe
		// Makes sure WP at least once is reported on disk change
		isDiskSwapped = false;
		return isImageWriteProtected ? 0x10 : 0;
	} else
		return isImageWriteProtected ? 0 : 0x10;
}

inline void FdcGcr::SetDriveMotor(unsigned char motoron)
{
	motorSpinning = motoron != 0;
}

inline unsigned char FdcGcr::getMotorState()
{
	return motorSpinning ? 1 : 0;
}

#ifndef FDC_CYCLE_BASED
/*
	Rotate the motor, shift bytes according to speed zone attributes, set byte ready etc.
*/
inline void FdcGcr::clockCycles()
{
	gcrCurrentBitcount += (unsigned int)(gcrTrackEnd - gcrTrackBegin);
	if (gcrCurrentBitcount >= 100000 * spinFactor)
	{
		gcrCurrentBitcount -= 100000 * spinFactor;
		if (motorSpinning) {
			// here set byte ready line high, but
			// only when no sync is found.
			if (writeMode) {
				// Rotate disk
				*gcrPtr++ = byteWritten;
				if (gcrPtr == gcrTrackEnd)
					gcrPtr = gcrTrackBegin; // Restart GCR buffer
			}
			else {
				// Rotate disk
				gcrPtr++;
				if (gcrPtr >= gcrTrackEnd)
					gcrPtr = gcrTrackBegin; // Restart GCR buffer
				unsigned char rawByte = *gcrPtr;
				// G64 encodes weak / unformatted flux zones as 0x00
				if (rawByte == 0x00) {
					// Inject pseudo-random noise bits instead of static 0x00
					// Real hardware generates random noise on empty flux
					rawByte = static_cast<unsigned char>(nextRandom() >> 16);
				}
				byteLatched = rawByte;
			}
			if (SyncFound() != 0) {
				byteReadEdgeCallback(byteReadEdgeCallbackParam);
				vFlagCallback(vFlagCallbackParam);
			}
		} else {
			// Spindle is stopped: head sees no flux transitions (no SYNC possible), BYTE READY fires
			byteLatched = static_cast<unsigned char>(nextRandom() >> 16);
			if (byteLatched == 0xFF) byteLatched &= 0xEE;
		}
	}
}

/*
	Select new speed zone (shift register frequency)
*/
inline void FdcGcr::SetDensity(unsigned int ds)
{
	const unsigned int currentSpeedZone = 16 - ((ds >> 5) & 3);
	gcrCurrentBitRate = currentSpeedZone * spinFactor;
}
#else
inline void FdcGcr::SetDensity(unsigned int ds)
{
	// CPU cycles per bit scaled by 1000 to avoid floats
	const unsigned int CPU_CYCLES_PER_BIT_FP[4] = { 4000, 3750, 3500, 3250 };
	currentSpeedZoneTarget = CPU_CYCLES_PER_BIT_FP[(ds >> 5) & 3];
	ue7Dcba = (ds >> 5) & 3;	// preset of the UE7 divider: 16 - dcba reference ticks per quarter bit cell
	//fprintf(stderr, "Speed zone set to: %u on halftrack: %u\n", (ds >> 5) & 3, currentHalfTrack);
}

inline void FdcGcr::clockCycles()
{
	if (writeMode) {
		bitAccumulator += (2000 / spinFactor);
		while (bitAccumulator >= currentSpeedZoneTarget) {
			bitAccumulator -= currentSpeedZoneTarget;
			feedOneBit();
		}
	} else
		readCircuit(32 / spinFactor); // 16 reference ticks (16 MHz) per 1 MHz drive cycle
}

// write mode only: bit clocked at the density rate
inline void FdcGcr::feedOneBit()
{
	if (bitOffset == 7) {
		*gcrPtr = byteWritten;
		byteReadEdgeCallback(byteReadEdgeCallbackParam);
		vFlagCallback(vFlagCallbackParam);
	}
	inSync = false;
	bitOffset++;
	if (bitOffset >= 8) {
		bitOffset = 0;
		if (motorSpinning) {
			gcrPtr++;
			if (gcrPtr >= gcrTrackEnd)
				gcrPtr = gcrTrackBegin;
		}
	}
}

// a flux reversal (real or noise) resynchronises the data separator
inline void FdcGcr::fluxReversal(bool isRandom)
{
	ue7Counter = ue7Dcba;
	uf4Counter = 0;
	// start seeing random flux reversals if 18us passed since the last real flux reversal
	frRandCount = isRandom ? (int)((nextRandom() >> 16) % 367) + 33
						   : (int)((nextRandom() >> 16) % 31) + 289;
}

// UF4 stage B rising edge clocks the read shifter; the NOR gate shifts a 1 only while UF4 < 4
inline void FdcGcr::clockShifter()
{
	lastReadData = ((lastReadData << 1) & 0x3FE) | (uf4Counter < 4 ? 1 : 0);
	if (lastReadData == 0x3FF) {
		// sync: byte counter is held in reset
		bitCounter = 0;
		inSync = isSyncEnabled;
		byteLatched = 0xFF;
	} else {
		inSync = false;
		if (++bitCounter == 8) {
			bitCounter = 0;
			byteLatched = (unsigned char)(lastReadData & 0xFF);
			// byte ready reaches the CPU after the next CPU clock edge
			soDelay = 16 - cycleTick;
			if (soDelay < 10)
				soDelay += 16;
		}
	}
}

inline void FdcGcr::readCircuit(unsigned int ticks)
{
	const unsigned int REV_TICKS = 3200000;	// 16 MHz * 200 ms (300 rpm)
	const unsigned int trackBits = (unsigned int)(gcrTrackEnd - gcrTrackBegin) * 8;

	while (ticks) {
		unsigned int step = ticks;
		unsigned int toBit = 0xFFFFFFFF;
		if (motorSpinning && trackBits) {
			toBit = (REV_TICKS - diskPhase + trackBits - 1) / trackBits;
			if (!toBit) toBit = 1;
			if (toBit < step) step = toBit;
		}
		if (16 - ue7Counter < step) step = 16 - ue7Counter;
		if (frRandCount > 0 && (unsigned int)frRandCount < step) step = (unsigned int)frRandCount;
		if (soDelay && soDelay < step) step = soDelay;
		if (!step) step = 1;

		ticks -= step;
		cycleTick = (cycleTick + step) & 15;
		ue7Counter += step;
		frRandCount -= (int)step;
		if (motorSpinning && trackBits)
			diskPhase += step * trackBits;

		// disk delivers its next bit cell; a 1 is a flux reversal
		if (motorSpinning && trackBits && diskPhase >= REV_TICKS) {
			diskPhase -= REV_TICKS;
			unsigned int bit = (*gcrPtr >> (7 - bitOffset)) & 1;
			if (++bitOffset >= 8) {
				bitOffset = 0;
				if (++gcrPtr >= gcrTrackEnd)
					gcrPtr = gcrTrackBegin;
			}
			if (bit)
				fluxReversal(false);
		}
		// no flux for 18 us: weak/random flux reversals from the read amplifier
		if (frRandCount <= 0)
			fluxReversal(true);
		// UE7 carry advances the bit cell phase
		if (ue7Counter >= 16) {
			ue7Counter = ue7Dcba;
			uf4Counter = (uf4Counter + 1) & 0xF;
			if ((uf4Counter & 3) == 2)
				clockShifter();
		}
		// byte ready edge towards VIA2 CA1 and the CPU's SO pin
		if (soDelay) {
			soDelay -= step;
			if (!soDelay) {
				byteReadEdgeCallback(byteReadEdgeCallbackParam);
				vFlagCallback(vFlagCallbackParam);
			}
		}
	}
}
#endif

#endif // _FDCGCR_H
