/*
	YAPE - Yet Another Plus/4 Emulator

	The program emulates the Commodore 264 family of 8 bit microcomputers

	This program is free software, you are welcome to distribute it,
	and/or modify it under certain conditions. For more information,
	read 'Copying'.

	(c) 2000, 2001, 2005 Attila Grósz
*/

#ifndef _INTERFACE_H
#define _INTERFACE_H

#include "archdep.h"

struct menu_t;

enum UI_MenuClass {
	UI_MENUITEM_MENULINK = 0,
	UI_MENUITEM_SEPARATOR,
	UI_PRG_ITEM,
	UI_TAP_ITEM,
	UI_D64_ITEM,
	UI_DIR_ITEM,
	UI_DRIVE_ITEM,
	UI_T64_ITEM,
	UI_FRE_ITEM,
	UI_FILE_LOAD_PRG,
	UI_TAPE_ATTACH_TAP,
	UI_TAPE_CREATE_TAP,
	UI_TAPE_DETACH_TAP,
	UI_TAPE_PLAY,
	UI_TAPE_RECORD,
	UI_TAPE_STOP,
	UI_TAPE_REWIND,
	UI_DRIVE_ATTACH_IMAGE,
	UI_DRIVE_DETACH_IMAGE,
	UI_DRIVE_SET_DIR,
	//UI_TAPE_CONTROLS,
	UI_EMULATOR_OPTIONS,
	UI_SNAPSHOT_LOAD,
	UI_SNAPSHOT_QUICKSAVE,
	UI_EMULATOR_MONITOR,
	UI_EMULATOR_RESUME,
	UI_EMULATOR_EXIT
};

struct element_t {
	char name[MAX_PATH];
	struct menu_t *child;
	UI_MenuClass menufunction;
};

struct menu_t {
	char title[512];
	char subtitle[512];
	struct element_t element[1000];
	struct menu_t *parent;
	int nr_of_elements;
	int curr_sel;
	int uppermost;
	int curr_line;
};

class MEM;

class UI {

	private:
		unsigned char *display; // pointer to the emulator screen
		unsigned char *charset; // pointer to the character ROM

		char bgcol; // background color
		char fgcol; // foreground color

		void screen_update(void);

		void clear(char color, char shade);  // clears the screen with given C= 264 color and shade
		void texttoscreen(int x,int y, char *scrtxt, size_t len);
		void chrtoscreen(int x,int y, char scrchr);
		void set_color(unsigned char foreground, unsigned char background);
		void hide_sel_bar(struct menu_t *menu);
		void show_sel_bar(struct menu_t *menu);
		void show_title(struct menu_t * menu);
		void show_menu(struct menu_t *menu);
		void show_file_list(struct menu_t * menu, UI_MenuClass type);
		int	 wait_events();
		bool handle_menu_command(struct element_t *element);
		struct menu_t *curr_menu;
		TED *ted8360;
		void menuMoveUp();
		void menuMoveDown();
		int menuEnter(bool forceAutoRun);

	public:
		UI(class TED *ted);
		~UI();
		char pet2asc(char c);
		unsigned char asc2pet(char c);
		char *petstr2ascstr(char *string);
		int enterMenu();
		void setNewMachine(TED *newTed) {
            ted8360 = newTed;
            display = ted8360->screen;
        };
};

#endif // _INTERFACE_H
