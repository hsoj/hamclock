/* maidenhead conversion functions.
 *
 * unit test: gcc -D_UNIT_TEST -o maidenhead{,.cpp}
 *   ./maidenhead DM42
 *
 *   ./maidenhead 32 -111
 *
 */

#ifdef _UNIT_TEST

// stand-alone test program

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <math.h>

typedef struct {
    float lat, lng;             // radians north, east
    float lat_d, lng_d;         // degrees
} LatLong;

float rad2deg (float r) { return (57.29578F*r); }
float deg2rad (float d) { return (0.01745329F*d); }

#else

// part of HamClock

#include "HamClock.h"

#endif // !_UNIT_TEST

/* core algorithm to convert location in degrees to grid square.
 */
static void ll2mhCore (char m[5], float lt, float lg)
{
    int16_t o;

    lg = floorf(lg + 180);
    o = lg/20;
    m[0] = 'A' + o;
    m[2] = '0' + (lg-o*20.0F)/2;

    lt = floorf(lt + 90);
    o = lt/10;
    m[1] = 'A' + o;
    m[3] = '0' + (lt-o*10.0F);

    m[4] = '\0';
}

/* convert lat_d,lng_d to 4-character maidenhead designation string.
 *   longitude: [A .. R] [-180 .. 180] / 20
 *   latitude:  [A .. R] [ -90 ..  90] / 10
 * grids grow northward from -90 and westward from -180
 * maid[0] is for full precision lat/long
 * maid[1] is for grid at lat/long rounded to whole degrees
 */
void ll2maidenhead (char maid[2][5], const LatLong &ll)
{

    // Serial.printf ("ll2grid: %g\t%g\n", ll.lng_d, ll.lat_d);

    // use full precision for maid[0]
    ll2mhCore (maid[0], ll.lat_d, ll.lng_d);

    // Serial.printf ("Prime: %g\t%g\t%s\n", lg, lt, maid[0]);

    // if not already on grid boundary, find location of boundary
    if (truncf(ll.lat_d) == ll.lat_d && truncf(ll.lng_d) == ll.lng_d) {
        memcpy (maid[1], maid[0], 5);
    } else {
        float lt = roundf(ll.lat_d);
        float lg = roundf(ll.lng_d);
        ll2mhCore (maid[1], lt, lg);
    }

    // Serial.printf ("Secondary: %g\t%g\t%s\n", lg, lt, maid[1]);
}

/* core algorithm to convert 4-char maidenhead string to ll, sw corner
 */
void maidenhead2ll (LatLong &ll, const char maid[5])
{
    ll.lng_d = (toupper(maid[0]) - 'A') * 20 + (toupper(maid[2]) - '0') * 2 - 180;
    ll.lng = deg2rad (ll.lat_d);

    ll.lat_d = (toupper(maid[1]) - 'A') * 10 + (toupper(maid[3]) - '0') - 90;
    ll.lat = deg2rad (ll.lat_d);
}


#if !defined(_UNIT_TEST)

/* draw the NVRAM grid square in the given screen location
 */
void drawMaidenhead(NV_Name nv, SBox &b, uint16_t color)
{
    uint32_t mnv;
    if (!NVReadUInt32(nv, &mnv))
        return; // nothing yet
    char maid[5];
    memcpy (maid, &mnv, 4);
    maid[4] = '\0';

    tft.fillRect (b.x, b.y, b.w, b.h, RA8875_BLACK);

    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (color);
    tft.setCursor (b.x, b.y+b.h-7);
    tft.print (maid);
}

/* set NVRAM nv to the primary maidenhead location for ll
 */
void setMaidenhead(NV_Name nv, LatLong &ll)
{
    char maid[2][5];
    ll2maidenhead (maid, ll);

    uint32_t mnv;
    memcpy (&mnv, maid[0], 4);
    NVWriteUInt32 (nv, mnv);
}

/* toggle the grid square in NVRAM nv for ll 
 */
void toggleMaidenhead(NV_Name nv, LatLong &ll)
{
    char maid[2][5];
    ll2maidenhead (maid, ll);

    uint8_t choice = 0;
    uint32_t mnv;
    if (NVReadUInt32 (nv, &mnv)) {
        uint32_t m0;
        memcpy (&m0, maid[0], 4);
        if (mnv == m0)
            choice = 1;
    }

    memcpy (&mnv, maid[choice], 4);
    NVWriteUInt32 (nv, mnv);
}

#endif // !_UNIT_TEST

#ifdef _UNIT_TEST

int main (int ac, char *av[])
{
    if (ac == 2) {
	// given maidenhead, find ll
	LatLong ll;
	char *maid = av[1];
	maidenhead2ll (ll, maid);
	printf ("%s: %g %g\n", maid, ll.lat_d, ll.lng_d);
    } else if (ac == 3) {
	// given ll, find maidenhead
	LatLong ll;
	ll.lat_d = atof(av[1]);
	ll.lng_d = atof(av[2]);
	char maid[2][5];
	ll2maidenhead (maid, ll);
	printf ("%g %g: %s (alt %s)\n", ll.lat_d, ll.lng_d, maid[0], maid[1]);
    } else {
	fprintf (stderr, "Purpose: comvert between lat/long and maidenhead grid square.\n");
	fprintf (stderr, "Usage 1: %s <grid>\n", av[0]);
	fprintf (stderr, "Usage 2: %s <lat> <long>\n", av[0]);
	exit (1);
    }

    return (0);
}

#endif // _UNIT_TEST
