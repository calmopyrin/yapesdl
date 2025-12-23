/*
	YAPE - Yet Another Plus/4 Emulator

	The program emulates the Commodore 264 family of 8 bit microcomputers

	This program is free software, you are welcome to distribute it,
	and/or modify it under certain conditions. For more information,
	read 'Copying'.

	(c) 2000, 2001, 2005, 2016 Attila Gr�z
	(c) 2005 VENESZ Roland
*/

/*
This file contains the architecture dependent function prototypes
*/

#ifndef _ARCHDEP_H
#define _ARCHDEP_H

enum UI_FileTypes {
	FT_DIR,
	FT_FILE,
	FT_VOLUME,
	FT_NONE
};

#include "types.h"

#if !defined(_WIN32) || defined(__EMSCRIPTEN__)
#define UNIX
#include <unistd.h>
#define MAX_PATH 256
#define VSYNC_LATENCY 200
#define PATH_SEP "/"
#else
#include <windows.h>
#define PATH_SEP "\\"
#endif

int		ad_set_curr_dir(char *path);
int		ad_find_first_file(const char *filefilter);
char		*ad_return_current_filename(void);
UI_FileTypes ad_return_current_filetype(void);
unsigned int	ad_get_current_filesize(void);
int		ad_find_next_file(void);
int		ad_find_file_close(void);
int		ad_get_curr_dir(char *pathstring);
int		ad_makedirs(char *temp);
void	ad_exit_drive_selector();

extern void				ad_vsync_init(unsigned int targetfps);
extern bool				ad_vsync(bool sync);
extern unsigned int		ad_get_fps(unsigned int& framesDrawn);
int						ad_change_target_speed(int change);

extern bool zipOpen(const char *zipName, unsigned char **buffer, unsigned int &bufferSize, unsigned int &fileType);
extern void unzipFiles(const char* zipFilename, const char* OutDir);

class Sync {
public:
	Sync();
	virtual ~Sync();
	virtual bool sync(bool sync) = 0;
	virtual unsigned int getFps() = 0;
};

class FileIO {
public:
	FileIO();
	virtual ~FileIO();
	virtual int	setCurrDir(char *path) = 0;
	virtual int	findFirstFile(const char *filefilter) = 0;
	virtual int findNextFile() = 0;
	virtual char *getCurrentFilename() = 0;
	virtual UI_FileTypes getCurrentFiletype() = 0;
	virtual unsigned int getCurrentFilesize() = 0;
};

extern rvar_t archDepSettings[];

#endif
