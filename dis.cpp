#include "cpu.h"
#include "mnemonic.h"
#include "types.h"
#include "tedmem.h"
#include <stdio.h>
#include <string.h>

// converts a decimal number to 'len' long '0' delimited hexa
static void hexform(unsigned int len, int numbr, char *tempstr)
{
	char hex[5];

	sprintf(hex,"%x",numbr);
	strcpy(tempstr,"");
	while ((len--)>strlen(hex))
		strcat(tempstr,"0");
	strcat(tempstr,hex);
}

static inline int bytetobin(int decnum)
{
	int i, j;

	//fprintf(stderr,"Input %02X.\n", decnum);
	j = 0;
	for ( i=7; i>=0 ; i--) {
		int k = (decnum >> i ) & 1;
		j += k;
		j *= 10;
	}
	j /= 10;
	//fprintf(stderr,"Output %i.\n", j);
	return j;
}

int CPU::disassemble(int pc, char *line)
{
	char hexstr[40], out[40];
	int current = mem->Read(pc);
	int next = mem->Read(pc+1);
	int word = (mem->Read(pc+2)<<8)|next;
	int mnetyp = ins[current].type;
	int mnelen = typlen[mnetyp];
	int reladdr;

	// put current PC
	sprintf(line,". %04X ",pc);
	// put bytes
	switch (mnelen) {
		case 2 :
			sprintf( hexstr, "%02X %02X    ",current,next);
			break;
		case 3 :
			sprintf( hexstr, "%02X %02X %02X ",current,next,(word&0xFF00)>>8);
			break;
		default :
			sprintf( hexstr,"%02X       ", current);
	}
	strcat(line,hexstr);
	// put current instruction
	sprintf(hexstr,"%4s ",ins[current].name);
	strcat(line,hexstr);
	// take the appropriate formatting for the mnemonic
	switch (mnetyp) {
		case 2 : hexform(mnelen,next,hexstr);
				 sprintf(out,"#$%s",hexstr);
				 break;
		case 3 : sprintf(out,"$%04X",word);
				 break;
		case 4 : sprintf(out,"($%04X)",word);
				 break;
		case 5 : hexform(mnelen,next,hexstr);
				 sprintf(out,"$%s",hexstr);
				 break;
		case 6 : hexform(mnelen,next,hexstr);
				 sprintf(out,"$%s,X",hexstr);
				 break;
		case 7 : hexform(mnelen,next,hexstr);
				 sprintf(out,"$%s,Y",hexstr);
				 break;
		case 8 : sprintf(out,"$%04X,X",word);
				 break;
		case 9 : sprintf(out,"$%04X,Y",word);
				 break;
		case 10 :hexform(mnelen,next,hexstr);
				 sprintf(out,"($%s),Y",hexstr);
				 break;
		case 11 :hexform(mnelen,next,hexstr);
				 sprintf(out,"($%s,X)",hexstr);
				 break;
		case 12 :reladdr = (pc + 2 + (signed char) next)&0xFFFF;
				 hexform(4,reladdr,hexstr);
				 sprintf(out,"$%s",hexstr);
				 break;
		default :strcpy(out,"");
				 break;
	}
	strcat(line, out);
	strcat(line, "\n");
	return pc + mnelen;
}

void CPU::regDump(char *line, int rowcount)
{
 	int flagmask = 0;

	if (rowcount == 0) {
		sprintf( line, "AC:%02X,XR:%02X,YR:%02X,SP:%02X,PC:%04X,ST:%04X|NV-BCIZC\n",
			REG_AC, REG_X, REG_Y, REG_SP, REG_PC, REG_ST);
	} else if (rowcount == 1) {
		char txt[][5] = {"HI","LO"};
		flagmask = bytetobin( REG_ST );
		sprintf( line, "IRQ(%s)|HC($%02X)|VC($%03X)             FL:%08i\n",
          txt[*irq_register ? 1 : 0], mem->Read(0xff1e), ((mem->Read(0xff1c)&1) << 8)|mem->Read(0xff1d), 
		  flagmask);
	}
}

void CPU::step()
{
#if 0
	if (cycle == 0)
		process();
	while(cycle != 0)
		process();
#else
	// make sure we are after the first CPU cycle...
	while (getcycle()!=1)
		theTed->ted_process(0);
	while (getcycle()!=0)
		theTed->ted_process(0);
#endif
}

