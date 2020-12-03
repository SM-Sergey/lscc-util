// lscc_util.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "CH341DLL.H"

// simple routine for skipping spaces and non-printable symbols
char* skipsp(char* s)
{
  while (*s && *s <= 0x20) s++;
  return s;
}

// Lattice MACHXO2 flash controller definitions
#define CFGCR   0x70
#define CFGTXDR 0x71
#define CFGSR   0x72
#define CFGRXDR 0x73

// Specific to flash access in GaN module (+0x80 to address = disable auto increment)
#define NOINC 0x80

// Lattice flash controller commands
#define IDCODE_PUB        0xE0
#define UIDCODE_PUB       0x19
#define ISC_ENABLE_X      0x74
#define USERCODE          0xC0
#define ISC_DISABLE       0x26
#define BYPASS            0xFF
#define LSC_INIT_ADDR_UFM 0x47
#define LSC_READ_TAG      0xCA
#define LSC_ERASE_TAG     0xCB
#define LSC_CHECK_BUSY    0xF0
#define LSC_READ_STATUS   0x3C
#define LSC_PROG_TAG      0xC9

// Global var's
unsigned char i2c_addr = 0x50;
unsigned char addr = 0;

// Send framed command to FPGA's Flash controller
void flash_control(int cmd, int np, unsigned char* params, int nr, unsigned char *rdbuf)
{
	unsigned char iobuf[128];

	// Start frame
	iobuf[0] = i2c_addr << 1;
	iobuf[1] = CFGCR;
	iobuf[2] = 0x80; // WBCE bit
	CH341StreamI2C(0, 3, iobuf, 0, iobuf);

	// Send command
	iobuf[0] = i2c_addr << 1;
	iobuf[1] = CFGTXDR | NOINC;
	iobuf[2] = cmd;
	// copy parameters
	if (np) memcpy(iobuf+3, params, np);
	CH341StreamI2C(0, 3+np, iobuf, 0, iobuf);

	if (nr) {
		// Read data if requested
		iobuf[0] = i2c_addr << 1;
		iobuf[1] = CFGRXDR | NOINC;
		CH341StreamI2C(0, 2, iobuf, nr, rdbuf);
	}

	// End frame
	iobuf[0] = i2c_addr << 1;
	iobuf[1] = CFGCR;
	iobuf[2] = 0x00;
	CH341StreamI2C(0, 3, iobuf, 0, iobuf);
}

// Read ID from FPGA
int read_id (void)
{
	unsigned char param[4] = {0,0,0,0};
	unsigned char rdbuf[4];

	flash_control(IDCODE_PUB, 3, param, 4, rdbuf);

	return rdbuf[3] | (rdbuf[2] << 8) | (rdbuf[1] << 16) | (rdbuf[0] << 24);
}

// Read UID from FPGA
int read_uid (void)
{
	unsigned char param[4] = {0,0,0,0};
	unsigned char rdbuf[4];

	flash_control(UIDCODE_PUB, 3, param, 4, rdbuf);

	return rdbuf[3] | (rdbuf[2] << 8) | (rdbuf[1] << 16) | (rdbuf[0] << 24);
}

// Read USERCODE from FPGA
int read_usercode (void)
{
	unsigned char param[4] = {8,0,0,0};
	unsigned char rdbuf[4];

	// Enable Flash controller
	flash_control(ISC_ENABLE_X, 3, param, 0, NULL);
	// Wait for controller not needed - 5 us guaranteed by SMBus
	// Read USERCODE
	param[0] = 0;
	flash_control(USERCODE, 3, param, 4, rdbuf);
	// Disable Flash controller
	flash_control(ISC_DISABLE, 3, param, 0, NULL);
	// "Bypass"
	param[0] = param[1] = param[2] = 0xFF;
	flash_control(BYPASS, 3, param, 0, NULL);

	return rdbuf[3] | (rdbuf[2] << 8) | (rdbuf[1] << 16) | (rdbuf[0] << 24);
}

