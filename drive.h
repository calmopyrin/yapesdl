#ifndef _DRIVE_H
#define _DRIVE_H

#include <cstddef>
#include "Clockable.h"

class CIECDrive;
class CTCBM;
class CIECDrive;
class CIECInterface;
class FDC;
class FdcGcr;
class DRIVEMEM;
class TED;

class Drive {
public:
	Drive(unsigned int dn) : devNr(dn) {
	};
	virtual ~Drive() {	};
	virtual void Reset() = 0;
	virtual void AttachDisk(char *fname) = 0;
	virtual void DetachDisk() = 0;
	static void ChangeEmulation();
protected:
	unsigned int devNr;
	unsigned char *ram;
	unsigned char *rom;
};

class FakeDrive : public Drive {
public:
	FakeDrive();
	virtual ~FakeDrive() {	};
	virtual void Reset();
	virtual void AttachDisk(char *fname);
	virtual void DetachDisk();
private:
	CTCBM *tcbm;
	CIECInterface *iec;
	CIECDrive	*device;
};

class CTrueDrive : public Drive
    , public Clockable
    {
public:
	CTrueDrive(unsigned int type, unsigned int dn);
	virtual ~CTrueDrive();
	static unsigned int NrOfDrivesAttached;
	//
	enum { TDE_1541, TDE_1541II, TDE_1551, TDE_1581 };
	static CTrueDrive *Drives[4];
	// Reset drive
	virtual void Reset();
	// Reset all drives
	static void ResetAllDrives();
	// Change drive type
	void ChangeDriveType(unsigned int type);
	// Obtain pointer to the drive FDC object
	FdcGcr *GetFdc() {
		return static_cast<FdcGcr*>(Fdc);
	};
	virtual void AttachDisk(char *fname);
	virtual void DetachDisk();
	static CTrueDrive *GetRoot() { return RootDevice; };
	CTrueDrive *GetNext() { return NextDevice; };
	unsigned int GetDevNr() { return devNr; };
	// Obtain index of highest available drive device number
	static unsigned int GetHighestDevNr() {
		unsigned int MaxofDrives = 0;
		CTrueDrive *Drive = CTrueDrive::GetRoot();
		while ( Drive ) {
			unsigned int dn = (Drive->GetDevNr() & 7) + 1;
			if ( dn > MaxofDrives)
				MaxofDrives = dn;
			Drive = Drive->GetNext();
		}
		return MaxofDrives;
	}
	static unsigned int GetLowestDevNr() {
		unsigned int MinofDrives = 3;
		CTrueDrive *Drive = CTrueDrive::GetRoot();
		while ( Drive ) {
			unsigned int dn = Drive->GetDevNr() & 7;
			if ( dn < MinofDrives)
				MinofDrives = dn;
			Drive = Drive->GetNext();
		}
		return MinofDrives;
	}
	unsigned int getDriveType() {
		return driveType;
	}
	int loadCustomRom(char *path);
	static bool hasCustomRom(char *path);

protected:
	//
    static CTrueDrive *RootDevice;
    static CTrueDrive *LastDevice;
	CTrueDrive *PrevDevice;
    CTrueDrive *NextDevice;
	//
	FdcGcr *FdcGCR, *Fdc;
	DRIVEMEM *Mem1541;
	//TCBMMEM *Mem1551;
	TED *Ted;
	unsigned char *originalRom;
	unsigned char *patchedRom;
	size_t romSize;
	static int loadCustomRom(char *path, unsigned char *buffer, size_t size);

private:
	unsigned int driveType;
};
//-------------

#endif // _DRIVE_H
