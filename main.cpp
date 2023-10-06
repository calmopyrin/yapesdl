/*
	YAPE - Yet Another Plus/4 Emulator

	The program emulates the Commodore 264 family of 8 bit microcomputers

	This program is free software, you are welcome to distribute it,
	and/or modify it under certain conditions. For more information,
	read 'Copying'.

	(c) 2000, 2001, 2004, 2005, 2007, 2015-2021 Attila Grósz
	(c) 2005 VENESZ Roland
*/

#define NAME    "Yape/SDL 0.80.1"
#define WINDOWX SCREENX
#define WINDOWY SCREENY

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "keyboard.h"
#include "cpu.h"
#include "tedmem.h"
#include "tape.h"
#include "sound.h"
#include "Sid.h"
#include "archdep.h"
#include "iec.h"
#include "device.h"
#include "tcbm.h"
#include "diskfs.h"
#include "monitor.h"
#include "prg.h"
#include "interface.h"
#include "video.h"
#include "drive.h"
#include "FdcGcr.h"
#include "icon.h"
#include "vic2mem.h"
#include "vicmem.h"
#include "SaveState.h"
#include "keyoverlay.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#define MAX_FRQ_INDEX 1
#else
#define MAX_FRQ_INDEX 5
#endif

// function prototypes
static void frameUpdate();
void setMainLoop(int looptype);
// used as GUI callbacks
static void toggleShowSpeed(void *none);
static void toggleFullThrottle(void *none);
static void toggleCrtEmulation(void *none);
static void toggleVsync(void *none);
static void flipMachineTypeFwd(void *name);
static const char *machineTypeLabel();
static void flipWindowScale(void *none);
static void poll_events(void);
static void toggleTrueDriveEmulation(void *none);

// SDL stuff
static SDL_Window* sdlWindow;
static SDL_Renderer *sdlRenderer;
static SDL_Texture *sdlTexture = NULL;

// class pointer for the user interface
static UI				*uinterface = NULL;
// timeout variables
static unsigned int		timeOutOverlayKeys = 0;
static unsigned int		timeOutMousePointer = 75;
static bool mouseBtnHeld = 0;

////////////////
// Supplementary

// Core
static TED				*ted8360 = NULL;
static CPU				*machine = NULL;
static CTCBM			*tcbm = NULL;
static CIECInterface	*iec = NULL;
static CIECDrive		*fsdrive = NULL;
static CTrueDrive		*drive1541 = NULL;
static FakeSerialDrive	*fsd1541 = NULL;

//
static char				textout[128];
static char				*inipath;
static char				*inifile;

unsigned int		g_bActive = true;
static unsigned int		g_inDebug = false;
static unsigned int		g_FrameRate = true;
static unsigned int		g_50Hz = true;
static unsigned int		g_bSaveSettings = true;
static unsigned int     g_bUseOverlay = 0;
static unsigned int		g_iWindowMultiplier = 2;
static unsigned int		g_iEmulationLevel = 0;
static unsigned int		g_bTrueDriveEmulation = 0;
static unsigned int		g_bVideoVsync = 0;
static char				lastSnapshotName[512] = "";
static rvar_t mainSettings[] = {
	{ "Show framerate", "DisplayFrameRate", toggleShowSpeed, &g_FrameRate, RVAR_TOGGLE, NULL },
	{ "Display debug info", "DisplayQuickDebugInfo", NULL, &g_inDebug, RVAR_TOGGLE, NULL },
	{ "Speed limit", "50HzTimerActive", toggleFullThrottle, &g_50Hz, RVAR_TOGGLE, NULL },
	{ "Window scale", "WindowMultiplier", flipWindowScale, &g_iWindowMultiplier, RVAR_INT, NULL },
	{ "Machine type", "EmulationLevel", flipMachineTypeFwd, &g_iEmulationLevel, RVAR_STRING_FLIPLIST, &machineTypeLabel },
	{ "CRT emulation", "CRTEmulation", toggleCrtEmulation, &g_bUseOverlay, RVAR_TOGGLE, NULL },
	{ "Video vertical sync", "VideoVsync", toggleVsync, &g_bVideoVsync, RVAR_TOGGLE, NULL },
	{ "True drive emulation", "TrueDriveEmulation", toggleTrueDriveEmulation, &g_bTrueDriveEmulation, RVAR_TOGGLE, NULL },
	{ "Save settings on exit", "SaveSettingsOnExit", NULL, &g_bSaveSettings, RVAR_TOGGLE, NULL },
	{ "", "", NULL, NULL, RVAR_NULL, NULL }
};
rvar_t *settings[] = {
	mainSettings,
	inputSettings,
	soundSettings,
	SIDsound::sidSettings,
	archDepSettings,
	TED::tedSettings,
	Vicmem::vicSettings,
	videoSettings,
	NULL
};

//-----------------------------------------------------------------------------
// Name: ShowFrameRate()
//-----------------------------------------------------------------------------
inline static void ShowFrameRate(unsigned int show)
{
	char fpstxt[16];
	static unsigned int fps = 50;
	unsigned int speed = ad_get_fps(fps);
	if (show) {
		sprintf(fpstxt, "%u%%/%ufps", speed, fps);
		unsigned int s = (unsigned int) strlen(fpstxt) << 3;
		ted8360->texttoscreen((ted8360->getCyclesPerRow() >= VIC_PIXELS_PER_ROW ? 472 : 408) - s, 34, fpstxt);
	}
}

//-----------------------------------------------------------------------------
// Name: DebugInfo()
//-----------------------------------------------------------------------------
inline static void DebugInfo()
{
	unsigned int hpos = (ted8360->getCyclesPerRow() == 456 ? 48 : 112), vpos = 10;

	sprintf(textout, "OPCODE: %02X ", machine->getcins());
	ted8360->texttoscreen(hpos, vpos, textout);
	ted8360->texttoscreen(hpos, vpos+8, "  PC  SR AC XR YR SP");
	sprintf(textout, ";%04X %02X %02X %02X %02X %02X", machine->getPC(),
		machine->getST(), machine->getAC(), machine->getX(), machine->getY(), machine->getSP());
	ted8360->texttoscreen(hpos, vpos+16, textout);
	vpos += 24;
	sprintf(textout, "TAPE: %08d ", ted8360->tap->tapeSoFar);
	CTrueDrive *d = CTrueDrive::Drives[0];
	if (d) {
		char driveText[64];
		unsigned int t, s;
		unsigned char ledState = d->getDriveMemHandler()->Read(0x1c00) & 8;
		unsigned char motorState = d->GetFdc()->getMotorState() ? 4 : 0;

		d->GetFdc()->trackSector(t, s);
		sprintf(driveText, "DRIVE:  T/S:%02u/%02u", t, s);
		strcat(textout, driveText);
		ted8360->texttoscreen(hpos, vpos, textout);
		ted8360->showled(hpos+21*8, vpos, motorState);
		ted8360->showled(hpos+22*8, vpos, ledState);
	} else {
		ted8360->texttoscreen(hpos, vpos, textout);
	}
}

