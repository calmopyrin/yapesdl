#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <cstring>
#include <vector>
#include <string>
#include "cpu.h"
#include "monitor.h"

using namespace std;

#define COLS 80
#define ROWS 20

static struct {
	char Str[128];
	unsigned int Code;
	char desc[128];
} monCmds[] = {
	{ "<dummy>", 0, "Dummy" },
	//	{ "b [<address>]", MON_CMD_BREAKPOINT, "List all or set breakpoint to <address>.") },
	{ "> [<address>] [<arg1>] [<arg2>] .. [<arg#>]", MON_CMD_CHANGEMEM, "Show/change memory from <address>." },
	{ "d [<address>]", MON_CMD_DISASS, "Disassemble (from <address>)." },
	{ "f <src_from> <src_to> <value>", MON_CMD_FILLMEM, "Fill memory range with <value>." },
	{ "g [<address>]", MON_CMD_GO, "Go (to [<address>])." },
	{ "m [<address>]", MON_CMD_MEM, "Memory dump (from [<address>])." },
	{ "t <src_from> <src_to> <target>", MON_CMD_TRANSFER, "Memory copy transfer (from start address)." },
	{ "x", MON_CMD_EXIT, "Exit monitor." },
	{ "z", MON_CMD_STEP, "Debug" },
	//{ "attach <filename>", MON_CMD_ATTACHIMAGE, "Attach image." },
	//{ "detach", MON_CMD_DETACHIMAGE, "Detach current image." },
	//	{ "loadbin <filename>", MON_CMD_LOADBIN, "Load binary file.") },
	//	{ "loadrom [<filename>]", MON_CMD_LOADROM, "Load as ROM or restore default one.") },
	//	{ "loadcharset [<filename>]", MON_CMD_LOADCHARSET, "Load charset or restore default one.") },
	{ "reg", MON_CMD_REGS, "Register dump." },
	{ "reset", MON_CMD_RESET, "Machine soft reset." },
	//{ "restart", MON_CMD_RESTART, "Machine restart." },
	//{ "savebin <filename> <from> <to>", MON_CMD_SAVEBIN, "Save binary file from address." },
	//	{ "rmb [<address>]", MON_CMD_REMOVEBREAKPOINT, "Remove breakpoint(s) from <address> or all.") },
	{ "sys <command>", MON_CMD_SYS, "Do OS <command>." },
	//	{ "w <address>", MON_CMD_WATCHPOINT, "Watchpoint to <address>.") },
	{ "?", MON_CMD_HELP, "Help" }
};

#define NR_OF_MON_CMDS (sizeof(monCmds)/sizeof(monCmds[0]))

static CPU *cpuptr;
static int disAssPc, memDumpPc;
static unsigned int command;

inline static char** new2d(int rows, int cols)
{
	char* data = new char [rows * cols];
	char** rowpointers = new char *[rows * sizeof(*rowpointers)];
	int i;
	if (data == NULL || rowpointers == NULL)
		return NULL;
	rowpointers[0] = data;
	for (i = 1; i < rows; ++i) {
		rowpointers[i] = rowpointers[i-1] + cols;
	}
	return rowpointers;
}

inline static void free2d(char** rowpointers)
{
	if (rowpointers == NULL)
		return;
	delete rowpointers[0];
	delete rowpointers;
	rowpointers = NULL;
}

char cbmToASCII(unsigned char c)
{
	if (c >= 0x00 && c <= 0x1A)
		return c ^ 0x40;
	if (c >= 0x1b && c < 0x20)
		return '_';
	if (c == 0x25)
		return '/';
	if (c >= 0x41 && c <= 0x5A)
		return c ^ 0x20;
	if ((c >= 0xc1) && (c <= 0xda))
		return c ^ 0x80;

	return (char) c;
}

int xtoi(const char *str)
{
	size_t ln = strlen(str);
	unsigned int i = 1;
	unsigned int s = 0;

	while (ln--) {
		char c = tolower(str[ln]);
		if (c >= 'a' && c <= 'f') {
			s += (c - 'a' + 10) * i;
		} else if (c >= '0' && c <= '9') {
			s += (c - '0') * i;
		}
		i *= 16;
	}
	return s;
}