// Read User defaults from FPGA
int read_userdata (int* flg)
{
	unsigned char param[4] = {8,0,0,0};
	unsigned char rdbuf[16];

	// Enable Flash controller
	flash_control(ISC_ENABLE_X, 3, param, 0, NULL);
	// Wait for controller not needed - 5 us guaranteed by SMBus
	param[0] = 0;
	// Init UFM address
	flash_control(LSC_INIT_ADDR_UFM, 3, param, 0, NULL);
	// Read 1 page from UFM
	param[0] = 0x10;
	param[1] = 0x00;
	param[2] = 0x01; // 1 page
	flash_control(LSC_READ_TAG, 3, param, 16, rdbuf);
	// Disable Flash controller
	param[0] = param[1] = param[2] = 0;
	flash_control(ISC_DISABLE, 3, param, 0, NULL);
	// "Bypass"
	param[0] = param[1] = param[2] = 0xFF;
	flash_control(BYPASS, 3, param, 0, NULL);

	*flg = rdbuf[0] ? TRUE : FALSE;

	return rdbuf[4] | (rdbuf[3] << 8) | (rdbuf[2] << 16) | (rdbuf[1] << 24);
}


int write_userdata (int en, int ud)
{
	unsigned char param[20] = {8,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0};
	unsigned char rdbuf[4];
	int rv, tm;

	// Enable Flash controller
	flash_control(ISC_ENABLE_X, 3, param, 0, NULL);
	// Wait for controller not needed - 5 us guaranteed by SMBus
	param[0] = 0;
	// Erase UFM
	tm = GetTickCount();
	flash_control(LSC_ERASE_TAG, 3, param, 0, NULL);
	// Wait for !busy
	do {
		flash_control(LSC_CHECK_BUSY, 3, param, 1, rdbuf);
	} while (rdbuf[0] & 0x80);
	tm = GetTickCount()-tm;
	printf("Erase is done in %i ms\n\r", tm);
	// read status
	flash_control(LSC_READ_STATUS, 3, param, 4, rdbuf);
	rv = (rdbuf[2] & 0x20) ? FALSE : TRUE; // FALSE if FAIL
	
	if (en && rv) {
		tm = GetTickCount();
		// Init UFM address
		flash_control(LSC_INIT_ADDR_UFM, 3, param, 0, NULL);
		// Write 1 page to UFM
		param[0] = 0x00;
		param[1] = 0x00;
		param[2] = 0x01;
		param[3] = 1;
		param[4] = (ud >> 24) & 0xFF;
		param[5] = (ud >> 16) & 0xFF;
		param[6] = (ud >> 8 ) & 0xFF;
		param[7] = (ud >> 0 ) & 0xFF;
		flash_control(LSC_PROG_TAG, 3+16, param, 0, NULL);
		// wait for busy
		param[0] = param[1] = param[2] = 0;
		do {
			flash_control(LSC_CHECK_BUSY, 3, param, 1, rdbuf);
		} while (rdbuf[0] & 0x80);
		tm = GetTickCount()-tm;
		printf("Write is done in %i ms\n\r", tm);
		// read status
		flash_control(LSC_READ_STATUS, 3, param, 4, rdbuf);
		rv = (rdbuf[2] & 0x20) ? FALSE : TRUE; // FALSE if FAIL
	}

	// Disable Flash controller
	param[0] = param[1] = param[2] = 0;
	flash_control(ISC_DISABLE, 3, param, 0, NULL);
	// "Bypass"
	param[0] = param[1] = param[2] = 0xFF;
	flash_control(BYPASS, 3, param, 0, NULL);

	return rv;
}

/////////////////
// options table
static char* opts[] = {
	"i",			// 0
	"-i2c_addr",	// 1
	"a",			// 2
	"-addr",		// 3
	"r",			// 4
	"-read",		// 5
	"w",			// 6
	"-write",		// 7
	"h",			// 8
	"?",			// 9
	"-help",		// 10
	"-uid",         // 11
	"-uc",          // 12
	"-ud",          // 13
	"-erase_ud",    // 14
	"-set_ud",      // 15
	NULL
};

