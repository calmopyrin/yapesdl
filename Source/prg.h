#pragma once

class TED;

extern bool PrgLoad(const char *fname, int loadaddress, TED *mem);
extern bool prgLoadFromT64(const char *t64path, unsigned short *loadAddress, TED *mem);
extern bool prgSaveBasicMemory(const char *prgname, TED *mem, unsigned short &beginAddr, unsigned short &endAddr, 
	bool isBasic = true);
extern bool mainSaveMemoryAsPrg(const char *prgname, unsigned short &beginAddr, unsigned short &endAddr);
extern bool mainLoadPrgToMemory(const char* prgname, unsigned short& beginAddr);