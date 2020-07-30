#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

#include "Arduino.h"

char *our_name;

/* return milliseconds since first call
 */
uint32_t millis(void)
{
	static struct timespec t0;

	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC_RAW, &t);

	if (t0.tv_sec == 0 && t0.tv_nsec == 0)
	    t0 = t;

	int32_t dt = (t.tv_sec - t0.tv_sec)*1000 + (t.tv_nsec - t0.tv_nsec)/1000000;
	// printf ("millis %u: %ld.%09ld - %ld.%09ld\n", dt, t.tv_sec, t.tv_nsec, t0.tv_sec, t0.tv_nsec);
	return (dt);
}

void delay (uint32_t ms)
{
	usleep (ms*1000);
}

int random(int max)
{
	return ((int)((max-1.0F)*::random()/RAND_MAX));
}

uint16_t analogRead(int pin)
{
	return (0);		// not supported on Pi, consider https://www.adafruit.com/product/1083
}



/* Every normal C program requires a main().
 * This is provided as magic in the Arduino IDE so here we must do it ourselves.
 */
int main (int ac, char *av[])
{
	// save our name for remote update
	our_name = av[0];

	// one time
	setup();

	// loop forever
	for (;;) {
	    loop();

            // keep cpu below 90%
            static uint32_t delay_m;
            uint32_t m = millis();
            if (m - delay_m > 10) {
                delay(1);
                delay_m = m;
            }
	}
}
