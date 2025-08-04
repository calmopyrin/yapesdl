/*

  Here we emulate low-level disk drive mechanics.

  The bit rate is not a function of spindle motor speed (as is the bit
  density, in bits/inch).  The bit rate is:

  Tracks 1 to 17       4,000,000/13
  Tracks 18 to 24      4,000,000/14
  Tracks 25 to 30      4,000,000/15
  Tracks 31 to 35      4,000,000/16

  These table gives us the number of gcr bits that rotated per tick * 4

  Tracks over 35 are not properly emulated...

*/
#include <memory.h>
#include <string.h>
#include <stdio.h>
#include "FdcGcr.h"

const unsigned int NUM_SYNC=5;
// this should be speed zone dependent
const unsigned int GCR_SECTOR_GAP_LENGTH = 6; // in VICE it's 6?!
							// SYNC Header Gap SYNC Data Gap
const unsigned int GCR_SECTOR_SIZE = NUM_SYNC+10+9+NUM_SYNC+325+GCR_SECTOR_GAP_LENGTH;
// Maximum track size is +3% of the theoretical value of max. bit rate (7928)
const unsigned int GCR_MAX_TRACK_SIZE = 4000000 / 8 / 13 / 5 * 10307 / 10000;//GCR_SECTOR_SIZE * 21;
// Total GCR encoded data size
const unsigned int GCR_DISK_SIZE = GCR_MAX_TRACK_SIZE * MAX_NUM_TRACKS;

// Nr of sectors on each track
const unsigned int d64NumOfSectors[MAX_NUM_TRACKS+1] = {
	0,
	21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21, // 1-17
	19,19,19,19,19,19,19, // 18-24
	18,18,18,18,18,18, // 25-30
	17,17,17,17,17, // 30-35
	// extra
	17,17,17,17,17,17 // 36-41
};

// Pointer offset vector to tracks and sectors in d64 file
const unsigned int d64SectorOffset[MAX_NUM_TRACKS+1] = {
	0,
	0,21,42,63,84,105,126,147,168,189,210,231,252,273,294,315,336,
	357,376,395,414,433,452,471,
	490,508,526,544,562,580,
	598,615,632,649,666,
	// extra tracks
	683,700,717,734,751,768
};

const struct {
	unsigned char error_code;
	char error_desc[40];
} job_codes[] = {
	{0 , "N/A"},
	{0 , "No error, sector ok."},
	{20, "Header block not found."},
	{21, "No sync character."},
	{22, "Data block not present."},
	{23, "Checksum error in data block."},
	{24, "N/A"},
	{25, "N/A"},
	{26, "N/A"},
	{27, "Checksum error in header block."},
	{28, "N/A"},
	{29, "Disk ID mismatch."}
};

const unsigned int speed_zone[MAX_NUM_TRACKS+1] = {
	0,
	3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3, // 1-17
	2,2,2,2,2,2,2, // 18-24
	1,1,1,1,1,1, // 25-30
	0,0,0,0,0, // 30-35
	// extra
	0,0,0,0,0,0 // 36-41
};

#define MAX_G64_TRACKS 84
typedef struct _G64HEADER {
	char headerstring[8];
	unsigned char version; // 0
	unsigned char no_of_tracks;
	unsigned short size_of_tracks;
	unsigned int track_offset_in_file[MAX_G64_TRACKS]; // max is 84
	unsigned int speedZone[MAX_G64_TRACKS];
} g64_header_t;
static g64_header_t g64hdr = { 0 };

unsigned int FdcGcr::sectorSize[MAX_NUM_TRACKS+1];

