#ifndef _EEPROM_H
#define _EEPROM_H

#include <stdint.h>

/* EEPROM class that uses a local file
 */

class EEPROM
{
    public:

        EEPROM(void);
	bool commit() { return (true); }
	void begin(int s);
	void write (uint32_t address, uint8_t byte);
	uint8_t read (uint32_t address);

    private:

	char *filename;
};

extern class EEPROM EEPROM;

#endif // _EEPROM_H
