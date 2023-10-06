/*
	YAPE - Yet Another Plus/4 Emulator

	The program emulates the Commodore 264 family of 8 bit microcomputers

	This program is free software, you are welcome to distribute it,
	and/or modify it under certain conditions. For more information,
	read 'Copying'.

	(c) 2000, 2001, 2005 Attila Grósz
*/

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "types.h"
#include "monitor.h"
#include "tedmem.h"
#include "interface.h"
#include "archdep.h"
#include "prg.h"
#include "tape.h"
#include "drive.h"
#include "roms.h"
#include "SaveState.h"

#ifndef SDL_CONTROLLERBUTTONDOWN // Emscripten
#define SDL_CONTROLLERBUTTONDOWN -1
#endif

extern bool autostart_file(const char *szFile, bool autostart = true);
extern bool start_file(const char *szFile, bool autostart = true);
extern bool openZipDisk(const char *fname, bool autostart);
extern void machineDoSomeFrames(unsigned int frames);
extern void machineEnable1551(bool enable);
extern bool machineIsTrueDriveEnabled(unsigned int dn);
extern void machineReset(unsigned int resetlevel);
extern void setMainLoop(int looptype);

#define COLOR(COL, SHADE) ((SHADE<<4)|COL|0x80)
#define MAX_LINES 25
#define MAX_INDEX (MAX_LINES - 1)
#define LISTOFFSET (ted8360->getCyclesPerRow() != 456 ? 114 : 52)

// TODO this is ugly and kludgy like hell but I don't have time for better
static menu_t main_menu = {
	"Main menu",
	"",
	{
		{"Load PRG file...", 0, UI_FILE_LOAD_PRG },
		{"Attach disk image...", 0, UI_DRIVE_ATTACH_IMAGE },
		{"Detach disk image", 0, UI_DRIVE_DETACH_IMAGE },
		{ "             ", 0, UI_MENUITEM_SEPARATOR },
		{"Load emulator snapshot...", 0, UI_FILE_LOAD_FRE },
		{"Save emulator snapshot", 0, UI_FILE_SAVE_FRE },
		{ "             ", 0, UI_MENUITEM_SEPARATOR },
		{ "Attach rom...", 0, UI_CRT_ATTACH },
		{ "Detach rom...", 0, UI_CRT_DETACH },
		{ "             ", 0, UI_MENUITEM_SEPARATOR },
		{"Tape controls...", 0, UI_MENUITEM_MENULINK },
		{"             ", 0, UI_MENUITEM_SEPARATOR },
		{"Options..."	, 0, UI_MENUITEM_MENULINK },
		{"             ", 0, UI_MENUITEM_SEPARATOR },
		{"Enter monitor", 0, UI_EMULATOR_MONITOR },
		{"Resume emulation"	, 0, UI_EMULATOR_RESUME },
		{ "             ", 0, UI_MENUITEM_SEPARATOR },
		{"Reset emulator", 0, UI_EMULATOR_RESET },
		{ "             ", 0, UI_MENUITEM_SEPARATOR },
		{"Quit emulation", 0, UI_EMULATOR_EXIT }
	},
	0,
	20,
	0,
	0,
	0
};

static menu_t tape_menu = {
	"Tape control menu",
	"",
	{
		{"Attach TAP image...", 0, UI_TAPE_ATTACH_TAP },
		{"             ", 0, UI_MENUITEM_SEPARATOR },
		{"Create TAP image", 0, UI_TAPE_CREATE_TAP },
		{"             ", 0, UI_MENUITEM_SEPARATOR },
		{"Detach TAP image", 0, UI_TAPE_DETACH_TAP },
		{"             ", 0, UI_MENUITEM_SEPARATOR },
		{"Press PLAY"	, 0, UI_TAPE_PLAY },
		{"Press RECORD"	, 0, UI_TAPE_RECORD },
		{"Press STOP"	, 0, UI_TAPE_STOP },
		{"Rewind tape"	, 0, UI_TAPE_REWIND }
	},
	0,
	10,
	0,
	0,
	0
};

static menu_t snapshot_menu = {
	"Emulator snapshot menu",
	"",
	{
		{"Load snapshot...", 0, UI_SNAPSHOT_LOAD },
		{"Save quicksnapshot...", 0, UI_SNAPSHOT_QUICKSAVE }
	},
	0,
	2,
	0,
	0,
	0
};

