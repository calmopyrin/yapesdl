/*
	YAPE - Yet Another Plus/4 Emulator

	The program emulates the Commodore 264 family of 8 bit microcomputers

	This program is free software, you are welcome to distribute it,
	and/or modify it under certain conditions. For more information,
	read 'Copying'.

	(c) 2000, 2001, 2004, 2005, 2007, 2015 Attila Grósz
	(c) 2005 VENESZ Roland
*/

#define NAME    "Yape/SDL 0.70.1"
#define WINDOWX SCREENX
#define WINDOWY SCREENY

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#ifdef _WIN32
#include <SDL/SDL.h>
#else
#include <SDL2/SDL.h>
#endif
#include "keyboard.h"
#include "cpu.h"
#include "tedmem.h"
#include "tape.h"
#include "sound.h"
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
#include "SaveState.h"

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

// SDL stuff
static SDL_Window* sdlWindow;
static SDL_Renderer *sdlRenderer;
static SDL_Texture *sdlTexture = NULL;

// class pointer for the user interface
static UI				*uinterface = NULL;

////////////////
// Supplementary
static unsigned int		g_TotFrames = 0;

static TED				*ted8360 = NULL;
static CPU				*machine = NULL;
static CTCBM			*tcbm = NULL;
static CIECInterface	*iec = NULL;
static CIECDrive		*fsdrive = NULL;
static CTrueDrive		*drive1541 = NULL;
static FakeSerialDrive	*fsd1541 = NULL;

//
static char				textout[64];
static char				*inipath;
static char				*inifile;

static bool				g_bActive = true;
static bool				g_inDebug = false;
static bool				g_FrameRate = true;
static bool				g_50Hz = true;
static bool				g_bSaveSettings = true;
static bool				g_SoundOn = true;
static bool             g_bUseOverlay = 0;
static unsigned int		g_bWindowMultiplier = 2;
static unsigned int		g_iEmulationLevel = 0;
static char				lastSnapshotName[512] = "";

//-----------------------------------------------------------------------------
// Name: ShowFrameRate()
//-----------------------------------------------------------------------------
inline static unsigned int ShowFrameRate(bool show)
{
	char fpstxt[80];
	unsigned int fps = ad_get_fps();
	if (show) {
		sprintf(fpstxt, " %i FPS ", fps);
		ted8360->texttoscreen(ted8360->getCyclesPerRow() - 128, 34, fpstxt);
	}
	return fps;
}

//-----------------------------------------------------------------------------
// Name: DebugInfo()
//-----------------------------------------------------------------------------
inline static void DebugInfo()
{
	const unsigned int hpos = (ted8360->getCyclesPerRow() == 456 ? 48 : 112), vpos = 10;

	sprintf(textout, "OPCODE: %02x ", machine->getcins());
	ted8360->texttoscreen(hpos, vpos, textout);
	ted8360->texttoscreen(hpos, vpos+8, "  PC  SR AC XR YR SP");
	sprintf(textout, ";%04x %02x %02x %02x %02x %02x", machine->getPC(),
		machine->getST(), machine->getAC(), machine->getX(), machine->getY(), machine->getSP());
	ted8360->texttoscreen(hpos, vpos+16, textout);
	sprintf(textout, "TAPE: %08d ", ted8360->tap->TapeSoFar);
	CTrueDrive *d = CTrueDrive::Drives[0];
	if (d) {
		char driveText[64];
		unsigned int t, s;
		unsigned char ledState = d->getDriveMemHandler()->Read(0x1c00) & 8;
		unsigned char motorState = d->GetFdc()->getMotorState() ? 4 : 0;

		d->GetFdc()->trackSector(t, s);
		sprintf(driveText, "DRIVE:  T/S:%02u/%02u", t, s);
		strcat(textout, driveText);
		ted8360->texttoscreen(hpos, vpos+24, textout);
		ted8360->showled(hpos+21*8, vpos+24, motorState);
		ted8360->showled(hpos+22*8, vpos+24, ledState);
	} else {
		ted8360->texttoscreen(hpos, vpos+24, textout);
	}
}

//-----------------------------------------------------------------------------

void machineReset(bool hardreset)
{
	if (hardreset) {
		ted8360->Reset(hardreset);
	}
	machine->Reset();
	CTrueDrive::ResetAllDrives();
}