FdcGcr::FdcGcr()
{
	setId("FDC8");
	diskImageHandle = NULL;

	gcrData = gcrPtr = gcrTrackBegin = new unsigned char[GCR_DISK_SIZE];
	gcrTrackEnd = gcrTrackBegin + GCR_MAX_TRACK_SIZE;
	currentHalfTrack = 2;

	isDiskInserted = false;
	motorSpinning = false;
	isDiskCorrupted = false;
	gcrCurrentBitcount = 0;
	spinFactor = 2;
	gcrCurrentBitRate = spinFactor * 16;
	writeMode = false;
	for (unsigned int i = 1; i<=MAX_NUM_TRACKS; i++ ) {
		// 300 rotation per min, 5 per sec
		sectorSize[i] = 4000000 / (16 - speed_zone[i]) / 8 / 5;
	}
	NrOfTracks = 35;
}

FdcGcr::~FdcGcr()
{
	closeDiskImage();
	isDiskInserted = false;
	if (gcrData)
		delete[] gcrData;
}

void FdcGcr::dumpState()
{
	unsigned int tmp;

	saveVar(imageName, sizeof(imageName));
	saveVar(&imageType, sizeof(imageType));
	saveVar(&NrOfTracks, sizeof(NrOfTracks));
	saveVar(&NrOfSectors, sizeof(NrOfSectors));
	saveVar(&gcrCurrentBitcount, sizeof(gcrCurrentBitcount));
	saveVar(&gcrCurrentBitRate, sizeof(gcrCurrentBitRate));
	saveVar(&currentHalfTrack, sizeof(currentHalfTrack));
	saveVar(gcrData, GCR_DISK_SIZE);
	tmp = (unsigned int) (gcrPtr - gcrData);
	saveVar(&tmp, sizeof(tmp));
	tmp = (unsigned int)(gcrTrackBegin - gcrData);
	saveVar(&tmp, sizeof(tmp));
	tmp = (unsigned int)(gcrTrackEnd - gcrData);
	saveVar(&tmp, sizeof(tmp));
	saveVar(&byteLatched, sizeof(byteLatched));
	saveVar(&byteWritten, sizeof(byteWritten));
	saveVar(&byteReady, sizeof(byteReady));
	saveVar(&byteReadyEdge, sizeof(byteReadyEdge));
	saveVar(&motorSpinning, sizeof(motorSpinning));
	saveVar(&isDiskInserted, sizeof(isDiskInserted));
	saveVar(&isImageWriteProtected, sizeof(isImageWriteProtected));
	saveVar(&isDiskSwapped, sizeof(isDiskSwapped));
	saveVar(&isDiskCorrupted, sizeof(isDiskCorrupted));
	saveVar(&isImageChanged, sizeof(isImageChanged));
	saveVar(&writeMode, sizeof(writeMode));
	saveVar(&spinFactor, sizeof(spinFactor));
}

void FdcGcr::readState()
{
	unsigned int tmp;

	// close old image
	closeDiskImage();

	readVar(imageName, sizeof(imageName));
	readVar(&imageType, sizeof(imageType));
	readVar(&NrOfTracks, sizeof(NrOfTracks));
	readVar(&NrOfSectors, sizeof(NrOfSectors));
	readVar(&gcrCurrentBitcount, sizeof(gcrCurrentBitcount));
	readVar(&gcrCurrentBitRate, sizeof(gcrCurrentBitRate));
	readVar(&currentHalfTrack, sizeof(currentHalfTrack));
	readVar(gcrData, GCR_DISK_SIZE);
	readVar(&tmp, sizeof(tmp));
	gcrPtr = tmp + gcrData;
	readVar(&tmp, sizeof(tmp));
	gcrTrackBegin = tmp + gcrData;
	readVar(&tmp, sizeof(tmp));
	gcrTrackEnd = tmp + gcrData;
	readVar(&byteLatched, sizeof(byteLatched));
	readVar(&byteWritten, sizeof(byteWritten));
	readVar(&byteReady, sizeof(byteReady));
	readVar(&byteReadyEdge, sizeof(byteReadyEdge));
	readVar(&motorSpinning, sizeof(motorSpinning));
	readVar(&isDiskInserted, sizeof(isDiskInserted));
	readVar(&isImageWriteProtected, sizeof(isImageWriteProtected));
	readVar(&isDiskSwapped, sizeof(isDiskSwapped));
	readVar(&isDiskCorrupted, sizeof(isDiskCorrupted));
	readVar(&isImageChanged, sizeof(isImageChanged));
	readVar(&writeMode, sizeof(writeMode));
	readVar(&spinFactor, sizeof(spinFactor));
}

