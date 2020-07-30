/* functions to compute sublunar and subsolar lat and long.
 *
 * unit test:
 *   g++ -DLINIX_STANDALONE_TEST -o astro astro.cpp && ./astro 2016 7 31 19
 *   Lunar RA 6.87175 hours, Dec 18.4172 degrees
 *   sub Lunar lat 18.4172, long -131.752, both degrees
 *   Solar RA 8.7611 hours, Dec 18.0089 degrees
 *   sub Solar lat 18.0089, long -103.412, both degrees
 *
 */

#ifdef LINIX_STANDALONE_TEST

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

// map lat, lng, + radians N and E
typedef struct {
    float lat, lng;
    float lat_d, lng_d;
} LatLong;

#define	myfmodf	fmod

#else

#include "HamClock.h"

#endif // LINIX_STANDALONE_TEST


// handy
float rad2deg (float r) { return (57.29578F*r); }
float deg2rad (float d) { return (0.01745329F*d); }

/* given seconds since 1/1/1970 compute subsolar lat and long.
 * http://aa.usno.navy.mil/faq/docs/SunApprox.php and GAST.php
 */
void subSolar (time_t t, LatLong &ll)
{
    double JD = (t/86400.0) + 2440587.5;
    double D = JD - 2451545.0;
    double g = 357.529 + 0.98560028*D;
    double q = 280.459 + 0.98564736*D;
    double L = q + 1.915*sin(M_PI/180*g) + 0.020*sin(M_PI/180*2*g);
    double e = 23.439 - 0.00000036*D;
    double RA = 180/M_PI*atan2 (cos(M_PI/180*e)*sin(M_PI/180*L), cos(M_PI/180*L));
    ll.lat = asin(sin(M_PI/180*e)*sin(M_PI/180*L));
    ll.lat_d = rad2deg(ll.lat);
#ifdef LINIX_STANDALONE_TEST
    printf ("Solar RA %g hours, Dec %g degrees\n", RA/15, 180/M_PI*ll.lat);
#endif // LINIX_STANDALONE_TEST
    double GMST = myfmodf(15*(18.697374558 + 24.06570982441908*D), 360.0);
    ll.lng_d = myfmodf(RA-GMST+36000.0+180.0, 360.0) - 180.0;
    ll.lng = deg2rad(ll.lng_d);
}

/* given seconds since 1/1/1970 compute sublunar lat and long.
 * http://www.stjarnhimlen.se/comp/ppcomp.html
 */