//-----------------------------------------------------------------------------

void machineReset(unsigned int resetlevel)
{
	ted8360->Reset(resetlevel);
	machine->Reset();
	CTrueDrive::ResetAllDrives();
}

void machineDoSomeFrames(unsigned int frames)
{
	ted8360->getKeys()->block(true);
	while (frames--) {
		ted8360->ted_process(1);
		if (g_inDebug)
			DebugInfo();
		frameUpdate();
	}
	ted8360->getKeys()->block(false);
}

void machineEnable1551(bool enable)
{
	if (enable) {
		if (drive1541) {
			delete drive1541;
			drive1541 = NULL;
		}
		if (ted8360->getEmulationLevel() < 2) {
			ted8360->HookTCBM(tcbm);
		} else {
			fsd1541 = new FakeSerialDrive(8);
		}
		g_bTrueDriveEmulation = 0;
	}
	else {
		ted8360->HookTCBM(NULL);
		if (fsd1541) {
			delete fsd1541;
			fsd1541 = NULL;
		}
		if (!drive1541) {
			CSerial::InitPorts();
			drive1541 = new CTrueDrive(1, 8);
			drive1541->Reset();
		}
		g_bTrueDriveEmulation = 1;
	}
}

bool machineIsTrueDriveEnabled(unsigned int dn = 8)
{
	return drive1541 != NULL;
}

static void toggleTrueDriveEmulation(void *none)
{
	bool e = !machineIsTrueDriveEnabled();
	machineEnable1551(!e);
}

static void startd64(const char *fileName, bool autostart)
{
	if (!machineIsTrueDriveEnabled()) {
		machineEnable1551(false);
		machineDoSomeFrames(70);
	}
	CTrueDrive::SwapDisk(fileName);
	if (autostart)
		ted8360->copyToKbBuffer("L\317\042*\042,8,1\rRUN:\r", 15);
	else
		ted8360->copyToKbBuffer("L\317\042*\042,8,1\r\r", 11);
}

bool openZipDisk(const char *fname, bool autostart)
{
	static unsigned char *b = NULL;
	unsigned int fsize = 0;
	unsigned int ftype;

	if (zipOpen(fname, &b, fsize, ftype)) {
		const char* tmpName = (ftype == 1) ? "unz.d64" : "unz.tap";
		FILE *tmp = fopen(tmpName, "wb");

		if (b && tmp) {
			fwrite(b, sizeof(char), fsize, tmp);
			fclose(tmp);
			if (ftype == 1)
				startd64(tmpName, autostart);
			else {
				ted8360->tap->detachTape();
				ted8360->tap->attachTape(tmpName);
				if (autostart) {
					ted8360->copyToKbBuffer("Lo:\rRUN\r");
					ted8360->tap->pressTapeButton(ted8360->GetClockCount(), 1);
				}
				if (b) {
					delete[] b;
					b = NULL;
				}
			}
			return true;
		}
	}
	return false;
}

bool start_file(const char *szFile, bool autostart = true)
{
	char *pFileExt = (char *) strrchr(szFile, '.');

	if (pFileExt) {
		char *fileext = pFileExt;
		if (!strcmp(fileext,".d64") || !strcmp(fileext,".D64")) {
			startd64(szFile, autostart);
			return true;
		}
		if (!strcmp(fileext,".zip") || !strcmp(fileext,".ZIP")) {
			return openZipDisk(szFile, autostart);
		}
		if (!strcmp(fileext,".prg") || !strcmp(fileext,".PRG")
			|| !strcmp(fileext,".p00") || !strcmp(fileext,".P00")) {
			if (PrgLoad(szFile, 0, ted8360)) {
				if (autostart)
					ted8360->copyToKbBuffer("RUN:\r");
				return true;
			}
		}
		if (!strcmp(fileext, ".t64") || !strcmp(fileext,".T64")) {
			if (prgLoadFromT64(szFile, 0, ted8360)) {
				if (autostart)
					ted8360->copyToKbBuffer("RUN:\r");
				return true;
			}
		}
		if (!strcmp(fileext,".tap") || !strcmp(fileext,".TAP") || !strcmp(fileext, ".wav") || !strcmp(fileext, ".WAV")) {
			ted8360->tap->detachTape();
			ted8360->tap->attachTape(szFile);
			if (autostart) {
				ted8360->copyToKbBuffer("Lo:\rRUN\r");
				ted8360->tap->pressTapeButton(ted8360->GetClockCount(), 1);
			}
			return true;
		}
		if (!strcmp(fileext, ".yss")) {
			fprintf(stderr, "Loading emulator state from %s.\n", szFile);
			return SaveState::openSnapshot(szFile, false);
		}
		return false;
	}
	return false;
}

bool autostart_file(const char *szFile, bool autostart)
{
	machineReset(true);
	// do some frames
	unsigned int frames = ted8360->getAutostartDelay();
	machineDoSomeFrames(frames);
	// to work around a few buggy defenders...
	if (!ted8360->getEmulationLevel())
		while (ted8360->getVerticalCount() != 160)
			ted8360->ted_process(0);
	// and then try to load the parameter as file
	return start_file(szFile, autostart);
}

/* ---------- Display functions ---------- */

static char popUpMessage[256] = "";
static unsigned int popupMessageTimeOut = 0;