void FdcGcr::openDiskImage(const char *filepath)
{
	closeDiskImage();
	attachDiskImage(filepath);
	strcpy(imageName, filepath);
}

void FdcGcr::reset()
{
	gcrCurrentBitcount = 0;
	writeMode = false;
}

void FdcGcr::closeDiskImage()
{
	if (diskImageHandle != NULL) {
		fflush(diskImageHandle);
		fclose(diskImageHandle);
		diskImageHandle = NULL;
	}
	// check if images has changed and save
	if (isDiskInserted && !isDiskCorrupted && isImageChanged) {
		if(DISK_D64 == imageType)
			gcr2disk();
	}
	strcpy(imageName, "");
	// Clear GCR buffer with gaps to avoid read errors later
	memset(gcrData, 0x55, GCR_DISK_SIZE);

	isDiskInserted = false;
}

void FdcGcr::attachDiskImage(const char *filepath)
{
	unsigned long size;
	unsigned char magic[4];
	unsigned char bam[256];

	// Try opening the file as R/W, then for read only if it failed
	isImageWriteProtected = false;
	diskImageHandle = fopen(filepath, "rb+");
	if (diskImageHandle == NULL) {
		isImageWriteProtected = true;
		diskImageHandle = fopen(filepath, "rb");
	}
	if (diskImageHandle != NULL) {
		isDiskCorrupted = false;
		fseek(diskImageHandle, 0, SEEK_END);
		size = ftell(diskImageHandle);
		fseek(diskImageHandle, 0, SEEK_SET);
		size_t read = fread(&g64hdr, 1, sizeof(g64hdr), diskImageHandle);
		// is it a G64 image?
		if (read == sizeof(g64hdr) && !strncmp(g64hdr.headerstring, "GCR-1541", 8)) {
			imageType = DISK_G64;
			NrOfTracks = g64hdr.no_of_tracks;
			for (unsigned int track = 0; track < NrOfTracks; track++) {

				int offset;
				unsigned char* p = gcrData + (track / 2) * GCR_MAX_TRACK_SIZE;

				if (p >= gcrData + GCR_DISK_SIZE) {
					fprintf(stderr, "Error opening G64 file: GCR buffer overflow on track %i.", track);
					break;
				}

				offset = g64hdr.track_offset_in_file[track];
				// don't process half tracks...
				if (offset && !(track & 1)) {
					unsigned short stored_track_size;

					fseek(diskImageHandle, offset, SEEK_SET);
					fread(&stored_track_size, 2, 1, diskImageHandle);
					fread(p, GCR_MAX_TRACK_SIZE, 1, diskImageHandle);
				}
			}
			fprintf(stderr, "Attached low level disk (%u tracks): %s\n", NrOfTracks, filepath);
		}
		else {
			// open as D64
			// rewind
			fseek(diskImageHandle, 0, SEEK_SET);
			// Check length
			if ((size < MIN_d64NumOfSectors * 256) || (size > MAX_d64NumOfSectors * 257)) {
				fclose(diskImageHandle);
				diskImageHandle = NULL;
				isDiskInserted = false;
				return;
			}

			switch (size) {
			case 174848:
			case 175531:
				NrOfTracks = 35;
				break;
			case (683 + 1 * 17) * 256:
			case (683 + 1 * 17) * 256 + 683 + 17:
				NrOfTracks = 36;
				break;
			case (683 + 2 * 17) * 256:
			case (683 + 2 * 17) * 256 + 683 + 17 * 2:
				NrOfTracks = 37;
				break;
			case (683 + 3 * 17) * 256:
			case (683 + 3 * 17) * 256 + 683 + 17 * 3:
				NrOfTracks = 38;
				break;
			case (683 + 4 * 17) * 256:
			case (683 + 4 * 17) * 256 + 683 + 17 * 4:
				NrOfTracks = 39;
				break;
			case (683 + 5 * 17) * 256:
			case (683 + 5 * 17) * 256 + 683 + 17 * 5:
				NrOfTracks = 40;
				break;
			default:
				NrOfTracks = 35;
				break;
			}
			NrOfSectors = d64SectorOffset[NrOfTracks + 1];

			// x64 image?
			fread(&magic, 4, 1, diskImageHandle);
			if (magic[0] == 0x43 && magic[1] == 0x15 && magic[2] == 0x41 && magic[3] == 0x64)
				diskImageHeaderSize = 64;
			else
				diskImageHeaderSize = 0;

			// Preset error info (all sectors no error)
			memset(diskErrorInfo, 1, sizeof(diskErrorInfo));

			// Load sector error info from .d64 file, if available
			if (!diskImageHeaderSize && size == NrOfSectors * 257) {
				fseek(diskImageHandle, NrOfSectors * 256, SEEK_SET);
				fread(&diskErrorInfo, NrOfSectors, 1, diskImageHandle);
			}

			// Read BAM and get ID
			if (!readSector(18, 0, bam)) {
				isDiskCorrupted = true;
			}
			else {
				id1 = bam[162];
				id2 = bam[163];
				// Create GCR encoded disk data from image
				disk2gcr();
			}
			imageType = DISK_D64;
			fprintf(stderr, "Attached disk (%u tracks, %u sectors): %s\n", NrOfTracks, NrOfSectors, filepath);
		}
		// indicate that the disk is present
		isDiskInserted = true;
		isImageChanged = false;
		isDiskSwapped = true;
		
		return;
	}
	isDiskInserted = false;
}

