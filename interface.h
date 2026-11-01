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

#include <string>
#include <vector>
#include <memory>
#include "archdep.h"

class TED;
struct UiMenu;
union SDL_Event;

// Text shown as the value of a setting, and the action taken when it is
// activated. They know nothing about SDL so any front end can use them.
std::string uiSettingText(const rvar_t *rv);
void uiSettingActivate(rvar_t *rv);

class UI {

	public:
		UI(class TED *ted);
		~UI();

		static unsigned char asc2pet(char c);
		static unsigned char* stringToPETSCII(unsigned char* asciiStr, unsigned int count, bool useCaps = false);

		int enterMenu();
		void drawMenu();
		int	 wait_events();
		void setNewMachine(TED *newTed) {
			ted8360 = newTed;
			display = ted8360->screen;
		};

	private:
		enum Command {
			CMD_NONE,
			CMD_UP,
			CMD_DOWN,
			CMD_PAGE_UP,
			CMD_PAGE_DOWN,
			CMD_HOME,
			CMD_END,
			CMD_BACK,
			CMD_ENTER,
			CMD_ENTER_AUTORUN,
			CMD_CLOSE
		};
		enum BrowserKind {
			BK_PRG,
			BK_TAP,
			BK_DISK,
			BK_SNAPSHOT,
			BK_ROM
		};

		// screen
		unsigned char *display; // pointer to the emulator screen
		std::vector<unsigned int> pixels;
		unsigned char bgcol; // background color
		unsigned char fgcol; // foreground color
		void screen_update();
		void clear(char color, char shade);
		void set_color(unsigned char foreground, unsigned char background);
		void glyphtoscreen(int x, int y, const unsigned char *rows);
		void texttoscreen(int x, int y, const std::string &text);
		void render();
		void renderTitle(const UiMenu &menu);
		void renderRows(const UiMenu &menu);
		static std::string shortName(const std::string &name);

		// menu model
		TED *ted8360;
		std::vector<std::unique_ptr<UiMenu> > menus;
		std::vector<UiMenu*> nav; // navigation stack, back() is the visible menu
		UiMenu *bankMenu;
		std::string storedPath;
		unsigned int axisState;
		UiMenu *newMenu(const char *title);
		UiMenu *newBrowser(const char *title, BrowserKind kind);
		void buildMenus();
		void enter(UiMenu *menu);
		void back();

		// browsing
		void refreshBrowser(UiMenu &menu);
		void changeDirectory(const std::string &name, bool isDrive);
		static bool acceptsExtension(BrowserKind kind, bool ted264, const std::string &ext);
		bool openFile(BrowserKind kind, const std::string &name, bool autorun);
		void openD64Item(const char *name, bool autostart);
		void populateBankMenu();
		bool loadBank(unsigned int index);

		// input
		Command translate(const SDL_Event &event);
		bool execute(Command cmd); // true when the menu has to be left
		bool activate(bool autorun);
};

extern void interfaceLoop(void *arg);
extern void snapshotSave();

#endif // _INTERFACE_H
