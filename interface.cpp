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
#ifdef _WIN32
#include <SDL/SDL.h>
#else
#include <SDL2/SDL.h>
#endif
#include "monitor.h"
#include "tedmem.h"
#include "interface.h"
#include "archdep.h"
#include "prg.h"
#include "tape.h"
#include "drive.h"
#include "roms.h"

extern bool autostart_file(char *szFile);
extern void machineDoSomeFrames(unsigned int frames);
extern void machineEnable1551(bool enable);

#define COLOR(COL, SHADE) ((SHADE<<4)|COL|0x80)
#define MAX_LINES 25
#define MAX_INDEX (MAX_LINES - 1)
#define LISTOFFSET (ted8360->getCyclesPerRow() == 456 ? 100 : 160)

// TODO this is ugly and kludgy like hell but I don't have time for better
static menu_t main_menu = {
	"Main menu",
	"",
	{
		{"Load PRG file...", 0, UI_FILE_LOAD_PRG },
		{"Attach disk image...", 0, UI_DRIVE_ATTACH_IMAGE },
		//{"Emulator snapshot...", 0, UI_MENUITEM_MENULINK },
		{"Detach disk image", 0, UI_DRIVE_DETACH_IMAGE },
		{"Tape controls...", 0, UI_MENUITEM_MENULINK },
		{"             ", 0, UI_MENUITEM_SEPARATOR },
		{"Options..."	, 0, UI_MENUITEM_MENULINK },
		{"             ", 0, UI_MENUITEM_SEPARATOR },
		{"Enter monitor"	, 0, UI_EMULATOR_MONITOR },
		{"Resume emulation"	, 0, UI_EMULATOR_RESUME },
		//{"             ", 0, UI_MENUITEM_SEPARATOR },
		{"Quit YAPE"	, 0, UI_EMULATOR_EXIT }
	},
	0,
	10,
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
		{"ROM settings", 0, UI_MENUITEM_MENULINK },
		{"             ", 0, UI_MENUITEM_SEPARATOR },
		{"RAM size     ", 0, UI_MENUITEM_MENULINK },
		{"             ", 0, UI_MENUITEM_SEPARATOR },
		{"Monitor settings"	, 0, UI_MENUITEM_MENULINK }
	},
	0,
	5,
	0,
	0,
	0
};

static menu_t file_menu;
static menu_t tap_list;
static menu_t d64_list;
static unsigned int pixels[512 * 312 * 2];
extern void frameUpdate(unsigned char *src, unsigned int *target);

UI::UI(class TED *ted) :
	display(ted->screen), charset(kernal + 0x1400), ted8360(ted)
{
	// Link menu structure
	main_menu.element[0].child = &file_menu;
	main_menu.element[1].child = &d64_list;
	//main_menu.element[2].child = &snapshot_menu;
	main_menu.element[3].child = &tape_menu;
	main_menu.element[5].child = &options_menu;
	tape_menu.element[0].child = &tap_list;

	curr_menu = &main_menu;
	strcpy( file_menu.title, "PRG file selection menu");
	strcpy( tape_menu.title, "Tape controls menu");
	strcpy( tap_list.title, "TAP image selection menu");
	strcpy( d64_list.title, "Disk image selection menu");
	strcpy( snapshot_menu.title, "Snapshot selection menu");
	file_menu.parent = curr_menu;
	options_menu.parent = curr_menu;
	tape_menu.parent = curr_menu;
	tap_list.parent = &tape_menu;
	d64_list.parent = curr_menu;
	snapshot_menu.parent = curr_menu;
}

void UI::screen_update()
{
	const unsigned int cyclesPerRow = ted8360->getCyclesPerRow();
	const int offset = cyclesPerRow == 504 ? -68 : 8;

	frameUpdate(display + (cyclesPerRow - offset - 384) / 2 + 10 * cyclesPerRow, pixels);
}