static menu_t options_menu = {
	"Options menu",
	"",
	{
		{"Dummy", 0, UI_MENUITEM_MENULINK },
	},
	0,
	5,
	0,
	0,
	0
};

static menu_t bank_selection_menu = {
	"CBM 264 ROM bank selection menu",
	"",
	{
		{"", 0, UI_FILE_LOAD_C0LO },
		{"", 0, UI_FILE_LOAD_C0HI },
		{"", 0, UI_FILE_LOAD_C1LO },
		{"", 0, UI_FILE_LOAD_C1HI },
		{"", 0, UI_FILE_LOAD_C2LO },
		{"", 0, UI_FILE_LOAD_C2HI },
		{"", 0, UI_FILE_LOAD_C3LO },
		{"", 0, UI_FILE_LOAD_C3HI }
	},
	0,	8,	0,	0,	0
};

static menu_t file_menu;
static menu_t tap_list;
static menu_t d64_list;
static menu_t fre_list;
static menu_t crt_list;
static menu_t bank_list;
static unsigned int *pixels;
extern void frameUpdate(unsigned char *src, unsigned int *target);

static rvar_t *findRVar(char *name)
{
	rvar_t **rv = settings;
	while (*rv) {
		rvar_t *item = *rv++;
		while (item && item->value) {
			if (!strcmp(name, item->menuName))
				return item;
			item++;
		}
	}
	return NULL;
}

UI::UI(class TED *ted) :
	display(ted->screen), charset(kernal + 0x1400), ted8360(ted)
{
	pixels = new unsigned int[568 * 312 * 2];
	// Link menu structure
	main_menu.element[0].child = &file_menu;
	main_menu.element[1].child = &d64_list;
	main_menu.element[4].child = &fre_list;// snapshot_menu;
	main_menu.element[7].child = &crt_list;
	main_menu.element[10].child = &tape_menu;
	main_menu.element[12].child = &options_menu;
	tape_menu.element[0].child = &tap_list;

	curr_menu = &main_menu;
	strcpy(file_menu.title, "PRG file selection menu");
	strcpy(tape_menu.title, "Tape controls menu");
	strcpy(tap_list.title, "TAP image selection menu");
	strcpy(d64_list.title, "Disk image selection menu");
	strcpy(fre_list.title, "Emulator savestate selection menu");
	strcpy(crt_list.title, "Cartridge/ROM selection menu");
	file_menu.parent = curr_menu;
	options_menu.parent = curr_menu;
	tape_menu.parent = curr_menu;
	tap_list.parent = &tape_menu;
	d64_list.parent = curr_menu;
	//snapshot_menu.parent = curr_menu;
	fre_list.parent = curr_menu;
	crt_list.parent = curr_menu;
	bank_list.parent = curr_menu;
	bank_selection_menu.parent = &crt_list;

	// initialize settings menu
	unsigned int i = 0, j = 0;
	rvar_t **rv = settings;
	while (rv[i]) {
		rvar_t *item = rv[i++];
		while (item && item->value) { // FIXME
			strcpy(options_menu.element[j].name, item->menuName);
			options_menu.element[j].menufunction = UI_MENUITEM_CALLBACKS;
			options_menu.element[j].type = (UI_MenuTypes) item->type;
			item++;
			j++;
		}
	}
	options_menu.nr_of_elements = j;
}

void UI::screen_update()
{
	const int offset[] = { 8, 8, -68, -4 };
	const unsigned int ix = ted8360->getEmulationLevel();
	const unsigned int cyclesPerRow = ted8360->getCyclesPerRow();

	frameUpdate(display + (cyclesPerRow - offset[ix] - 384) / 2 + 10 * cyclesPerRow, pixels);
}

void UI::clear(char color, char shade)
{
	memset(display, COLOR(color, shade), 568 * SCR_VSIZE);
	screen_update();
}

void UI::set_color(unsigned char foreground, unsigned char background)
{
	bgcol = background;
	fgcol = foreground;
}

void UI::texttoscreen(int x, int y, char *scrtxt, size_t len = 0)
{
	int i = 0;

	if (!len) len = strlen(scrtxt);
	while (scrtxt[i] != 0 && len--) {
		chrtoscreen(x + i*8, y, asc2pet(scrtxt[i]));
		i++;
	}
}

