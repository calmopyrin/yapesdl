#include "device.h"
#include "iec.h"
#include "diskfs.h"
#include "1541rom.h"
#include <string.h>
#include <ctype.h>

CIECFSDrive::CIECFSDrive(const char *path)
{
	strcpy(orig_dir_path, path);
	dir_path[0] = 0;

	if (ChangeDir(orig_dir_path)) {
		for (int i=0; i<16; i++)
			file[i] = NULL;
		Reset();
	}
}

CIECFSDrive::~CIECFSDrive()
{
	CloseAllChannels();
}

void CIECFSDrive::Reset(void)
{
	CloseAllChannels();
	cmd_len = 0;
	name_length = 0;
	SetError(ERR_STARTUP, 0, 0);
}

bool CIECFSDrive::ChangeDir(char *dirpath)
{
	if ( dirpath[0] ) {
		strcpy(dir_path, dirpath);
		strncpy(dir_title, dir_path, 16);
		return !!ad_set_curr_dir(dir_path);
	} else
		return false;
}

unsigned char CIECFSDrive::Open(int channel)
{
	SetError(ERR_OK, 0, 0);

	if (channel == 15) {
		ExecuteCommand(cmd_buffer);
		return ST_OK;
	}

	if (file[channel]) {
		fclose(file[channel]);
		file[channel] = NULL;
	}

	switch ( name_buf[0] ) {
		case '$':
			return OpenDirectory(channel, name_buf+1);
		case '#':
			SetError(ERR_NOCHANNEL, 0, 0);
			return ST_OK;
		default:
			return OpenFile(channel, name_buf);
	}
}

unsigned char CIECFSDrive::Open(int channel, char *nameBuf)
{
	if (nameBuf)
		strcpy(name_buf, nameBuf);
	return Open(channel);
}

unsigned char CIECFSDrive::OpenFile(int channel, char *filename)
{
	char plainname[NAMEBUF_LENGTH];
	int filemode;
	int filetype;
	bool wildflag = false;
	char mode[4];
	char extension[5];		/* File extension */

	ParseFileName(filename, plainname, &filemode, &filetype, &wildflag);

	// Channel 0 is READ PRG, channel 1 is WRITE PRG
	if (channel <= 1) {
		filemode = channel ? FMODE_WRITE : FMODE_READ;
		if (filetype == FTYPE_DEL)
			filetype = FTYPE_PRG;
	}

	if (wildflag) {
		if (filemode != FMODE_READ) {
			SetError(ERR_SYNTAX33, 0, 0);
			return ST_OK;
		}
		findFirstFile(plainname);
	} else {

		switch (filetype) {
		case FTYPE_PRG:
			strcpy(extension, ".prg"); // Add .prg extension
			break;
			/*case FTYPE_SEQ:
				strcpy( extension, ".seq");	// Add .seq extension
				break;*/
		default:
			strcpy(extension, ".prg");
		}
		strcat(plainname, extension);	/* Add extension */
	}
	//fprintf(stderr,"Searching for: %s\n", plainname);

	switch (filemode) {
	    default:
		case FMODE_READ:
			strcpy(mode, "rb");
			break;
		case FMODE_WRITE:
			strcpy(mode, "wb");
			break;
		case FMODE_APPEND:
			strcpy(mode, "ab");
			break;
	}

	if ((file[channel] = fopen(plainname, mode)) != NULL) {
		if (filemode == FMODE_READ)	// Read and buffer first byte
			read_char[channel] = fgetc(file[channel]);
	} else {
		SetError(ERR_FILENOTFOUND, 0, 0);
		return ST_ERROR;
	}

	return ST_OK;
}


void CIECFSDrive::ParseFileName(char *srcname, char *destname, int *filemode, int *filetype, bool *wildflag)
{
	char *p, *q;
	int i;

	if ((p = strchr(srcname, ':')) != NULL)
		p++;
	else
		p = srcname;

	q = destname;
	for (i=0; i<NAMEBUF_LENGTH && (*q++ = ToPETSCII(*p++)); i++) ;

	p = destname;
	while ((p = strchr(p, ',')) != NULL) {

		*p++ = 0;

		switch (*p) {
			case 'p':
				*filetype = FTYPE_PRG;
				break;
			case 's':
				*filetype = FTYPE_SEQ;
				break;
			case 'u':
				*filetype = FTYPE_USR;
				break;
			case 'r':
				*filemode = FMODE_READ;
				break;
			case 'w':
				*filemode = FMODE_WRITE;
				break;
			case 'a':
				*filemode = FMODE_APPEND;
				break;
		}
	}

	*wildflag = strpbrk(destname, "?*") != NULL;
}

