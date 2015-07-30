/*
	YAPE - Yet Another Plus/4 Emulator

	The program emulates the Commodore 264 family of 8 bit microcomputers

	This program is free software, you are welcome to distribute it,
	and/or modify it under certain conditions. For more information,
	read 'Copying'.

	(c) 2000, 2001, 2005 Attila Gr�z
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
	FT_VOLUME
};

#ifdef _WIN32
#include <SDL/SDL.h>
#define _WIN32_WINNT 0x400
#include <windows.h>
#ifdef _MSC_VER
#pragma comment(lib,"winmm.lib")
#endif
#else
#include <SDL2/SDL.h>
#include <unistd.h>
#define MAX_PATH 256
#define VSYNC_LATENCY 200
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

extern void				ad_vsync_init(void);
extern bool				ad_vsync(bool sync);
extern unsigned int		ad_get_fps();

#endif