void UI::chrtoscreen(int x,int y, char scrchr)
{
	int j, k;
	unsigned char *cset;
	const unsigned int CPR = ted8360->getCyclesPerRow();

	cset = charset + (scrchr << 3);
	for (j = 0; j<8; j++)
		for (k = 0; k < 8; k++)
			display[(y + j) * CPR + x + k] = (cset[j] & (0x80 >> k)) ? fgcol : bgcol;
}

void UI::hide_sel_bar(menu_t *menu)
{
	if ( menu->element[ menu->curr_sel ].menufunction == UI_DIR_ITEM)
		set_color( COLOR(2,1), COLOR(1,5) );
	else
		set_color( COLOR(0,0), COLOR(1,5) );
	texttoscreen(LISTOFFSET, 64 + (curr_menu->curr_line<<3),
		menu->element[ curr_menu->curr_sel ].name);
	screen_update();
}

void UI::show_sel_bar(menu_t * menu)
{
	set_color( COLOR(0,0), COLOR(15,7) );
	texttoscreen(LISTOFFSET, 64 + (curr_menu->curr_line<<3),
		menu->element[ curr_menu->curr_sel ].name);
	screen_update();
}

void UI::show_settings_list(menu_t * menu, rvar_t *rvItems)
{
	int nf = 0;
	menu->nr_of_elements = 0;
	rvar_t *rvPtr = *settings;

	while (rvPtr) {
		strcpy(menu->element[nf].name, rvPtr->menuName);
		menu->element[nf].child = 0;
		menu->element[nf].type = MENU_CALLBACK;
		nf++;
		rvPtr++;
	}
	menu->nr_of_elements = nf;
}

void UI::populateBankSelectionMenu()
{
	for (unsigned int i = 0; i < 8; i++) {
		char txt[MAX_PATH + 16];
		char *currbankTxt = (i & 1) ? TED::romhighpath[i >> 1] : TED::romlopath[i >> 1];
		size_t currbankTxtLen = strlen(currbankTxt);
		char *cbTxtPtr = currbankTxt + MAX(int(currbankTxtLen) - 30, 0);
		size_t cbTxtLen = MIN(strlen(currbankTxt), 30);
		cbTxtPtr[cbTxtLen] = '\0';
		sprintf(txt, "BANK#%u %s : %s", i >> 1, (i & 1) ? "HI" : "LO", cbTxtPtr);
		strcpy(bank_selection_menu.element[i].name, txt);
	}
}

void UI::show_file_list(menu_t * menu, UI_MenuClass type)
{
	int nf = 0, res;
	char cfilename[512];
	char *fileFoundName;
	UI_FileTypes ft = FT_NONE;

	menu->nr_of_elements = 0;
	strcpy( menu->element[nf++].name, "..");
	menu->element[0].menufunction = UI_DIR_ITEM;

	res = ad_find_first_file("*");
	while (res) {
		fileFoundName = ad_return_current_filename();
		if (fileFoundName && strcmp(fileFoundName, ".") && strcmp(fileFoundName, "..")) {
			ft = ad_return_current_filetype();
			// fprintf(stderr,"Parsed: %s\n", ad_return_current_filename());
			if (ft != FT_FILE && nf < sizeof(menu->element)/sizeof(menu->element[0])) {
				strcpy(menu->element[nf].name, ad_return_current_filename());
				menu->element[nf].menufunction = (ft == FT_DIR ? UI_DIR_ITEM : UI_DRIVE_ITEM);
				++nf;
			}
		}
		res = ad_find_next_file();
	}
	ad_find_file_close();
	// populate file list
	if (ft != FT_VOLUME) {

		unsigned int nrOfExts = 1;
		element_t ftypes[10];

		switch ( type ) {
			case UI_T64_ITEM:
			case UI_PRG_ITEM:
				nrOfExts = 3;
				strcpy(ftypes[0].name, "*.prg");
				strcpy(ftypes[1].name, "*.P00");
				strcpy(ftypes[2].name, "*.T64");
				ftypes[0].menufunction = UI_PRG_ITEM;
				ftypes[1].menufunction = UI_PRG_ITEM;
				ftypes[2].menufunction = UI_T64_ITEM;
				break;
			case UI_TAP_ITEM:
				strcpy(ftypes[0].name, "*.tap");
				strcpy(ftypes[1].name, "*.wav");
				ftypes[0].menufunction = UI_TAP_ITEM;
				ftypes[1].menufunction = UI_TAP_ITEM;
				nrOfExts = 2;
				break;
			case UI_D64_ITEM:
			case UI_ZIP_ITEM:
				strcpy(ftypes[0].name, "*.d64");
				ftypes[0].menufunction = UI_D64_ITEM;
				strcpy(ftypes[1].name, "*.zip");
				ftypes[1].menufunction = UI_ZIP_ITEM;
#ifdef _WIN32
				nrOfExts = 2;
#else
				nrOfExts = 3;
				strcpy(ftypes[2].name, "*.D64");
				ftypes[2].menufunction = UI_D64_ITEM;
#endif
				break;
			case UI_FRE_ITEM:
				strcpy(ftypes[0].name, "*.yss");
				ftypes[0].menufunction = UI_FRE_ITEM;
				break;
			case UI_CRT_ITEM:
				strcpy(ftypes[0].name, "*.crt");
				strcpy(ftypes[1].name, "*.rom");
				strcpy(ftypes[2].name, "*.bin");
				strcpy(ftypes[3].name, "*.prg");
				ftypes[0].menufunction = 
				ftypes[1].menufunction =
				ftypes[2].menufunction = 
				ftypes[3].menufunction = UI_CRT_ITEM;
				nrOfExts = ted8360->getEmulationLevel() == 3 ? 4 : 3;
				break;
			case UI_DRIVE_SET_DIR:
				menu->nr_of_elements = nf;
			default:
				return;
		}
		//
		unsigned int k = nrOfExts - 1;
		do {
			strcpy(cfilename, ftypes[k].name);
			type = ftypes[k].menufunction;
			res = ad_find_first_file(cfilename);
			while(res) {
				strcpy(menu->element[nf].name, ad_return_current_filename());
				menu->element[nf].menufunction = type;
				++nf;
				res = ad_find_next_file();
			}
			ad_find_file_close();
		} while (k--);
		//
	}
	menu->nr_of_elements = nf;
}

