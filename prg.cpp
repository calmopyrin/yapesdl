#include <stdio.h>
#include <string.h>
#include "tedmem.h"
#include "types.h"

static FILE	*prg;
static unsigned char lpBufPtr[0x10000];

static void prgLoadFromBuffer(unsigned short &adr, unsigned int size, unsigned char *buf, TED *mem )
{
    unsigned int endaddr = adr + size;

    for (unsigned int i = 0; i < size; i++)
        mem->poke(adr + i, buf[i]);

    mem->Write(0x2D,(endaddr)&0xFF);
    mem->Write(0x2E,(endaddr)>>8);
    mem->Write(0x2F,(endaddr)&0xFF);
    mem->Write(0x30,(endaddr)>>8);
    mem->Write(0x31,(endaddr)&0xFF);
    mem->Write(0x32,(endaddr)>>8);
	unsigned short loadEndAddPtr = mem->getEndLoadAddressPtr();
    mem->Write(loadEndAddPtr,(endaddr)&0xFF);
    mem->Write(loadEndAddPtr + 1,(endaddr)>>8);
}

bool PrgLoad(const char *fname, int loadaddress, TED *mem)
{
	unsigned short	loadaddr;
	unsigned int	fsize;
	char			*fext;
	int				p00offset = 0;

	if ((prg = fopen(fname, "rb"))== NULL) {
    	return false;
	} else {
		fext = (char *) strrchr( fname, '.');
		if (!strcmp( fext, ".p00") || !strcmp(fext, ".P00"))
			p00offset = 26;
		// load PRG file
		fseek(prg, 0L, SEEK_END);
		fsize = ftell(prg) - p00offset;
		fsize &= 0xFFFF;
		fseek(prg, p00offset, SEEK_SET);
		fread(lpBufPtr, 1, fsize, prg);
		fclose(prg);
		// copy to memory
		if (loadaddress&0x10000)
			loadaddr = loadaddress&0xFFFF;
		else
			loadaddr = lpBufPtr[0]|(lpBufPtr[1] << 8);

		if (fsize < 2)
			return false;
        fsize -= 2;
        prgLoadFromBuffer(loadaddr, fsize, lpBufPtr + 2, mem);
	}
	fprintf( stderr, "Loaded: %s at $%04X-$%04X\n", fname, loadaddr, loadaddr + fsize);
	return true;
}

bool prgLoadFromT64(const char *t64path, unsigned short *loadAddress, TED *mem)
{
    bool rv = false;

	if ((prg = fopen(t64path, "rb"))) {
	    unsigned char dirEntry[32];
	    fseek(prg, 0x40, SEEK_SET);
        if (fread(dirEntry, 1, 32, prg) == 32) {
            // PRG type?
            if (dirEntry[1]) {
                unsigned short adr = dirEntry[2] | (dirEntry[3] << 8);
                unsigned short endAddr =  dirEntry[4] | (dirEntry[5] << 8);
                int fsize = int(endAddr - adr);
                if (fsize > 0) {
                    unsigned int offsetInFile = dirEntry[8] | (dirEntry[9] << 8) | (dirEntry[10] << 16) | (dirEntry[11] << 24);
                    fseek(prg, offsetInFile, SEEK_SET);
                    if (fread(lpBufPtr, 1, fsize, prg)) {
                        prgLoadFromBuffer(adr, fsize, lpBufPtr, mem);
                        fprintf(stderr, "First PRG loaded from '%s' to $%04X-$%04X\n", t64path, adr, endAddr);
                        rv = true;
                    }
                }
            }
        }
	    fclose(prg);
	}
    return rv;
}

bool prgSaveBasicMemory(const char *prgname, TED *mem, unsigned short &beginAddr, unsigned short &endAddr, bool isBasic)
{
    if ((prg = fopen(prgname, "wb"))) {
		if (isBasic || beginAddr >= endAddr) {
			beginAddr = (mem->Read(0x2C) << 8) | mem->Read(0x2B);
			endAddr = (mem->Read(0x2E) << 8) | mem->Read(0x2F);
		}
		fputc(beginAddr & 0xFF, prg);
		fputc(beginAddr >> 8, prg);
        for(unsigned int i = beginAddr; i <= endAddr; i++)
            fputc(mem->Ram[i], prg);
        fclose(prg);
        fprintf(stderr, "Memory $%04X-$%04X saved to %s\n", beginAddr, endAddr, prgname);
    }
    return false;
}
