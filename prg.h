#pragma once

class MemoryHandler;

extern bool PrgLoad(char *fname, int loadaddress, MemoryHandler *mem);
extern bool prgLoadFromT64(char *t64path, unsigned short *loadAddress, MemoryHandler *mem);