void UI::openD64Item(const char *name)
{
	machineEnable1551(false);
	if (CTrueDrive::GetRoot()) {
		CTrueDrive::SwapDisk(name);
		const Uint8 *state = SDL_GetKeyboardState(NULL);
		if (state[SDL_SCANCODE_LSHIFT] || state[SDL_SCANCODE_RSHIFT]) {
			autostart_file(name);
		}
	}
}

bool UI::handle_menu_command( struct element_t *element)
{
	int menuSelection;
	UI_MenuClass ptype = UI_MENUITEM_MENULINK;

	// FIXME this must be nicer
	if (!curr_menu->parent) {
		menuSelection = element->menufunction;
	} else {
		ptype = curr_menu->parent->element[ curr_menu->parent->curr_sel ].menufunction;
		menuSelection = element->menufunction;
	}

	switch ( menuSelection ) {

		case UI_MENUITEM_MENULINK:
			return false;

		case UI_MENUITEM_CALLBACKS:
			{	// find rvar and use callback
				rvar_t *rv = findRVar(element->name);
				if (rv) {
					switch (rv->type) {
						case RVAR_TOGGLE:
							// if callback specified, call it otherwise toggle ourselves
							if (rv->callback)
								(rv->callback)(NULL);
							else {
								int *value = ((int*)(rv->value));
								*value = !*value;
							}
							break;

						case RVAR_HEX:
						case RVAR_INT:
							if (rv->callback)
								(rv->callback)(NULL);
							break;

						case RVAR_STRING_FLIPLIST:
							if (rv->callback) {
								char text[64];
								(rv->callback)(text);
							}
							break;

						default:;
					}
				}
			}
			return false;

		case UI_DRIVE_ITEM:
			ad_exit_drive_selector();
		case UI_DIR_ITEM:
			if (ad_set_curr_dir(element->name)) {
				ad_get_curr_dir( (char*)&(curr_menu->subtitle) );
				if ( ptype == UI_FILE_LOAD_PRG )
					show_file_list( &file_menu, UI_PRG_ITEM );
				else if ( ptype == UI_TAPE_ATTACH_TAP )
					show_file_list( &tap_list, UI_TAP_ITEM );
				else if ( ptype == UI_DRIVE_ATTACH_IMAGE )
					show_file_list( &d64_list, UI_D64_ITEM );
				else if (ptype == UI_FILE_LOAD_FRE)
					show_file_list(&fre_list, UI_FRE_ITEM);
				else if (ptype == UI_CRT_ATTACH)
					show_file_list(&crt_list, UI_CRT_ITEM);
				curr_menu->curr_sel = 0;
				curr_menu->curr_line = 0;
				curr_menu->uppermost = 0;
			}
			/*if ( curr_menu->curr_sel >= file_menu.nr_of_elements )
				curr_menu->curr_sel = file_menu.nr_of_elements - 1;*/
			return false;

		case UI_T64_ITEM:
		case UI_PRG_ITEM:
			{
				const Uint8 *state = SDL_GetKeyboardState(NULL);
				if (state[SDL_SCANCODE_LSHIFT] || state[SDL_SCANCODE_RSHIFT] || autoStartNext) {
					autostart_file(element->name);
				} else {
					if (menuSelection != UI_T64_ITEM)
						PrgLoad(element->name, 0, ted8360);
					else
						prgLoadFromT64(element->name, 0, ted8360);
				}
#if (SDL_VERSION_ATLEAST(2,24,0))
				SDL_ResetKeyboard();
#endif
			}
			clear (0, 0);
			break;

		case UI_TAP_ITEM:
			{
				const Uint8 *state = SDL_GetKeyboardState(NULL);
				if (state[SDL_SCANCODE_LSHIFT] || state[SDL_SCANCODE_RSHIFT]) {
					autostart_file(element->name);
				} else 
					ted8360->tap->attachTape(element->name);
			}
			break;
		case UI_D64_ITEM:
			openD64Item(element->name);
			break;
		case UI_ZIP_ITEM:
			start_file(element->name, true);
			break;
		case UI_CRT_ITEM:
			if (ted8360->getEmulationLevel() < 2) {
				curr_menu = &bank_selection_menu;
				ad_get_curr_dir(storedPath);
				strcat(storedPath, PATH_SEP);
				strcat(storedPath, element->name);
				populateBankSelectionMenu();
				return false;
			}
			ted8360->loadromfromfile(1, element->name, 0);
			break;
		case UI_FILE_LOAD_C0LO:
		case UI_FILE_LOAD_C0HI:
		case UI_FILE_LOAD_C1LO:
		case UI_FILE_LOAD_C1HI:
		case UI_FILE_LOAD_C2LO:
		case UI_FILE_LOAD_C2HI:
		case UI_FILE_LOAD_C3LO:
		case UI_FILE_LOAD_C3HI:
			{
				unsigned int ix = menuSelection - UI_FILE_LOAD_C0LO;
				unsigned int rbank = ix >> 1;
				unsigned int roffset = (ix & 1) ? 0x4000 : 0;
				ted8360->loadromfromfile(rbank, storedPath, roffset);
				machineReset(1);
				curr_menu = bank_selection_menu.parent;
			}
			break;

		case UI_FRE_ITEM:
			fprintf(stderr, "Loading emulator state from %s.\n", element->name);
			SaveState::openSnapshot(element->name, false);
			//clear(0, 0);
			break;

		case UI_TAPE_DETACH_TAP:
			ted8360->tap->detachTape();
			break;
		case UI_TAPE_REWIND:
			ted8360->tap->rewind();
			break;
		case UI_TAPE_STOP:
			ted8360->tap->pressTapeButton(ted8360->GetClockCount(), 0);
			break;
		case UI_TAPE_PLAY:
			ted8360->tap->pressTapeButton(ted8360->GetClockCount(), 1);
			break;
		case UI_FILE_LOAD_PRG:
			show_file_list( &file_menu, UI_PRG_ITEM );
			return false;
		case UI_TAPE_ATTACH_TAP:
			show_file_list( &tap_list, UI_TAP_ITEM );
			return false;
		case UI_CRT_ATTACH:
			show_file_list(&crt_list, UI_CRT_ITEM);
			return false;
		case UI_CRT_DETACH:
			if (ted8360->getEmulationLevel() < 2) {
				curr_menu = &bank_selection_menu;
				bank_selection_menu.parent = &main_menu;
				strcpy(storedPath, "*");
				populateBankSelectionMenu();
				return false;
			}
			ted8360->loadromfromfile(1, "", 0);
			ted8360->Reset();
			break;
		case UI_FILE_LOAD_FRE:
			show_file_list(&fre_list, UI_FRE_ITEM);
			return false;
		case UI_DRIVE_ATTACH_IMAGE:
			show_file_list( &d64_list, UI_D64_ITEM );
			//ted8360->HookTCBM(0);
			return false;
		case UI_DRIVE_DETACH_IMAGE:
			if (CTrueDrive::GetRoot()) {
				CTrueDrive *d = CTrueDrive::GetRoot();
				d->DetachDisk();
			}
			machineEnable1551(true);
			return true;
		case UI_SNAPSHOT_LOAD:
			show_file_list( &snapshot_menu, UI_FRE_ITEM );
			return false;
		case UI_FILE_SAVE_FRE:
			snapshotSave();
			return true;
		case UI_EMULATOR_MONITOR:
			monitorEnter(ted8360->cpuptr);
			return true;
		case UI_EMULATOR_RESUME:
			void ();
			return true;
		case UI_EMULATOR_RESET:
			machineReset(1);
			return true;
		case UI_EMULATOR_EXIT:
			exit(1);
			break;
		default:
			break;
	}
	return true;
}