static void showPopUpMessage()
{
	unsigned int ix;
	size_t len = strlen(popUpMessage);
	unsigned int offset = ted8360->getCyclesPerRow() == 456 ? 456 : 592;
	char dummy[40];

	ix = (unsigned int)(len);
	while( ix-->0)
		dummy[ix] = 32;
	dummy[len] = '\0';

	int tab = offset / 2 - (int(len) << 2);
	int line = 130;
	ted8360->texttoscreen(tab, line, dummy);
	ted8360->texttoscreen(tab, line + 8, popUpMessage);
	ted8360->texttoscreen(tab, line + 16, dummy);

	if(popupMessageTimeOut)
		popupMessageTimeOut -= 1;
}

inline void PopupMsg(const char *msg, ...)
{
	va_list args;
	va_start(args, msg);
	vsprintf(popUpMessage, msg, args);
	va_end(args);

	popupMessageTimeOut = 60; // frames
}

static void showKeyboardOverlay()
{
	static SDL_Texture* texture = NULL;
	static SDL_Rect rc = { 0 };

	if (!texture) {
		SDL_RWops *rwops = SDL_RWFromMem((void*) keyoverlay, sizeof(keyoverlay));
		SDL_Surface* loadedSurface = SDL_LoadBMP_RW(rwops, 1);
		if (loadedSurface) {
			// Note: the logical size of the window surface is 2×!
			rc.w = loadedSurface->w * 2;
			rc.h = loadedSurface->h * 2;
			rc.x = SCREENX - rc.w / 2;
			rc.y = SCREENY * 2 - rc.h;
			texture = SDL_CreateTextureFromSurface(sdlRenderer, loadedSurface);
			SDL_FreeSurface(loadedSurface);
			if (!texture)
				return;
			SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
		}
	}
	SDL_SetTextureAlphaMod(texture, timeOutOverlayKeys);
	SDL_RenderCopy(sdlRenderer, texture, NULL, &rc);
}

static unsigned int pixels[568 * SCR_VSIZE * 2];

void frameUpdate(unsigned char *src, unsigned int *target)
{
	const unsigned int pixelsPerRow = ted8360->getCyclesPerRow();
	const unsigned int sourcePitch = (pixelsPerRow - SCREENX);
	const unsigned int targetPitch = sourcePitch;
	const unsigned int *texture = target;
	unsigned int i, j;

	//
	if (g_bUseOverlay) {
		video_convert_buffer(target, pixelsPerRow, src);
	} else {
		const unsigned int *palette = palette_get_rgb();
		for(i = 0; i < SCREENY; i++) {
			for(j = 0; j < SCREENX; j++) {
				*target++ = palette[*src++];
			}
			src += sourcePitch;
			target += targetPitch;
		}
	}
	// TODO: use SDL_LockTexture instead
	int e = SDL_UpdateTexture(sdlTexture, NULL, texture, pixelsPerRow * sizeof (unsigned int));
	e = SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
	if (timeOutOverlayKeys) {
		showKeyboardOverlay();
		//SDL_StartTextInput();
		if (!mouseBtnHeld)
			timeOutOverlayKeys -= 4;
	}
	SDL_RenderPresent(sdlRenderer);
}

static void frameUpdate()
{
	const unsigned int cyclesPerRow = ted8360->getCyclesPerRow();
	const int offsetX = cyclesPerRow == VIC_PIXELS_PER_ROW ? -72 : 8;
	const int offsetY = cyclesPerRow == VIC_PIXELS_PER_ROW ? 9 : 0;

	if (popupMessageTimeOut)
		showPopUpMessage();
	frameUpdate(ted8360->getScreenData() + (cyclesPerRow - 384 - offsetX) / 2 + offsetY * cyclesPerRow, pixels);
}

/* ---------- Management of settings ---------- */

//-----------------------------------------------------------------------------
// Name: SaveSettings
//-----------------------------------------------------------------------------
bool SaveSettings(char *inifileName)
{
	unsigned int i;
	char tmpStr[MAX_PATH];
	FILE *ini;
	unsigned int rammask;

	if ((ini = fopen(inifileName, "wt"))) {

		fprintf(ini, "[Yape configuration file]\n");
		fprintf(ini,"DisplayFrameRate = %d\n",g_FrameRate);
		fprintf(ini,"DisplayQuickDebugInfo = %d\n",g_inDebug);
		fprintf(ini,"50HzTimerActive = %d\n",g_50Hz);
		fprintf(ini,"ActiveJoystick = %d\n", KEYS::activejoy);
		rammask = ted8360->getRamMask();
		fprintf(ini,"RamMask = %x\n",rammask);
		fprintf(ini,"256KBRAM = %u\n",ted8360->reuSizeKb);
		fprintf(ini,"SaveSettingsOnExit = %x\n",g_bSaveSettings);

		for (i = 0; i<4; i++) {
			fprintf(ini,"ROMC%dLOW = %s\n",i, ted8360->romlopath[i]);
			fprintf(ini,"ROMC%dHIGH = %s\n",i, ted8360->romhighpath[i]);
		}
		ad_get_curr_dir(tmpStr);
		fprintf(ini, "CurrentDirectory = %s\n", tmpStr);
		fprintf(ini, "CRTEmulation = %u\n", g_bUseOverlay);
		fprintf(ini, "WindowMultiplier = %u\n", g_iWindowMultiplier);
		fprintf(ini, "EmulationLevel = %u\n", g_iEmulationLevel);
		fprintf(ini, "JoystickKeysIndex = %u\n", KEYS::joystickScanCodeIndex);
		fprintf(ini, "Vic20RamExpSize = %u\n", Vicmem::ramExpSizeKb);

		fclose(ini);
		return true;
	}
	return false;
}

