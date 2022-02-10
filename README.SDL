README.SDL

This is the README for the SDL port of Yape/SDL (Yet Another Plus/4 Emulator)

Current version is 0.71.1

LEGAL
=====

  Yape for SDL is 
  (C) 2000,2001,2004,2007,2008,2015-2021 Attila Grósz (gyros@freemail.hu)
  http://yape.plus4.net

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of the
  License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not see <http://www.gnu.org/licenses/>.

  The ROM file headers in the package used to be 
  Copyright (C) by Commodore Business Machines.

COMPILE
=======

  The source code is hosted on GitHub.
  https://github.com/calmopyrin/yapesdl

  Define AUDIO_CALLBACK if you want to use the older audio API using
  callbacks (pre-2.0.4 versions of SDL2 only support this one).

  Define explicitly ZIP_SUPPORT in case you need preliminary ZIP file 
  support - only used for opening compressed D64 files for now.

  GNU
  ---
  
  To be able to compile on Linux, you must have the latest SDL
  package from http://libsdl.org. The recent stable release as of time of
  this writing is 2.0.16. You will also need the development libraries.
  
  The provided makefile is written for the GNU C++ compiler but can also
  be easily adjusted for other compilers.
  
  It is very likely that if SDL and GCC are supported on your system, then 
  Yape can be compiled and run. Hence, an Android port is also possible, 
  and Yape has been succesfully compiled on systems other than Windows and 
  Linux as well.
  
  Once the libraries and header files are installed, you should
  extract the source tarball to a directory, and simply type:
  
  make

  Alternatively, you can use the provided Code::Blocks project
  file to compile Yape.
  
  If the compilation finished with no error, you can type:
  
      ./yapesdl [PRG/P00/T64/TAP/D64/ZIP filename] [-c64]
  
  to start the emulator, where [] means optional arguments.

  Mac OS X
  --------

  Other than downloading the relevant SDL development libraries, you also 
  need to copy the SDL2.framework file to the /Library/Frameworks folder. 
  From then on, just use the provided Xcode workspace.

  Windows
  -------

  Make sure you install either the Visual Studio or the Mingw version
  of the SDL development library. You can use either compiler
  targeting 32 as well as 64-bit binary builds.

  It is recommended that you use the provided Microsoft Visual Studio 14 
  solution file. The Community 2015 and 2017 Editions are terrific and free 
  IDE's with an excellent debugger.
  
  Note that the 2.0.4 version of the Windows SDL runtime library had a
  serious crash in its DX9 driver. Upgrade to 2.0.5+ to avoid it.

  Emscripten
  ----------

  Install Emscripten using the instructions here first:
  https://emscripten.org/docs/getting_started/downloads.html

  In case you are using Visual Studio, grab the toolchain extension here:
  https://github.com/nokotan/VSExtForEmscripten/releases/
  https://marketplace.visualstudio.com/items?itemName=KamenokoSoft.emscriptenproj1

  Under Windows it's best to use the Solution configuration provided.
  Compiling for Emscripten manually requires a lot of manual workarounds. 
  These will be documented at a later stage.  

  You won't need to download SDL2 headers as these are pulled in automatically during
  the first compilation but they are available from here:
  https://github.com/kripken/emscripten
  https://github.com/emscripten-ports/SDL2

  Prebuilt SDL2 libraries required for linking can be found here, but these are not
  needed if you are using the VS solution:
  https://buildbot.libsdl.org/sdl-builds/sdl-emscripten/?C=M;O=D

USAGE
=====

  The user interface isn't yet ready, so you MUST memorise a couple
  of keyboard shortcuts:
  
  F12 or ESCAPE : exits the emulator
  F11 or Alt+R  : soft reset
  LCTRL + F11	: forced reset (memory remains intact)
  SHIFT + F11	: hard reset (clear memory, reloads ROMs, rewinds tape)
  
  F5		: press PLAY on tape
  F6		: press STOP on tape
  F7		: save screenshot to BMP file
  F8		: enter the user interface, press F8 again or ESC to quit.
              You can move around in the menus with the arrow keys, press ENTER for selection.
  F9		: quick debugger
  F10		: save current settings to user's home directory

  LALT + 1-3   : set window size
  LALT + L	   : switch among emulators (C+4 cycle based; C+4 line based; C64 cycle based)
  LALT + I	   : switch emulated joystick port
  LALT + M     : enter console based external monitor and disassembler
  LALT + P	   : toggle CRT emulation
  LALT + R     : machine forced reset
  LALT + S     : display frame rate on/off  
  LALT + W	   : toggle between unlimited speed and 50 Hz frame rate (original speed)
  LALT + ENTER : toggle full screen mode
  LALT + F5    : save emulator state
  LALT + F6    : load emulator state
  LALT + F8	   : save memory pointed by $2B/$2C and $2D/$2E
  LALT + KPLUS : collate replay frequencies
  
  Internal disk LOAD/SAVE operations are supported to the current
  file system directory - which is usually '/home/<username>/yape'.
  Any PRG files you may wish to load, should go there, although you can browse the directory tree
  from the user interface, too.
   
  This means that an exact filename match will load
  the requested program, similarly can you save a file.
  
  Full ROM banking is supported on the plus/4, via the 'Attach/Detach ROM...' menu.
  Yape also supports the default ROM's. These are:
  
  - 'BASIC' 	 - the ROM image containing the BASIC interpreter
  - 'KERNAL' 	 - the system kernal ROM image
  - '3PLUS1LOW'  - the low bank ROM image of the built-in plus/4 software
  - '3PLUS1HIGH' - the high bank ROM image of the built-in plus/4 software
  
  In C64 mode, the 'Attach/Detach ROM...' can be used for cartridges, but right now
  only a handful are supported.

KEYBOARD MAPPINGS
=================

  For the keys at different positions, the emulator relies on SDL, so your
  mileage may vary.
  
  There are a few keys that need to be mapped to the emulator keyboard
  differently. These are:
  
  Real C=     Emulator
  ------------------------
  Pound 	= End
  Clr/Home	= Home
  Restore   = Insert
  C=        = Left Ctrl
  CONTROL   = Right Ctrl
  INST/DEL  = Backspace
  RUN/STOP  = TAB

FEATURES
========

  YapeSDL features:
  
  - full, cycle exact MOS 6502/6510/7501/8501 CPU emulation
  - almost full MOS 7360/8360 aka 'TED' chip emulation
  - almost complete MOS 6569 aka 'VIC-II' chip emulation
  - reasonable MOS 6581/8580 aka 'SID' chip emulation
  - somewhat incomplete CIA 6526 aka 'CIA' emulation
  - real 1541 drive emulation (Read/Write)
  - full ROM banking on +4
  - almost full tape emulation
  - joystick emulation via cursor keys and gamepads
  - PRG, P00, T64, D64 and TAP file format support
  - partial CRT emulation
  - serial IEC disk LOAD/SAVE to the file system
  - REU 1700/1750/1764 and Hannes 256kB memory expansion support
  - snapshots / savestates

LINKS
=====

  https://github.com/calmopyrin/yapesdl : official source repository for yape/SDL
  http://yape.homeserver.hu             : Yape homepage
  http://plus4world.powweb.com          : Commodore +4 World (games, tape images, scans etc.)
  http://c64.rulez.org	                : Commodore plus/4 FTP archive
  http://gaia.atilia.eu                 : My 8-bit emulator page