void UI::show_title(menu_t * menu)
{
	const unsigned int CPR = ted8360->getCanvasX();

	size_t titlen = strlen( menu->title ) << 3;
	set_color( COLOR(8,7), COLOR(7,0) );
	texttoscreen( (CPR - int(titlen))/2, 24, menu->title);

	titlen = strlen( menu->subtitle );
	set_color( COLOR(8,0), COLOR(1,5) );
	int chunks = (int) titlen / (CPR / 8) + 1;
	int startline = 40 - (chunks / 3) * 8;
	char *subtitle = menu->subtitle;
	do {
		const int MAXLEN = 46;//55;
		int chunklen = (int) (titlen > MAXLEN ? MAXLEN : titlen);
		texttoscreen(CPR / 2 - int(chunklen) * 4, startline,
			petstr2ascstr(subtitle), chunklen);
		titlen -= MAXLEN;
		subtitle += chunklen;
		startline += 8;
	} while (chunks--);

	char helptxt[] = "Arrows: navigate, ENTER: select, ESC: resume";
	texttoscreen( (CPR - int(strlen(helptxt)*8)) / 2 - 8, 312 - 32, petstr2ascstr(helptxt));
	set_color( fgcol, bgcol );
}

void UI::show_menu(menu_t * menu)
{
	int i;
	int shown = menu->nr_of_elements - menu->uppermost;

	show_title(menu);
	if (shown > 0) {
		if (shown > MAX_LINES)
			shown = MAX_LINES + 1;

		for (i = 2; i < 16; i++) {
			const char c = ' ';
			const unsigned int offset = LISTOFFSET + 32 + i * 16;
			bgcol = COLOR(i, 3);
			chrtoscreen(offset, 312 - 24, c);
			chrtoscreen(offset + 8, 312 - 24, c);
		}

		set_color( COLOR(0,0), COLOR(1,5) );
		for (i = 0; i < shown; ++i) {
			unsigned int ix = menu->uppermost + i;
			element_t &elem = menu->element[ix];

			if (elem.menufunction == UI_DIR_ITEM)
				set_color( COLOR(2,1), COLOR(1,5) );
			else
				set_color( COLOR(0,0), COLOR(1,5) );
			// display menu list element
			texttoscreen(LISTOFFSET, 64+(i<<3), elem.name);

			// if interactive menu, find out what to do
			if (elem.menufunction == UI_MENUITEM_CALLBACKS) {
				char valuetxt[256] = "";
				unsigned int valuePos = LISTOFFSET + (43 << 3);
				unsigned int valueTxtLen;
				
				rvar_t *rv = findRVar(elem.name);
				unsigned int type = (RvarTypes) elem.type;
				switch (type) {
					case RVAR_TOGGLE:
						strcpy(valuetxt, *((unsigned int*)(rv->value)) ? "ON" : "OFF");
						break;

					case RVAR_INT:
						sprintf(valuetxt, "%i", *((int*)(rv->value)));
						break;

					case RVAR_HEX:
						sprintf(valuetxt, "$%04X", *((unsigned int*)(rv->value)));
						break;

					case RVAR_STRING_FLIPLIST:
						if (rv->label)
							sprintf(valuetxt, "%s", rv->label());
						break;

					default:;
				}
				valueTxtLen = (unsigned int) (strlen(valuetxt) << 3);
				set_color(COLOR(2,1), COLOR(1,5));
				texttoscreen(valuePos - valueTxtLen, 64 + (i << 3), valuetxt);
			}
		}
	}
	screen_update();
}