bool LoadSettings(char *inifileName)
{
	FILE *ini;
	unsigned int rammask;
	char keyword[256], line[256], value[256];

	if ((ini = fopen(inifileName, "r"))) {

		fscanf(ini,"%s configuration file]\n", keyword);
		if (strcmp(keyword, "[Yape"))
			return false;

		while(fgets(line, 255, ini)) {
			strcpy(value, "");
			if (sscanf(line, "%s = %[^,\n,\r]", keyword, value) ) {
				int number = atoi(value);
				if (!strcmp(keyword, "DisplayFrameRate")) {
					g_FrameRate = !!atoi(value);
					fprintf(stderr, "Display frame rate: %i\n", g_FrameRate);
				}
				else if (!strcmp(keyword, "DisplayQuickDebugInfo"))
					g_inDebug = !!atoi(value);
				else if (!strcmp(keyword, "50HzTimerActive"))
					g_50Hz = !!atoi(value);
				else if (!strcmp(keyword, "ActiveJoystick"))
					KEYS::activejoy = atoi(value) & 3;
				else if (!strcmp(keyword, "RamMask")) {
					sscanf(value, "%04x", &rammask);
					ted8360->setRamMask(rammask);
				}
				else if (!strcmp(keyword, "256KBRAM")) {
					rammask = atoi(value);
					if (rammask == 128 || rammask == 256 || rammask == 512) {
						ted8360->enableREU(rammask);
					} else
						ted8360->enableREU(0);
				}
				else if (!strcmp(keyword, "SaveSettingsOnExit"))
					g_bSaveSettings = !!atoi(value);
				else if (!strcmp(keyword, "ROMC0LOW"))
					strcpy(ted8360->romlopath[0], value);
				else if (!strcmp(keyword, "ROMC1LOW"))
					strcpy(ted8360->romlopath[1], value);
				else if (!strcmp(keyword, "ROMC2LOW"))
					strcpy(ted8360->romlopath[2], value);
				else if (!strcmp(keyword, "ROMC3LOW"))
					strcpy(ted8360->romlopath[3], value);
				else if (!strcmp(keyword, "ROMC0HIGH"))
					strcpy(ted8360->romhighpath[0], value);
				else if (!strcmp(keyword, "ROMC1HIGH"))
					strcpy(ted8360->romhighpath[1], value);
				else if (!strcmp(keyword, "ROMC2HIGH"))
					strcpy(ted8360->romhighpath[2], value);
				else if (!strcmp(keyword, "ROMC3HIGH"))
					strcpy(ted8360->romhighpath[3], value);
				else if (!strcmp(keyword, "CurrentDirectory"))
					ad_set_curr_dir(value);
				else if (!strcmp(keyword, "CRTEmulation"))
					g_bUseOverlay = !!atoi(value);
				else if (!strcmp(keyword, "WindowMultiplier"))
					g_iWindowMultiplier = number ? (number & 3) : 1;
				else if (!strcmp(keyword, "EmulationLevel"))
					g_iEmulationLevel = atoi(value);
				else if (!strcmp(keyword, "JoystickKeysIndex"))
					KEYS::joystickScanCodeIndex = atoi(value) % 3;
				else if (!strcmp(keyword, "Vic20RamExpSize"))
					Vicmem::ramExpSizeKb = atoi(value);
			}
		}
		fclose(ini);

		return true;
	}
	return false;
}

static bool saveScreenshotBMP(char* filepath, SDL_Window* SDLWindow, SDL_Renderer* SDLRenderer)
{
	SDL_Surface* saveSurface = NULL;
	int w, h;

	SDL_GetRendererOutputSize(SDLRenderer, &w, &h);
	saveSurface = SDL_CreateRGBSurface(0, w, h, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000);
	if (saveSurface && 0 == SDL_RenderReadPixels(SDLRenderer, 0, SDL_PIXELFORMAT_ARGB8888, saveSurface->pixels, saveSurface->pitch)) {
		SDL_SaveBMP(saveSurface, filepath);
		SDL_FreeSurface(saveSurface);
		return true;
	}
	return false;
}

static bool getSerializedFilename(const char *name, const char *extension, char *out)
{
	char dummy[512];
	unsigned int i = 0;

	if (!name || !extension || !out)
		return false;

	bool found = true;
	do {
		sprintf(dummy, "%s%06d.%s", name, i++, extension);
		FILE *fp = fopen(dummy, "rb");
		if (fp) {
			fclose(fp);
		} else {
			found = false;
			strcpy(out, dummy);
		}
	} while (found && i < 0x1000000);
	return !found;
}

void snapshotSave()
{
	getSerializedFilename("snapshot", "yss", lastSnapshotName);
	SaveState::openSnapshot(lastSnapshotName, true);
	PopupMsg(" Saving snapshot... ");
	fprintf(stderr, "Saved emulator state to %s.\n", lastSnapshotName);
}

//-----------------------------------------------------------------------------
// Name: SaveBitmap()
// Desc: Saves the SDL surface to Windows bitmap file named as yapeXXXX.bmp
//-----------------------------------------------------------------------------
static int SaveBitmap()
{
	char bmpname[512];

	// finding the last yapeXXXX.bmp image
	if (getSerializedFilename("yape", "bmp", bmpname)) {
		if (saveScreenshotBMP(bmpname, sdlWindow, sdlRenderer)) {
			fprintf(stderr, "Screenshot saved: %s\n", bmpname);
			return true;
		}
	}
	return false;
}

bool mainSaveMemoryAsPrg(const char *prgname, unsigned short &beginAddr, unsigned short &endAddr)
{
	char newPrgname[512];
	if (!prgname) {
		if (!getSerializedFilename("noname", "prg", newPrgname))
			return false;
	} else {
		strcpy(newPrgname, prgname);
	}
	if (beginAddr >= endAddr)
		beginAddr = endAddr = 0;
	return prgSaveBasicMemory(newPrgname, ted8360, beginAddr, endAddr, beginAddr == endAddr);
}

bool mainLoadPrgToMemory(const char* prgname, unsigned short& beginAddr)
{
	return PrgLoad(prgname, beginAddr, ted8360);
}

static void doSwapJoy()
{
	KEYS::swapjoy(NULL);
	PopupMsg(" ACTIVE JOY IS : %s ", KEYS::activeJoyTxt());
}

static void doSwapKeyset()
{
	KEYS::swapKeyset(NULL);
	PopupMsg(" ACTIVE KEYSET IS : %s ", KEYS::activeJoyKeyset());
}

static void enterMenu()
{
	timeOutOverlayKeys = 0;
	sound_pause();
#ifdef __EMSCRIPTEN__
	setMainLoop(0);
#else
	uinterface->enterMenu();
#endif
	if (g_50Hz)
		sound_resume();
	if (!g_bActive) {
		PopupMsg(" PAUSED ");
		frameUpdate();
	}
}

