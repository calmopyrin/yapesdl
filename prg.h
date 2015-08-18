#pragma once

class TED;

extern bool PrgLoad(char *fname, int loadaddress, TED *mem);
extern bool prgLoadFromT64(char *t64path, unsigned short *loadAddress, TED *mem);