void UI::drawMenu()
{
	ad_get_curr_dir(tap_list.subtitle);
	ad_get_curr_dir(file_menu.subtitle);
	ad_get_curr_dir(d64_list.subtitle);
	ad_get_curr_dir(fre_list.subtitle);
	ad_get_curr_dir(crt_list.subtitle);

	clear (1, 5);
	show_menu( curr_menu);
	show_sel_bar( curr_menu );
}

int UI::enterMenu()
{
	int retval;

	drawMenu();
	retval = wait_events();

	clear (0, 0);
	return retval;
}

void UI::menuMove(int direction)
{
	if ((direction == -1 && curr_menu->curr_sel > 0) ||
		(direction == +1 && curr_menu->curr_sel < curr_menu->nr_of_elements - 1))
	{
		int step = 0;
		const int limit = direction == -1 ? 0 : MAX_LINES;
		hide_sel_bar( curr_menu);
		// Step and skip menu separators
		do {
			step++;
		} while (curr_menu->element[curr_menu->curr_sel + step * direction].menufunction
			== UI_MENUITEM_SEPARATOR);
		if (curr_menu->curr_line == limit) {
			curr_menu->uppermost += direction;
			clear (1, 5);
			show_menu( curr_menu );
		} else
			curr_menu->curr_line += step * direction;
		curr_menu->curr_sel += step * direction;
		show_sel_bar( curr_menu);
	}
}