void subLunar (time_t t, LatLong &ll)
{
    // want days since 1999 Dec 31, 0:00 UT
    double d = (t - 946598400)/(3600.0*24.0);

    /* use this if given year month day hour
     * double d = 367*y - 7 * ( y + (m+9)/12 ) / 4 + 275*m/9 + D - 730530;	// all integer divisions
     * d = d + UT/24.0;
     */

    // obliquity of the ecliptic
    double ecl = M_PI/180.0*(23.4393 - 3.563E-7 * d);

    /* N = longitude of the ascending node
     * i = inclination to the ecliptic
     * w = argument of perihelion
     * a = semi-major axis
     * e = eccentricity (0=circle, 0-1=ellipse, 1=parabola)
     * M = mean anomaly (0 at perihelion; increases uniformly with time)
     */

    // lunar orbital elements, with respect to Earth
    double N_m = M_PI/180.0*(125.1228 - 0.0529538083 * d);
    double i_m = M_PI/180.0*(5.1454);
    double w_m = M_PI/180.0*(318.0634 + 0.1643573223 * d);
    double a_m = 60.2666;		// Earth radii
    double e_m = 0.054900;
    double M_m = M_PI/180.0*(115.3654 + 13.0649929509 * d);

    // solar orbital elements (really Earth's)
    // double N_s = M_PI/180.0 * (0.0);
    // double i_s = M_PI/180.0 * (0.0);
    double w_s = M_PI/180.0 * (282.9404 + 4.70935E-5 * d);
    // double a_s = 1.000000;			// AU
    // double e_s = 0.016709 - 1.151E-9 * d;
    double M_s = M_PI/180.0 * (356.0470 + 0.9856002585 * d);

    // solar eccentric anomaly
    // double E_s = M_s + e_s * sin(M_s) * ( 1.0 + e_s * cos(M_s) );

    // eccentric anomaly, no need to refine if e < ~0.05
    double E_m = M_m + e_m * sin(M_m) * ( 1.0 + e_m * cos(M_m) );

    // solar distance and true anomaly
    // double xv_s = cos(E_s) - e_s;
    // double yv_s = sqrt(1.0 - e_s*e_s) * sin(E_s);
    // double v_s = atan2( yv_s, xv_s );
    // double r_s = sqrt( xv_s*xv_s + yv_s*yv_s );

    // lunar distance and true anomaly
    double xv_m = a_m * ( cos(E_m) - e_m );
    double yv_m = a_m * ( sqrt(1.0 - e_m*e_m) * sin(E_m) );
    double v_m = atan2 ( yv_m, xv_m );
    double r_m = sqrt ( xv_m*xv_m + yv_m*yv_m );

    // ideal (without perturbations) geocentric ecliptic position in 3-dimensional space:
    double xh_m = r_m * ( cos(N_m) * cos(v_m+w_m) - sin(N_m) * sin(v_m+w_m) * cos(i_m) );
    double yh_m = r_m * ( sin(N_m) * cos(v_m+w_m) + cos(N_m) * sin(v_m+w_m) * cos(i_m) );
    double zh_m = r_m * ( sin(v_m+w_m) * sin(i_m) );

    // ecliptic long and lat
    double lonecl_m = atan2( yh_m, xh_m );
    double latecl_m = atan2( zh_m, sqrt(xh_m*xh_m+yh_m*yh_m) );

    // add enough perturbations to yield max error 0.25 degrees long, 0.15 degs lat
    double L_s = M_s + w_s;					// Mean Longitude of the Sun (Ns=0)
    double L_m = M_m + w_m + N_m;				// Mean longitude of the Moon
    double D_m = L_m - L_s;        				// Mean elongation of the Moon
    double F_m = L_m - N_m; 					// Argument of latitude for the Moon
    lonecl_m += M_PI/180.0 * (-1.274 * sin(M_m - 2*D_m));	// Ptolemy's "Evection"
    lonecl_m +=	M_PI/180.0 * ( 0.658 * sin(2*D_m));		// Brahe's "Variation"
    lonecl_m += M_PI/180.0 * ( 0.186 * sin(M_s));		// Brahe's "Yearly Equation"
    latecl_m += M_PI/180.0 * (-0.173 * sin(F_m - 2*D_m));

    // convert back to geocentric, now with perturbations applied
    xh_m = r_m * cos(lonecl_m) * cos(latecl_m);
    yh_m = r_m * sin(lonecl_m) * cos(latecl_m);
    zh_m = r_m * sin(latecl_m);

    // lunar ecliptic to geocentric (already)
    double xg_m = xh_m;
    double yg_m = yh_m;
    double zg_m = zh_m;

    // convert to equatorial by rotating ecliptic by obliquity
    double xe_m = xg_m;
    double ye_m = yg_m * cos(ecl) - zg_m * sin(ecl);
    double ze_m = yg_m * sin(ecl) + zg_m * cos(ecl);

    // compute the planet's Right Ascension (RA) and Declination (Dec):
    double RA  = 180/M_PI * myfmodf (atan2( ye_m, xe_m ) + 2*M_PI, 2*M_PI);	// degrees
    double Dec = atan2( ze_m, sqrt(xe_m*xe_m+ye_m*ye_m) );			// rads
#ifdef LINIX_STANDALONE_TEST
    printf ("Lunar RA %g hours, Dec %g degrees\n", RA/15, 180/M_PI*Dec);
#endif // LINIX_STANDALONE_TEST

    ll.lat = Dec;
    ll.lat_d = rad2deg(ll.lat);

    double JD = (t/86400.0) + 2440587.5;
    double D = JD - 2451545.0;
    double GMST = myfmodf(15*(18.697374558 + 24.06570982441908*D), 360.0);
    ll.lng_d = myfmodf(RA-GMST+36000.0+180.0, 360.0) - 180.0;
    ll.lng = deg2rad(ll.lng_d);
}


#ifndef LINIX_STANDALONE_TEST
// sorry, no stand-alone unit test for sunrs() because of heavy use of TimeLib

/* Sunrise/Sunset Algorithm
 * http://williams.best.vwh.net/sunrise_sunset_algorithm.htm
 */



static float sind(float x) { return (sinf(deg2rad(x))); }
static float cosd(float x) { return (cosf(deg2rad(x))); }
static float tand(float x) { return (tanf(deg2rad(x))); }
static float acosd(float x) { return (rad2deg(acosf(x))); }
static float atand(float x) { return (rad2deg(atanf(x))); }
static void range(float *x, float r) { while(*x < 0) *x += r; while (*x >= r) *x -= r; }


/* given UNIX time, lat rads +N and lng rads +E, return UNIX secs of today's rise and set.
 * if sun never rises: *trise (only) will be 0; if never sets: *tset (only) will be 0.
 */
