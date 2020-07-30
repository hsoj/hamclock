#ifndef _ARDUINO_H
#define _ARDUINO_H

/* Arduino.h over unix
 */

#if defined(__arm__) && defined(__linux__)
#define _IS_RPI
#endif

#include <stdint.h>
#include <string>

#define	String std::string

#include "ESP.h"
#include "Serial.h"

#define	pinMode(x,y)
#define	digitalWrite(a,b)
#define	digitalRead(a)  a
#define	randomSeed(x)

#define	PROGMEM	
#define	F(X)	 X
#define	PSTR(X)	 X
#define FPSTR(X) X
#define PGM_P    const char *
#define	__FlashStringHelper char
#define strlen_P  strlen
#define strcpy_P  strcpy
#define strcmp_P  strcmp
#define strncmp_P  strncmp
#define strspn_P  strspn

#define LSBFIRST 0
#define MSBFIRST 1

// normally in cores/esp8266/flash_utils.h
#define FLASH_SECTOR_SIZE       4096

#define	OUTPUT	1
#define	HIGH	1
#define	A0	0
#define	pgm_read_byte(a)	(*(a))
#define	pgm_read_word(a)	(*(a))
#define	pgm_read_float(a)	(*(a))

extern uint32_t millis(void);
extern int random(int max);
extern void delay (uint32_t ms);
extern uint16_t analogRead(int pin);
extern void setup(void);
extern void loop(void);
extern char *our_name;


#endif // _ARDUINO_H
