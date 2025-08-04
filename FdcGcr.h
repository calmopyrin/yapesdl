#ifndef _FDCGCR_H
#define _FDCGCR_H

#include <stdio.h>
#include "types.h"
#include "SaveState.h"

enum {
	DISK_D64, DISK_G64, DISK_D81
};

// Number of tracks/sectors
static const unsigned int MAX_NUM_TRACKS = 42; // 35 41 42
static const unsigned int MIN_d64NumOfSectors = 683;
static const unsigned int MAX_d64NumOfSectors = 768; // This copes with max 40 tracks size d64 images

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
	inline void clearByteReady() { byteReady = 0; };
	void WriteGCRByte(unsigned char byte);
	unsigned char WPState();
	void SetDriveMotor(unsigned char motoron);
	void SpinMotor();
	//
	inline unsigned char *getByteReadyEdge() { return (&byteReadyEdge); };
	inline unsigned char isByteReady() { return byteReady; };
	unsigned char *getSectorHeaderOffset(unsigned int track, unsigned int sector,
										unsigned char *trackStart, unsigned char *trackEnd);

	// this is 2 for 1541 drives and 4 for 1551 drives
	void setSpinFactor(unsigned int f) { spinFactor = f; };
	void SetDensity( unsigned int ds);
	//bool open_g64_file(char *filepath);
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
//	static bool createG64image(char *d64name, char *hdr, char *id1, char *id2);
//	static bool G64writeHeader(FILE *g64);
//	static bool gcrToG64(char *filename, unsigned char *buffer);

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
	unsigned char byteReady;		// Flag: Shift reg finished, new byte is ready
	unsigned char byteReadyEdge;	// Flag: rising edge of byte ready
	bool motorSpinning;		// Flag: Disk motor is on/off
	bool isDiskInserted;		// Flag: Disk inserted
	bool isImageWriteProtected;	// Flag: Disk write-protected
	bool isDiskSwapped;		// Flag: Disk changed (WP sensor strobe control)
	bool isDiskCorrupted;
	bool isImageChanged;		// Flag: D64 image changed
	unsigned int writeMode;		// Flag: R/W mode flag
	unsigned int spinFactor;		// used for synching rotation speed with drive speed
};

/*
   Returns if drive head is over a sync area (10 consecutive '1' bits)
*/
inline unsigned char FdcGcr::SyncFound(void)
{
	// seems like byte ready also set when sync happened
	// but latter has priority...
	// Synch is found after the 10th consecutive 1 bits
	if ( *gcrPtr != 0xFF || writeMode)
		return 0x80;
	else {
		unsigned char *prev_gcr_byte_ptr;
		prev_gcr_byte_ptr = (gcrPtr == gcrTrackBegin) ? (gcrTrackEnd - 1) : gcrPtr - 1;
		if ((*prev_gcr_byte_ptr & 3) != 3) {
			unsigned int shiftReg = gcrCurrentBitcount * 100 / gcrCurrentBitRate;
			if ( shiftReg <= 75 ) { // -> shift register >= 2
				unsigned char *next_gcr_byte_ptr;
				next_gcr_byte_ptr = (gcrPtr == gcrTrackEnd - 1) ? (gcrTrackBegin) : gcrPtr + 1;

				if ((*next_gcr_byte_ptr & 0xC0) == 0xC0)
					return 0x00;
			}
			return 0x80;
		}
		return 0x00;
	}
}

/*
	Toggle the R/W head
*/
inline void FdcGcr::SetRWMode(unsigned int rwmode)
{
	writeMode = (rwmode==0);
	//writeMode ? Log::write("Write mode on\n") : Log::write("Read mode on\n");
	if ( writeMode && isDiskInserted && !isImageWriteProtected) {
		isImageChanged = true;
	}
};

/*
   Write one GCR byte to disk
*/
inline void FdcGcr::WriteGCRByte(unsigned char byte)
{
	//byteReadyEdge = 0;
	byteReady = 0x00;
	byteWritten = byte;
};

/*
   Read a GCR encoded byte from the disk surface, clear byte ready (?)
*/
inline unsigned char FdcGcr::readGCRByte(void)
{
	// FIXME! Reading the disk takes a short, constant amount of time (about
	// 1/20th of a disk rotation
	// important! reading $1C01 clears the byte ready line
	//byteReady = 0x00; // commented out 1.0.4
	byteReadyEdge = 0;
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

/*
	Rotate the motor, shift bytes according to speed zone attributes, set byte ready etc.
*/
inline void FdcGcr::SpinMotor()
{
	if (motorSpinning && !--gcrCurrentBitcount)
	{
		gcrCurrentBitcount = gcrCurrentBitRate;
		// here set byte ready line high, but
		// only when no sync is found.
		if (writeMode) {
			// Rotate disk
			*gcrPtr++ = byteWritten;
			if ( gcrPtr == gcrTrackEnd)
				gcrPtr = gcrTrackBegin; // Restart GCR buffer
		} else {
			// Rotate disk
			gcrPtr++;
			if ( gcrPtr == gcrTrackEnd)
				gcrPtr = gcrTrackBegin; // Restart GCR buffer
			byteLatched = *gcrPtr;
		}
		if (SyncFound()!=0)
		{
			byteReadyEdge = 1;
			byteReady = 0x80;
		}
	}
}

/*
	Select new speed zone (shift register frequency)
*/
inline void FdcGcr::SetDensity( unsigned int ds)
{
	gcrCurrentBitRate = (16 - ((ds>>5)&0x03)) * spinFactor;
	gcrCurrentBitcount = gcrCurrentBitRate;
}

#endif // _FDCGCR_H
