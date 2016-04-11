#include "drive.h"
#include "device.h"
#include "diskfs.h"
#include "iec.h"
#include "tcbm.h"
#include "FdcGcr.h"
#include "1541rom.h"
#include "1541mem.h"

extern void machineDoSomeFrames(unsigned int frames);

FakeDrive::FakeDrive() : Drive(8)
{
	CFakeTCBM *tcbm_l = new CFakeTCBM();
	CFakeIEC *iec_l = new CFakeIEC(8);
	CIECFSDrive *fsdrive = new CIECFSDrive(".");

	iec = iec_l;
	tcbm = tcbm_l;
	tcbm_l->AddIECInterface((CIECInterface*)iec);
	iec_l->AddIECDevice((CIECDevice*)fsdrive);
	
	FakeDrive::Reset();
}

void FakeDrive::Reset()
{
	tcbm->Reset();
	iec->Reset();
	device->Reset();	
}

void FakeDrive::AttachDisk(const char *fname)
{
	
}

void FakeDrive::DetachDisk()
{
	
}

CTrueDrive *CTrueDrive::Drives[4];
unsigned int CTrueDrive::NrOfDrivesAttached;
CTrueDrive *CTrueDrive::RootDevice = 0;
CTrueDrive *CTrueDrive::LastDevice = 0;

CTrueDrive::CTrueDrive(unsigned int type, unsigned int dn)
			: Drive(dn) , Clockable(dn)
{
	Fdc = NULL;
	FdcGCR = NULL;
	Mem = NULL;
	Mem1541 = NULL;
//	Mem1551 = NULL;
//	Mem1581 = NULL;
	Cpu = NULL;
	patchedRom = 0;
	ClockRate = ClockCount = 0;
	ram = new unsigned char[0x4000];
	ChangeDriveType(type);
	//
	NrOfDrivesAttached++;
	NextDevice = 0;
	PrevDevice = 0;
	if (RootDevice == 0) {
	    for (int i=0; i<4; i++)
	    	Drives[i] = 0;
	    RootDevice = this;
	} else {
	    PrevDevice = LastDevice;
	    LastDevice->NextDevice = this;
	}
	LastDevice = this;
	//
	Drives[dn&7] = this;
}

void CTrueDrive::AttachDisk(const char *fname)
{
	if (FdcGCR) {
		FdcGCR->openDiskImage(fname);
	}
}

void CTrueDrive::SwapDisk(const char *fname)
{
	CTrueDrive *d = CTrueDrive::GetRoot();
	if (d) {
		d->DetachDisk();
		// important to note disk change
		machineDoSomeFrames(2);
		d->AttachDisk(fname);
	}
}

void CTrueDrive::DetachDisk()
{
	if (FdcGCR) {
		FdcGCR->closeDiskImage();
	}
}

bool CTrueDrive::hasCustomRom(char *path)
{
	FILE *fp = fopen(path, "rb");
	if (fp) {
		fclose(fp);
		return true;
	}
	return false;
}

int CTrueDrive::loadCustomRom(char *path, unsigned char *buffer, size_t size)
{
	FILE *fp = fopen(path, "rb");
	if (fp) {
		size_t j = 0;
		while (!feof(fp) && j < size) {
			buffer[j++] = fgetc(fp);
		}
		fclose(fp);
		return 0;
	}
	return -1;
}

int CTrueDrive::loadCustomRom(char *path)
{
	int retval;
	FILE *fp = fopen(path, "rb");
	if (fp) {
		size_t j = 0;
		while (!feof(fp) && j < romSize) {
			patchedRom[j++] = fgetc(fp);
		}
		fclose(fp);
		rom = patchedRom;
		retval = 0;
	} else {
		rom = originalRom;
		retval = -1;
	}
	getDriveMemHandler()->setNewRom(rom);
	Reset();
	return retval;
}

void CTrueDrive::ChangeDriveType(unsigned int type)
{
	if (Cpu)
		delete Cpu;
	if (patchedRom) {
		delete patchedRom;
		patchedRom = 0;
	}
	if (!type)
        return;
    if (!FdcGCR) {
        FdcGCR = new FdcGcr();
        FdcGCR->reset();
    }
    Fdc = FdcGCR;
    romSize = 0x4000;
    patchedRom = new unsigned char[romSize];
    originalRom = const_cast<unsigned char*>(rom1541);
//    if (!loadCustomRom(setting.customDriveRom[DevNr & 3], patchedRom, romSize))
//    {
//        Rom = patchedRom;
//    } else
        rom = originalRom;
    Mem1541 = new DRIVEMEM(FdcGCR, ram, rom, devNr);
    Mem = static_cast<DRVMEM*>(Mem1541);
    Cpu = new DRIVECPU( Mem1541, &(Mem->irqFlag), ram+0x100,
        FdcGCR->getByteReadyEdge(), Mem1541->get_via2pcr(), devNr);
    FdcGCR->setSpinFactor(2);
    ClockRate = 10000000; //DRIVE_CLK_1541; DRIVE_CLK_1541; // 10027928 DRIVE_CLK_1541 10000000

	Mem->Reset();
	Cpu->Reset();
	Fdc->reset();
	driveType = type;
}

CTrueDrive::~CTrueDrive()
{
	unsigned int Slot = devNr & 7;

	if (ram)
		delete [] ram;
	if (Mem1541)
		delete Mem1541;
	if (FdcGCR)
		delete FdcGCR;
	Fdc = NULL;
	if (Cpu)
		delete Cpu;
	if (patchedRom)
		delete patchedRom;
	//
	//
	if (!--NrOfDrivesAttached) {
	    RootDevice = 0;
	    LastDevice = 0;
	} else {
		if (PrevDevice) {
			PrevDevice->NextDevice = NextDevice;
		} else {
			RootDevice = NextDevice;
		}
		if (NextDevice) {
			NextDevice->PrevDevice = PrevDevice;
		} else {
			LastDevice = PrevDevice;
		}
	}
	Drives[Slot] = NULL;
}

void CTrueDrive::Reset()
{
	Fdc->reset();
	Mem->Reset();
	Cpu->Reset();
}

void CTrueDrive::ResetAllDrives()
{
	for (int i=0; i<4; i++) {
		if ( CTrueDrive::Drives[i] )
			CTrueDrive::Drives[i]->Reset();
	}
}

//---------------------------------------

FakeSerialDrive::~FakeSerialDrive() 
{
	delete iecDrive;
	delete iecInterFace;
}

FakeSerialDrive::FakeSerialDrive(unsigned int dn) : Drive(dn) 
{
	//devNr = dn;
	iecDrive = new CIECFSDrive(".");
	iecInterFace = new IecFakeSerial(dn, iecDrive);
}