static void toggleFullThrottle(void *none)
{
	g_50Hz = !g_50Hz;

	sprintf(textout, " 50 HZ TIMER IS ");
	if (g_50Hz) {
		sound_resume();
		strcat(textout,"ON ");
#ifdef __EMSCRIPTEN__
		emscripten_set_main_loop_timing(EM_TIMING_RAF, 0);
#endif
	}
	else {
		sound_pause();
		strcat(textout, "OFF ");
#ifdef __EMSCRIPTEN__
		emscripten_set_main_loop_timing(EM_TIMING_SETTIMEOUT, 1);
#endif
	}
	PopupMsg(textout);
}

static void setEmulationLevel(unsigned int level)
{
	unsigned char ram[RAMSIZE];
	unsigned int i = ted8360->getEmulationLevel();

	if (level != i) {
		g_bActive = 0;
		sound_pause();
		// Back up RAM
		memcpy(ram, ted8360->Ram, RAMSIZE);
		// Back up TED
		unsigned int oldCpr = ted8360->getCyclesPerRow();
		unsigned int romEnabled = ram[0xff13] & 1;
		for(i = 0; i < 0x20; i++) {
			ram[0xFF00 + i] = ted8360->Read(0xFF00 + i);
		}
		unsigned char prddr = ted8360->Read(0);
		unsigned char prp = ted8360->Read(1);
		// destroy old TED object
		if (ted8360)
			delete ted8360;
		if (fsd1541) {
			delete fsd1541;
			fsd1541 = NULL;
		}
		switch (level) {
			default:
			case 0:
				ted8360 = new TED;
				break;
			case 1:
				ted8360 = new TEDFAST;
				break;
			case 2:
				ted8360 = new Vic2mem;
				break;
			case 3:
				ted8360 = new Vicmem;
				break;
		}
		uinterface->setNewMachine(ted8360);
		unsigned int newCpr = ted8360->getCanvasX();
		//ted8360->Reset();
		machine->setMem(ted8360, ted8360->getIrqReg(), &(ted8360->Ram[0x0100]));
		ted8360->HookTCBM(tcbm);
		ted8360->setCpuPtr(machine);
		// restore RAM
		memcpy(ted8360->Ram, ram, RAMSIZE);
		// reload ROMs for machine type switch
		ted8360->loadroms();
		if (oldCpr != newCpr) {
			machineEnable1551(!g_bTrueDriveEmulation);
			machine->Reset();
			ted8360->Reset(false);
			init_palette(ted8360);
		} else {
			// Restore TED register state
			for(i = 0; i < 0x20; i++) {
				ted8360->Write(0xFF00 + i, ram[0xFF00 + i]);
			}
			ted8360->Write(0xFF3F - romEnabled, 0);
			// processor ports
			ted8360->Write(0, prddr);
			ted8360->Write(1, prp & prddr);
		}
		//
		sound_resume();
		g_bActive = 1;
	}
}

static void toggleShowSpeed(void *none)
{
	g_FrameRate = !g_FrameRate;
}

static void toggleVsync(void *none)
{
	char value[2];
	g_bVideoVsync = 1 - g_bVideoVsync;
	value[0] = 0x30 + g_bVideoVsync;
	value[1] = 0;
	SDL_SetHint(SDL_HINT_RENDER_VSYNC, value);
}

static void toggleCrtEmulation(void *none)
{
	g_bUseOverlay = !g_bUseOverlay;
	SDL_DestroyTexture(sdlTexture);
	sdlTexture = SDL_CreateTexture(sdlRenderer,
			g_bUseOverlay ? SDL_PIXELFORMAT_UYVY : SDL_PIXELFORMAT_ARGB8888,
			SDL_TEXTUREACCESS_STREAMING,
			WINDOWX, WINDOWY * (g_bUseOverlay ? 2 : 1));
	PopupMsg(" CRT emulation %s ", g_bUseOverlay ? "ON" : "OFF");
}

static const char *machineTypeLabel(unsigned int index)
{
	const char *label[] = { "ACCURATE +4", "FAST +4", "COMMODORE 64", "COMMODORE VIC20"};
	return label[index % (sizeof(label)/sizeof(label[0]))];
}
static const char* machineTypeLabel()
{
	return machineTypeLabel(g_iEmulationLevel);
}

static void flipMachineType(char *name, int dir)
{
	g_iEmulationLevel = (g_iEmulationLevel + dir) % 4;
	setEmulationLevel(g_iEmulationLevel);
	strcpy(name, machineTypeLabel());
}

static void flipMachineTypeFwd(void *name)
{
	flipMachineType((char *) name, 1);
}

static void setWindowScale(int newScale)
{
	SDL_SetWindowSize(sdlWindow, SCREENX * newScale, SCREENY * newScale);
}

static void flipWindowScale(void *none)
{
	g_iWindowMultiplier = (g_iWindowMultiplier % 3) + 1;
	setWindowScale(g_iWindowMultiplier);
}

static void confirmEmulationLevelChange(unsigned int shiftPressed)
{
	int dir = shiftPressed ? -1 : 1;
#ifndef __EMSCRIPTEN__
	int buttonid;

	sprintf(textout, "Switch to %s emulation and lose data in current session?", machineTypeLabel(g_iEmulationLevel + dir));
	const SDL_MessageBoxButtonData buttons[] = {
		{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 1, "No" },
		{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "Yes" },
	};
	const SDL_MessageBoxData messageboxdata = {
		SDL_MESSAGEBOX_INFORMATION,
		sdlWindow,
		"Confirmation required",
		textout,
		SDL_arraysize(buttons),
		buttons,
		NULL
	};

	if (SDL_ShowMessageBox(&messageboxdata, &buttonid) < 0)
		SDL_Log("error opening window");
	else if (buttonid == 0) 
#endif
	{
		char name[64];
		flipMachineType(name, dir);
		sprintf(textout, " EMULATION : %s ", name);
		PopupMsg(textout);
	}
}

