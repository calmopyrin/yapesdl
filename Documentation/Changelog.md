Changes in v0.70.2
==================

- tape loading in WAV format and MTAP support in C64 mode added
- preliminary CRT support for C64 mode
- hunt command and repeating the last command on ENTER implemented in monitor
- CIA fixes
- VIC2 sprite fixes
- TED sound fixes & optimizations

Changes in v0.70.1
==================

- serial IEC implemented to complement the Commodore plus/4's parallel IEC
- much improved VIC-II emulation (lightpen latches, sprite crunching, DMA)
- improved SID emulation & replay (6581R4 default for C64, 8580 for plus/4)
- minor CIA changes
- ported and compiled to Javascript using Emscripten
- savestates support
- implemented a simple options menu at long last
- video speed increased via 'SDL_SetRenderTarget'
- use 'SDL_GameController' API rather than 'SDL_Joystick'
- more consistent positional keymapping
- simple overlay keyboard for hints
- more jostick keyset options
- preliminary ZIP file support

Changes in v0.58.2
==================

- Commodore 64 emulation! Complete with sprites, drive and SID of course
- some CPU fixes ported over from Yape for Windows
- sound replay improvements
- P00, T64 format support
- fast line based +4 emulation mode added for lower end platforms (toggle with Alt+E)
- able to select among physical drives (Windows only of course)
- many bugfixes

Changes in v0.58.1
==================

- true 1541 drive emulation and R/W D64 support added
- fixed a few GCC warnings
- bugfixes

Changes in v0.36.2
==================

Small update

- Linux compilation fix
- updated the Makefile
- primitive STDIO based external monitor & disassembler added (Alt+M)
- bugfixes

Changes in v0.36.1
==================

Even more years passed. This time a larger than average update.

- ported to SDL 2.0
- SID card support added
- simpler, better sound replay
- simple CRT emulation
- minor UI changes and improvements
- minor TED improvements

Changes in v0.32.5
==================

Again many years passed. This version IS a major update in terms of emulation accuracy
as it is based on the code tree of the Windows version from around 2005. Due to lack of
spare time it has not been fully synced with it ever since but is still capable of much
more than the previous open source version. It also implements a user interface (though
it is not very advanced). The almost full list of changes is:

- "new" TED engine, backported and refactored from the Windows version
- sound much better
- faster overall emulation
- disk emulation via the file system enhanced
- user interface (available via the F8 key)

Changes in v0.32.4
==================

After all these years, a new SDL release, nothing fancy, mostly SDL related fixes.
It's still the very old 0.32 codebase, not that of the Windows version. It has been
updated merely out of fun.

- renamed to YapeSDL
- DevC++ workspace file included
- a few keyboard fixes (ALT based shortcuts work again)
- better sound rendering
- better CPU management
- various fixes here and there

Changes in v0.32.3
==================

- noise channel fixed
- the FPS counter now reports values below 50 FPS correctly
- 50 Hz synchronization is also fixed

Changes in v0.32.2
==================

- sound is implemented! (yet a bit clumsier than in the Windows version)
- configuration settings now saved to user's home directory
- some other bugs with the configuration file fixed
- some keyboard mapping problems solved (cursor keys are now always usable)
- save screenshots by pressing F7
- many new bugs
- can't remember more, it's been too long since last version :)

0.32.1
======

- first public release of the SDL version of Yape
