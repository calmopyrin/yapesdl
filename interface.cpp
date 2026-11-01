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
#include <ctype.h>
#include <algorithm>
#include <functional>
#include "types.h"
#include "monitor.h"
#include "tedmem.h"
#include "interface.h"
#include "archdep.h"
#include "prg.h"
#include "tape.h"
#include "drive.h"
#include "SaveState.h"

extern bool autostart_file(const char *szFile, bool autostart = true);
extern bool start_file(const char *szFile, bool autostart = true);
extern bool openZipDisk(const char *fname, bool autostart);
extern void machineDoSomeFrames(unsigned int frames);
extern void machineEnable1551(bool enable);
extern bool machineIsTrueDriveEnabled(unsigned int dn);
extern void machineReset(unsigned int resetlevel);
extern void setMainLoop(int looptype);
extern void frameUpdate(unsigned char *src, unsigned int *target);

#define COLOR(COL, SHADE) ((SHADE<<4)|COL|0x80)

static const int VISIBLE_ROWS = 26;   // list lines that fit on the screen
static const int LIST_TOP = 64;       // pixel row of the first list line
static const int MAX_NAME_CHARS = 40; // longer names are shortened in the middle
static const int VALUE_COLUMN = 43;   // right edge of setting values, in characters
static const int SUBTITLE_WIDTH = 46; // characters per subtitle line

//-----------------------------------------------------------------------------
// Menu model
//-----------------------------------------------------------------------------

struct UiMenu;

// An action returns true when the menu has to be left afterwards. Its
// argument is true when the user asked for the "autostart" variant.
typedef std::function<bool(bool)> UiAction;

struct UiItem {
	enum Kind { SEPARATOR, SUBMENU, ACTION, SETTING, DIR, FILE_ITEM };

	Kind kind;
	std::string label;
	UiMenu *submenu;   // SUBMENU
	rvar_t *setting;   // SETTING
	bool drive;        // DIR: entry of the drive list
	UiAction action;   // ACTION, DIR, FILE_ITEM

	UiItem(Kind k = ACTION, const std::string &text = std::string())
		: kind(k), label(text), submenu(0), setting(0), drive(false) {}
};

// A list with a cursor. All cursor movement lives here, so every menu
// (including the file browsers) scrolls and pages the same way.
struct UiMenu {
	std::string title;
	std::string subtitle;
	std::vector<UiItem> items;
	int sel;   // index of the selected item
	int top;   // index of the first visible item
	bool isBrowser;
	int browserKind;

	UiMenu() : sel(0), top(0), isBrowser(false), browserKind(0) {}

	int count() const { return (int) items.size(); }

	// Selects the nearest selectable item to 'index', looking in direction
	// 'dir' first, and scrolls it into view.
	void select(int index, int dir)
	{
		const int n = count();
		if (n == 0) {
			sel = top = 0;
			return;
		}
		index = MAX(0, MIN(index, n - 1));
		int i = index;
		while (i >= 0 && i < n && items[i].kind == UiItem::SEPARATOR)
			i += dir;
		if (i < 0 || i >= n) {
			i = index;
			while (i >= 0 && i < n && items[i].kind == UiItem::SEPARATOR)
				i -= dir;
			if (i < 0 || i >= n)
				i = index;
		}
		sel = i;
		ensureVisible();
	}

	void ensureVisible()
	{
		const int n = count();
		if (sel < top)
			top = sel;
		if (sel >= top + VISIBLE_ROWS)
			top = sel - VISIBLE_ROWS + 1;
		top = MAX(0, MIN(top, n - VISIBLE_ROWS));
	}

	void moveBy(int delta) { select(sel + delta, delta < 0 ? -1 : +1); }
	void home() { select(0, +1); }
	void end() { select(count() - 1, -1); }

	// First press moves to the edge of the visible page, the next one
	// scrolls by a whole page.
	void pageBy(int dir)
	{
		const int before = sel;
		const int page = VISIBLE_ROWS - 1;
		if (dir < 0) {
			select(top, +1);
			if (sel == before)
				select(sel - page, -1);
		} else {
			select(top + page, -1);
			if (sel == before)
				select(sel + page, +1);
		}
	}
};

