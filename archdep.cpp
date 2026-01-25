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
#include <cstdio>

#ifdef _WIN32
typedef struct IUnknown IUnknown;
#include <windows.h>

// fixes a missing export in SDL2 for VS 2015
#if _MSC_VER >= 1900
FILE _iob[] = { *stdin, *stdout, *stderr };

extern "C" FILE * __cdecl __iob_func(void)
{
	return _iob;
}
#endif
#endif

// forward declarations
static void flipMaxFps(void *v);

unsigned int	tick_50hz, tick_vsync, fps;
static unsigned int framesShown = 50;
static const unsigned int maxFpsValues[] = { 100, 60, 50, 25, 10 };
static unsigned int maxFps = 100;
static unsigned int maxFpsIndex = 0;
static unsigned int targetFps;
static unsigned int fpsInterval;

rvar_t archDepSettings[] = {
	{ "Maximum framerate", "MaxFrameRate", flipMaxFps, &maxFps, RVAR_INT, NULL },
	{ "", "", NULL, NULL, RVAR_NULL, NULL }
};

static void flipMaxFps(void *v)
{
	maxFpsIndex = (maxFpsIndex + 1) % (sizeof(maxFpsValues) / sizeof(maxFpsValues[0]));
	maxFps = maxFpsValues[maxFpsIndex];
}

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
static Uint64 timeelapsed;

void ad_vsync_init(unsigned int targetfps)
{
	timeelapsed = SDL_GetTicks64();
	targetFps = targetfps;
	fpsInterval = 1000 / targetFps;
	if (targetfps > maxFps) {
		maxFpsIndex = 0;
		maxFps = maxFpsValues[maxFpsIndex];
	}
}

bool ad_vsync(bool sync)
{
	Uint64 time_limit = timeelapsed + fpsInterval;
	static Uint64 nextFrameTime = timeelapsed + (1000 / maxFps);

	timeelapsed = SDL_GetTicks64();
	if (sync) {
		if (time_limit > timeelapsed) {
			Uint32 nr10ms = Uint32((time_limit - timeelapsed) / 10) * 10;
			SDL_Delay(nr10ms);
			timeelapsed = SDL_GetTicks64();
			while (time_limit > timeelapsed) {
				SDL_Delay(0);
				timeelapsed = SDL_GetTicks64();
			}
		}
	}
	if (nextFrameTime > timeelapsed) {
		return false;
	} else {
		nextFrameTime = timeelapsed + ((1000 + maxFps / 2) / maxFps);
		framesShown++;
		return true;
	}
}

unsigned int ad_get_fps(unsigned int &framesDrawn)
{
	static unsigned int speed = targetFps * 2;
	static Uint64 g_TotElapsed = SDL_GetTicks64();
	static unsigned int g_TotFrames = 0;
	const unsigned int measureIntervalMsec = 2000 - (targetFps - 50) * 40;

	if (g_TotElapsed + measureIntervalMsec <= timeelapsed) {
		g_TotElapsed = SDL_GetTicks64();
		speed = g_TotFrames;
		/*printf("TotFrames: %u TotElapsed:%llu FpsInterval:%u TargetFPS:%u MeasureInterval:%u \n", g_TotFrames, g_TotElapsed, fpsInterval, 
			targetFps, measureIntervalMsec);*/
		g_TotFrames = 0;
		framesDrawn = framesShown * targetFps / 100;
		framesShown = 0;
	} else
		g_TotFrames++;
	return speed;
}

int ad_change_target_speed(int change)
{
	fpsInterval += change;
	if (fpsInterval <= 0) fpsInterval = 1;
	else if (fpsInterval > 2000) fpsInterval = 2000;
	return 2000 / fpsInterval;
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
		fps = (int) (targetFps * ((double) tick_vsync / (double) tick_50hz));
		tick_vsync = 0;
		tick_50hz = 0;
	}
}

