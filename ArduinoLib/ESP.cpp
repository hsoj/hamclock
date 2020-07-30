#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "Arduino.h"
#include "ESP8266WiFi.h"
#include "ESP.h"

class ESP ESP;

ESP::ESP()
{
    sn = 0;
}

uint32_t ESP::getFreeHeap(void)
{
        return (100000000U);
}

void ESP::restart(void)
{
        printf ("Executing %s\n", our_name);
        for (int fd = 3; fd < 1000; fd++)
            (void) close (fd);
        execl (our_name, our_name, (char*)0);

        printf ("%s has disappeared\n", our_name);
        exit(1);
}

/* try to get some sort of system serial number.
 * return 0xFFFFFFFF if unknown.
 */
uint32_t ESP::getChipId()
{
        // reuse once found
        if (sn)
            return (sn);

#if defined(_IS_RPI)

        // try cpu serial number on rpi

        FILE *fp = popen ("awk -F: '/Serial/{print $2}' /proc/cpuinfo", "r");
        if (fp) {
            char buf[1024];
            while (fgets (buf, sizeof(buf), fp)) {
                int l = strlen(buf);                            // includes nl
                if (l >= 9) {                                   // 8 + nl
                    sn = strtoul (&buf[l-9], NULL, 16);         // 8 LSB
                    if (sn) {
                        printf ("Found ChipId '%.*s' -> 0x%X\n", l-1, buf, sn);
                        break;
                    }
                }
            }
            pclose (fp);
            if (sn)
                return (sn);
        }

#endif // _IS_RPI

        // try MAC address

        std::string mac = WiFi.macAddress();
        unsigned int m1, m2, m3, m4, m5, m6;
        if (sscanf (mac.c_str(), "%x:%x:%x:%x:%x:%x", &m1, &m2, &m3, &m4, &m5, &m6) == 6) {
            sn = (m3<<24) + (m4<<16) + (m5<<8) + m6;
            printf ("Found ChipId from MAC '%s' -> 0x%x\n", mac.c_str(), sn);
        } else {
            printf ("No ChipId\n");
            sn = 0xFFFFFFFF;
        }

        return (sn);
}

void yield(void)
{
}


