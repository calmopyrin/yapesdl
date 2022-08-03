#pragma once

#include <cstdio>
#include "types.h"

class SaveState :
	public LinkedList<SaveState>
{
public:
	SaveState();
	~SaveState();
	static bool openSnapshot(const char *fname, bool isWrite);
	void setId(const char *id);
	char *getId() { return componentName; }
	static SaveState *findId(const char *id);
protected:
	virtual void dumpState() = 0;
	virtual void readState() = 0;
	static void saveVar(void *p, size_t size);
	static size_t readVar(void *p, size_t size);
private:
	static void closeSnapshot();
	static FILE *ssfp;
	char componentName[8];
};