bool FdcGcr::readSector(int track, int sector, unsigned char *buffer)
{
	int offset;

	// Convert track/sector to byte offset in file
	if ((offset = offsetFromTS(track, sector)) < 0)
		return false;

	fseek(diskImageHandle, offset + diskImageHeaderSize, SEEK_SET);

	size_t r = fread(buffer, 1, 256, diskImageHandle);
	return r == 256;
}

void FdcGcr::trackSector(unsigned int &track, unsigned int &sector)
{
	track = ((currentHalfTrack >> 1));
	sector = (unsigned int) ((gcrPtr - gcrTrackBegin) / GCR_SECTOR_SIZE);
}

/*
   Convert track/sector to offset
*/

unsigned int FdcGcr::secnumFromTS(unsigned int track, unsigned int sector)
{
	return d64SectorOffset[track] + sector;
}

int FdcGcr::offsetFromTS(unsigned int track, unsigned int sector)
{
	if ((track < 1) || (track > MAX_NUM_TRACKS) || (sector >= d64NumOfSectors[track]))
		return -1;

	return (d64SectorOffset[track] + sector) << 8;
}

/*
   Convert between 4 bytes and 5 GCR encoded bytes
*/
const unsigned int tblEncodeToGCR[16] = {
	0x0A, 0x0B, 0x12, 0x13, 0x0E, 0x0F, 0x16, 0x17,
	0x09, 0x19, 0x1A, 0x1B, 0x0D, 0x1D, 0x1E, 0x15
};

const unsigned int tblDecodeFromGCR[32] = {
	0, 0, 0, 0, 0, 0, 0, 0,  0, 8, 0, 1, 0,12, 4, 5,
	0, 0, 2, 3, 0,15, 6, 7,  0, 9,10,11, 0,13,14, 0
};