bool CIECFSDrive::findFirstFile(char *name)
{
	bool found = true;
	char *filename, cmpname[NAMEBUF_LENGTH];
	char fname[NAMEBUF_LENGTH];

	// convert to uppercase
	for ( filename = name; filename<name+strlen(name); filename++)
		*filename=toupper(*filename);

	if ( !(ad_set_curr_dir(dir_path)) )
		return false;

	strcat(name, ".prg");
	if (!ad_find_first_file(name)) 
		return false;
	found = ad_return_current_filename() != 0;

	while (found) {
		char *currfname = ad_return_current_filename();
		if (currfname) {
			char *ext;
			strncpy( fname, currfname, NAMEBUF_LENGTH);
			strcpy( cmpname, currfname);

			ext = strrchr( cmpname, '.' );
			// FIXME! filetype check!
			if ( ext ) {
				*ext++ = '\0';
			}
			// Match found? Then copy real file name
			if (Match(name, cmpname)) {
				strncpy(name, fname, strlen(fname)+1 );
				return true;
			}
			// Get next directory entry
		}
		found = !!ad_find_next_file();
	}
	ad_find_file_close();
	return found;
}

unsigned char CIECFSDrive::OpenDirectory(int channel, char *filename)
{
	char buf[] = "\001\004\001\001\0\0\022\042                \042 00 2A";
	char str[NAMEBUF_LENGTH];
	char pattern[NAMEBUF_LENGTH];
	char *p, *q;
	int i;
	int filemode;
	int filetype;
	bool wildflag;

	bool found = true;
	char fname[NAMEBUF_LENGTH] = "";

	if (filename[0] == '0' && filename[1] == 0)
		filename += 1;

	ParseFileName(filename, pattern, &filemode, &filetype, &wildflag);

	if ( ad_find_first_file("*.prg"))
		strcpy( fname, ad_return_current_filename());

	if ( !fname[0] )
		found = false;

	if ((file[channel] = tmpfile()) == NULL) {
		return ST_OK;
	}

	p = &buf[8];
	for (i=0; i<16 && dir_title[i] ; i++)
		*p++ = ToPETSCII(dir_title[i]);
	fwrite(buf, 1, 32, file[channel]);

	while (found) {

		if (Match(pattern, fname)) {

			memset(buf, ' ', 31);
			buf[31] = 0;

			p = buf;
			*p++ = 0x01;
			*p++ = 0x01;

			int size = ad_get_current_filesize();
			// Size in blocks
			i = (size + 254) / 254;
			*p++ = i & 0xff;
			*p++ = (i >> 8) & 0xff;

			p++;
			if (i < 10) p++;
			if (i < 100) p++;

			strcpy(str, fname);
			str[ strlen(str)-4 ] = '\0';
			*p++ = '\"';
			q = p;
			for (i=0; i<16 && str[i]; i++)
				*q++ = ToPETSCII(str[i]);
			*q++ = '\"';
			p += 18;

			strncpy( p, "PRG", 3);
			p += 3;

			fwrite(buf, 1, 32, file[channel]);
		}

		// Take next directory entry
		if ((found = !!ad_find_next_file())) {
			strcpy( fname, ad_return_current_filename());
		}
	}

	fwrite("\001\001\0\0BLOCKS FREE.             \0\0", 1, 32, file[channel]);

	rewind(file[channel]);
	read_char[channel] = fgetc(file[channel]);

	ad_find_file_close();

	return ST_OK;
}

unsigned char CIECFSDrive::Close(int channel)
{
	// Closing channel 15 closes all other channels
	if (channel == 15) {
		CloseAllChannels();
		return ST_OK;
	}
	if (file[channel]) {
		fclose(file[channel]);
		file[channel] = NULL;
	}

	return ST_OK;
}

