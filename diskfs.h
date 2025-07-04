#ifndef _DISKFS_H
#define _DISKFS_H

#include <stdio.h>
#include "archdep.h"
#include "device.h"

#define NAMEBUF_LENGTH MAX_PATH

class CIECFSDrive : public CIECDrive {
public:
	CIECFSDrive(const char *path);
	virtual ~CIECFSDrive();
	virtual unsigned char Open(int channel);
	virtual unsigned char Open(int channel, char *nameBuf);
	virtual unsigned char Close(int channel);
	virtual unsigned char Read(int channel, unsigned char *data);
	virtual void setEoI(unsigned int channel);
	virtual unsigned char Write(int channel, unsigned char data, unsigned int cmd, bool eoi);
	virtual void Reset();

private:
	virtual unsigned char OpenFile(int channel, char *filename);
	virtual unsigned char OpenDirectory(int channel, char *filename);
	virtual void ParseFileName(char *srcname, char *destname, int *filemode, int *filetype, bool *wildflag);
	virtual void ExecuteCommand(char *command);
	bool findFirstFile(char *name);
	bool ChangeDir(char *dirpath);
	void ChangeDirCmd(char *dirpath);
	char dir_path[MAX_PATH];
	char orig_dir_path[MAX_PATH];
	char dir_title[16];
	FILE *file[16];

	char cmd_buffer[256]; // in reality: 58
	int cmd_len;

	unsigned char read_char[16];
};

#endif // _DISKFS_H