void FdcGcr::gcrConv4bytesTo5(unsigned char *from, unsigned char *to)
{
	unsigned int g;

	g = (tblEncodeToGCR[*from >> 4] << 5) | tblEncodeToGCR[*from & 15];
	*to++ = g >> 2;
	*to = (g << 6) & 0xC0;
	from++;

	g = (tblEncodeToGCR[*from >> 4] << 5) | tblEncodeToGCR[*from & 15];
	*to++ |= (g >> 4) & 0x3F;
	*to = (g << 4) & 0xF0;
	from++;

	g = (tblEncodeToGCR[*from >> 4] << 5) | tblEncodeToGCR[*from & 15];
	*to++ |= (g >> 6) & 0x0F;
	*to = (g << 2) & 0xFC;
	from++;

	g = (tblEncodeToGCR[*from >> 4] << 5) | tblEncodeToGCR[*from & 15];
	*to++ |= (g >> 8) & 0x03;
	*to = (unsigned char) g;
}

void FdcGcr::gcrConv5bytesTo4(unsigned char *buffer, unsigned char *ptr)
{
	unsigned int i;
	unsigned char gcr_bytes[8];

	gcr_bytes[0] = (*buffer)>>3;
	gcr_bytes[1] =((*buffer++)&0x07)<<2;
	gcr_bytes[1] |=(*buffer)>>6;
	gcr_bytes[2] =((*buffer)&0x3E)>>1;
	gcr_bytes[3] =((*buffer++)&0x01)<<4;
	gcr_bytes[3]|=((*buffer)&0xF0)>>4;
	gcr_bytes[4] =((*buffer++)&0x0F)<<1;
	gcr_bytes[4] |=(*buffer)>>7;
	gcr_bytes[5] =((*buffer)&0x7C)>>2;
	gcr_bytes[6] =((*buffer++)&0x03)<<3;
	gcr_bytes[6]|=((*buffer)&0xE0)>>5;
	gcr_bytes[7] = (*buffer)&0x1F;

	for (i = 0; i < 4; i++, ptr++) {
		*ptr = tblDecodeFromGCR[gcr_bytes[2 * i]] << 4;
		*ptr |= tblDecodeFromGCR[gcr_bytes[2 * i + 1]];
	}
}

void FdcGcr::sector2gcr(int track, int sector)
{
	unsigned char block[256];
	unsigned char buf[4], headerID1;
/*
	This formula did not work with Nibble hack em
	unsigned char *p = gcrData + (track-1) * GCR_MAX_TRACK_SIZE +
		sector * sectorSize[track] / d64NumOfSectors[track];
*/
	unsigned char *p = gcrData + (track-1) * GCR_MAX_TRACK_SIZE + sector * GCR_SECTOR_SIZE;
	//Log::write("Track %02i, sector %02i at offset %04i.\n", track, sector, p - gcrData );
	readSector(track, sector, block);

	const unsigned char errCode = job_codes[ diskErrorInfo[d64SectorOffset[track]+sector]].error_code;
	const unsigned char syncByte = errCode == 21 ? 0x55 : 0xFF;

	headerID1 = (errCode == 29) ? id1^0xFF : id1;
	// Create GCR header
	memset( p, syncByte, NUM_SYNC);	// Header SYNC bytes
	p += NUM_SYNC;
	buf[0] = (errCode == 20) ? 0xFF : 0x08;	// Header mark
	buf[1] = sector ^ track ^ id2 ^ headerID1; // Checksum
	buf[2] = sector;
	buf[3] = track;

	if (errCode == 27)
		buf[1] ^= 0xFF;

	gcrConv4bytesTo5(buf, p);
	buf[0] = id2;
	buf[1] = headerID1;
	buf[2] = buf[3] = 0x0f;
	gcrConv4bytesTo5(buf, p+5);
	p += 10;
	memset(p, 0x55, 9);		// Header gap
	p += 9;

	// Create GCR data
	unsigned char sum;
	memset( p, syncByte, NUM_SYNC);	// Data SYNC bytes
	p += NUM_SYNC;

	buf[0] = errCode == 22 ? 0xFF : 0x07; // Data mark
	sum = buf[1] = block[0];
	sum ^= buf[2] = block[1];
	sum ^= buf[3] = block[2];
	gcrConv4bytesTo5(buf, p);
	p += 5;
	for (int i=3; i<255; i+=4) {
		sum ^= buf[0] = block[i];
		sum ^= buf[1] = block[i+1];
		sum ^= buf[2] = block[i+2];
		sum ^= buf[3] = block[i+3];
		gcrConv4bytesTo5(buf, p);
		p += 5;
	}
	sum ^= buf[0] = block[255];
	if (errCode == 23) sum ^= 0xFF;
	buf[1] = sum;	// Checksum
	// These bytes are needed to have a multiple of four bytes
	buf[2] = 0;
	buf[3] = 0;
	gcrConv4bytesTo5(buf, p);
	p += 5;

	memset(p, 0x55, GCR_SECTOR_GAP_LENGTH);	// Gap before next sector
	//p += GCR_SECTOR_GAP_LENGTH;
}

