/*
YAPE - Yet Another Plus/4 Emulator

The program emulates the Commodore 264 family of 8 bit microcomputers

This program is free software, you are welcome to distribute it,
and/or modify it under certain conditions. For more information,
read 'Copying'.

(c) 2000, 2001, 2005, 2007 Attila Gr�z
(c) 2005 VENESZ Roland
*/

#include "archdep.h"
#include <stdio.h>

#ifdef _WIN32
// fixes a missing export in SDL2 for VS 2015
#if _MSC_VER >= 1900
FILE _iob[] = { *stdin, *stdout, *stderr };

extern "C" FILE * __cdecl __iob_func(void)
{
	return _iob;
}
#endif
#endif

unsigned int	tick_50hz, tick_vsync, fps;

/* functions for Windows */
#if defined(_WIN32)

static HANDLE				handle;
static WIN32_FIND_DATA			rec;
static char temp[512];
static BOOL showDriveLetters = FALSE;
static DWORD driveBitFields;
static UINT currentDriveIndex;
static char currentDrive[MAX_PATH];

/* file management */
void ad_exit_drive_selector()
{
	showDriveLetters = FALSE;
}

static int ad_get_next_drive()
{
	do {
		driveBitFields >>= 1;
		currentDriveIndex++;
	} while (!(driveBitFields & 1) && driveBitFields != 0);
	if (driveBitFields) {
		sprintf(currentDrive, "%c:/", 'A' + currentDriveIndex);
		return 1;
	} else {
		return 0;
	}
}

static void ad_get_first_drive()
{
	currentDriveIndex = 0;
	driveBitFields = GetLogicalDrives();
	while (!(driveBitFields & 1) && driveBitFields != 0) {
		driveBitFields >>= 1;
		currentDriveIndex++;
	}
	sprintf(currentDrive, "%c:/", 'A' + currentDriveIndex);
}

int ad_get_curr_dir(char *pathstring)
{
	return GetCurrentDirectory(MAX_PATH, pathstring);
}

int ad_set_curr_dir(char *path)
{
	char origPath[MAX_PATH];
	char cp[MAX_PATH];

	GetCurrentDirectory(MAX_PATH, origPath);
	BOOL retval = SetCurrentDirectory(path);
	if (retval) {
		if (!strcmp(path, "..")) {
			GetCurrentDirectory(MAX_PATH, cp);
			// root reached?
			if (!strcmp(cp, origPath)) {
				showDriveLetters = TRUE;
			} else {
				showDriveLetters = FALSE;
			}
		}
	}
	return retval;
}

int ad_find_first_file(const char *filefilter)
{
	if (showDriveLetters) {
		ad_get_first_drive();
		return 1;
	} else {
		handle = FindFirstFile( filefilter, &rec);
		if (handle != INVALID_HANDLE_VALUE) return 1;
		return 0;
	}
}

char *ad_return_current_filename(void)
{
	if (showDriveLetters) {
		return currentDrive;
	} else {
		return (char *) rec.cFileName;
	}
}