static void pasteFromClipboard()
{
	if (SDL_HasClipboardText()) {
		char* lptstr = SDL_GetClipboardText();
		size_t origSize = strlen(lptstr);
		size_t t = origSize;
		char bufferdata[16];

		while (t) {
			unsigned int chunkSize = (unsigned int)MIN(10, t);
			strncpy(bufferdata, lptstr, chunkSize);
			UI::stringToPETSCII((unsigned char*)bufferdata, chunkSize);
			bufferdata[chunkSize] = 0;
			ted8360->copyToKbBuffer(bufferdata, chunkSize);
			unsigned int maxFrames = 200;
			while (ted8360->Read(ted8360->getKbBufferSizePtr()) != 0 && --maxFrames) {
				ted8360->ted_process(1);
				frameUpdate();
			}
			lptstr += chunkSize;
			t -= chunkSize;
		}
		//SDL_free((void*)lptstr);
	}
}

//-----------------------------------------------------------------------------
// Name: poll_events()
// Desc: polls SDL events if there's any in the message queue
//-----------------------------------------------------------------------------
inline static void poll_events(void)
{
	SDL_Event event;
	
	if (timeOutMousePointer) {
		timeOutMousePointer--;
		if (!timeOutMousePointer)
			SDL_ShowCursor(0);
	}

	if ( SDL_PollEvent(&event) ) {
		switch (event.type) {

			case SDL_WINDOWEVENT:
				if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
					frameUpdate();
				}
				break;

			case SDL_KEYDOWN:

//				printf("Keysim: %x, scancode: %x - %s key was pressed!\n",
//				event.key.keysym.sym, event.key.keysym.scancode, SDL_GetKeyName(event.key.keysym.sym));
				if (event.key.keysym.mod & KMOD_LALT) {
					sound_pause();
						switch (event.key.keysym.sym) {
							default:;
								break;

							case SDLK_KP_PLUS:
							{
								sound_pause();
								static unsigned int currFrq = 0;
								unsigned int frq[] = { 48000, 96000, 192000, 22050, 44100 };
								currFrq = (currFrq + 1) % MAX_FRQ_INDEX;
								unsigned int rate = frq[currFrq];
								sound_change_freq(rate);
								PopupMsg(" AUDIO FREQUENCY: %u ", rate);
							}
							break;

							case SDLK_1:
							case SDLK_2:
							case SDLK_3:
								{
									int mult = (event.key.keysym.sym - SDLK_0);
									g_iWindowMultiplier = mult;
									setWindowScale(mult);
									PopupMsg(" WINDOW SIZE: %ux ", mult);
								}
								break;

							case SDLK_l:
								confirmEmulationLevelChange(event.key.keysym.mod & KMOD_SHIFT);
								break;
							case SDLK_i :
								doSwapJoy();
								break;
							case SDLK_k:
								doSwapKeyset();
								break;
							case SDLK_m :
								monitorEnter(machine);
								break;
							case SDLK_p:
								toggleCrtEmulation(NULL);
								break;
							case SDLK_r:
								ted8360->Reset(false);
								machineReset((event.key.keysym.mod & (KMOD_LSHIFT | KMOD_RSHIFT)) != 0);
								break;
							case SDLK_s:
								toggleShowSpeed(NULL);
								break;

							case SDLK_v:
								pasteFromClipboard();
								break;

							case SDLK_w :
								toggleFullThrottle(NULL);
								break;
							case SDLK_RETURN:
								{
									Uint32 isFS = SDL_GetWindowFlags(sdlWindow) & SDL_WINDOW_FULLSCREEN_DESKTOP;
									SDL_SetWindowFullscreen(sdlWindow, isFS ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
								}
								break;
							case SDLK_F5:
								snapshotSave();
								break;
							case SDLK_F6:
								if (SaveState::openSnapshot(lastSnapshotName, false))
									PopupMsg(" Loading %s... ", lastSnapshotName);
								break;

							case SDLK_F8:
								{
									unsigned short d1 = 0, d2 = 0;
									mainSaveMemoryAsPrg(0, d1, d2);
								}
								break;
						};
						if (g_50Hz)
							sound_resume();
						return;
				}
				switch (event.key.keysym.sym) {

					case SDLK_PAUSE :
						PopupMsg(!g_bActive ? " RESUMING " : " PAUSED ");
						g_bActive=!g_bActive;
						break;
					case SDLK_PAGEDOWN:
						g_bActive = 0;
						machineDoSomeFrames(1);
						break;
					case SDLK_PAGEUP:
						ted8360->triggerNMI();
						break;
					case SDLK_F5 :
					case SDLK_F6 :
						{
							unsigned int buttonPressed = ted8360->tap->IsButtonPressed();
							ted8360->tap->pressTapeButton(ted8360->GetClockCount(), !buttonPressed);
							PopupMsg(" %s TAPE... ", buttonPressed ? "STOPPING" : "PLAYING");
						}
						break;

						break;
					case SDLK_F7:
						SaveBitmap();
						break;
					case SDLK_ESCAPE:
					case SDLK_F8:
					case SDLK_DELETE:
						SDL_ShowCursor(1);
						enterMenu();
						break;
					case SDLK_F9 :
						g_inDebug=!g_inDebug;
						break;
					case SDLK_F10 :
						if (SaveSettings(inifile))
						  fprintf( stderr, "Settings saved to %s.\n", inifile);
						else
						  fprintf( stderr, "Oops! Could not save settings... %s\n", inifile);
						break;
					case SDLK_F11 :
						g_bActive = false;
						break;
					case SDLK_F12 :
						exit(0);
					default :
						return;
				}
				break;

			case SDL_KEYUP:
				switch (event.key.keysym.sym) {

					case SDLK_PRINTSCREEN:
						machine->clearNmi();
						break;
					case SDLK_F11 :
						g_bActive = true;
						machineReset( 
							((event.key.keysym.mod & (KMOD_LSHIFT | KMOD_RSHIFT)) ? 2 : 0)
							|((event.key.keysym.mod & (KMOD_LCTRL | KMOD_RCTRL)) ? 1 : 0)
							);
						break;
						default:
							/*if (!(event.key.keysym.mod & KMOD_ALT))
								setEmulationLevel(g_iEmulationLevel);*/
							;
				}
				break;

			case SDL_JOYBUTTONUP:
				//printf("joy: %u, button: %u was pressed!\n", event.jbutton.which, event.jbutton.button);
				if (event.jbutton.button == 5 || event.jbutton.button == 14) { // START button
					enterMenu();
				} else if (event.jbutton.button == 4) {
					ted8360->copyToKbBuffer("RUN:\r",5);
				} else if (event.jbutton.button == 11 || event.jbutton.button == 1) {
					doSwapJoy();
				} else if (event.jbutton.button == 12) {
					toggleFullThrottle(NULL);
				}
				break;

			case SDL_DROPFILE:
				{
					const Uint8 *keystate = SDL_GetKeyboardState(NULL);
					char *dropped_file_dir = event.drop.file;
					bool autostart = (!keystate[SDL_SCANCODE_LSHIFT] && !keystate[SDL_SCANCODE_RSHIFT]);
					if (!autostart_file(dropped_file_dir, autostart))
						SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", "File failed to load properly.", sdlWindow);
					SDL_free(dropped_file_dir);
				}
				break;

			case SDL_MOUSEBUTTONDOWN:
				// stupid workaround for too early detection
				if (ted8360->GetClockCount() > TED_REAL_CLOCK_M10 / 10) {
					if (timeOutOverlayKeys) {
					}
					timeOutOverlayKeys = 192;
					mouseBtnHeld = true;
				}
#ifdef __EMSCRIPTEN__
				// FIXME, this is a hack
				EM_ASM("var SDL2 = Module['SDL2']; if (SDL2.audioContext.state !== 'running') { SDL2.audioContext.resume(); }", 0);
#endif
				break;

			case SDL_MOUSEBUTTONUP:
				mouseBtnHeld = false;
				break;

			case SDL_MOUSEMOTION:
				SDL_ShowCursor(1);
				timeOutMousePointer = 75;
				break;

			case SDL_QUIT:
				exit(0);
		}
	}
}