void machineDoSomeFrames(unsigned int frames)
{
	ted8360->getKeys()->block(true);
	while (frames--) {
		ted8360->ted_process(1);
		frameUpdate();
	}
	ted8360->getKeys()->block(false);
}

void machineEnable1551(bool enable)
{
	if (enable) {
		if (ted8360->getEmulationLevel() != 2) {
			ted8360->HookTCBM(tcbm);
			if (drive1541) {
				delete drive1541;
				drive1541 = NULL;
			}
		} else {
			fsd1541 = new FakeSerialDrive(8);
		}
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
	}
}

bool machineIsTrueDriveEnabled(unsigned int dn = 8)
{
	return drive1541 != NULL;
}

bool start_file(char *szFile )
{
	char *pFileExt = strrchr(szFile, '.');

	if (pFileExt) {
		char *fileext = pFileExt;
		do {
			*pFileExt = tolower(*pFileExt);
			pFileExt++;
		} while (*pFileExt);
		if (!strcmp(fileext,".d64") || !strcmp(fileext,".D64")) {
			if (!machineIsTrueDriveEnabled()) {
				machineEnable1551(false);
				machineDoSomeFrames(70);
				CTrueDrive::SwapDisk(szFile);
			}
			ted8360->copyToKbBuffer("L\317\042*\042,8,1\rRUN:\r", 15);
			return true;
		}
		if (!strcmp(fileext,".prg") || !strcmp(fileext,".PRG")
			|| !strcmp(fileext,".p00") || !strcmp(fileext,".P00")) {
			PrgLoad(szFile, 0, ted8360 );
			ted8360->copyToKbBuffer("RUN:\r",5);
			return true;
		}
		if (!strcmp(fileext, ".t64") || !strcmp(fileext,".T64")) {
            prgLoadFromT64(szFile, 0, ted8360);
            ted8360->copyToKbBuffer("RUN:\r",5);
		}
		if (!strcmp(fileext,".tap") || !strcmp(fileext,".TAP")) {
			ted8360->tap->detach_tap();
			strcpy(ted8360->tap->tapefilename, szFile);
			ted8360->tap->attach_tap();
			return true;
		}
		if (!strcmp(fileext, ".yss")) {
			return SaveState::openSnapshot(szFile, false);
		}
		return false;
    }
	return false;
}

bool autostart_file(char *szFile)
{
	machineReset(true);
	// do some frames
	unsigned int frames = ted8360->getAutostartDelay();
	machineDoSomeFrames(frames);
	// and then try to load the parameter as file
	return start_file(szFile);
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

static unsigned int pixels[512 * SCR_VSIZE * 2];

void frameUpdate(unsigned char *src, unsigned int *target)
{
    unsigned int i, j;
    unsigned int *texture = target;
	unsigned int pixelsPerRow = ted8360->getCyclesPerRow();

    //
    if (g_bUseOverlay) {
        video_convert_buffer(target, pixelsPerRow, src);
    } else {
        unsigned int *palette = palette_get_rgb();
        for(i = 0; i < SCREENY; i++) {
            for(j = 0; j < SCREENX; j++) {
				*target++ = palette[*src++];
			}
			src += (pixelsPerRow - SCREENX);
			target += (pixelsPerRow - SCREENX);
		}
    }
    //
	int e = SDL_UpdateTexture(sdlTexture, NULL, texture, pixelsPerRow * sizeof (unsigned int));
	e = SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
	SDL_RenderPresent(sdlRenderer);
}

static void frameUpdate()
{
	const unsigned int cyclesPerRow = ted8360->getCyclesPerRow();
	const int offsetX = cyclesPerRow == 504 ? -72 : 8;
	const int offsetY = cyclesPerRow == 504 ? 9 : 0;

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
		fprintf(ini,"ActiveJoystick = %d\n",ted8360->getKeys()->activejoy);
		rammask = ted8360->getRamMask();
		fprintf(ini,"RamMask = %x\n",rammask);
		fprintf(ini,"256KBRAM = %x\n",ted8360->bigram);
		fprintf(ini,"SaveSettingsOnExit = %x\n",g_bSaveSettings);

		for (i = 0; i<4; i++) {
			fprintf(ini,"ROMC%dLOW = %s\n",i, ted8360->romlopath[i]);
			fprintf(ini,"ROMC%dHIGH = %s\n",i, ted8360->romhighpath[i]);
		}
		ad_get_curr_dir(tmpStr);
		fprintf(ini, "CurrentDirectory = %s\n", tmpStr);
		fprintf(ini, "CRTEmulation = %u\n", g_bUseOverlay);
		fprintf(ini, "WindowMultiplier = %u\n", g_bWindowMultiplier);
		fprintf(ini, "EmulationLevel = %u\n", g_iEmulationLevel);

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
					fprintf( stderr, "Display frame rate: %i\n", g_FrameRate);
				} else if (!strcmp(keyword, "DisplayQuickDebugInfo"))
					g_inDebug = !!atoi(value);
				else if (!strcmp(keyword, "50HzTimerActive"))
					g_50Hz = !!atoi(value);
				else if (!strcmp(keyword, "ActiveJoystick"))
					ted8360->getKeys()->activejoy = atoi(value);
				else if (!strcmp(keyword, "RamMask")) {
					sscanf( value, "%04x", &rammask);
					ted8360->setRamMask( rammask );
				}
				else if (!strcmp(keyword, "256KBRAM"))
					ted8360->bigram = !!atoi(value);
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
					g_bWindowMultiplier = number ? (number & 3) : 1;
				else if (!strcmp(keyword, "EmulationLevel"))
					g_iEmulationLevel = atoi(value);
			}
		}
		fclose(ini);

		if (!g_50Hz)
			g_SoundOn = false;

		return true;
	}
	return false;
}

