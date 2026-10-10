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
	unsigned char byteReady;		// Shift reg finished, new byte is ready
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
	CallBackFunctor byteReadEdgeCallback = NULL;
	void* byteReadEdgeCallbackParam = NULL;
	CallBackFunctor vFlagCallback = NULL;
	void* vFlagCallbackParam = NULL;
#ifdef FDC_CYCLE_BASED
	unsigned int bitAccumulator = 0;
	unsigned int currentSpeedZoneTarget = 4000;
	unsigned int bitOffset = 0;
	bool inSync = false;
	unsigned int postSyncBitCount = 0;
	unsigned int shiftRegister = 0;
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

		unsigned int shiftReg = gcrCurrentBitcount * 8 / gcrCurrentBitRate;
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
	gcrCurrentBitcount += 1;
	if (gcrCurrentBitcount >= gcrCurrentBitRate)
	{
		gcrCurrentBitcount -= gcrCurrentBitRate;
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
					rawByte = static_cast<unsigned char>(rand() & 0xFF);
				}
				byteLatched = rawByte;
			}
			if (SyncFound() != 0) {
				byteReadEdgeCallback(byteReadEdgeCallbackParam);
				vFlagCallback(vFlagCallbackParam);
			}
		} else {
			// Spindle is stopped: head sees no flux transitions (no SYNC possible), BYTE READY fires
			byteLatched = static_cast<unsigned char>(rand() & 0xFF); // should be random
			if (byteLatched == 0xFF) byteLatched &= 0xEE;
		}
	}
}
#endif

/*
	Select new speed zone (shift register frequency)
*/
inline void FdcGcr::SetDensity(unsigned int ds)
{
#ifdef FDC_CYCLE_BASED
	// CPU cycles per bit scaled by 1000 to avoid floats
	const unsigned int CPU_CYCLES_PER_BIT_FP[4] = { 4000, 3750, 3500, 3250 };
	currentSpeedZoneTarget = CPU_CYCLES_PER_BIT_FP[(ds >> 5) & 3];
	//fprintf(stderr, "Speed zone set to: %u on halftrack: %u\n", (ds >> 5) & 3, currentHalfTrack);
#else
	const unsigned int currentSpeedZone = 16 - ((ds >> 5) & 3);
	gcrCurrentBitRate = currentSpeedZone * spinFactor;
#endif
}
#ifdef FDC_CYCLE_BASED
inline void FdcGcr::clockCycles()
{
	bitAccumulator += (spinFactor * 500);

	while (bitAccumulator >= currentSpeedZoneTarget) {
		bitAccumulator -= currentSpeedZoneTarget;
		feedOneBit();
	}
}

inline void FdcGcr::feedOneBit()
{
	if (writeMode) {
		// --- WRITE MODE ---
		if (bitOffset == 7) {
			*gcrPtr = byteWritten;
			byteReadEdgeCallback(byteReadEdgeCallbackParam);
			vFlagCallback(vFlagCallbackParam);
		}
		inSync = false;
	}
	else {
		unsigned int bit = (byteReady >> (7 - bitOffset)) & 1;

		// shift into 10-bit Gate Array SYNC window
		shiftRegister = ((shiftRegister << 1) | bit) & 0x03FF;

		if (shiftRegister == 0x03FF) {
			// 10 consecutive ones = SYNC detected
			inSync = isSyncEnabled;
			//postSyncBitCount = 0;
			byteLatched = 0xFF;
		}
		else if (inSync) {
			// First '0' bit exits SYNC state
			inSync = false;
			postSyncBitCount = 0;
		}

		// assemble 8 post-SYNC bits into byte latch
		if (!inSync) {
			postSyncBitCount++;
			if (postSyncBitCount == 8) {
				postSyncBitCount = 0;
				byteLatched = shiftRegister & 0xFF;
				// assert SO pin + VIA2 CA1 interrupt
				// make sure this is never null!
				byteReadEdgeCallback(byteReadEdgeCallbackParam);
				vFlagCallback(vFlagCallbackParam);
			}
		}
	}

	// advance GCR bit stream pointer
	bitOffset++;
	if (bitOffset >= 8) {
		bitOffset = 0;
		if (motorSpinning) {
			gcrPtr++;
			if (gcrPtr >= gcrTrackEnd) {
				gcrPtr = gcrTrackBegin;
			}
			if (!writeMode)
				// fetch raw byte from GCR buffer
				byteReady = *gcrPtr;
		} else 
			byteReady = 0;
		// FIXME inject weak-bit noise on empty flux in read mode
	}
}
#endif

#endif // _FDCGCR_H