void UI::clear(char color, char shade)
{
	memset(display, COLOR(color, shade), 512 * SCR_VSIZE);
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

void UI::hide_sel_bar(struct menu_t *menu)
{
	if ( menu->element[ menu->curr_sel ].menufunction == UI_DIR_ITEM)
		set_color( COLOR(2,1), COLOR(1,5) );
	else
		set_color( COLOR(0,0), COLOR(1,5) );
	texttoscreen(LISTOFFSET, 64 + (curr_menu->curr_line<<3),
		menu->element[ curr_menu->curr_sel ].name);
	screen_update();
}

void UI::show_sel_bar(struct menu_t * menu)
{
	set_color( COLOR(0,0), COLOR(15,7) );
	texttoscreen(LISTOFFSET, 64 + (curr_menu->curr_line<<3),
		menu->element[ curr_menu->curr_sel ].name);
	screen_update();
}

void UI::show_file_list(struct menu_t * menu, UI_MenuClass type)
{
	int nf = 0, res;
	char cfilename[512];
	char *fileFoundName;
	UI_FileTypes ft;

	menu->nr_of_elements = 0;
	res = ad_find_first_file("*");
	fileFoundName = ad_return_current_filename();
	ft = ad_return_current_filetype();
	if (fileFoundName && !strcmp(fileFoundName, ".") )
		res = ad_find_next_file();
	if (!fileFoundName || strcmp(fileFoundName, "..") /*&& ft != UI_DRIVE_ITEM*/) {
		strcpy( menu->element[nf++].name, "..");
		menu->element[0].menufunction = UI_DIR_ITEM;
	}
	while (res) {
		ft = ad_return_current_filetype();
		// fprintf(stderr,"Parsed: %s\n", ad_return_current_filename());
		if (ft != FT_FILE) {
			strcpy(menu->element[nf].name, ad_return_current_filename());
			menu->element[nf].menufunction = (ft == FT_DIR ? UI_DIR_ITEM : UI_DRIVE_ITEM);
			++nf;
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
				ftypes[0].menufunction = UI_TAP_ITEM;
				break;
			case UI_D64_ITEM:
                strcpy(ftypes[0].name, "*.d64");
				ftypes[0].menufunction = UI_D64_ITEM;
#ifndef WIN32
                nrOfExts = 2;
				strcpy(ftypes[1].name, "*.D64");
                ftypes[1].menufunction = UI_D64_ITEM;
#endif
				break;
			case UI_FRE_ITEM:
				strcpy(ftypes[0].name, "*.fre");
				ftypes[0].menufunction = UI_FRE_ITEM;
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
				if (state[SDL_SCANCODE_LSHIFT] || state[SDL_SCANCODE_RSHIFT]) {
					autostart_file(element->name);
				} else {
				    if (menuSelection != UI_T64_ITEM)
                        PrgLoad(element->name, 0, ted8360);
                    else
                        prgLoadFromT64(element->name, 0, ted8360);
				}
			}
			clear (0, 0);
            break;

		case UI_TAP_ITEM:
			strcpy( ted8360->tap->tapefilename, element->name);
			ted8360->tap->attach_tap();
			break;
		case UI_D64_ITEM:
			machineEnable1551(false);
			if (CTrueDrive::GetRoot()) {
				CTrueDrive *d = CTrueDrive::GetRoot();
				d->DetachDisk();
				// important to note disk change
				machineDoSomeFrames(2);
				d->AttachDisk(element->name);
				// autostart?
				const Uint8 *state = SDL_GetKeyboardState(NULL);
				if (state[SDL_SCANCODE_LSHIFT] || state[SDL_SCANCODE_RSHIFT]) {
					autostart_file(element->name);
				}
			}
			break;
		case UI_FRE_ITEM:
			break;

		case UI_TAPE_DETACH_TAP:
			ted8360->tap->detach_tap();
			break;
		case UI_TAPE_REWIND:
			ted8360->tap->rewind();
			break;
		case UI_FILE_LOAD_PRG:
			show_file_list( &file_menu, UI_PRG_ITEM );
			return false;
		case UI_TAPE_ATTACH_TAP:
			show_file_list( &tap_list, UI_TAP_ITEM );
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
		case UI_MENUITEM_MENULINK:
			return false;
		case UI_EMULATOR_MONITOR:
			monitorEnter(ted8360->cpuptr);
			return true;
		case UI_EMULATOR_RESUME:
			void ();
			return true;
		case UI_EMULATOR_EXIT:
			exit(1);
			break;
		default:
			break;
	}
	return true;
}

void UI::show_title(struct menu_t * menu)
{
	const unsigned int CPR = ted8360->getCyclesPerRow() == 504 ? 576 : 456;

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

void UI::show_menu(struct menu_t * menu)
{
	int i;
	int shown = menu->nr_of_elements - menu->uppermost;

	show_title( menu );
	if ( shown > 0 ) {
		if ( shown > MAX_LINES ) shown = MAX_LINES + 1;

		set_color( COLOR(0,0), COLOR(1,5) );
		for ( i = 0; i < shown; ++i) {
			if ( menu->element[menu->uppermost + i].menufunction == UI_DIR_ITEM)
				set_color( COLOR(2,1), COLOR(1,5) );
			else
				set_color( COLOR(0,0), COLOR(1,5) );
			texttoscreen(LISTOFFSET, 64+(i<<3),menu->element[menu->uppermost + i].name);
		}
	}
	screen_update();
}

int UI::enterMenu()
{
	int retval;
	ad_get_curr_dir(tap_list.subtitle);
	ad_get_curr_dir(file_menu.subtitle);
	ad_get_curr_dir(d64_list.subtitle);

	clear (1, 5);
	show_menu( curr_menu);
	show_sel_bar( curr_menu );
	retval = wait_events();

	clear (0, 0);
	return retval;
}

void UI::menuMoveUp()
{
	if (curr_menu->curr_sel > 0) {
		int step = 0;
		hide_sel_bar( curr_menu);
		// Step and skip menu separators
		do {
			step++;
		} while (curr_menu->element[curr_menu->curr_sel - step].menufunction
			== UI_MENUITEM_SEPARATOR);
		if (curr_menu->curr_line == 0) {
			curr_menu->uppermost -= 1;
			clear (1, 5);
			show_menu( curr_menu );
		} else
			curr_menu->curr_line -= step;
		curr_menu->curr_sel -= step;
		show_sel_bar( curr_menu);
	}
}

void UI::menuMoveDown()
{
	if (curr_menu->curr_sel < curr_menu->nr_of_elements - 1) {
		int step = 0;
		hide_sel_bar( curr_menu);
		// Step and skip menu separators
		do {
			step++;
		} while (curr_menu->element[curr_menu->curr_sel + step].menufunction
			== UI_MENUITEM_SEPARATOR);
		if (curr_menu->curr_line==MAX_LINES) {
			curr_menu->uppermost += 1;
			clear (1, 5);
			show_menu( curr_menu );
		} else
			curr_menu->curr_line += step;
		curr_menu->curr_sel += step;
		show_sel_bar( curr_menu);
	}
}

int UI::menuEnter(bool forceAutoRun = false)
{
	element_t *elem;

	if ( curr_menu->element[ curr_menu->curr_sel ].child ) {
		elem = &(curr_menu->element[ curr_menu->curr_sel ]);
		curr_menu = elem->child;
	} else {
		elem = &(curr_menu->element[ curr_menu->curr_sel ]);
	}
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

	while (SDL_WaitEvent(&event)) {
		switch (event.type) {

			case SDL_WINDOWEVENT:
				if (event.window.event == SDL_WINDOWEVENT_RESIZED
					|| event.window.event == SDL_WINDOWEVENT_RESTORED) {
					screen_update();
				}
				break;

			case SDL_JOYAXISMOTION:
				//printf("joy: %u, button: %u was pressed!\n", event.jaxis.which, event.jaxis.axis);
				break;

			case SDL_CONTROLLERBUTTONDOWN:
			case SDL_JOYBUTTONDOWN:
				//printf("joy: %u, button: %u was pressed!\n", event.jbutton.which, event.jbutton.button);
				break;

			case SDL_JOYBUTTONUP:
				switch (event.jbutton.button) { // START button
					case 10:
						if (!menuEnter())
							return 1;
						break;
					case 14:
					case 5:
						return 0;
					case 0:
						menuMoveUp();
						break;
					case 1:
						menuMoveDown();
						break;
					default:
						//printf("joy: %u, button: %u was pressed!\n", event.jbutton.which, event.jbutton.button);
						break;
				}
				break;

	        case SDL_KEYDOWN:
				//printf("key: %u, button was pressed!\n", event.key.keysym.scancode);
				switch (event.key.keysym.sym) {
					case SDLK_ESCAPE :
						/*if ( curr_menu->parent ) {
							clear (1, 5);
							curr_menu = curr_menu->parent;
							show_menu( curr_menu );
							show_sel_bar( curr_menu );
							break;
						}*/
					case SDLK_F8:
						clear (0, 0);
						return 0;

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
						/*hide_sel_bar( curr_menu);
						clear (1, 5);
						curr_menu->curr_sel =
						curr_menu->uppermost = curr_menu->nr_of_elements - 1;
						curr_menu->curr_line =
							curr_menu->nr_of_elements >= MAX_LINES ?
								MAX_LINES - 1 : curr_menu->nr_of_elements;
						show_menu( curr_menu );
						show_sel_bar( curr_menu);*/
						break;
					case SDLK_UP:
						menuMoveUp();
						break;
					case SDLK_DOWN:
						menuMoveDown();
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
						if ( curr_menu->parent ) {
							curr_menu = curr_menu->parent;
							clear (1, 5);
							show_menu( curr_menu );
							show_sel_bar( curr_menu );
						}
						break;
					case SDLK_RIGHT:
					case SDLK_RETURN:
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
	return 1;
}

char UI::pet2asc(char c)
{
	if ((c == '\\'))
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
		return 0x4D;
	/*if ((c >= 0xc1) && (c <= 0xda))
		return c ^ 0x80;*/
	return c;
}

UI::~UI()
{
}