static bool saveScreenshotBMP(char* filepath, SDL_Window* SDLWindow, SDL_Renderer* SDLRenderer)
{
	SDL_Surface* saveSurface = NULL;
	SDL_Surface* infoSurface = NULL;
	infoSurface = SDL_GetWindowSurface(SDLWindow);
	if (infoSurface == NULL) {
		return false;
	} else {
		unsigned char *pxls = new unsigned char[infoSurface->w * infoSurface->h * infoSurface->format->BytesPerPixel];
		if (SDL_RenderReadPixels(SDLRenderer, &infoSurface->clip_rect, infoSurface->format->format, pxls, infoSurface->w * infoSurface->format->BytesPerPixel) != 0) {
			pxls = NULL;
			return false;
		} else {
			saveSurface = SDL_CreateRGBSurfaceFrom(pixels, infoSurface->w, infoSurface->h, infoSurface->format->BitsPerPixel,
				infoSurface->w * infoSurface->format->BytesPerPixel, infoSurface->format->Rmask,
				infoSurface->format->Gmask, infoSurface->format->Bmask, infoSurface->format->Amask);
			if (saveSurface == NULL) {
				return false;
			}
			SDL_SaveBMP(saveSurface, filepath);
			SDL_FreeSurface(saveSurface);
			saveSurface = NULL;
		}
		delete[] pxls;
		SDL_FreeSurface(infoSurface);
		infoSurface = NULL;
	}
	return true;
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

//-----------------------------------------------------------------------------
// Name: SaveBitmap()
// Desc: Saves the SDL surface to Windows bitmap file named as yapeXXXX.bmp
//-----------------------------------------------------------------------------
static int SaveBitmap()
{
    char bmpname[512];

    // finding the last yapeXXXX.bmp image
	if (getSerializedFilename("yape", ".bmp", bmpname)) {
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

static void doSwapJoy()
{
	const char *txt[] = { "NONE", "1", "2", "BOTH" };
	unsigned int active = ted8360->getKeys()->swapjoy();
	sprintf(textout, " ACTIVE JOY IS : %s ", txt[active]);
	printf("%s\n", textout);
	PopupMsg(textout);
}

static void enterMenu()
{
	sound_pause();
#ifdef __EMSCRIPTEN__
	setMainLoop(0);
#else
	uinterface->enterMenu();
#endif
	if (g_SoundOn && g_50Hz)
		sound_resume();
	if (!g_bActive) {
		PopupMsg(" PAUSED ");
		frameUpdate();
	}
}

static void toggleFullThrottle()
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
	// restart counter
	g_TotFrames = 0;
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
            case 0:
                ted8360 = new TED;
                break;
            case 1:
                ted8360 = new TEDFAST;
                break;
            case 2:
                ted8360 = new Vic2mem;
				fsd1541 = new FakeSerialDrive(8);
                break;
		}
		uinterface->setNewMachine(ted8360);
		unsigned int newCpr = ted8360->getCyclesPerRow();
		//ted8360->Reset();
		machine->setMem(ted8360, ted8360->getIrqReg(), &(ted8360->Ram[0x0100]));
		ted8360->HookTCBM(tcbm);
		ted8360->setCpuPtr(machine);
		// restore RAM
		memcpy(ted8360->Ram, ram, RAMSIZE);
		// reload ROMs for machine type switch
		ted8360->loadroms();
		if (oldCpr != newCpr) {
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
		//CSerial::InitPorts();
		//CTrueDrive::ResetAllDrives();
		//
		sound_resume();
		g_bActive = 1;
	}
}

//-----------------------------------------------------------------------------
// Name: poll_events()
// Desc: polls SDL events if there's any in the message queue
//-----------------------------------------------------------------------------
inline static void poll_events(void)
{
	SDL_Event event;

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
								sound_resume();
							}
							break;

							case SDLK_1:
							case SDLK_2:
							case SDLK_3:
								{
									int mult = (event.key.keysym.sym - SDLK_0);
									g_bWindowMultiplier = mult;
									SDL_SetWindowSize(sdlWindow, SCREENX * mult, SCREENY * mult);
									PopupMsg(" WINDOW SIZE: %ux ", mult);
								}
								break;
							case SDLK_e:
							case SDLK_l:
								{
									int dir = (event.key.keysym.mod & KMOD_SHIFT) ? -1 : 1;
									g_iEmulationLevel = (g_iEmulationLevel + dir) % 3;
									setEmulationLevel(g_iEmulationLevel);
									sprintf(textout, " EMULATION : ");
									switch (g_iEmulationLevel) {
									default:
									case 0:
										strcat(textout, "ACCURATE +4 ");
										break;
									case 1:
										strcat(textout, "FAST +4 ");
										break;
									case 2:
										strcat(textout, "COMMODORE 64 ");
									}
									printf("%s.\n", textout);
								}
								PopupMsg(textout);
								break;
							case SDLK_i :
								doSwapJoy();
								break;
							case SDLK_m :
								sound_pause();
								monitorEnter(machine);
								sound_resume();
								break;
                            case SDLK_p:
                                g_bUseOverlay = !g_bUseOverlay;
                                SDL_DestroyTexture(sdlTexture);
                                sdlTexture = SDL_CreateTexture(sdlRenderer,
                                        g_bUseOverlay ? SDL_PIXELFORMAT_UYVY : SDL_PIXELFORMAT_ARGB8888,
                                        SDL_TEXTUREACCESS_STREAMING,
										WINDOWX, WINDOWY * (g_bUseOverlay ? 2 : 1));
                                PopupMsg(" CRT emulation %s ", g_bUseOverlay ? "ON" : "OFF");
                                break;
							case SDLK_r:
								ted8360->Reset(false);
								machineReset(false);
								printf("Resetting...\n");
								PopupMsg(" RESET ");
								break;
							case SDLK_s:
								g_TotFrames = 0;
								g_FrameRate = !g_FrameRate;
								break;
							/*case SDLK_u:
								enterMenu();
								break;*/
							case SDLK_w :
								toggleFullThrottle();
								break;
							case SDLK_RETURN:
								{
									static bool fs = false;
									fs = !fs;
									SDL_SetWindowFullscreen(sdlWindow, fs ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
								}
								break;
							case SDLK_F5:
								getSerializedFilename("snapshot", "yss", lastSnapshotName);
								SaveState::openSnapshot(lastSnapshotName, true);
								PopupMsg(" Saving snapshot... ");
								fprintf(stderr, "Saved emulator state to %s.\n", lastSnapshotName);
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
						if (g_50Hz) sound_resume();
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
                    case SDLK_PRINTSCREEN:
                        machine->triggerNmi();
                        break;
					case SDLK_F5 :
					case SDLK_F6 :
						ted8360->tap->PressTapeButton(ted8360->GetClockCount());
						break;

						break;
					case SDLK_F7:
						SaveBitmap();
						break;
					case SDLK_ESCAPE:
					case SDLK_F8:
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
						if (event.key.keysym.mod & (KMOD_LSHIFT|KMOD_RSHIFT) ) {
							machineReset(true);
							break;
						}
						if (event.key.keysym.mod & (KMOD_LCTRL|KMOD_RCTRL) ) {
							ted8360->Reset(false);
						} else {
							ted8360->Reset(false);
						}
						machineReset(false);
						break;
						default:
							;
				}
				break;

			case SDL_JOYBUTTONUP:
				//printf("joy: %u, button: %u was pressed!\n", event.jbutton.which, event.jbutton.button);
				if (event.jbutton.button == 5 || event.jbutton.button == 14) { // START button
					enterMenu();
				} else if (event.jbutton.button == 4) {
					ted8360->copyToKbBuffer("RUN:\r",5);
				} else if (event.jbutton.button == 11) {
					doSwapJoy();
				} else if (event.jbutton.button == 12) {
					toggleFullThrottle();
				}
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
		delete [] inifile;
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
    if ( SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) < 0 ) { //  SDL_INIT_AUDIO|
        fprintf(stderr, "Unable to init SDL: %s\n", SDL_GetError());
        exit(1);
    }
	atexit(app_close);

	// check the video driver we have
	fprintf(stderr, "Using video driver : %s\n", SDL_GetCurrentVideoDriver());

    // create a new window
    sdlWindow = SDL_CreateWindow(NAME,
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOWX * g_bWindowMultiplier, WINDOWY * g_bWindowMultiplier,
		SDL_WINDOW_RESIZABLE); // SDL_WINDOW_FULLSCREEN_DESKTOP
    if ( !sdlWindow ) {
        printf("Unable to create window: %s\n", SDL_GetError());
        return;
    }
	setSDLIcon(sdlWindow);
    sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, 0);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    SDL_RenderSetLogicalSize(sdlRenderer, 768, 576);

    sdlTexture = SDL_CreateTexture(sdlRenderer,
                               g_bUseOverlay ? SDL_PIXELFORMAT_UYVY : SDL_PIXELFORMAT_ARGB8888,
                               SDL_TEXTUREACCESS_STREAMING,
							   WINDOWX, WINDOWY * (g_bUseOverlay ? 2 : 1));

	init_audio();
}