static void machineInit()
{
	// 1551 IEC
	CFakeTCBM *tcbm_l = new CFakeTCBM();
	CFakeIEC *iec_l = new CFakeIEC(8);
	fsdrive = new CIECFSDrive(".");
	iec = iec_l;
	tcbm = tcbm_l;
	tcbm_l->AddIECInterface((CIECInterface*)iec);
	iec_l->AddIECDevice((CIECDevice*)fsdrive);
	tcbm_l->Reset();
	iec->Reset();
	fsdrive->Reset();
	//
	// TED
	//
	ted8360 = new TED;
	machine = new CPU(ted8360, ted8360->getIrqReg(), &(ted8360->Ram[0x0100]));
	ted8360->HookTCBM(tcbm);
	ted8360->setCpuPtr(machine);
	ted8360->Reset(true);
	// Serial init
	CSerial::InitPorts();
	// calculate and initialise palette
	init_palette(ted8360);
	// CPU
	machine->Reset();
}

void machineShutDown()
{
	machineEnable1551(true);
	delete fsdrive;
	delete iec;
	delete tcbm;
	delete machine;
	delete ted8360;
}

static void app_close()
{
	close_audio();
	// Save settings if required
	if (g_bSaveSettings)
		SaveSettings(inifile);
	if (inifile) {
		delete[] inifile;
		inifile = NULL;
	}
	delete uinterface;
	KEYS::closePcJoys();
	if (sdlRenderer)
		SDL_DestroyRenderer(sdlRenderer);
	if (sdlTexture)
		SDL_DestroyTexture(sdlTexture);
	if (sdlWindow)
		SDL_DestroyWindow(sdlWindow);
	SDL_Quit();
}

static void setSDLIcon(SDL_Window* window)
{
	// these masks are needed to tell SDL_CreateRGBSurface(From)
	// to assume the data it gets is byte-wise RGB(A) data
	unsigned int rmask, gmask, bmask, amask;

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	int shift = (sdlIconPlus4.bytes_per_pixel == 3) ? 8 : 0;
	rmask = 0xff000000 >> shift;
	gmask = 0x00ff0000 >> shift;
	bmask = 0x0000ff00 >> shift;
	amask = 0x000000ff >> shift;
#else // little endian, like x86
	rmask = 0x000000ff;
	gmask = 0x0000ff00;
	bmask = 0x00ff0000;
	amask = (sdlIconPlus4.bytes_per_pixel == 3) ? 0 : 0xff000000;
#endif
	SDL_Surface *icon = SDL_CreateRGBSurfaceFrom((void*) sdlIconPlus4.pixel_data, sdlIconPlus4.width,
		sdlIconPlus4.height, sdlIconPlus4.bytes_per_pixel*8, sdlIconPlus4.bytes_per_pixel*sdlIconPlus4.width,
		rmask, gmask, bmask, amask);
	SDL_SetWindowIcon(window, icon);
	SDL_FreeSurface(icon);
}

static void app_initialise()
{
	SDL_version sdlv;

	SDL_GetVersion(&sdlv);
	printf("SDL version detected: %d.%d.%d\n", sdlv.major, sdlv.minor, sdlv.patch);
#ifdef __EMSCRIPTEN__
	// very important for performance reasons
	SDL_SetHint(SDL_HINT_EMSCRIPTEN_ASYNCIFY, "0");
#endif
	if ( SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) < 0 ) {
		fprintf(stderr, "Unable to init SDL with controller support: %s\n", SDL_GetError());
		if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
			fprintf(stderr, "Unable to init SDL: %s\n", SDL_GetError());
			exit(1);
		}
	}
	atexit(app_close);

	//SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, "#screen");
	// create a new window
	sdlWindow = SDL_CreateWindow(NAME,
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		WINDOWX * g_iWindowMultiplier, WINDOWY * g_iWindowMultiplier,
		SDL_WINDOW_RESIZABLE); // SDL_WINDOW_FULLSCREEN_DESKTOP
	if (!sdlWindow) {
		printf("Unable to create window: %s\n", SDL_GetError());
		return;
	}
	setSDLIcon(sdlWindow);
	SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
	// check the video driver we have
	printf("Using video driver : %s\n", SDL_GetCurrentVideoDriver());
	//
	printf("On-screen keyboard");
	if (!SDL_HasScreenKeyboardSupport())
		printf(" not");
	printf(" supported.\n");
	//
	sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	SDL_RenderSetLogicalSize(sdlRenderer, SCREENX * 2, SCREENY * 2);

	sdlTexture = SDL_CreateTexture(sdlRenderer,
							   g_bUseOverlay ? SDL_PIXELFORMAT_UYVY : SDL_PIXELFORMAT_ARGB8888,
							   SDL_TEXTUREACCESS_STREAMING | SDL_TEXTUREACCESS_TARGET,
							   WINDOWX, WINDOWY * (g_bUseOverlay ? 2 : 1));
	// Make target texture to render to
	SDL_SetRenderTarget(sdlRenderer, sdlTexture);
	init_audio();
	if (!g_50Hz)
		sound_pause();
	KEYS::initPcJoys();
}

