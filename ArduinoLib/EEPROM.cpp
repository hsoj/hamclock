/* implement EEPROM class using a local file.
 * format is %08X %02X\n for each address/byte pair.
 * updates of existing address are performed in place.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "EEPROM.h"

class EEPROM EEPROM;

EEPROM::EEPROM()
{
        filename = NULL;
}

void EEPROM::begin (int s)
{
	if (!filename) {
	    char fn[1024];
	    sprintf (fn, "%s/.rpihamclock_eeprom", getenv ("HOME"));
	    filename = strdup (fn);
	}
}

void EEPROM::write (uint32_t address, uint8_t byte)
{
	// open, create if first time
	FILE *fp = fopen (filename, "r+");
	if (!fp)
	    fp = fopen (filename, "w+");
	if (!fp)
	    return;

	// address as string
	char add_str[32];
	int add_str_l = sprintf (add_str, "%08X", address);

	// scan for address, position offset at start of line if found
	long line_offset = 0L;
	char line[256];
	while (fgets (line, sizeof(line), fp)) {
	    if (strncmp (line, add_str, add_str_l) == 0) {
		fseek (fp, line_offset, SEEK_SET);
		break;
	    }
	    line_offset = ftell (fp);
	}

	// write new
	// printf ("W: %08X %02X\n", address, byte);
	fprintf (fp, "%08X %02X\n", address, byte);

	fclose (fp);
}

uint8_t EEPROM::read (uint32_t address)
{
	// open
	FILE *fp = fopen (filename, "r");
	if (!fp)
	    return (0);

	// search
	char line[256];
	unsigned int a, b;
	while (fgets (line, sizeof(line), fp)) {
	    // sscanf (line, "%x %x", &a, &b); printf ("F: %x %x\n", a, b);
	    if (sscanf (line, "%x %x", &a, &b) == 2 && a == address) {
		fclose (fp);
		return ((uint8_t)b);
	    }
	}

	// done with fp
	fclose (fp);

	// return not found
	return (0);
}
