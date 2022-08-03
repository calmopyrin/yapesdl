#include <cstring>
#include "SaveState.h"

const char SSSTRING[] = "YSS0";

FILE *SaveState::ssfp = NULL;
template<> unsigned int LinkedList<SaveState>::count = 0;
template<> SaveState* LinkedList<SaveState>::root = 0;
template<> SaveState* LinkedList<SaveState>::last = 0;

SaveState::SaveState()
{
	add(this);
}

SaveState::~SaveState()
{
	remove(this);
}

void SaveState::setId(const char *id)
{
	strcpy(componentName, id);
}

SaveState *SaveState::findId(const char *id)
{
	SaveState *ss = SaveState::getRoot();
	while (ss) {
		if (!strncmp(ss->getId(), id, 4))
			return ss;
		ss = ss->getNext();
	}
	return NULL;
}

bool SaveState::openSnapshot(const char *fname, bool isWrite)
{
	const char *mode = isWrite ? "wb" : "rb";
	ssfp = fopen(fname, mode);
	if (!ssfp)
		return false;
	if (isWrite) {
		SaveState *ss = SaveState::getRoot();
		fwrite(SSSTRING, 4, 1, ssfp);
		while (ss) {
			fwrite(ss->getId(), 4, 1, ssfp);
			ss->dumpState();
			ss = ss->getNext();
		}
	} else {
		char hdr[5];
		fread(hdr, 4, 1, ssfp);
		// check header signature
		if (strncmp(SSSTRING, hdr, 4)) {
			closeSnapshot();
			return false;
		}
		do {
			char id[8];
			// read chunk header & find it
			// keep reading if not found (should be 4-byte aligned)
			fread(id, 4, 1, ssfp);
			SaveState *ss = findId(id);
			if (ss) {
				fprintf(stderr, "Snapshot chunk %.*s found.\n", 4, id);
				ss->readState();
			}
		} while (!feof(ssfp));
	}
	closeSnapshot();
	return true;
}

void SaveState::closeSnapshot()
{
	fclose(ssfp);
}

void SaveState::saveVar(void *p, size_t size)
{
	if (ssfp) {
		size_t w = fwrite(p, 1, size, ssfp);
		// padding to 4-byte boundary so that chunk ID's always found
		w = ((w + 3) & ~3);
		// dummy output
		while (size++ < w) {
			fputc('\0', ssfp);
		}
	}
}

size_t SaveState::readVar(void *p, size_t size)
{
	if (ssfp) {
		size_t r = fread(p, 1, size, ssfp);
		// padding to 4-byte boundary so that chunk ID's always found
		r = ((r + 3) & ~3);
		// dummy input
		while (size++ < r) {
			unsigned char in = fgetc(ssfp);
		}
		return size;
	}
	return 0;
}