unsigned char CIECFSDrive::Read(int channel, unsigned char *byte)
{
	int c;

	if (channel == 15) {
		*byte = *errorPtr++;
		errorLength--;
		if (*byte != '\r' || !errorLength)
			return ST_OK;
		else {	// End of message
			SetError(ERR_OK, 0, 0);
			return ST_ERROR; // not ST_EOF!
		}
	}

	if (!file[channel]) return ST_ERROR;

	// Read one byte
	*byte = read_char[channel];
	c = fgetc(file[channel]);
	if (c == EOF)
		return ST_EOF;
	else {
		read_char[channel] = c;
		return ST_OK;
	}
}

void CIECFSDrive::setEoI(unsigned int channel)
{
	if (channel == 15) {
		cmd_buffer[cmd_len] = 0;
		cmd_len = 0;
	}
	else {
		name_buf[name_length] = 0;
		name_length = 0;
	}
}

Uint8 CIECFSDrive::Write(int channel, unsigned char data, unsigned int cmd, bool eoi)
{
	if (channel == 15) {

		if (cmd_len >= 58)
			return ST_ERROR;
		cmd_buffer[cmd_len++] = data;
		return ST_OK;

	}

	switch (cmd) {
		case IEC_CMD_OPEN:
			name_buf[name_length++] = data;
			if (name_length>=16)
	    		return ST_ERROR;
			return ST_OK;
		case IEC_CMD_DATA:
			if (!file[channel]) {
				SetError(ERR_FILENOTOPEN, 0, 0);
				return ST_ERROR;
			}

			if (fputc(data, file[channel]) == EOF && !eoi) {
				SetError(ERR_WRITEERROR, 0, 0);
				return ST_ERROR;
			}
			return ST_OK;
		default:
			return ST_ERROR;
	}
}

void CIECFSDrive::ExecuteCommand(char *command)
{
	unsigned short adr;
	int len, i;

	switch (command[0]) {
		case 'B':
			if (command[1] != '-')
				SetError(ERR_SYNTAX30, 0, 0);
			else
				switch (command[2]) {
					case 'E':
						adr = ((unsigned char)command[4] << 8) | ((unsigned char)command[3]);
						fprintf( stderr, "B-E ($%04X) : not supported with DOS level emulation.\n", adr);
						break;
					default:
						SetError(ERR_SYNTAX30, 0, 0);
						break;
				}
			break;

		case 'I':
			CloseAllChannels();
			SetError(ERR_OK, 0, 0);
			break;

		case 'U':
			if ((command[1] & 0x0f) == 0x0a) {
				Reset();
			} else
				SetError(ERR_SYNTAX30, 0, 0);
			break;

		case 'G':
			if (command[1] != ':')
				SetError(ERR_SYNTAX30, 0, 0);
			else
				ChangeDirCmd(&command[2]);
			break;

		case 'M':
			if (command[1] != '-')
				SetError(ERR_SYNTAX30, 0, 0);
			else
				switch (command[2]) {
					case 'R':
						adr = (command[4] << 8) | (command[3]);
						if (adr < 0xC000)
							errorPtr = (char*)(ram + (adr & 0x07ff));
						else
							errorPtr = (char *)(rom1541 + (adr & 0x3fff));
						if (!(errorLength = command[5]))
							errorLength = 1;
						break;

					case 'W':
						adr = (command[4] << 8) | (command[3]);
						len = command[5];
						if (adr<0x1000)
							for (i=0; i<len; i++)
								ram[(adr+i)&0x0FFF] = command[i+6];
						break;

					case 'E':
						adr = (command[4] << 8) | (command[3]);
						fprintf( stderr, "M-E ($%04X) : not supported with DOS level emulation.\n", adr);
					default:
						SetError(ERR_SYNTAX30, 0, 0);
				}
			break;
		default:
			SetError(ERR_SYNTAX30, 0, 0);
	}
}

void CIECFSDrive::ChangeDirCmd(char *dirpath)
{
	char str[NAMEBUF_LENGTH];
	char *p = str;

	CloseAllChannels();

	if (dirpath[0] == '.' && dirpath[1] == 0) {
		ChangeDir(orig_dir_path);
	} else {
		// Convert directory name
		for (int i=0; i<NAMEBUF_LENGTH && (*p++ = ToASCII(*dirpath++)); i++) ;

		if (!ChangeDir(str))
			SetError(ERR_NOTREADY, 0, 0);
	}
}