void ad_vsync_init(unsigned int targetfps)
{
	struct sigaction ac;

	// Initialization
	sem_vsync = 0x01;
	sem_fps = 0x01;
	tick_vsync = 0;
	tick_50hz = 0;
	fps = targetfps;
	targetFps = targetfps;

	ac.sa_handler = _ad_vsync_sigalrm_handler;
	sigemptyset(&ac.sa_mask);
	ac.sa_flags = 0;

	if (sigaction(SIGALRM, &ac, NULL) == 0) ualarm(100, 1000000 / targetfps);
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

unsigned int ad_get_fps(unsigned int &framesDrawn)
{
	framesDrawn = fps;
	return fps << 1;
}

int ad_change_target_speed(int change)
{
    if (int(targetFps) - change > 0)
        targetFps -= change;
    return targetFps * 2;
}

#endif

#if defined(__EMSCRIPTEN__) || defined(ZIP_SUPPORT)
#pragma comment(lib, "zlibstat.lib")
#include "zlib/unzip.h"

bool zipOpen(const char *zipName, unsigned char **buffer, unsigned int &bufferSize, unsigned int &fileType)
{
	static unsigned int storedFileSize = 0;
	unzFile zipFile = unzOpen(zipName);
	fileType = 0;

	if (zipFile) {
		if (unzGoToFirstFile(zipFile) == UNZ_OK) {
			unz_file_info zipfileinfo;
			char zippedName[512];
			if (unzOpenCurrentFile(zipFile) == UNZ_OK) {
				int result = unzGetCurrentFileInfo(zipFile, &zipfileinfo, zippedName, 512, NULL, 0, NULL, 0);
				fprintf(stderr, "First file in ZIP: %s\n", zippedName);
				unsigned int fsize = zipfileinfo.uncompressed_size;
				char *ext = strrchr(zippedName, '.');
				if (ext) {
					if (fsize > storedFileSize) {
						if (*buffer) {
							delete[] *buffer;
							*buffer = NULL;
						}
						*buffer = new unsigned char[fsize];
						storedFileSize = fsize;
					}
					if (*buffer) {
						if (!strcmp(ext, ".tap") || !strcmp(ext, ".TAP"))
							fileType = 2;
						else
							fileType = 1;
						bufferSize = fsize;
						unzReadCurrentFile(zipFile, *buffer, fsize);
						unzCloseCurrentFile(zipFile);
						unzClose(zipFile);
					}
					return true;
				}
			}
		}
	}
	return false;
}
#else
bool zipOpen(const char *zipName, unsigned char **buffer, unsigned int &bufferSize, unsigned int &fileType)
{
	return false;
}
#endif

void unzipFiles(const char* zipFilename, const char* OutDir)
{
#ifdef __EMSCRIPTEN__
	unzFile zipfile = unzOpen(zipFilename);
	if (zipfile == NULL) {
		fprintf(stderr, "Error opening ZIP file %s\n", zipFilename);
		return;
	}

	// Get the total number of files in the ZIP archive
	unz_global_info global_info;
	if (unzGetGlobalInfo(zipfile, &global_info) != UNZ_OK) {
		fprintf(stderr, "Error getting global info for %s\n", zipFilename);
		unzClose(zipfile);
		return;
	}

	// Loop through each file in the ZIP archive
	for (unsigned int i = 0; i < global_info.number_entry; i++) {
		// Get file info
		unz_file_info file_info;
		char filename_in_zip[256];
		if (unzGetCurrentFileInfo(zipfile, &file_info, filename_in_zip, sizeof(filename_in_zip), NULL, 0, NULL, 0) != UNZ_OK) {
			fprintf(stderr, "Error getting file info for entry %d\n", i);
			unzClose(zipfile);
			return;
		}

		// Construct the full output path for the file
		char output_path[512];
		snprintf(output_path, sizeof(output_path), "%s/%s", OutDir, filename_in_zip);

		// Open the file for extraction
		if (unzOpenCurrentFile(zipfile) != UNZ_OK) {
			fprintf(stderr, "Error opening file %s\n", filename_in_zip);
			unzClose(zipfile);
			return;
		}

		// Create the directory if necessary
		FILE* out_file = fopen(output_path, "wb");
		if (out_file == NULL) {
			fprintf(stderr, "Error creating file %s\n", output_path);
			unzClose(zipfile);
			return;
		}

		// Allocate buffer for reading the file content
		unsigned char buffer[4096];
		int bytes_read = 0;

		// Extract the file content
		while ((bytes_read = unzReadCurrentFile(zipfile, buffer, sizeof(buffer))) > 0) {
			fwrite(buffer, 1, bytes_read, out_file);
		}

		if (bytes_read < 0) {
			fprintf(stderr, "Error reading file %s\n", filename_in_zip);
		}

		fclose(out_file);
		unzCloseCurrentFile(zipfile);

		// Move to the next file in the ZIP archive
		if (i < global_info.number_entry - 1) {
			if (unzGoToNextFile(zipfile) != UNZ_OK) {
				fprintf(stderr, "Error moving to next file in ZIP archive\n");
				unzClose(zipfile);
				return;
			}
		}
	}
	// Close the ZIP file
	unzClose(zipfile);
#endif
}