static void addSeparator(UiMenu &menu)
{
	menu.items.push_back(UiItem(UiItem::SEPARATOR));
}

static void addSubmenu(UiMenu &menu, const char *label, UiMenu *submenu)
{
	UiItem item(UiItem::SUBMENU, label);
	item.submenu = submenu;
	menu.items.push_back(item);
}

static void addAction(UiMenu &menu, const char *label, const UiAction &action)
{
	UiItem item(UiItem::ACTION, label);
	item.action = action;
	menu.items.push_back(item);
}

//-----------------------------------------------------------------------------
// Settings
//-----------------------------------------------------------------------------

std::string uiSettingText(const rvar_t *rv)
{
	char buf[32];

	switch (rv->type) {
		case RVAR_TOGGLE:
			return *((const unsigned int*)(rv->value)) ? "ON" : "OFF";
		case RVAR_INT:
			snprintf(buf, sizeof(buf), "%i", *((const int*)(rv->value)));
			return buf;
		case RVAR_HEX:
			snprintf(buf, sizeof(buf), "$%04X", *((const unsigned int*)(rv->value)));
			return buf;
		case RVAR_STRING_FLIPLIST:
			if (rv->label) {
				const char *text = rv->label();
				if (text)
					return text;
			}
			break;
		default:;
	}
	return std::string();
}

