objects =		\
1541mem.o \
archdep.o		\
Cia.o		\
cpu.o	\
dis.o	\
diskfs.o \
dos.o \
drive.o \
FdcGcr.o \
iec.o \
interface.o		\
keyboard.o \
keys64.o \
main.o \
monitor.o 	\
prg.o \
SaveState.o	\
serial.o	\
Sid.o \
sound.o	\
tape.o \
tcbm.o \
tedmem.o \
tedsound.o \
vic2mem.o \
video.o

EXENAME = yapesdl
SRCPACKAGENAME = $(EXENAME)_0.70.1-1
BINPACKAGENAME = $(SRCPACKAGENAME)_amd64
SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LDFLAGS := $(shell sdl2-config --libs)

headers = $(objects:.o=.h)
sources = $(objects:.o=.cpp)
allfiles = $(headers) $(sources)
hasnoheader = main.h dos.h dis.h tedsound.h
sourcefiles = $(filter-out $(hasnoheader),$(allfiles)) icon.h device.h mem.h mnemonic.h \
				roms.h types.h Clockable.h 1541rom.h YapeSDL.cbp YapeSDL.Linux.cbp

CC = g++
cflags = -O3 -w $(SDL_CFLAGS)
libs = $(SDL_LDFLAGS)

yape : $(objects)
	$(CC) $(cflags) -o $(EXENAME) -s $(objects) $(libs)

yapedebug : $(objects)
	$(CC) $(cflags) $(libs) -g -Og -o $(EXENAME)d $^

1541mem.o : 1541mem.cpp
	$(CC) $(cflags) -c $<

archdep.o : archdep.cpp
	$(CC) $(cflags) -c $<

Cia.o : Cia.cpp
	$(CC) $(cflags) -c $<

cpu.o : cpu.cpp tedmem.h
	$(CC) $(cflags) -c $<

diskfs.o : diskfs.cpp diskfs.h device.h iec.h
	$(CC) $(cflags) -c $<

dis.o : dis.cpp
	$(CC) $(cflags) -c $<

dos.o : dos.cpp
	$(CC) $(cflags) -c $<

drive.o : drive.cpp iec.cpp drive.h device.h diskfs.h iec.h tcbm.h
	$(CC) $(cflags) -c $<

FdcGcr.o : FdcGcr.cpp
	$(CC) $(cflags) -c $<

iec.o : iec.cpp iec.h
	$(CC) $(cflags) -c $<

interface.o : interface.cpp
	$(CC) $(cflags) -c $<

keyboard.o : keyboard.cpp keyboard.h
	$(CC) $(cflags) -c $<

keys64.o : keys64.cpp keys64.h
	$(CC) $(cflags) -c $<

main.o : main.cpp
	$(CC) $(cflags) -c $<

monitor.o : monitor.cpp
	$(CC) $(cflags) -c $<

prg.o : prg.cpp prg.h
	$(CC) $(cflags) -c $<

SaveState.o : SaveState.cpp SaveState.h
	$(CC) $(cflags) -c $<

serial.o : serial.cpp
	$(CC) $(cflags) -c $<

sound.o : sound.cpp sound.h
	$(CC) $(cflags) -c $<

Sid.o : Sid.cpp
	$(CC) $(cflags) -c $<

tape.o : tape.cpp tape.h
	$(CC) $(cflags) -c $<

tcbm.o : tcbm.cpp tcbm.h
	$(CC) $(cflags) -c $<

tedmem.o : tedmem.cpp
	$(CC) $(cflags) -c $<

tedsound.o : tedsound.cpp
	$(CC) $(cflags) -c $<

vic2mem.o : vic2mem.cpp vic2mem.h
	$(CC) $(cflags) -c $<

video.o : video.cpp
	$(CC) $(cflags) -c $<

clean :
	rm -f ./*.o
	rm ./$(EXENAME)

tgz :
	tar -czf $(SRCPACKAGENAME).tar.gz $(sourcefiles) Makefile COPYING README.SDL Changes

e :
	#emacs -fn 9x13 Makefile *.h *.cpp &
	emacs -fn 10x20 Makefile *.h *.cpp &

deb:
	cp ./$(EXENAME) ./$(SRCPACKAGENAME)/usr/local/bin
	dpkg-deb --build $(SRCPACKAGENAME)
	mv $(SRCPACKAGENAME).deb $(BINPACKAGENAME).deb

install :
#	@if [ ! -e $(HOME)/yape ]; then mkdir $(HOME)/.yape ; fi
#	@cp yape.conf $(HOME)/.yape $^
	@cp ./$(EXENAME) $(BINDIR)