static void mainLoop()
{
	// hook into the emulation loop if active
	if (g_bActive) {
		ted8360->ted_process(1);
		poll_events();
		if (g_inDebug)
			DebugInfo();
		ShowFrameRate(g_FrameRate);
		if (ad_vsync(g_50Hz))
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
		//printf("Entering main loop...\n");
		emscripten_set_main_loop((em_callback_func) mainLoop, 0, 1);
	} else {
		//printf("Entering menu loop...\n");
		while (SDL_PollEvent(NULL));
		uinterface->drawMenu();
		emscripten_set_main_loop_arg((em_arg_callback_func) interfaceLoop, uinterface, 0, 1);
	}
#endif
}

/* ---------- MAIN ---------- */

int main(int argc, char *argv[])
{
	machineInit();
	uinterface = new UI(ted8360);

#ifndef __EMSCRIPTEN__
	inipath = SDL_GetPrefPath("Gaia", "yapeSDL");
	if (inipath) {
		fprintf(stderr, "Home directory is %s\n", inipath);
	} else {
		inipath = SDL_GetBasePath();
		fprintf(stderr, "No home directory, using application directory: %s\n", inipath);
	}

	inifile = new char[strlen(inipath) + 16];
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
	} else
		fprintf(stderr,"Error loading settings or no .ini file present...\n");
#endif

	app_initialise();
	if (!g_SoundOn || !g_50Hz) sound_pause();
	KEYS::initPcJoys();
	machine->Reset();
	/* ---------- Command line parameters ---------- */
	if (argv[1]!='\0') {
		printf("Parameter 1 :%s\n", argv[1]);
		// and then try to load the parameter as file
		autostart_file(argv[1]);
	}
	/* ---------- MAIN LOOP ---------- */
#ifdef __EMSCRIPTEN__
	printf("Type DIRECTORY (or LOAD\"$\",8 and then LIST) and press ENTER to see disk contents. Type LOAD\"filename*\",8 to load a specific file!\n");
	printf("Or enter the menu by pressing ESC and shift+ENTER to autostart games from there.\n"); 
	printf("Commodore +4 games start with uppercase, C64 ones with lowercase.\n");
	printf("Usage:\n");
	printf("LALT + 1-3   : set window size\n");
	printf("LALT + I     : switch emulated joystick port\n");
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
	printf("Joystick buttons are the arrow keys and SPACE\n");
	setMainLoop(1);
#else
	ad_vsync_init();
	for (;;) {
		mainLoop();
	}
#endif
	return 0;
}
