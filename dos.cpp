#include <ctype.h>
#include <stdio.h>
#include "device.h"

#define DRIVE_RAM_SIZE 0x0800

// DOS error messages
const char *DriveErrors[] = {
		"00, OK",
		"01,FILES SCRATCHED",
		"25,WRITE ERROR",
		"26,WRITE PROTECT ON",
		"30,SYNTAX ERROR",
		"33,SYNTAX ERROR",
		"60,WRITE FILE OPEN",
		"61,FILE NOT OPEN",
		"62,FILE NOT FOUND",
		"63,FILE EXISTS",
		"65,NO BLOCK",
		"67,ILLEGAL TRACK OR SECTOR",
		"70,NO CHANNEL",
		"72,DISK FULL",
		"73,CBM DOS V2.6 YAPESDL IEC",
		"74,DRIVE NOT READY"
};

CIECDrive::CIECDrive() : ram(NULL)
{
	for (int i=0; i<15; i++) {
		ch[i].mode = CHMOD_FREE;
		ch[i].data = NULL;
	}
	ch[15].mode = CHMOD_COMMAND;
	ram = new unsigned char[DRIVE_RAM_SIZE];
}

CIECDrive::~CIECDrive()
{
	if (ram) {
		delete [] ram;
		ram = NULL;
	}
}

/*
   Set error message on drive
*/
void CIECDrive::SetError(unsigned int error, unsigned int track, unsigned int sector)
{
	sprintf( current_error, "%s,%02d,%02d\r", DriveErrors[error], track, sector);
	errorPtr = current_error;
	errorLength = strlen(errorPtr);
	currentError = error;
}

void CIECDrive::Reset()
{
	SetError(ERR_STARTUP, 0, 0);
	memset(ram, 0, DRIVE_RAM_SIZE);
}

void CIECDrive::CloseAllChannels()
{
	for (int i=0; i<15; i++)
		Close(i);

	//cmd_len = 0;
}

int CIECDrive::BufferAlloc(int buf)
{
	switch (buf) {
		case 0:
		case 1:
		case 2:
		case 3:
			if (bufferFree[buf])
				return buf;
		case -1:
			for(buf=3; buf>=0; buf--)
				if (bufferFree[buf])
					return buf;
		default:
			return -1;
	}
}

void CIECDrive::BufferFree(int buf)
{
	bufferFree[buf] = true;
}

/*
   Conversion PETSCII->ASCII
*/
char CIECDrive::ToASCII(unsigned char c)
{
	if (c=='/' || c=='<' || c=='>') /* maybe also | and \ */
		return '_';
	if (((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z')))
		return c ^ 0x20;
	if ((c >= 0xc1) && (c <= 0xda))
		return c ^ 0x80;

	return c;
}

/*
   Conversion ASCII->PETSCII
*/

unsigned char CIECDrive::ToPETSCII(unsigned char c)
{
	if ( isalpha(c) )
		return tolower(c)^0x20;

	if (c == '\\')
		return '/';

	return c;
}

/*
	Find first file matching wildcard pattern and get its real name
	Return true if name 'n' matches pattern 'p'
*/
bool CIECDrive::Match(char *p, char *n)
{
	if (!*p)		// Null pattern matches everything
		return true;

	do {
		if (*p == '*')	// Wildcard '*' matches all following characters
			return true;
		if ((*p != toupper(*n)) && (*p != '?'))	// Wildcard '?' matches single character
			return false;
		p++; n++;
	} while (*p);

	return !*n;
}

void CIECDrive::ParseFileName(char *srcName, char *targetName, int &fileMode,
		int &fileType, int &recLength, bool convert)
{

}