void UI::menuMoveLeft()
{
	if (curr_menu->parent) {
		curr_menu = curr_menu->parent;
		clear(1, 5);
		show_menu(curr_menu);
		show_sel_bar(curr_menu);
	}
}

int UI::menuEnter(bool forceAutoRun = false)
{
	element_t *elem;
	autoStartNext = forceAutoRun;

	if ( curr_menu->element[ curr_menu->curr_sel ].child ) {
		elem = &(curr_menu->element[ curr_menu->curr_sel ]);
		curr_menu = elem->child;
	} else {
		elem = &(curr_menu->element[ curr_menu->curr_sel ]);
	}
	// returns zero if we have to get back to the main loop, 1 otherwise
	if ( handle_menu_command( elem ) ) {
		clear (0, 0);
		return 0;
	} else {
		clear (1, 5);
		show_menu( curr_menu );
		show_sel_bar( curr_menu);
	}
	return 1;
}

int UI::wait_events()
{
	SDL_Event event;

#ifdef __EMSCRIPTEN__
	while (SDL_PollEvent(&event)) {
#else
	while (SDL_WaitEvent(&event)) {
#endif

		switch (event.type) {

			case SDL_WINDOWEVENT:
				if (event.window.event == SDL_WINDOWEVENT_RESIZED
					|| event.window.event == SDL_WINDOWEVENT_RESTORED) {
					screen_update();
				}
				break;

			case SDL_CONTROLLERAXISMOTION:
				{
					//printf("controller: %u, axis: %u/%i was pressed!\n", event.caxis.which, event.caxis.axis, event.caxis.value);
					unsigned int axis = (event.caxis.axis & 1) << 1;
					unsigned int pressed = abs(event.caxis.value) > 8192 ? 1 : 0;
					unsigned int dir = (event.caxis.value > 0);
					static unsigned int previousState = 0;

					if (pressed && (!previousState || abs(event.caxis.value) >= 32767)) {
						switch (axis | dir) {
						default:
							break;
						case 0:
							menuMoveLeft();
							break;
						case 1:
							if (!menuEnter(true))
								return 1;
							break;
						case 2:
							menuMove(-1);
							break;
						case 3:
							menuMove(+1);
							break;
						}
					}
					previousState = pressed;
				}
				break;

			case (Uint32)SDL_CONTROLLERBUTTONUP:
				//printf("controller: %u, button: %u was pressed!\n", event.cbutton.which, event.cbutton.button);
				switch (event.cbutton.button) {
				default:
					break;
				case SDL_CONTROLLER_BUTTON_DPAD_UP:
					menuMove(-1);
					break;
				case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
					menuMove(+1);
					break;
				case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
					menuMoveLeft();
					break;

				case SDL_CONTROLLER_BUTTON_A:
				case SDL_CONTROLLER_BUTTON_START:
				case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
				case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
					if (!menuEnter(true))
						return 1;
					break;
				}
				break;

			case SDL_KEYDOWN:
				//printf("key: %u, button was pressed!\n", event.key.keysym.scancode);
				switch (event.key.keysym.sym) {
					case SDLK_ESCAPE :
					case SDLK_F8:
					case SDLK_DELETE:
						clear (0, 0);
						return 1;

					case SDLK_HOME:
						hide_sel_bar( curr_menu);
						clear (1, 5);
						curr_menu->curr_sel = 0;
						curr_menu->curr_line = 0;
						curr_menu->uppermost = 0;
						show_menu( curr_menu );
						show_sel_bar( curr_menu);
						break;
					case SDLK_END:
						break;
					case SDLK_UP:
						menuMove(-1);
						break;
					case SDLK_DOWN:
						menuMove(+1);
						break;
					case SDLK_PAGEUP:
						hide_sel_bar( curr_menu);
						if (curr_menu->curr_line != 0 ) {
							curr_menu->curr_sel = curr_menu->uppermost;
							curr_menu->curr_line = 0;
						} else {
							curr_menu->curr_sel -= MAX_LINES;
							if ( curr_menu->curr_sel < 0 )
								curr_menu->curr_sel = 0;
							curr_menu->uppermost = curr_menu->curr_sel;
							clear (1, 5);
							show_menu( curr_menu );
						}
						show_sel_bar( curr_menu);
						break;
					case SDLK_PAGEDOWN:
						hide_sel_bar( curr_menu );
						if (curr_menu->curr_line != MAX_LINES ) {
							int nr_el = curr_menu->nr_of_elements - 1;
							curr_menu->curr_sel = curr_menu->curr_line =
								nr_el < MAX_LINES ? nr_el : MAX_LINES;
						} else {
							int nr_el = curr_menu->nr_of_elements - 1;
							curr_menu->curr_sel += MAX_LINES;
							if ( curr_menu->curr_sel > nr_el )
								curr_menu->curr_sel = nr_el;
							curr_menu->uppermost = curr_menu->curr_sel - MAX_LINES;
							clear (1, 5);
							show_menu( curr_menu );
						}
						show_sel_bar( curr_menu);
						break;
					case SDLK_LEFT:
						menuMoveLeft();
						break;
					case SDLK_RIGHT:
					case SDLK_RETURN:
						// return 1 if we get back to the main loop
						if (!menuEnter())
							return 1;
						break;
					default :
						break;
				};
				break;
			case SDL_QUIT:
				//return 2;
				exit(0);
		}
	}
	return 0;
}

char UI::pet2asc(char c)
{
	if (c == '\\')
		return char(0x4D|0x80);
	if (c == 0x5F)
		return char(0x6F|0x80);
	/*if ((c >= 'A') && (c <= 'Z') || (c >= 'a') && (c <= 'z'))
		return c ^ 0x20;*/
	/*if ((c >= 0xc1) && (c <= 0xda))
		return c ^ 0x80;*/

	return c;
}

char *UI::petstr2ascstr(char *string)
{
	char *p = string;

	while (*p) {
		*p = pet2asc( *p );
		p++;
	}
	return string;
}

unsigned char UI::asc2pet(char c)
{
	if ((c >= 'A') && (c <= 'Z'))
		return c ;
	if ((c >= 'a') && (c <= 'z'))
		return c - 96;
	if (c == '\\')
		return 0xCE;
	if (c == '/') 
		return 0xCD;
	if (c == '_')
		return 0x64;
	/*if ((c >= 0xc1) && (c <= 0xda))
		return c ^ 0x80;*/
	return c;
}

unsigned char* UI::stringToPETSCII(unsigned char* asciiStr, unsigned int count, bool useCaps)
{
	unsigned char* origPtr = asciiStr;
	while (asciiStr && *asciiStr && count--) {
		unsigned char c = *asciiStr;
		if ((c == 0x0A) || (c == 0x0D))
			c = 0x0D;
		else if (c == 0x09)
			c = 32;
		else if (c >= 0x00 && c <= 0x1A)
			c ^= 0x00;
		else if (c >= 'A' && c <= 'Z')
			c += useCaps ? 0x20 : 0;
		else if (c >= 'a' && c <= 'z')
			c -= 0x20;
		*asciiStr = c;
		asciiStr++;
	}
	return origPtr;
}

UI::~UI()
{
	delete []pixels;
}

void interfaceLoop(void *arg)
{
	UI *ui = (UI*) arg;

	if (ui->wait_events()) {
		setMainLoop(1);
	}
}