void FdcGcr::gcr2sector(unsigned char *buffer, unsigned char *p,
							   unsigned char *trackStart,
							   unsigned char *trackEnd)
{
	unsigned int	i, j;
	unsigned char	*offset = p;
	unsigned char	gcrBufferPtr[5];

	for ( i=0; i<65; i++) {
		for ( j=0; j<5; j++) {
			gcrBufferPtr[j] = *offset++;
			if (offset == trackEnd)
				offset = trackStart;
		}
		gcrConv5bytesTo4(gcrBufferPtr, buffer);
		buffer += 4;
	}
}

void FdcGcr::dumpGcr(unsigned char *p)
{
	FILE *gcrdump = NULL;
	char dumpName[512];

	sprintf(dumpName, "GCRDUMP%s.BIN", imageName);
	gcrdump = fopen(dumpName, "wb");
	if (!gcrdump)
		return;
	for (unsigned int i = 0; i < GCR_MAX_TRACK_SIZE * 35; i++)
		fputc( *p++, gcrdump );
	fclose( gcrdump );
}

void FdcGcr::disk2gcr(void)
{
	// Convert all tracks and sectors
	for ( unsigned int track=1; track<=NrOfTracks; track++)
		for( unsigned int sector=0; sector<d64NumOfSectors[track]; sector++)
			sector2gcr(track, sector);
#if 0
	dumpGcr(gcrData);
#endif
}

unsigned char *FdcGcr::getSectorHeaderOffset(unsigned int track, unsigned int sector,
										unsigned char *trackStart, unsigned char *trackEnd)
{
	unsigned char *offset = trackStart;
	unsigned char gcr_header[5], header_data[4];
	bool wrap_around = false;
	int nr_of_syncs_found = 0;

	while ((offset < trackEnd) && !wrap_around) {
		while (*offset != 0xFF) {
			offset++;
			if (offset >= trackEnd) {
				if (wrap_around) {
//					Log::write(_T("GCR error: no synch found.\n"));
					dumpGcr(gcrData);
					return NULL;
				} else {
					wrap_around = true;
					offset = trackStart;
				}
			}
		}
		while (*offset == 0xFF) {
			offset++;
			if (offset == trackEnd) {
				offset = trackStart;
				wrap_around = true;
			}
			// more sync than size is found?
			if( (trackEnd - trackStart) <= ++nr_of_syncs_found ) {
//				Log::write(_T("GCR error: too many synch bytes found.\n"));
				return NULL;
			}
		}
		for (unsigned int i = 0; i < 5; i++) {
			gcr_header[i] = *(offset++);
			if (offset >= trackEnd) {
				offset = trackStart;
				wrap_around = true;
			}
		}
		gcrConv5bytesTo4(gcr_header, header_data);
		if (header_data[0] == 0x08 && header_data[2] == sector && header_data[3] == track)
			return offset;
	}
	return NULL;
}