void uiSettingActivate(rvar_t *rv)
{
	switch (rv->type) {
		case RVAR_TOGGLE:
			// if a callback is specified call it, otherwise toggle the value
			if (rv->callback)
				(rv->callback)(NULL);
			else {
				int *value = (int*)(rv->value);
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

//-----------------------------------------------------------------------------
// Helpers
//-----------------------------------------------------------------------------

static std::string toLower(std::string text)
{
	for (size_t i = 0; i < text.size(); i++)
		text[i] = (char) tolower((unsigned char) text[i]);
	return text;
}

static std::string extensionOf(const std::string &name)
{
	const size_t dot = name.rfind('.');
	return dot == std::string::npos ? std::string() : toLower(name.substr(dot + 1));
}

static bool itemLess(const UiItem &a, const UiItem &b)
{
	return toLower(a.label) < toLower(b.label);
}

static bool shiftHeld()
{
	const Uint8 *state = SDL_GetKeyboardState(NULL);
	return state[SDL_SCANCODE_LSHIFT] || state[SDL_SCANCODE_RSHIFT];
}

static std::string currentDirectory()
{
	char path[512] = "";
	ad_get_curr_dir(path);
	path[sizeof(path) - 1] = '\0';
	return path;
}

//-----------------------------------------------------------------------------
// Construction
//-----------------------------------------------------------------------------

UI::UI(class TED *ted) :
	display(ted->screen), pixels(568 * 312 * 2), bgcol(0), fgcol(0),
	ted8360(ted), bankMenu(0), axisState(0)
{
	buildMenus();
}

UI::~UI()
{
}

UiMenu *UI::newMenu(const char *title)
{
	menus.push_back(std::unique_ptr<UiMenu>(new UiMenu));
	menus.back()->title = title;
	return menus.back().get();
}

UiMenu *UI::newBrowser(const char *title, BrowserKind kind)
{
	UiMenu *menu = newMenu(title);
	menu->isBrowser = true;
	menu->browserKind = kind;
	return menu;
}

void UI::buildMenus()
{
	UiMenu *mainMenu = newMenu("Main menu");
	UiMenu *tapeMenu = newMenu("Tape controls menu");
	UiMenu *optionsMenu = newMenu("Options menu");
	UiMenu *prgBrowser = newBrowser("PRG file selection menu", BK_PRG);
	UiMenu *tapBrowser = newBrowser("TAP image selection menu", BK_TAP);
	UiMenu *diskBrowser = newBrowser("Disk image selection menu", BK_DISK);
	UiMenu *snapshotBrowser = newBrowser("Emulator savestate selection menu", BK_SNAPSHOT);
	UiMenu *romBrowser = newBrowser("Cartridge/ROM selection menu", BK_ROM);
	bankMenu = newMenu("CBM 264 ROM bank selection menu");

	// main menu
	addSubmenu(*mainMenu, "Load PRG file...", prgBrowser);
	addSubmenu(*mainMenu, "Attach disk image...", diskBrowser);
	addAction(*mainMenu, "Detach disk image", [this](bool) {
		if (CTrueDrive::GetRoot())
			CTrueDrive::GetRoot()->DetachDisk();
		machineEnable1551(true);
		return true;
	});
	addSeparator(*mainMenu);
	addSubmenu(*mainMenu, "Load emulator snapshot...", snapshotBrowser);
	addAction(*mainMenu, "Save emulator snapshot", [](bool) {
		snapshotSave();
		return true;
	});
	addSeparator(*mainMenu);
	addSubmenu(*mainMenu, "Attach rom...", romBrowser);
	addAction(*mainMenu, "Detach rom...", [this](bool) -> bool {
		if (ted8360->getEmulationLevel() < 2) {
			storedPath = "*";
			populateBankMenu();
			enter(bankMenu);
			return false;
		}
		ted8360->loadromfromfile(1, "", 0);
		ted8360->Reset();
		return true;
	});
	addSeparator(*mainMenu);
	addSubmenu(*mainMenu, "Tape controls...", tapeMenu);
	addSeparator(*mainMenu);
	addSubmenu(*mainMenu, "Options...", optionsMenu);
	addSeparator(*mainMenu);
	addAction(*mainMenu, "Enter monitor", [this](bool) {
		monitorEnter(ted8360->getCpuPtr());
		return true;
	});
	addAction(*mainMenu, "Resume emulation", [](bool) { return true; });
	addSeparator(*mainMenu);
	addAction(*mainMenu, "Reset emulator", [](bool) {
		machineReset(1);
		return true;
	});
	addSeparator(*mainMenu);
	addAction(*mainMenu, "Quit emulation", [](bool) -> bool {
		exit(1);
	});

	// tape menu
	addSubmenu(*tapeMenu, "Attach TAP image...", tapBrowser);
	addSeparator(*tapeMenu);
	addAction(*tapeMenu, "Create TAP image", [](bool) { return false; });
	addSeparator(*tapeMenu);
	addAction(*tapeMenu, "Detach TAP image", [this](bool) {
		ted8360->tap->detachTape();
		return true;
	});
	addSeparator(*tapeMenu);
	addAction(*tapeMenu, "Press PLAY", [this](bool) {
		ted8360->tap->pressTapeButton(ted8360->GetClockCount(), 1);
		return true;
	});
	addAction(*tapeMenu, "Press RECORD", [](bool) { return false; });
	addAction(*tapeMenu, "Press STOP", [this](bool) {
		ted8360->tap->pressTapeButton(ted8360->GetClockCount(), 0);
		return true;
	});
	addAction(*tapeMenu, "Rewind tape", [this](bool) {
		ted8360->tap->rewind();
		return true;
	});

	// options menu: one entry per emulator setting
	for (rvar_t **group = settings; *group; ++group) {
		for (rvar_t *rv = *group; rv->value; ++rv) {
			UiItem item(UiItem::SETTING, rv->menuName);
			item.setting = rv;
			optionsMenu->items.push_back(item);
		}
	}

	nav.push_back(mainMenu);
	mainMenu->select(0, +1);
}

void UI::enter(UiMenu *menu)
{
	nav.push_back(menu);
	if (menu->isBrowser)
		refreshBrowser(*menu);
	else
		menu->select(menu->sel, +1);
}

void UI::back()
{
	if (nav.size() > 1)
		nav.pop_back();
}

//-----------------------------------------------------------------------------
// Drawing
//-----------------------------------------------------------------------------

void UI::screen_update()
{
	const int offset[] = { 8, 8, -68, -4 };
	const unsigned int ix = ted8360->getEmulationLevel();
	const unsigned int cyclesPerRow = ted8360->getCyclesPerRow();

	frameUpdate(display + (cyclesPerRow - offset[ix] - 384) / 2 + 10 * cyclesPerRow, &pixels[0]);
}

void UI::clear(char color, char shade)
{
	memset(display, COLOR(color, shade), 568 * SCR_VSIZE);
}

void UI::set_color(unsigned char foreground, unsigned char background)
{
	bgcol = background;
	fgcol = foreground;
}

// Draws one 8x8 cell from eight bitmap rows.
void UI::glyphtoscreen(int x, int y, const unsigned char *rows)
{
	const unsigned int CPR = ted8360->getCyclesPerRow();

	for (int j = 0; j < 8; j++)
		for (int k = 0; k < 8; k++)
			display[(y + j) * CPR + x + k] = (rows[j] & (0x80 >> k)) ? fgcol : bgcol;
}

// The text uses the character ROM of the running machine. The backslash has
// no glyph in it, so it is drawn from a small bitmap of its own.
void UI::texttoscreen(int x, int y, const std::string &text)
{
	static const unsigned char backslash[8] = { 0xC0, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x00 };
	const unsigned char *cset = ted8360->getCharSetPtr();

	for (size_t i = 0; i < text.size(); i++) {
		const int cx = x + (int) i * 8;
		if (text[i] == '\\')
			glyphtoscreen(cx, y, backslash);
		else
			glyphtoscreen(cx, y, cset + (asc2pet(text[i]) << 3));
	}
}

std::string UI::shortName(const std::string &name)
{
	if ((int) name.size() > MAX_NAME_CHARS)
		return name.substr(0, 20) + "..." + name.substr(name.size() - 20);
	return name;
}

void UI::renderTitle(const UiMenu &menu)
{
	const int canvas = (int) ted8360->getCanvasX();

	set_color(COLOR(8,7), COLOR(7,0));
	texttoscreen((canvas - (int) menu.title.size() * 8) / 2, 24, menu.title);

	// the subtitle (current directory) gets at most three lines, keeping its tail
	set_color(COLOR(8,0), COLOR(1,5));
	std::string subtitle = menu.subtitle;
	const size_t maxChars = SUBTITLE_WIDTH * 3;
	if (subtitle.size() > maxChars)
		subtitle = "..." + subtitle.substr(subtitle.size() - (maxChars - 3));
	int y = 40;
	for (size_t pos = 0; pos < subtitle.size(); pos += SUBTITLE_WIDTH, y += 8) {
		const std::string line = subtitle.substr(pos, SUBTITLE_WIDTH);
		texttoscreen(canvas / 2 - (int) line.size() * 4, y, line);
	}

	const std::string help = "Arrows: navigate, ENTER: select, ESC: resume";
	texttoscreen((canvas - (int) help.size() * 8) / 2 - 8, SCR_VSIZE - 32, help);
}

void UI::renderRows(const UiMenu &menu)
{
	const int listOffset = ted8360->getCyclesPerRow() != 456 ? 114 : 52;

	for (int row = 0; row < VISIBLE_ROWS; row++) {
		const int ix = menu.top + row;
		if (ix >= menu.count())
			break;
		const UiItem &item = menu.items[ix];
		if (item.kind == UiItem::SEPARATOR)
			continue;

		if (ix == menu.sel)
			set_color(COLOR(0,0), COLOR(15,7));
		else if (item.kind == UiItem::DIR)
			set_color(COLOR(2,1), COLOR(1,5));
		else
			set_color(COLOR(0,0), COLOR(1,5));
		const int y = LIST_TOP + (row << 3);
		texttoscreen(listOffset, y, shortName(item.label));

		if (item.kind == UiItem::SETTING) {
			const std::string value = uiSettingText(item.setting);
			set_color(COLOR(2,1), COLOR(1,5));
			texttoscreen(listOffset + (VALUE_COLUMN << 3) - (int) value.size() * 8, y, value);
		}
	}
}

void UI::render()
{
	static const unsigned char blank[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	const UiMenu &menu = *nav.back();
	const int listOffset = ted8360->getCyclesPerRow() != 456 ? 114 : 52;

	clear(1, 5);
	renderTitle(menu);

	// color palette strip
	for (int i = 2; i < 16; i++) {
		const int x = listOffset + 32 + i * 16;
		set_color(0, COLOR(i, 3));
		glyphtoscreen(x, SCR_VSIZE - 24, blank);
		glyphtoscreen(x + 8, SCR_VSIZE - 24, blank);
	}

	renderRows(menu);
	screen_update();
}

void UI::drawMenu()
{
	UiMenu &menu = *nav.back();
	if (menu.isBrowser)
		refreshBrowser(menu);
	render();
}

//-----------------------------------------------------------------------------
// File browsing
//-----------------------------------------------------------------------------

bool UI::acceptsExtension(BrowserKind kind, bool ted264, const std::string &ext)
{
	switch (kind) {
		case BK_PRG:
			return ext == "prg" || ext == "p00" || ext == "t64";
		case BK_TAP:
			return ext == "tap" || ext == "wav";
		case BK_DISK:
			return ext == "d64" || ext == "g64" || ext == "zip";
		case BK_SNAPSHOT:
			return ext == "yss";
		case BK_ROM:
			return ext == "crt" || ext == "rom" || ext == "bin" || (ted264 && ext == "prg");
	}
	return false;
}

void UI::refreshBrowser(UiMenu &menu)
{
	const BrowserKind kind = (BrowserKind) menu.browserKind;
	const bool ted264 = ted8360->getEmulationLevel() == 3;
	std::vector<UiItem> dirs, files;
	bool drives = false;

	menu.subtitle = currentDirectory();

	int found = ad_find_first_file("*");
	while (found) {
		const char *found_name = ad_return_current_filename();
		if (found_name && strcmp(found_name, ".") && strcmp(found_name, "..")) {
			const std::string name = found_name;
			const UI_FileTypes type = ad_return_current_filetype();

			if (type == FT_DIR || type == FT_VOLUME) {
				const bool isDrive = type == FT_VOLUME;
				UiItem item(UiItem::DIR, name);
				item.drive = isDrive;
				item.action = [this, name, isDrive](bool) {
					changeDirectory(name, isDrive);
					return false;
				};
				dirs.push_back(item);
				drives = drives || isDrive;
			} else if (type == FT_FILE && acceptsExtension(kind, ted264, extensionOf(name))) {
				UiItem item(UiItem::FILE_ITEM, name);
				item.action = [this, kind, name](bool autorun) {
					return openFile(kind, name, autorun);
				};
				files.push_back(item);
			}
		}
		found = ad_find_next_file();
	}
	ad_find_file_close();

	if (!drives)
		std::sort(dirs.begin(), dirs.end(), itemLess);
	std::sort(files.begin(), files.end(), itemLess);

	menu.items.clear();
	UiItem parent(UiItem::DIR, "..");
	parent.action = [this](bool) {
		changeDirectory("..", false);
		return false;
	};
	menu.items.push_back(parent);
	menu.items.insert(menu.items.end(), dirs.begin(), dirs.end());
	if (!drives)
		menu.items.insert(menu.items.end(), files.begin(), files.end());

	menu.select(menu.sel, +1);
}

void UI::changeDirectory(const std::string &name, bool isDrive)
{
	char path[2 * MAX_PATH];

	if (isDrive)
		ad_exit_drive_selector();
	snprintf(path, sizeof(path), "%s", name.c_str());
	if (ad_set_curr_dir(path)) {
		UiMenu &menu = *nav.back();
		menu.sel = menu.top = 0;
		refreshBrowser(menu);
	}
}

void UI::openD64Item(const char *name, bool autostart)
{
	machineEnable1551(false);
	if (CTrueDrive::GetRoot()) {
		if (shiftHeld() || autostart) {
			autostart_file(name);
		} else
			CTrueDrive::SwapDisk(name);
	}
}

bool UI::openFile(BrowserKind kind, const std::string &name, bool autorun)
{
	const char *file = name.c_str();
	const bool autostart = autorun || shiftHeld();
	const std::string ext = extensionOf(name);

	switch (kind) {
		case BK_PRG:
			if (autostart)
				autostart_file(file);
			else if (ext == "t64")
				prgLoadFromT64(file, 0, ted8360);
			else
				PrgLoad(file, 0, ted8360);
#if (SDL_VERSION_ATLEAST(2,24,0))
			SDL_ResetKeyboard();
#endif
			return true;

		case BK_TAP:
			if (autostart)
				autostart_file(file);
			else
				ted8360->tap->attachTape(file);
			return true;

		case BK_DISK:
			if (ext == "zip")
				start_file(file, true);
			else
				openD64Item(file, autorun);
			return true;

		case BK_SNAPSHOT:
			fprintf(stderr, "Loading emulator state from %s.\n", file);
			SaveState::openSnapshot(file, false);
			return true;

		case BK_ROM:
			if (ted8360->getEmulationLevel() < 2) {
				// the ROM can go to any of the banks
				storedPath = currentDirectory() + PATH_SEP + name;
				populateBankMenu();
				enter(bankMenu);
				return false;
			}
			ted8360->loadromfromfile(1, file, 0);
			return true;
	}
	return true;
}

void UI::populateBankMenu()
{
	bankMenu->items.clear();
	for (unsigned int i = 0; i < 8; i++) {
		std::string path = (i & 1) ? TED::romhighpath[i >> 1] : TED::romlopath[i >> 1];
		if (path.size() > 28)
			path = ".." + path.substr(path.size() - 26);
		char prefix[32];
		snprintf(prefix, sizeof(prefix), "BANK#%u %s : ", i >> 1, (i & 1) ? "HI" : "LO");
		UiItem item(UiItem::ACTION, prefix + path);
		item.action = [this, i](bool) { return loadBank(i); };
		bankMenu->items.push_back(item);
	}
	bankMenu->select(bankMenu->sel, +1);
}

bool UI::loadBank(unsigned int index)
{
	const unsigned int bank = index >> 1;
	const unsigned int offset = (index & 1) ? 0x4000 : 0;

	ted8360->loadromfromfile(bank, storedPath.c_str(), offset);
	machineReset(1);
	back(); // leave the bank list
	return true;
}

//-----------------------------------------------------------------------------
// Input
//-----------------------------------------------------------------------------

UI::Command UI::translate(const SDL_Event &event)
{
	switch (event.type) {

		case SDL_MOUSEWHEEL:
			{
				int y = event.wheel.y;
				if (event.wheel.direction != SDL_MOUSEWHEEL_FLIPPED)
					y = -y;
				return y < 0 ? CMD_UP : (y > 0 ? CMD_DOWN : CMD_NONE);
			}

		case SDL_MOUSEBUTTONDOWN:
			if (event.button.button == SDL_BUTTON_RIGHT)
				return CMD_CLOSE;
			if (event.button.button == SDL_BUTTON_X1)
				return CMD_BACK;
			return event.button.button == SDL_BUTTON_MIDDLE ? CMD_ENTER_AUTORUN : CMD_ENTER;

		case SDL_CONTROLLERAXISMOTION:
			{
				// only the two sticks navigate, a trigger must not
				if (event.caxis.axis > SDL_CONTROLLER_AXIS_RIGHTY)
					return CMD_NONE;
				const unsigned int axis = (event.caxis.axis & 1) << 1;
				const unsigned int pressed = abs(event.caxis.value) > 8192 ? 1 : 0;
				const unsigned int dir = (event.caxis.value > 0);
				const bool fire = pressed && (!axisState || abs(event.caxis.value) >= 32767);

				axisState = pressed;
				if (fire) {
					switch (axis | dir) {
						case 0: return CMD_BACK;
						case 1: return CMD_ENTER_AUTORUN;
						case 2: return CMD_UP;
						case 3: return CMD_DOWN;
					}
				}
				return CMD_NONE;
			}

		case (Uint32)SDL_CONTROLLERBUTTONUP:
			switch (event.cbutton.button) {
				case SDL_CONTROLLER_BUTTON_DPAD_UP:
					return CMD_UP;
				case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
					return CMD_DOWN;
				case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
					return CMD_BACK;
				case SDL_CONTROLLER_BUTTON_A:
				case SDL_CONTROLLER_BUTTON_START:
				case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
				case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
					return CMD_ENTER_AUTORUN;
				default:
					return CMD_NONE;
			}

		case SDL_KEYDOWN:
			switch (event.key.keysym.sym) {
				case SDLK_ESCAPE:
				case SDLK_F8:
				case SDLK_DELETE:
					return CMD_CLOSE;
				case SDLK_HOME:
					return CMD_HOME;
				case SDLK_END:
					return CMD_END;
				case SDLK_UP:
					return CMD_UP;
				case SDLK_DOWN:
					return CMD_DOWN;
				case SDLK_PAGEUP:
					return CMD_PAGE_UP;
				case SDLK_PAGEDOWN:
					return CMD_PAGE_DOWN;
				case SDLK_LEFT:
					return CMD_BACK;
				case SDLK_RIGHT:
				case SDLK_RETURN:
				case SDLK_KP_ENTER:
					return CMD_ENTER;
				default:
					return CMD_NONE;
			}
	}
	return CMD_NONE;
}

bool UI::activate(bool autorun)
{
	UiMenu &menu = *nav.back();
	if (menu.items.empty())
		return false;

	const UiItem &item = menu.items[menu.sel];
	switch (item.kind) {
		case UiItem::SEPARATOR:
			return false;
		case UiItem::SUBMENU:
			enter(item.submenu);
			return false;
		case UiItem::SETTING:
			uiSettingActivate(item.setting);
			return false;
		default:
			break;
	}
	// the action may rebuild the list it came from, so run a copy
	const UiAction action = item.action;
	return action ? action(autorun) : false;
}

bool UI::execute(Command cmd)
{
	UiMenu &menu = *nav.back();

	switch (cmd) {
		case CMD_UP:
			menu.moveBy(-1);
			break;
		case CMD_DOWN:
			menu.moveBy(+1);
			break;
		case CMD_PAGE_UP:
			menu.pageBy(-1);
			break;
		case CMD_PAGE_DOWN:
			menu.pageBy(+1);
			break;
		case CMD_HOME:
			menu.home();
			break;
		case CMD_END:
			menu.end();
			break;
		case CMD_BACK:
			back();
			break;
		case CMD_ENTER:
			return activate(false);
		case CMD_ENTER_AUTORUN:
			return activate(true);
		case CMD_CLOSE:
			return true;
		default:
			break;
	}
	return false;
}

int UI::enterMenu()
{
	drawMenu();
	const int retval = wait_events();

	clear(0, 0);
	return retval;
}

// Returns 1 when the menu has been left, 0 when there are no more events.
int UI::wait_events()
{
	SDL_Event event;

#ifdef __EMSCRIPTEN__
	while (SDL_PollEvent(&event)) {
#else
	while (SDL_WaitEvent(&event)) {
#endif
		if (event.type == SDL_QUIT)
			exit(0);

		if (event.type == SDL_WINDOWEVENT) {
			if (event.window.event == SDL_WINDOWEVENT_RESIZED
				|| event.window.event == SDL_WINDOWEVENT_RESTORED)
				screen_update();
			continue;
		}

		const Command cmd = translate(event);
		if (cmd == CMD_NONE)
			continue;
		if (execute(cmd)) {
			clear(0, 0);
			return 1;
		}
		render();
	}
	return 0;
}

//-----------------------------------------------------------------------------
// Character conversion
//-----------------------------------------------------------------------------

// Index of the glyph for an ASCII character in the machine's character ROM.
unsigned char UI::asc2pet(char c)
{
	const unsigned char u = (unsigned char) c;

	if (u >= 'a' && u <= 'z')
		return u - 96;
	if (u == '_')
		return 0x64;
	if (u < 0x20 || u >= 0x7F)
		return '?';
	return u;
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

void interfaceLoop(void *arg)
{
	UI *ui = (UI*) arg;

	if (ui->wait_events()) {
		setMainLoop(1);
	}
}