/* ---------- MAIN LOOP ---------- */
static void mainLoop()
{
	// hook into the emulation loop if active
	if (g_bActive) {
		ted8360->ted_process(1);
		poll_events();
		if (g_inDebug)
			DebugInfo();
		ShowFrameRate(g_FrameRate);
		if (ad_vsync(!!g_50Hz))
			frameUpdate();
	} else {
#ifndef __EMSCRIPTEN__ // does not work in Emscripten
		if (SDL_WaitEvent(NULL))
#endif
			poll_events();
	}
}

void setMainLoop(int looptype)
{
#ifdef __EMSCRIPTEN__
	emscripten_cancel_main_loop();
	if (looptype) {
		sound_resume();
		emscripten_set_main_loop((em_callback_func) mainLoop, 0, 1);
	} else {
		while (SDL_PollEvent(NULL));
		sound_pause();
		uinterface->drawMenu();
		emscripten_set_main_loop_arg((em_arg_callback_func) interfaceLoop, uinterface, 0, 1);
	}
	// lock browser keys with inline JS
	EM_ASM({ document.onkeydown = KeyCheck; }, 0);
#endif
}

static void printSpecialKeys()
{
	printf("\nSpecial keys:\n");
	printf("CLR/HOME     : %s\n", SDL_GetKeyName(SDLK_HOME));
	printf("C=           : %s\n", SDL_GetKeyName(SDLK_LCTRL));
	printf("CONTROL      : %s\n", SDL_GetKeyName(SDLK_RCTRL));
	printf("INST/DEL     : %s\n", SDL_GetKeyName(SDLK_BACKSPACE));
	printf("RUN/STOP     : %s\n", SDL_GetKeyName(SDLK_TAB));
	printf("POUND        : %s\n", SDL_GetKeyName(SDLK_END));
	printf("RESTORE      : %s\n", SDL_GetKeyName(SDLK_INSERT));
}

/* ---------- MAIN ---------- */
int main(int argc, char *argv[])
{
	machineInit();
	uinterface = new UI(ted8360);

	fprintf(stderr, "%s\n", NAME);
#ifndef __EMSCRIPTEN__
	inipath = SDL_GetPrefPath("Gaia", "yapeSDL");
	if (inipath) {
		fprintf(stderr, "Home directory is %s\n", inipath);
	} else {
		inipath = SDL_GetBasePath();
		fprintf(stderr, "No home directory, using application directory: %s\n", inipath);
	}

	inifile = new char[strlen(inipath) + 16];
	if (!inifile)
		return 1;

	strcpy(inifile, inipath);
	SDL_free(inipath);
	strcat(inifile, "yape.conf");
	fprintf(stderr, "Config file: %s\n", inifile);

	if (LoadSettings(inifile)) {
		char tmpStr[512];
		fprintf(stderr, "Settings loaded successfully.\n");
		ad_get_curr_dir(tmpStr);
		fprintf(stderr, "IEC drive path: %s\n", tmpStr);
		setEmulationLevel(g_iEmulationLevel);
		machineReset(true);
	} else
		fprintf(stderr,"Error loading settings or no .ini file present...\n");
#endif

	app_initialise();

	/* ---------- Command line parameters ---------- */
	if (argc > 1) {
		printf("Parameter 1 :%s\n", argv[1]);
#ifdef __EMSCRIPTEN__
		printf("Parameter 2 :%s\n", argv[2]);
		emscripten_wget(argv[1], argv[2]);
		if (argc >= 3 && !strcmp(argv[3], "-c64"))
			setEmulationLevel(2);
		autostart_file(argv[2], true);
#else
		char *pathBackup = new char[4096];
		char* exePath = SDL_GetBasePath();
		// switch back to the application folder
		int r = ad_get_curr_dir(pathBackup);
		ad_set_curr_dir(exePath);
		// and then try to load the parameter as file
		autostart_file(argv[1], true);
		if (!autostart_file(argv[1], true))
			fprintf(stderr, "Failed loading: %s\n", argv[1]);
		// revert to working directory (TODO: to be overriden with CL switch)
		ad_set_curr_dir(pathBackup);
		SDL_free(exePath);
		delete[] pathBackup;
#endif
	}
#ifdef __EMSCRIPTEN__
	printf("%s - Javascript/WASM build using Emscripten.\n", NAME);
	printf("Type DIRECTORY (or LOAD\"$\",8 and then LIST) and press ENTER to see disk contents. Type LOAD\"filename*\",8 to load a specific file!\n");
	printf("Or enter the menu by pressing ESC and shift+ENTER to autostart games from there.\n");
	printf("Commodore +4 games start with uppercase, C64 ones with lowercase.\n");
	printf("Usage:\n");
	printf("LALT + 1-3   : set window size\n");
	printf("LALT + I     : switch emulated joystick port\n");
	printf("LALT + K     : flip keyset for joystick\n");
	printf("LALT + L     : switch among emulators (C+4 cycle based; C+4 line based; C64 cycle based)\n");
	printf("LALT + M     : enter console based external monitor and disassembler (currently deadlocks!)\n");
	printf("LALT + P     : toggle CRT emulation\n");
	printf("LALT + R     : machine reset (press Shift+F11 for hard reset)\n");
	printf("LALT + S     : display frame rate on/off\n");
	printf("LALT + W     : toggle between unlimited speed and original speed\n");
	printf("LALT + ENTER : toggle full screen mode\n");
	printf("LALT + F5    : save emulator snapshot\n");
	printf("LALT + F6    : load emulator snapshot\n");
	printf("ESC          : enter/leave menu\n");
	printf("PAUSE        : suspend/resume emulation\n");
	printSpecialKeys();
	printf("Joystick buttons are the arrow keys and SPACE. You can change it in the menu under 'Options...'\n");
	setMainLoop(1);
#else
	printSpecialKeys();
	ad_vsync_init();
	for (;;) {
		mainLoop();
	}
#endif
	return 0;
}