UI_FileTypes ad_return_current_filetype(void)
{
	if (showDriveLetters)
		return FT_VOLUME;
	else if (rec.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
		return FT_DIR;
	else
		return FT_FILE;
}

unsigned int ad_get_current_filesize(void)
{
	return (unsigned int) rec.nFileSizeLow;
}

int ad_find_next_file(void)
{
	if (showDriveLetters) {
		return ad_get_next_drive();
	} else {
		return FindNextFile( handle, &rec);
	}
}

int	ad_find_file_close(void)
{
	if (showDriveLetters)
		return 1;
	return FindClose(handle);
}

int ad_makedirs(char *path)
{
	strcpy(temp,path);
	strcat(temp, "/yape");
	CreateDirectory(temp, NULL);

	return 1;
}
#endif /* end of Windows functions */

#if defined(_WIN32) || defined(__EMSCRIPTEN__)
static unsigned int timeelapsed;

void ad_vsync_init(void)
{
	timeelapsed = SDL_GetTicks();
}

bool ad_vsync(bool sync)
{
	unsigned int time_limit = timeelapsed + 20;
	timeelapsed = SDL_GetTicks();
	if (sync) {
		if (time_limit > timeelapsed) {
			int nr10ms = ((time_limit-timeelapsed)/10) * 10;
			SDL_Delay(nr10ms);
			timeelapsed = SDL_GetTicks();
			while (time_limit>timeelapsed) {
				SDL_Delay(0);
				timeelapsed = SDL_GetTicks();
			}
		}
		return true;
	} else {
		static unsigned int prevFrameTime = timeelapsed;
		if (prevFrameTime + 10 > timeelapsed) {
			return false;
		} else {
			prevFrameTime = timeelapsed;
			return true;
		}
	}
}

unsigned int ad_get_fps()
{
	static unsigned int fps = 50;
	static unsigned int g_TotElapsed = SDL_GetTicks();
	static unsigned int g_TotFrames = 0;

	g_TotFrames++;
	if (g_TotElapsed + 2000 < timeelapsed) {
		g_TotElapsed = SDL_GetTicks();
		fps = g_TotFrames / 2;
		g_TotFrames = 0;
	}
	return fps;
}
#endif

#if !defined(_WIN32)

/* ---------- UNIX ---------- */

#include <stdio.h>
#include <unistd.h>
#include <glob.h>
#include <dirent.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>

#define INVALID_HANDLE_VALUE 0x00

glob_t		gl;
unsigned int	gl_curr;
unsigned int	gl_currsize;
char		temp[512];
char		sem_vsync, sem_fps;

void ad_exit_drive_selector()
{
	//
}

int ad_get_curr_dir(char *pathstring, size_t size)
{
	char *p = getcwd(pathstring, size);
	return 1;
}

int ad_get_curr_dir(char *pathstring)
{
	size_t size = 512;
	getcwd(pathstring, size);
	return !!pathstring;
}

int ad_set_curr_dir(char *path)
{
	if (chdir(path) == 0) return 1;
	return 0;
}

int ad_find_first_file(const char *filefilter)
{
	// We will search in the current working directory.
	if (glob(filefilter, 0, NULL, &gl) != 0) return 0;
	gl_curr = 0; gl_currsize = 0;
	return 1;
}

char *ad_return_current_filename(void)
{
	//	fprintf(stderr, "File: %s parsed\n", gl.gl_pathv[gl_curr]);
	if (!gl.gl_pathc)
		return 0;
	return (char *) gl.gl_pathv[gl_curr];
}

UI_FileTypes ad_return_current_filetype(void)
{
	// Probably too kludgy but I dunno Unix so who cares...
	DIR *dirp;
	if (gl.gl_pathv && (dirp = opendir(gl.gl_pathv[gl_curr])) != NULL) {
		closedir(dirp);
		return FT_DIR;
	} else
		return FT_FILE;
}

unsigned int ad_get_current_filesize(void)
{
	struct stat buf;

	// Size cache!
	if (gl_currsize != 0) return gl_currsize;

	// If the file size actually is 0, subsequent calls to this
	// function will stat the file over and over again. I don't care.
	if (stat(gl.gl_pathv[gl_curr], &buf) == 0) {
		return (gl_currsize = (unsigned int) buf.st_size);
	} else return 0;
}

int ad_find_next_file(void)
{
	if (++gl_curr >= gl.gl_pathc) {
		// No more matches: we don't need the glob any more.
		//globfree(&gl);
		return 0;
	}
	gl_currsize = 0;
	return 1;
}

int	ad_find_file_close(void)
{
	return 1;
}

int ad_makedirs(char *path)
{
	// Possible buffer overflow fixed.
	strncpy(temp, path, 512);
	if (strlen(temp) > 506) return 0;
	strcat(temp, "/yape");
	mkdir(temp, 0777);

	return 1;
}
#endif

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
void _ad_vsync_sigalrm_handler(int signal)
{
	sem_vsync = 0x01;

	// Refresh FPS every 3 seconds. To avoid race condition, do
	// it in the next call if tick_vsync is locked.
	if (++tick_50hz >= 3*20 && sem_fps != 0x00) {
		fps = (int) (50 * ((double) tick_vsync / (double) tick_50hz));
		tick_vsync = 0;
		tick_50hz = 0;
	}
}

void ad_vsync_init(void)
{
	struct sigaction ac;

	// Initialization
	sem_vsync = 0x01;
	sem_fps = 0x01;
	tick_vsync = 0;
	tick_50hz = 0;
	fps = 50;

	ac.sa_handler = _ad_vsync_sigalrm_handler;
	sigemptyset(&ac.sa_mask);
	ac.sa_flags = 0;

	if (sigaction(SIGALRM, &ac, NULL) == 0) ualarm(100, 20000);
}

bool ad_vsync(bool sync)
{
	if (sync) {
		while (sem_vsync == 0x00) usleep(VSYNC_LATENCY);
		sem_vsync = 0x00;
	}
	sem_fps = 0x00;
	tick_vsync++;
	sem_fps = 0x01;
	return true;
}

unsigned int ad_get_fps()
{
	return fps;
}

#endif
