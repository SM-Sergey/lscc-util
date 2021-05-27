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
#define IDCODE_PUB           0xE0
#define UIDCODE_PUB          0x19
#define ISC_ENABLE_X         0x74
#define USERCODE             0xC0
#define ISC_DISABLE          0x26
#define BYPASS               0xFF
#define LSC_INIT_ADDR_UFM    0x47
#define LSC_READ_TAG         0xCA
#define LSC_ERASE_TAG        0xCB
#define LSC_CHECK_BUSY       0xF0
#define LSC_READ_STATUS      0x3C
#define LSC_PROG_TAG		 0xC9
#define ISC_PROGRAM_USERCODE 0xC2

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
unsigned __int64 read_uid (void)
{
	unsigned char param[4] = {0,0,0,0};
	unsigned char rdbuf[8];

	flash_control(UIDCODE_PUB, 3, param, 8, rdbuf);

	return (unsigned __int64)rdbuf[7] | ((unsigned __int64)rdbuf[6] << 8) | ((unsigned __int64)rdbuf[5] << 16) | ((unsigned __int64)rdbuf[4] << 24) | 
		((unsigned __int64)rdbuf[3] << 32) | ((unsigned __int64)rdbuf[2] << 40) | ((unsigned __int64)rdbuf[1] << 48) | ((unsigned __int64)rdbuf[0] << 56);
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
int read_userdata (void)
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

	return rdbuf[3] | (rdbuf[2] << 8) | (rdbuf[1] << 16) | (rdbuf[0] << 24);
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
		ud |= 0x80000000; // Enable defaults bit
		param[0] = 0x00;
		param[1] = 0x00;
		param[2] = 0x01;
		param[3] = (ud >> 24) & 0xFF;
		param[4] = (ud >> 16) & 0xFF;
		param[5] = (ud >> 8 ) & 0xFF;
		param[6] = (ud >> 0 ) & 0xFF;
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

int write_usercode (int uc)
{
	unsigned char param[20] = {8,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0};
	unsigned char rdbuf[4];
	int rv, tm;

	// Enable Flash controller
	flash_control(ISC_ENABLE_X, 3, param, 0, NULL);
	// Wait for controller not needed - 5 us guaranteed by SMBus
	param[0] = 0;
	tm = GetTickCount();
	// Write USERCODE
	uc |= 0x80000000; // Enable defaults bit
	param[0] = 0x00;
	param[1] = 0x00;
	param[2] = 0x00;
	param[3] = (uc >> 24) & 0xFF;
	param[4] = (uc >> 16) & 0xFF;
	param[5] = (uc >> 8 ) & 0xFF;
	param[6] = (uc >> 0 ) & 0xFF;
	flash_control(ISC_PROGRAM_USERCODE, 3+4, param, 0, NULL);
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
	"-set_uc",		// 16
	"n",			// 17
	"-no_addr",     // 18
	"-dt",			// 19
	"-ot",			// 20
	"-oc",			// 21
	"p",			// 22
	"-perm",		// 23
	"v",			// 24
	"-adc",			// 25
	"-test",		// 26
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
	int omit_addr = 0;
	int dt=-1, ot=-1, oc=-1;
	int perm=0;
	int adr_set=0;
	int verb=0;
	int dt_val=0, ot_val = 0, oc_val=0;
	int d[4];
	double flt;
	unsigned __int64 i64;

	printf("LSCC control utility v1.0a\n\r\n\r");

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

	if (cmdl) {
		if (*cmdl) cmdl++;
	} else {
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
					printf("USAGE: lscc_util <options>\n\r");
					printf("Generic options:\n\r");
					printf("  -i <val>, --i2c_addr <val>   - set device address to <val>, by default 0x50 (0x60 for DAC commands)\n\r");
					printf("  -a <val>, --addr <val>       - set register address to <val>, by default 0\n\r");
					printf("\n\r");
					printf("Generic actions:\n\r");
					printf("  -h, -?, --help               - display this help\n\r");
					printf("  -r <len>, --read <len>       - read from device's register[s] <len> bytes\n\r");
					printf("                                 device and register addresses are set by '-i'/'-a' opts\n\r");
					printf("  -w <b0> .. <bN>, --write ..  - write to device's register[s] bytes <b0>...<bN>\n\r");
					printf("                                 device and register addresses are set by '-i'/'-a' opts\n\r");
					printf("  -n, --no_addr                - Omit address/command phase in read/write commands\n\r");
					printf("\n\r");
					printf("Lattice FPGA actions:\n\r");
					printf("  --uid                        - read UID from FPGA\n\r");
					printf("  --uc                         - read USERCODE (factory defaults) from FPGA\n\r");
					printf("  --ud                         - read user defaults from FPGA\n\r");
					printf("  --erase_ud                   - erase and disable user defaults in FPGA\n\r");
					printf("  --set_ud <val>               - erase and enable and set user defaults to <val> in FPGA\n\r");
					printf("  --set_uc <val>               - Set USERCODE (factory defaults) to <val> in FPGA\n\r");
					printf("\n\r");
					printf("NOTE: --set_ud and --set_uc commands automatically sets high bit of <val> \n\r");
					printf("NOTE: this enables applying of these defaults when FPGA initializes\n\r");
					printf("\n\r");
					printf("DAC actions (on GaN module):\n\r");
					printf("  --dt <ns>                    - set dead time to <ns> ns\n\r");
					printf("  --ot <temp>                  - set OT level to <temp> Celsius degrees\n\r");
					printf("  --oc <amps>                  - set OC level to <amps> Amperes\n\r");
					printf("  -p, --perm                   - update not only current values, and EEPROM also\n\r");
					printf("\n\r");
					printf("ADC actions (on GaN module):\n\r");
					printf("  --adc                        - read ADC and display translated results\n\r");
					printf("\n\r");
					printf("Other actions (on GaN module):\n\r");
					printf("  --test                       - continuous test of I2C bus (CTRL-C to break)\n\r");
					
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
					adr_set = 1;
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
						op = i - 8; // 11 => 3; 12 => 4; ...
					}
					break;
				case 15:
				case 16:
					// set_ud, set_uc
					if (!isdigit(*cmdl)) {
						err = 3;
						break;
					}
					val = strtoul(cmdl, &cmdl, 0);
					op = 7 + (i-15); // opcodes 7, 8
					break;
				case 17:
				case 18:
					omit_addr = 1;
					break;

				case 19: // DT
					if (op && op != 9) {
						err = 6;
						break;
					}
					if (!isdigit(*cmdl)) {
						err = 3;
						break;
					}
					op = 9;
					dt = strtoul(cmdl, &cmdl, 0);
					if (dt < 2 || dt > 20) {
						err = 9;
						break;
					}
					break;
				case 20: // OT
					if (op && op != 9) {
						err = 6;
						break;
					}
					if (!isdigit(*cmdl)) {
						err = 3;
						break;
					}
					op = 9;
					ot = strtoul(cmdl, &cmdl, 0);
					if (ot < 20 || ot > 125) {
						err = 10;
						break;
					}
					break;
				case 21: // OC
					if (op && op != 9) {
						err = 6;
						break;
					}
					if (!isdigit(*cmdl)) {
						err = 3;
						break;
					}
					op = 9;
					oc = strtoul(cmdl, &cmdl, 0);
					if (oc < 1 || oc > 32) {
						err = 11;
						break;
					}
					break;
				case 22:
				case 23:
					perm = 1;
					break;
				case 24:
					verb = 1;
					break;
				case 25: // ADC
					if (op) err = 6;
					else {
						op = 10;
					}
					break;
				case 26:
					if (op) err = 6;
					else {
						op = 11;
					}
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
	case 9:
		printf("Dead time should be from 2 to 20 ns\n\r");
		break;
	case 10:
		printf("Temperature should be from 20 to 125 C\n\r");
		break;
	case 11:
		printf("Current should be from 1 to 32 A\n\r");
		break;
	default:
		printf("Parser internal error 2\n\r");
		break;
	}

	if (err)
		exit(err+1);

	if (!op) {
		printf("No action given. Please use options (-h for help).\n\r");
		exit(0);
	}

	if (op > 2 && omit_addr)
	{
		printf("Can't omit address/command phase in non-generic commands\n\r");
		exit(-1);
	}

	if (op != 9 && perm)
	{
		printf("Permanent option can be applied only to DAC actions\n\r");
		exit(-2);
	}

	hnd = CH341OpenDevice(0);

	if (hnd != INVALID_HANDLE_VALUE)
	{
		printf("I2C driver open success.\n\r");

		switch (op) {
			case 1:
				iobuf[0] = i2c_addr << 1;
				iobuf[1] = addr;
				b = CH341StreamI2C(0, omit_addr ? 1 : 2, iobuf, len, iobuf);
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
				if (omit_addr)
					memmove(&iobuf[1], &iobuf[2], len);
				b = CH341StreamI2C(0, (omit_addr ? 1 : 2) + len, iobuf, 0, iobuf);
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
			case 8:
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
					i64 = read_uid();
					printf("FPGA UID = %08X%08X\n\r", (int)(i64 >> 32), (int)i64);
					break;
				case 4:
					// read USERCODE
					i = read_usercode();
					printf("Factory defaults (USERCODE) = %08X\n\r", i);
					break;
				case 5:
					// read user defaults
					i = read_userdata();
					printf("User defaults %s; data = %08X\n\r", (i & 0x80000000) ? "enabled" : "disabled", i);
					break;
				case 6:
					// disable user defaults
					i = write_userdata(0,0);
					printf("Erasing user data %s\n\r", i ? "successful" : "failed");
					break;
				case 7:
					// enable and set user defaults
					i = write_userdata(1,val);
					printf("Setting user data to 0x%08X %s\n\r", val, i ? "successful" : "failed");
					break;
				case 8:
					// enable and set factory defaults (USERCODE)
					i = write_usercode(val);
					printf("Setting USERCODE to 0x%08X %s\n\r", val, i ? "successful" : "failed");
					break;
				}				
				break;
			case 9:
				printf("Setting DAC to");
				if (dt >= 0) printf(" DT=%ins", dt);
				if (ot >= 0) printf(" OT=%i\176C", ot);
				if (oc >= 0) printf(" OC=%iA", oc);
				if (perm) printf(" and writing to EEPROM");
				printf ("\n\r");

				if (!adr_set) i2c_addr = 0x60;

				// DT
				if (dt >= 0) {
					dt_val = 3600 - dt*100;
					dt_val |= 0x8000;
					if (verb) printf("dt_val = 0x%04X\n\r", dt_val);
					if (perm) {
						iobuf[0] = i2c_addr << 1;
						iobuf[1] = 0x54;
						iobuf[2] = dt_val >> 8;
						iobuf[3] = dt_val & 0xFF;
						iobuf[4] = dt_val >> 8;
						iobuf[5] = dt_val & 0xFF;
						b = CH341StreamI2C(0, 6, iobuf, 0, iobuf);
					} else {
						iobuf[0] = i2c_addr << 1;
						iobuf[1] = 0x44;
						iobuf[2] = dt_val >> 8;
						iobuf[3] = dt_val & 0xFF;
						iobuf[4] = 0x46;
						iobuf[5] = dt_val >> 8;
						iobuf[6] = dt_val & 0xFF;
						b = CH341StreamI2C(0, 7, iobuf, 0, iobuf);
					}
					if (!b)
						printf("SMBus write failed\n\r");
					Sleep(100);
				}

				// OT
				if (ot >= 0) {
					ot_val = 1000 + ot*20;
					ot_val |= 0x8000;
					if (verb) printf("ot_val = 0x%04X\n\r", ot_val);
					iobuf[0] = i2c_addr << 1;
					iobuf[1] = perm ? 0x5A : 0x42;
					iobuf[2] = ot_val >> 8;
					iobuf[3] = ot_val & 0xFF;

					b = CH341StreamI2C(0, 4, iobuf, 0, iobuf);
					if (!b)
						printf("SMBus write failed\n\r");
					Sleep(100);
				}

				// OC
				if (oc >= 0) {
					oc_val = oc*100;
					oc_val |= 0x9000;
					if (verb) printf("oc_val = 0x%04X\n\r", oc_val);
					iobuf[0] = i2c_addr << 1;
					iobuf[1] = perm ? 0x58 : 0x40;
					iobuf[2] = oc_val >> 8;
					iobuf[3] = oc_val & 0xFF;

					b = CH341StreamI2C(0, 4, iobuf, 0, iobuf);
					if (!b)
						printf("SMBus write failed\n\r");
					Sleep(100);
				}

				// Read all
				iobuf[0] = 1 | (i2c_addr << 1);
				b = CH341StreamI2C(0, 1, iobuf, 24, iobuf);
				if (!b)
					printf("SMBus read failed\n\r");

				b = 0;
				// verify OC
				if (oc_val) {
					if (oc_val != ((iobuf[1] << 8) | iobuf[2]))
						b = 1;
					if (perm)
						if (oc_val != ((iobuf[4] << 8) | iobuf[5]))
							b = 1;
				}
				// verify OT
				if (ot_val) {
					if (ot_val != ((iobuf[7] << 8) | iobuf[8]))
						b = 1;
					if (perm)
						if (ot_val != ((iobuf[10] << 8) | iobuf[11]) )
							b = 1;
				}
				// verify DT
				if (dt_val) {
					if ( (dt_val != ((iobuf[13] << 8) | iobuf[14])) || (dt_val != ((iobuf[19] << 8) | iobuf[20])))
						b = 1;
					if (perm)
						if ( (dt_val != ((iobuf[16] << 8) | iobuf[17])) || (dt_val != ((iobuf[22] << 8) | iobuf[23])))
							b = 1;
				}

				printf ("Verify %s", b ? "Error" : "OK");
				if (b || verb) {
					printf ("; Readback =");
					for (i=0; i<8; i++)
						printf (" 0x%04X", (iobuf[i*3+1]<<8)|iobuf[i*3+2]); 
				}
				printf("\r\n");
				break;
			case 10:
				// ADC
				printf("Reading ADC.\n\r");
				if (!adr_set) i2c_addr = 0x48;

				iobuf[0] = i2c_addr << 1;
				iobuf[1] = 0x01;
				iobuf[2] = 0xC3;
				iobuf[3] = 0x03;
				b = CH341StreamI2C(0, 4, iobuf, 0, iobuf);
				if (!b)
					printf("SMBus write failed\n\r");
				Sleep(50);
				iobuf[0] = i2c_addr << 1;
				iobuf[1] = 0x00;
				b = CH341StreamI2C(0, 2, iobuf, 2, iobuf);
				d[0] = (iobuf[0] << 8) | iobuf[1];
				if (!b)
					printf("SMBus read failed\n\r");

				iobuf[0] = i2c_addr << 1;
				iobuf[1] = 0x01;
				iobuf[2] = 0xD3;
				iobuf[3] = 0x03;
				b = CH341StreamI2C(0, 4, iobuf, 0, iobuf);
				if (!b)
					printf("SMBus write failed\n\r");
				Sleep(50);
				iobuf[0] = i2c_addr << 1;
				iobuf[1] = 0x00;
				b = CH341StreamI2C(0, 2, iobuf, 2, iobuf);
				d[1] = (iobuf[0] << 8) | iobuf[1];
				if (!b)
					printf("SMBus read failed\n\r");

				iobuf[0] = i2c_addr << 1;
				iobuf[1] = 0x01;
				iobuf[2] = 0xE3;
				iobuf[3] = 0x03;
				b = CH341StreamI2C(0, 4, iobuf, 0, iobuf);
				if (!b)
					printf("SMBus write failed\n\r");
				Sleep(50);
				iobuf[0] = i2c_addr << 1;
				iobuf[1] = 0x00;
				b = CH341StreamI2C(0, 2, iobuf, 2, iobuf);
				d[2] = (iobuf[0] << 8) | iobuf[1];
				if (!b)
					printf("SMBus read failed\n\r");

				iobuf[0] = i2c_addr << 1;
				iobuf[1] = 0x01;
				iobuf[2] = 0xF3;
				iobuf[3] = 0x03;
				b = CH341StreamI2C(0, 4, iobuf, 0, iobuf);
				if (!b)
					printf("SMBus write failed\n\r");
				Sleep(50);
				iobuf[0] = i2c_addr << 1;
				iobuf[1] = 0x00;
				b = CH341StreamI2C(0, 2, iobuf, 2, iobuf);
				d[3] = (iobuf[0] << 8) | iobuf[1];
				if (!b)
					printf("SMBus read failed\n\r");

				flt = d[0]/32768.0*4.096*12.0;
				printf("VCC = %.2f V\n\r", flt);
				flt = d[1]/32768.0*4.096*51.0;
				printf("PWR = %.2f V\n\r", flt);
				flt = d[2]/32768.0*4.096/50.0/0.002;
				printf("I   = %.2f A\n\r", flt);
				flt = (d[3]/32768.0*4096 - 500.0) / 10.0;
				printf("T   = %.2f \176C\n\r", flt);

				if (verb)
					printf("Raw data 0x%04X 0x%04X 0x%04X 0x%04X\n\r", d[0], d[1], d[2], d[3]);

				break;
			
			case 11:
				// I2C test
				l = 0;
				while (9) {
					val = rand() & 0xFF;
					iobuf[0] = i2c_addr << 1;
					iobuf[1] = 6;
					iobuf[2] = val;
					b = CH341StreamI2C(0, 3, iobuf, 0, iobuf);
					if (!b) {
						printf("\n\rSMBus write failed\n\r");
						break;
					}
					memset(iobuf,0,sizeof(iobuf));
					iobuf[0] = i2c_addr << 1;
					iobuf[1] = 6+128;
					b = CH341StreamI2C(0, 2, iobuf, 8, iobuf);
					if (!b){
						printf("\n\rSMBus read failed\n\r");
						break;
					}
					len = 0;
					for (i=0; i<8; i++)
						if (iobuf[i] != val) {
							printf("\n\rData Error 1 0x%02X / 0x%02X", val, iobuf[i]);
							len++;
						}
					memset(iobuf,0,sizeof(iobuf));
					iobuf[0] = i2c_addr << 1;
					iobuf[1] = 6+128;
					b = CH341StreamI2C(0, 2, iobuf, 8, iobuf);
					if (!b){
						printf("\n\rSMBus read failed\n\r");
						break;
					}
					for (i=0; i<8; i++)
						if (iobuf[i] != val) {
							printf("\n\rData Error 2 0x%02X / 0x%02X", val, iobuf[i]);
							len++;
						}
					if (len) {
						printf("\n\r");
						break;
					}
					l++;
					if ((l&255) == 0)
						printf(".");
				}

				break;
		}

		CH341CloseDevice(0);
	} else
		printf("Couldn't open I2C driver\n\r");

	return 0;
}