int _tmain(int argc, _TCHAR* argv[])
{

    HANDLE hnd;
	BOOL b;
	unsigned char iobuf[34];
	LPSTR cmdl;
	int err;
	char *s;
	int i, l;
	int op = 0;
	int len, val;

	// Command line
	cmdl = GetCommandLineA();

	// Skip path to self
	if (cmdl[0] == '"')
	{
		// skip to leading '"'
		cmdl = strchr(cmdl+1, '"');
	}
	else
	{
		// skip to space
		s = strchr(cmdl, ' ');
		if (!s) s = strchr(cmdl, '\t');
		if (!s) s = cmdl + strlen(cmdl);
		cmdl = s;
	}

	if (cmdl && *cmdl) 
		cmdl++;
	else {
		printf("Parser internal error 1\n\r");
		exit(1);
	}

	// Command line parser
	err = 0;
	do 
	{
		cmdl = skipsp(cmdl);
		if (!*cmdl)
			break;
		if (*cmdl == '-')
		{
			cmdl++;
			// find option in table
			i=0;
			while (opts[i]) {
				if (!strnicmp(cmdl, opts[i], strlen(opts[i])))
					break;
				i++;
			}

			if (!opts[i]) {
				// not found
				err = 2;
				break;
			} else {
				// skip option text and spaces, go to next position
				cmdl = skipsp(cmdl+strlen(opts[i]));
				// process options
				switch(i) {
				case 8:
				case 9:
				case 10:
					printf("LSCC control utility v1.0a\n\r\n\r");
					printf("USAGE: lscc_util <options>\n\r");
					printf("Generic options:\n\r");
					printf("  -i <val>, --i2c_addr <val>   - set device address to <val>, by default 0x50\n\r");
					printf("  -a <val>, --addr <val>       - set register address to <val>, by default 0\n\r");
					printf("\n\r");
					printf("Actions:\n\r");
					printf("  -h, -?, --help               - display this help\n\r");
					printf("  -r <len>, --read <len>       - read from device's register[s] <len> bytes\n\r");
					printf("                                 device and register addresses are set by '-i'/'-a' opts\n\r");
					printf("  -w <b0> .. <bN>, --write ..  - write to device's register[s] bytes <b0>...<bN>\n\r");
					printf("                                 device and register addresses are set by '-i'/'-a' opts\n\r");
					printf("  --uid                        - read UID from FPGA\n\r");
					printf("  --uc                         - read USERCODE (factory defaults) from FPGA\n\r");
					printf("  --ud                         - read user defaults from FPGA\n\r");
					printf("  --erase_ud                   - erase and disable user defaults in FPGA\n\r");
					printf("  --set_ud <val>               - erase and enable and set user defaults to <val> in FPGA\n\r");

					printf("\n\r");
					exit(0);
					break;


				case 0:
				case 1:
					// i2c_addr
					if (!isdigit(*cmdl)) {
						err = 3;
						break;
					}
					l = strtol(cmdl, &cmdl, 0);
					if (l < 0 || l > 0x7F)
						err = 4;
					i2c_addr = l;
					break;
				case 2:
				case 3:
					// addr
					if (!isdigit(*cmdl)) {
						err = 3;
						break;
					}
					l = strtol(cmdl, &cmdl, 0);
					if (l < 0 || l > 0xFF)
						err = 5;
					addr = l;
					break;
				case 4:
				case 5:
					// Read - one parameter, length of data, optional
					if (op) err = 6;
					else {
						if (!isdigit(*cmdl)) {
							len = 1; // no length
						} else {
							l = strtol(cmdl, &cmdl, 0);
							if (l < 1 || l > 32)
								err = 7;
							len = l;
						}
						op = 1;
					}
					break;
				case 6:
				case 7:
					// Write - N parameters => N data bytes
					if (op) err = 6;
					else {
						len = 0;
						while (isdigit(*cmdl) || (*cmdl == '-' && isdigit(cmdl[1]))) {
							l = strtol(cmdl, &cmdl, 0);
							cmdl = skipsp(cmdl);
							if (l < -128 || l > 255) {
								err = 8;
								break;
							}
							if (len >= 32) {
								err = 7;
								break;
							}
							iobuf[2+(len++)] = l;
						}
						op = 2;
					}
					break;
				case 11:
				case 12:
				case 13:
				case 14:
					if (op) err = 6;
					else {
						op = i - 8; // 11 => 3; 12 => 4;
					}
					break;
				case 15:
					// set_ud
					if (!isdigit(*cmdl)) {
						err = 3;
						break;
					}
					val = strtol(cmdl, &cmdl, 0);
					op = 7;
					break;
				}

				if (err)
					break;
			}

		} else {
			err = 1;
			break;
		}
	} while (*cmdl);

	switch(err) {
	case 0:
		break;
	case 1:
		printf("Syntax error in command line\n\r");
		break;
	case 2:
		printf("Invalid option in command line\n\r");
		break;
	case 3:
		printf("Invalid value in command line\n\r");
		break;
	case 4:
		printf("I2C address out of range\n\r");
		break;
	case 5:
		printf("Register address out of range\n\r");
		break;
	case 6:
		printf("Incompatible operations at one time\n\r");
		break;
	case 7:
		printf("Invalid length requested\n\r");
		break;
	case 8:
		printf("Data byte out of range\n\r");
		break;
	default:
		printf("Parser internal error 2\n\r");
		break;
	}

	if (err)
		exit(err+1);

	hnd = CH341OpenDevice(0);

	if (hnd != INVALID_HANDLE_VALUE)
	{
		printf("I2C driver open success.\n\r");

		switch (op) {
			case 1:
				iobuf[0] = i2c_addr << 1;
				iobuf[1] = addr;
				b = CH341StreamI2C(0, 2, iobuf, len, iobuf);
				if (!b)
					printf("SMBus read failed\n\r");
				else {
					printf("SMBus RD dev(0x%02X) addr(0x%02X) =>", i2c_addr, addr);
					for (i=0; i<len; i++) printf(" %02X", iobuf[i]);
					printf("\n\r");
				}
				break;
			case 2:
				iobuf[0] = i2c_addr << 1;
				iobuf[1] = addr;
				b = CH341StreamI2C(0, 2+len, iobuf, 0, iobuf);
				if (!b)
					printf("SMBus write failed\n\r");
				else {
					printf("SMBus WR dev(0x%02X) addr(0x%02X) =>", i2c_addr, addr);
					for (i=0; i<len; i++) printf(" %02X", iobuf[i+2]);
					printf("\n\r");
				}
				break;
			case 3:
			case 4:
			case 5:
			case 6:
			case 7:
				// read UID // USERCODE // User data // Erase_UD
				i = read_id();
				if (i == -1 || !i) {
					printf("Error reading FPGA ID (bad address or electrical problems)\n\r");
					break;
				}
				printf("FPGA ID = %08X\n\r", i);
				switch (op) {
				case 3:
					// read UID
					i = read_uid();
					printf("FPGA UID = %08X\n\r", i);
					break;
				case 4:
					// read USERCODE
					i = read_usercode();
					printf("Factory defaults (USERCODE) = %08X\n\r", i);
					break;
				case 5:
					// read user defaults
					i = read_userdata(&b);
					printf("User defaults %s; data = %08X\n\r", b ? "enabled" : "disabled", i);
					break;
				case 6:
					// disable user defaults
					i = write_userdata(0,0);
					printf("Erasing user data %s\n\r", i ? "successful" : "failed");
					break;
				case 7:
					// disable user defaults
					i = write_userdata(1,val);
					printf("Setting user data %s\n\r", i ? "successful" : "failed");
					break;
				}				
				break;
		}

		CH341CloseDevice(0);
	} else
		printf("Couldn't open I2C driver\n\r");

	return 0;
}