static void getFirstString(char *from, char *to)
{
	char *begin = from;

	while (begin && begin[0] == ' ') begin++;
	strcpy(to, begin);

	char *end = strpbrk(to, " \r\n");
	if (end) {
		*end = 0;
	}
}

static void showHelp()
{
	for(unsigned int i=1; i<NR_OF_MON_CMDS; i++) {
		printf("%s : %s\n", monCmds[i].Str, monCmds[i].desc);
	}
}

static void parseCommand(char *line, unsigned int &cmdCode)
{
	unsigned int i;

	// Strip spaces
	while (*line == ' ')
		line++;

	char commandString[256];
	memset(commandString, 0, sizeof(commandString));
	getFirstString(line, commandString);
	cmdCode = MON_CMD_NONE;
	if (0 == strlen(commandString))
		return;

	for(i=1; i<NR_OF_MON_CMDS; i++) {
		if ( !strncmp(commandString, monCmds[i].Str, strlen(commandString)) ) { // CompareString
			cmdCode = monCmds[i].Code;
			break;
		}
	}
}

//static int parseArgs(char *line, char args[4][256])
//{
//	char *param;
//	int argCount = 0;
//
//	// Strip spaces
//	while (*line == ' ')
//		line++;
//	argCount = 0;
//	param = strchr(line, ' ');
//	while (param && param[1] && argCount<4) {
//		line = param + 1;
//		sscanf(line, "%s", args[argCount]);
//		param = strchr(line, ' ');
//		argCount += 1;
//	}
//	return argCount;
//}

static void memShow(int pc, int dumpwidth)
{
	unsigned short current_pc;
	int i;
	unsigned int rowcount, rows, cols;
	char temp[256];
	char line[256];
	char **linebuffer;
	MemoryHandler &mem = cpuptr->getMem();

	//con->getDimensions(cols, rows);
	cols = COLS;
	rows = ROWS;

	linebuffer = new2d(rows,cols);
	if (memDumpPc == 0)
		memDumpPc = pc;

	current_pc = memDumpPc;
	rowcount = 0;
	while ( rowcount < rows ) {
		sprintf( line, " %04X: ", current_pc );
		for  ( i = 0; i<dumpwidth/2; i++) {
			if ((i&0xF)==8) strcat( line, " ");
			sprintf( temp, "%02X ", mem.Read(current_pc+i) & 0xFF);
			strcat( line, temp);
		}
		strcat( line, ":");
		for  ( i = 0; i<dumpwidth/2; i++) {
			unsigned char a = mem.Read(current_pc+i);
			if (!(a & 0x80))
				sprintf(temp, "%c", cbmToASCII(a));
			else
				sprintf(temp, ".");
			strcat( line, temp);
		}
		strcat( line, "\n");
		printf("%s", line);
		strcpy( line, "\0");
		current_pc += dumpwidth/2;
		rowcount++;
	}
	memDumpPc = current_pc;

	free2d(linebuffer);
}

static void dumpRegs()
{
	char line[256];

	cpuptr->regDump(line,0);
	printf("%s", line);
	cpuptr->regDump(line,1);
	printf("%s", line);
}

static void memDump()
{
	int dumpwidth = ((COLS - 16) / 2) & 0xF8;
	memShow(memDumpPc,dumpwidth);
}

static void memChange(vector<string> args)
{
	unsigned int argCount = args.size();
	if (argCount--) {
		memDumpPc = xtoi(args[0].c_str()) & 0xffff;
	} else {
		memDump();
		return;
	}
	while (argCount--) {
		unsigned char a = xtoi(args[argCount + 1].c_str()) & 0xff;
		cpuptr->getMem().Write(memDumpPc + argCount, a);
	}
}

static void step()
{
	char line[256];
	unsigned short current_pc = cpuptr->getPC();

	disAssPc = cpuptr->disassemble(current_pc, line);
	printf("%s", line);
	cpuptr->step();
	dumpRegs();
}

static void dumpDisAss(int pc)
{
	unsigned short current_pc;
	unsigned int rows, cols, rowcount;
	char **linebuffer;

	//con->getDimensions(cols, rows);
	cols = COLS;
	rows = ROWS;

	if (rows+cols == 0)
		return;

	linebuffer = new2d(rows,cols);
	if (disAssPc == 0)
		disAssPc = pc;

	current_pc = disAssPc;
	rowcount = 2;
	while ( rowcount < rows ) {
		current_pc = cpuptr->disassemble(current_pc, linebuffer[rowcount]);
		//con->outDisassString(rowcount,linebuffer[rowcount++]);
		printf("%s", linebuffer[rowcount++]);
	}
	disAssPc = current_pc;

	free2d(linebuffer);
}