void FdcGcr::gcr2disk(void)
{
	FILE			*d64dump;
	char			new_d64_file[266];
	unsigned char	*offset;
	unsigned char	sector_buffer[260];

	strcpy( new_d64_file, imageName);

	d64dump = fopen( const_cast<char*> (new_d64_file), "wb");
	if (!d64dump)
		return;

	// Convert all tracks and sectors
	for ( unsigned int track = 1; track<=NrOfTracks; track ++) { // NUM_TRACKS

		unsigned char *trackStart	= gcrData + (track - 1) * GCR_MAX_TRACK_SIZE;
		unsigned char *trackEnd	= trackStart + sectorSize[track];
		//offset = trackStart;

		for ( unsigned int sec = 0; sec<d64NumOfSectors[track]; sec++) {
			offset = getSectorHeaderOffset( track, sec, trackStart, trackEnd);

			if ( NULL == offset ) {
//				Log::write(_T("Error dumping gcr data (illegal pointer) at track: %i, sector: %i.\n"),
//					track, sec);
				isDiskCorrupted = true;
				fclose(d64dump);
				return;
			}

			int header = 0;
			// seek data header
			while (*offset != 0xFF) {
				offset++;
				if (offset == trackEnd)
					offset = trackStart;
				header++;
				if (header >= 10000) {
//					Log::write(_T("Error dumping gcr data (no header) at track: %i, sector: %i.\n"),
//						track, sec);
					isDiskCorrupted = true;
					fclose(d64dump);
					return;
				}
			}
			// seek sector data
			bool fullyRotated = false;
			while (*offset == 0xFF && !fullyRotated) { // data mark (0x07) in GCR (0x55)
				offset++;
				if (offset == trackEnd) {
					if (!fullyRotated) {
						offset = trackStart;
						fullyRotated = true;
					} else {
	//					Log::write(_T("Error dumping gcr data (no data mark) at track: %i, sector: %i.\n"),
				//			track, sec);
						isDiskCorrupted = true;
					}
				}
			}
			// convert to 260 bytes
			gcr2sector( sector_buffer, offset, trackStart, trackEnd);
			if (sector_buffer[0] != 0x07) {
				isDiskCorrupted = true;
				//fclose(d64dump);
				//return;
//				Log::write(_T("Warning: head of buffer not data mark at track: %i, sector %i, file offset: %i.\n"),
//					track, sec, ftell(d64dump));
			}
			// write one sector
			fwrite( &(sector_buffer[1]), 256, 1, d64dump);
		}
	}
	fclose(d64dump);
}

// Move R/W head inwards (towards higher tracks)
void FdcGcr::moveHeadIn(void)
{
	unsigned long newGCRoffset;

	if (currentHalfTrack >= NrOfTracks*2) {
//		fprintf(stderr, "Drive head moved over highest track of current image.");
		if (currentHalfTrack == MAX_NUM_TRACKS*2) {
//			fprintf(sdterr, (_T("Attempt to move drive head over track 41."));
			return;
		}
		NrOfTracks = currentHalfTrack >> 1;
	}
	// Note actual position within the old track...
	newGCRoffset = (unsigned long)(gcrPtr - gcrTrackBegin) * sectorSize[(currentHalfTrack+1) >> 1]
		/ sectorSize[(currentHalfTrack) >> 1] ;

	currentHalfTrack++;

	gcrTrackBegin = gcrData + ((currentHalfTrack >> 1) - 1) * GCR_MAX_TRACK_SIZE;
	gcrTrackEnd = gcrTrackBegin + sectorSize[currentHalfTrack >> 1];

	gcrPtr = gcrTrackBegin + newGCRoffset;
}

// Move R/W head outwards (towards lower tracks)
void FdcGcr::moveHeadOut(void)
{
	unsigned long newGCRoffset;

	if (currentHalfTrack == 2)
		return;

	// Note actual position within the old track...
	newGCRoffset = (unsigned long) (gcrPtr - gcrTrackBegin) * sectorSize[(currentHalfTrack-1) >> 1]
		/ sectorSize[(currentHalfTrack) >> 1];

	currentHalfTrack--;

	gcrTrackBegin = gcrData + ((currentHalfTrack >> 1) - 1) * GCR_MAX_TRACK_SIZE;
	gcrTrackEnd = gcrTrackBegin + sectorSize[currentHalfTrack >> 1];

	gcrPtr = gcrTrackBegin + newGCRoffset;
}