void sunrs (const time_t &t0, const LatLong &ll, time_t *trise, time_t *tset)
{
	// xxx_r denotes variable is used to compute rise time, xxx_s used for set

	// convert UNIX to day month year

	uint16_t dd = day(t0);
	uint8_t mm = month(t0);
	uint16_t yy = year(t0);

	// 1. first calculate the day of the year

	int N1 = floor(275 * mm / 9);
	int N2 = floor((mm + 9) / 12);
	int N3 = (1 + floor((yy - 4 * floor(yy / 4) + 2) / 3));
	int N = N1 - (N2 * N3) + dd - 30;

	// 2. convert the longitude to hour value and calculate an approximate time

	float lngHour = rad2deg(ll.lng) / 15;
	
	float t_r = N + ((6 - lngHour) / 24);
	float t_s = N + ((18 - lngHour) / 24);

	// 3. calculate the Sun's mean anomaly
	
	float M_r = (0.9856 * t_r) - 3.289;
	float M_s = (0.9856 * t_s) - 3.289;

	// 4. calculate the Sun's true longitude
	// NOTE: L potentially needs to be adjusted into the range [0,360) by adding/subtracting 360
	
	float L_r = M_r + (1.916 * sind(M_r)) + (0.020 * sind(2 * M_r)) + 282.634;
	range (&L_r, 360.0);
	float L_s = M_s + (1.916 * sind(M_s)) + (0.020 * sind(2 * M_s)) + 282.634;
	range (&L_s, 360.0);

	// 5a. calculate the Sun's right ascension
	// NOTE: RA potentially needs to be adjusted into the range [0,360) by adding/subtracting 360
	
	float RA_r = atand(0.91764 * tand(L_r));
	range (&RA_r, 360.0);
	float RA_s = atand(0.91764 * tand(L_s));
	range (&RA_s, 360.0);

	// 5b. right ascension value needs to be in the same quadrant as L

	float Lquadrant_r  = (floor( L_r/90)) * 90;
	float RAquadrant_r = (floor(RA_r/90)) * 90;
	RA_r = RA_r + (Lquadrant_r - RAquadrant_r);
	float Lquadrant_s  = (floor( L_s/90)) * 90;
	float RAquadrant_s = (floor(RA_s/90)) * 90;
	RA_s = RA_s + (Lquadrant_s - RAquadrant_s);

	// 5c. right ascension value needs to be converted into hours

	RA_r = RA_r / 15;
	RA_s = RA_s / 15;

	// 6. calculate the Sun's declination

	float sinDec_r = 0.39782 * sind(L_r);
	float cosDec_r = cos(asin(sinDec_r));
	float sinDec_s = 0.39782 * sind(L_s);
	float cosDec_s = cos(asin(sinDec_s));

	// 7a. calculate the Sun's local hour angle
	
        #define RSZENANGLE      90.833F         // zenith angle of rise/set event
	float cosH_r = (cosd(RSZENANGLE) - (sinDec_r * sin(ll.lat))) / (cosDec_r * cos(ll.lat));
	float cosH_s = (cosd(RSZENANGLE) - (sinDec_s * sin(ll.lat))) / (cosDec_s * cos(ll.lat));
	
	// if (cosH >  1) 
	//   the sun never rises on this location (on the specified date)
	// if (cosH < -1)
	//   the sun never sets on this location (on the specified date)

	if (cosH_r > 1 || cosH_s > 1) {
	    *trise = 0;
	    *tset = 1;	// anything other than 0
	    return;
	}
	if (cosH_s < -1 || cosH_s < -1) {
	    *tset = 0;
	    *trise = 1;	// anything other than 0
	    return;
	}


	// 7b. finish calculating H and convert into hours
	
	// if if rising time is desired:
	//   H = 360 - acos(cosH)
	// if setting time is desired:
	//   H = acos(cosH)
	
	float H_r = 360 - acosd(cosH_r);
	H_r = H_r / 15;
	float H_s = acosd(cosH_s);
	H_s = H_s / 15;

	// 8. calculate local mean time of rising/setting
	
	float T_r = H_r + RA_r - (0.06571 * t_r) - 6.622;
	float T_s = H_s + RA_s - (0.06571 * t_s) - 6.622;

	// 9. adjust back to UTC
	// NOTE: UT potentially needs to be adjusted into the range [0,24) by adding/subtracting 24
	
	float UT_r = T_r - lngHour;
	range (&UT_r, 24.0);
	float UT_s = T_s - lngHour;
	range (&UT_s, 24.0);

	// convert to UNIX time based on UT_x being from start of today

	time_t day0 = previousMidnight(t0);
	*trise = day0 + SECS_PER_HOUR*UT_r;
	*tset = day0 + SECS_PER_HOUR*UT_s;
}


#endif // !LINIX_STANDALONE_TEST



#ifdef LINIX_STANDALONE_TEST

int main (int ac, char *av[])
{
    if (ac != 5) {
	fprintf (stderr, "year month date hours\n");
	return(1);
    }

    int y = atoi(av[1]);
    int m = atoi(av[2]);
    int d = atoi(av[3]);
    int h = atoi(av[4]);

    struct tm tm;
    memset (&tm, 0, sizeof(tm));
    tm.tm_hour = h;
    tm.tm_mday = d;
    tm.tm_mon = m - 1;
    tm.tm_year = y - 1900;
    time_t t0 = timegm (&tm);	// GNU only

    LatLong ll;
    subLunar (t0, ll);
    printf ("sub Lunar lat %g, long %g, both degrees\n", ll.lat_d, ll.lng_d);
    subSolar (t0, ll);
    printf ("sub Solar lat %g, long %g, both degrees\n", ll.lat_d, ll.lng_d);

    return (0);
}

#endif // LINIX_STANDALONE_TEST