static void disAss(int dir)
{
	if (dir == 0) {
		dumpDisAss(cpuptr->getPC());
	} else {
		dumpDisAss(disAssPc);
	}
}

void executeCmd(unsigned int cmd, vector<string> &args, char *wholeLine)
{
	vector<unsigned int> argval;
	unsigned int argCount = args.size();
	unsigned int r = 0;

	while (r < argCount) {
		argval.push_back(xtoi(args[r].c_str()));
		r++;
	}

	switch (cmd) {

		case 0:
			// empty line
			break;

		case MON_CMD_MEM:
			if (argCount >= 1) {
				memDumpPc = argval[0];
			}
			memDump();
			break;

		case MON_CMD_CHANGEMEM:
			memChange(args);
			break;

		case MON_CMD_FILLMEM:
			if (argCount == 3) {
				while (argval[0] <= argval[1]) {
					cpuptr->getMem().Write(argval[0]++, argval[2]);
				}
			}
			break;

		case MON_CMD_TRANSFER:
			if (argCount == 3) {
				while (argval[0] <= argval[1]) {
					int readval = cpuptr->getMem().Read(argval[0]++);
					cpuptr->getMem().Write(argval[2]++, readval);
				}
			}
			break;

		case MON_CMD_DISASS:
			if (argCount >= 1) {
				disAssPc = argval[0];
			}
			disAss(+1);
			break;

		case MON_CMD_STEP:
			step();
			break;

		case MON_CMD_REGS:
			dumpRegs();
			break;

		case MON_CMD_GO:
			if (argCount >= 1) {
				cpuptr->setPC(argval[0]);
			}
			command = MON_CMD_EXIT;
			break;

		//case MON_CMD_ATTACHIMAGE:
		//	if (argCount >= 1) {
		//		theMachine->attachTape(args[0]);
		//	}
		//	break;

		//case MON_CMD_DETACHIMAGE:
		//	theMachine->detachTape();
		//	break;

		case MON_CMD_RESET:
			cpuptr->Reset();
			printf("Machine soft reset.\n");
			break;

		//case MON_CMD_RESTART:
		//	theMachine->coldRestart();
		//	printf("Machine restart.\n");
		//	break;

		case MON_CMD_LOADBIN:
			if (argCount >= 2) {
				//cpuptr->loadBinFromFile(args[0], argval[1]);
			}
			break;

		case MON_CMD_SAVEBIN:
			if (argCount >= 3) {
				//cpuptr->saveToFile(args[0], argval[1], argval[2]);
			}
			break;

		case MON_CMD_SYS:
			if (argCount >= 1) {
				wholeLine = strchr(wholeLine, ' ');
				if (wholeLine) {
					wholeLine++;
					system(wholeLine);
				}
			}
			break;

		case MON_CMD_HELP:
			showHelp();
			break;

		case MON_CMD_EXIT:
			printf("Exiting the monitor...\n");
			break;

		default:
			printf("Unknown command code: %i.\n", cmd);
			break;
	}
}

static void parseLine(char *line, unsigned int &cmdCode)
{
	vector<string> args;
	const char *dl = " ,.-\n\r\t";

	parseCommand(line, cmdCode);
	// first token is command string
	char *carg = strtok(line, dl);
	carg = strtok(NULL, dl);
	//
	while (carg) {
		args.push_back(carg);
		carg = strtok(NULL, dl);
	}
	executeCmd(cmdCode, args, line);
}

void monitorEnter(CPU *cpu)
{
	char buffer[256];

	cpuptr = cpu;
	disAssPc = memDumpPc = cpuptr->getPC();

	printf("Welcome to the monitor!\n");
	printf("Type ? for help!\n");

	do {
		command = MON_CMD_NONE;
		strcpy(buffer, "");
		fgets(buffer, 256, stdin);
		parseLine(buffer, command);
	} while (command != MON_CMD_EXIT);
}
