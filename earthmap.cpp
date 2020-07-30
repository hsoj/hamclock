/* code to manage the earth map
 */

/* main map drawing routines.
 */


#include "HamClock.h"


// DX location and path to DE
SCircle dx_c = {{0,0},DX_R};    	        // screen coords of DX symbol
LatLong dx_ll;				        // geo coords of dx spot

// DE and AntiPodal location
SCircle de_c = {{0,0},DE_R};    	        // screen coords of DE symbol
LatLong de_ll;				        // geo coords of DE
float sdelat, cdelat;			        // handy tri
SCircle deap_c = {{0,0},DEAP_R};                // screen coords of DE antipode symbol
LatLong deap_ll;			        // geo coords of DE antipode

// sun
SCircle sun_c = {{0,0},SUN_R};	                // screen coords of sun symbol
LatLong sun_ss_ll;			        // subsolar location
float csslat, ssslat;			        // handy trig

// moon
SCircle moon_c = {{0,0},MOON_R};                // screen coords of moon symbol
LatLong moon_ss_ll;			        // sublunar location

// dx options
uint8_t show_km;			        // show great circle dist in km, else miles
uint8_t show_lp;                                // display long path, else short part heading

#define	GRAYLINE_COS	(-0.208F)	        // cos(90 + grayline angle), we use 12 degs
#define	GRAYLINE_POW	(0.75F)	                // cos power exponent, sqrt is too severe, 1 is too gradual
static SCoord moremap_s;		        // drawMoreEarth() scanning location 


/* erase the DE symbol by restoring map contents.
 * N.B. we assume coords insure marker will be wholy within map boundaries.
 */
void eraseDEMarker()
{
    eraseSCircle (de_c);
}

/* draw DE marker.
 * N.B. we assume coords insure marker will be wholy within map boundaries.
 */
void drawDEMarker(bool force)
{
    // test for over visible map unless force, eg might be under RSS now
    if (!force && !overMap(de_c.s))
        return;

    tft.fillCircle (de_c.s.x, de_c.s.y, DE_R, RA8875_BLACK);
    tft.drawCircle (de_c.s.x, de_c.s.y, DE_R, DE_COLOR);
    tft.fillCircle (de_c.s.x, de_c.s.y, DE_R/2, DE_COLOR);
}

/* erase the antipode symbol by restoring map contents.
 * N.B. we assume coords insure marker will be wholy within map boundaries.
 */
void eraseDEAPMarker()
{
    eraseSCircle (deap_c);
}

/* draw antipodal marker.
 * N.B. we assume coords insure marker will be wholy within map boundaries.
 */
void drawDEAPMarker()
{
    tft.fillCircle (deap_c.s.x, deap_c.s.y, DEAP_R, DE_COLOR);
    tft.drawCircle (deap_c.s.x, deap_c.s.y, DEAP_R, RA8875_BLACK);
    tft.fillCircle (deap_c.s.x, deap_c.s.y, DEAP_R/2, RA8875_BLACK);
}

/* draw de_info_b according to de_time_fmt
 */
void drawDEInfo()
{
    // init info block
    tft.fillRect (de_info_b.x, de_info_b.y, de_info_b.w, de_info_b.h, RA8875_BLACK);

    // draw desired contents
    if (de_time_fmt == DETIME_INFO) {

        uint16_t vspace = de_info_b.h/DE_INFO_ROWS;
        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        tft.setTextColor (DE_COLOR);

        // time
        drawDETime(false);

        // lat and lon
        char buf[30];
        sprintf (buf, "%.0f%c  %.0f%c",
                    roundf(fabsf(de_ll.lat_d)), de_ll.lat_d < 0 ? 'S' : 'N',
                    roundf(fabsf(de_ll.lng_d)), de_ll.lng_d < 0 ? 'W' : 'E');
        tft.setCursor (de_info_b.x, de_info_b.y+2*vspace-6);
        tft.print(buf);

        // maidenhead
        drawMaidenhead(NV_DE_GRID, de_maid_b, DE_COLOR);

        // sun rise/set info
        drawDESunRiseSetInfo();

    } else if (de_time_fmt == DETIME_ANALOG) {

        drawDETime(true);
        updateClocks(true);

    } else if (de_time_fmt == DETIME_CAL) {

        drawDETime(true);
        drawCalendar(true);

    }
}

void drawDETime(bool center)
{
    drawTZ (de_tz);

    // get time
    time_t utc = nowWO();
    time_t local = utc + de_tz.tz_secs;
    int hr = hour (local);
    int mn = minute (local);
    int dy = day(local);
    int mo = month(local);

    // generate text
    char buf[32];
    sprintf (buf, "%02d:%02d %s %d", hr, mn, monthShortStr(mo), dy);

    // set position
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    uint16_t vspace = de_info_b.h/DE_INFO_ROWS;
    uint16_t x0 = de_info_b.x;
    if (center) {
        uint16_t bw = getTextWidth (buf);
        x0 += (de_info_b.w - bw)/2;
    }

    // draw
    tft.fillRect (de_info_b.x, de_info_b.y, de_info_b.w, vspace, RA8875_BLACK);
    tft.setTextColor (DE_COLOR);
    tft.setCursor (x0, de_info_b.y+vspace-6);
    tft.print(buf);
}

/* draw some fake stars for the azimuthal projection
 */
void drawAzmStars()
{
    #define N_AZMSTARS 100
    uint8_t n_stars = 0;
    while (n_stars < N_AZMSTARS) {
	int32_t x = random (map_b.w);
	int32_t y = random (map_b.h);
	int32_t dx = (x > map_b.w/2) ? (x - 3*map_b.w/4) : (x - map_b.w/4);
	int32_t dy = y - map_b.h/2;
	if (dx*dx + dy*dy > map_b.w*map_b.w/16) {
	    uint16_t c = random(256);
	    c = RGB565(c,c,c);
	    tft.drawPixel (map_b.x+x, map_b.y+y, c);
	    n_stars++;
	}
    }
}

static void updateCircumstances()
{
    time_t utc = nowWO();
    subSolar (utc, sun_ss_ll);
    csslat = cosf(sun_ss_ll.lat);
    ssslat = sinf(sun_ss_ll.lat);
    ll2s (sun_ss_ll, sun_c.s, SUN_R+1);
    subLunar (utc, moon_ss_ll);
    ll2s (moon_ss_ll, moon_c.s, MOON_R+1);
    updateSatPath();
}

/* restart map given de_ll and dx_ll
 */
void initEarthMap()
{
    resetWatchdog();

    // completely erase map
    tft.fillRect (map_b.x, map_b.y, map_b.w, map_b.h, RA8875_BLACK);

    // add funky star field if azm
    if (azm_on)
	drawAzmStars();

    // draw RSS button and get fresh content
    drawRSSButton();
    updateRSSNow();

    // draw other buttons over map
    drawAzmMercButton();
    drawLLGridButton();

    // reset any pending great circle path
    setDXPathInvalid();

    // update astro info
    updateCircumstances();

    // update DE and DX info
    sdelat = sinf(de_ll.lat);
    cdelat = cosf(de_ll.lat);
    ll2s (de_ll, de_c.s, DE_R);
    antipode (deap_ll, de_ll);
    ll2s (deap_ll, deap_c.s, DEAP_R);
    ll2s (dx_ll, dx_c.s, DX_R);

    // show updated info
    drawDEInfo();
    drawDXInfo();

    // insure NCDXF screen coords match current map type
    updateBeaconScreenLocations();

    // init scan line in map_b
    moremap_s.x = 0;                    // avoid updateCircumstances() first call to drawMoreEarth()
    moremap_s.y = map_b.y;

    // now main loop can resume with drawMoreEarth()
}

/* display more earth map at mmoremap_s.
 * _USE_DESKTOP draws all the map then all symbols then updates screen, but ESP has to take care not to
 *   clobber symbols while drawing the map.
 */
void drawMoreEarth()
{
    resetWatchdog();

    // handy health indicator and update timer
    digitalWrite(LIFE_LED, !digitalRead(LIFE_LED));

    // refresh circumstances at start of each map scan but not very first call after initEarthMap()
    if (moremap_s.y == map_b.y && moremap_s.x != 0)
        updateCircumstances();
    
    // draw next row
    uint16_t last_x = map_b.x + EARTH_W*EARTH_XW - EARTH_XW;

#if defined(_USE_DESKTOP)


    // draw the entire map then overlay the symbols just before displaying

    for (moremap_s.x = map_b.x; moremap_s.x <= last_x; moremap_s.x += EARTH_XW) {

	resetWatchdog();

        drawMapCoord (moremap_s);

    }


#else   // !defined(_USE_DESKTOP)


    // avoid symbols as the map is drawn

    // whether found any symbols on this row or col
    uint8_t n_symbols_this_row = 0;

    // preserve whether any symbols were found on this row for next time
    static uint8_t n_symbols_prev_row;

    for (moremap_s.x = map_b.x; moremap_s.x <= last_x; moremap_s.x += 1) {

	resetWatchdog();

        // test whether now over a symbol
        bool over_symbol = overAnySymbol (moremap_s);
        n_symbols_this_row += over_symbol;

        // draw map if not
        if (!over_symbol)
            drawMapCoord (moremap_s);
    }

    // draw all symbols if hit one on line above but none on this row
    if (n_symbols_this_row == 0 && n_symbols_prev_row > 0)
        drawAllSymbols(false);

    // save whether hit any symbols on this row
    n_symbols_prev_row = n_symbols_this_row;


#endif  // defined(_USE_DESKTOP)

    // check for clobbering sat path or name
    drawSatNameOnRow (moremap_s.y);
    drawSatPointsOnRow (moremap_s.y);

    // advance row, accounting for any row replication, and wrap at the end
    if ((moremap_s.y += EARTH_XH) >= map_b.y + EARTH_H*EARTH_XH) {
	moremap_s.y = map_b.y;

#if defined(_USE_DESKTOP)
        drawAllSymbols(false);
        tft.drawPR();
#endif

        // #define _TIME_MAP
        #if defined(_TIME_MAP)
            static uint32_t map_t0;
            uint32_t map_t = millis();
            if (map_t0 != 0)
                Serial.printf ("Map paint %ld ms\n", map_t - map_t0);
            map_t0 = map_t;
        #endif
    }
}

/* convert lat and long in radians to screen coords.
 * keep result no closer than the given edge distance.
 * N.B. we assume lat/lng are in range [-90,90] [-180,180)
 */
void ll2s (float lat, float lng, SCoord &s, uint8_t edge)
{
    LatLong ll;
    ll.lat = lat;
    ll.lat_d = rad2deg(ll.lat);
    ll.lng = lng;
    ll.lng_d = rad2deg(ll.lng);
    ll2s (ll, s, edge);
}
void ll2s (const LatLong &ll, SCoord &s, uint8_t edge)
{
    resetWatchdog();

    if (azm_on) {

	// azimuthal projection

	// sph tri between de, dx and N pole
	float ca, B;
	solveSphere (ll.lng - de_ll.lng, M_PI_2F-ll.lat, sdelat, cdelat, &ca, &B);

	if (ca > 0) {
	    // front (left) side, centered at DE
	    float a = acosf (ca);
	    float R = fminf (a*map_b.w/(2*M_PIF), map_b.w/4 - edge - 1);        // well clear
	    float dx = R*sinf(B);
	    float dy = R*cosf(B);
	    s.x = map_b.x + map_b.w/4 + dx;
	    s.y = map_b.y + map_b.h/2 - dy;
	} else {
	    // back (right) side, centered at DE antipode
	    float a = M_PIF - acosf (ca);
	    float R = fminf (a*map_b.w/(2*M_PIF), map_b.w/4 - edge - 1);        // well clear
	    float dx = -R*sinf(B);
	    float dy = R*cosf(B);
	    s.x = map_b.x + 3*map_b.w/4 + dx;
	    s.y = map_b.y + map_b.h/2 - dy;
	}

    } else {

	// straight rectangular Mercator projection
	s.x = map_b.x + map_b.w*(ll.lng_d+180)/360;
	s.y = map_b.y + map_b.h*(90-ll.lat_d)/180;

	// guard edge
	uint16_t e;
	e = map_b.x + edge;
	if (s.x < e)
	    s.x = e;
	e = map_b.x + map_b.w - edge - 1;
	if (s.x > e)
	    s.x = e;
	e = map_b.y + edge;
	if (s.y < e)
	    s.y = e;
	e = map_b.y + map_b.h - edge - 1;
	if (s.y > e)
	    s.y = e;
    }

}

/* convert a screen coord to lat and long.
 * return whether location is really over valid map.
 */
bool s2ll (uint16_t x, uint16_t y, LatLong &ll)
{
    SCoord s;
    s.x = x;
    s.y = y;
    return (s2ll (s, ll));
}
bool s2ll (const SCoord &s, LatLong &ll)
{
    if (!overMap(s))
	return (false);

    if (azm_on) {

	// radius from center of point's hemisphere
	bool on_right = s.x > map_b.x + map_b.w/2;
	int32_t dx = on_right ? s.x - (map_b.x + 3*map_b.w/4) : s.x - (map_b.x + map_b.w/4);
	int32_t dy = (map_b.y + map_b.h/2) - s.y;
	int32_t r2 = dx*dx + dy*dy;

	// see if really on surface
	int32_t w2 = map_b.w*map_b.w/16;
	if (r2 > w2)
	    return(false);

	// use screen triangle to find globe
	float b = sqrtf((float)r2/w2)*(M_PI_2F);
	float A = (M_PI_2F) - atan2f (dy, dx);
	float ca, B;
	solveSphere (A, b, (on_right ? -1 : 1) * sdelat, cdelat, &ca, &B);
	float lt = M_PI_2F - acosf(ca);
	ll.lat_d = rad2deg(lt);
	float lg = myfmodf (de_ll.lng + B + (on_right?6:5)*M_PIF, 2*M_PIF) - M_PIF;
	ll.lng_d = rad2deg(lg);

    } else {

	// straight rectangular mercator projection

	ll.lat_d = 90 - 180.0F*(s.y - map_b.y)/(EARTH_H*EARTH_XH);
	ll.lng_d = 360.0F*(s.x - map_b.x)/(EARTH_W*EARTH_XW) - 180;

    }

    normalizeLL(ll);

    return (true);
}

#if !defined(_USE_DESKTOP)

/* given lat/lng and cos of angle from terminator, return earth map pixel
 */
static uint16_t getEarthMapPix (LatLong ll, float cos_t)
{
    uint16_t pix_c;

    // indices into pixel array at this location
    uint16_t ex = (uint16_t)((EARTH_W*(ll.lng_d+180)/360)+0.5F) % EARTH_W;
    uint16_t ey = (uint16_t)((EARTH_H*(90-ll.lat_d)/180)+0.5F) % EARTH_H;

    // decide color
    if (cos_t > 0) {
        // < 90 deg: sunlit
        pix_c = pgm_read_word(&DEARTH[ey][ex]);
    } else if (cos_t > GRAYLINE_COS) {
        // blend from day to night
        uint16_t day_pix = pgm_read_word(&DEARTH[ey][ex]);
        uint16_t night_pix = pgm_read_word(&NEARTH[ey][ex]);
        uint8_t day_r = RGB565_R(day_pix);
        uint8_t day_g = RGB565_G(day_pix);
        uint8_t day_b = RGB565_B(day_pix);
        uint8_t night_r = RGB565_R(night_pix);
        uint8_t night_g = RGB565_G(night_pix);
        uint8_t night_b = RGB565_B(night_pix);
        float fract_night = powf(cos_t/GRAYLINE_COS, GRAYLINE_POW);
        float fract_day = 1 - fract_night;
        uint8_t twi_r = (fract_day*day_r + fract_night*night_r);
        uint8_t twi_g = (fract_day*day_g + fract_night*night_g);
        uint8_t twi_b = (fract_day*day_b + fract_night*night_b);
        pix_c = RGB565 (twi_r, twi_g, twi_b);
    } else {
        // night side
        pix_c = pgm_read_word(&NEARTH[ey][ex]);
    }

    return (pix_c);
}

#endif

/* draw EARTH_XWxEARTH_XH at the given screen location, if it's over the map.
 * We are called for every value of x but for the low-res ESP map we duplicate the odd values.
 */
void drawMapCoord (uint16_t x, uint16_t y)
{

    SCoord s;
    s.x = x;
    s.y = y;
    drawMapCoord (s);
}
void drawMapCoord (const SCoord &s)
{
    // grid colors
    #define GRIDC   RGB565(35,35,35)
    #define GRIDC00 RGB565(120,120,120)


    #if defined(_USE_DESKTOP)

        // draw one application pixel at full screen resolution. requires lat/lng gradients.


        // we only support 1x1 to avoid looping for the general case
        #if EARTH_XW != 1 or EARTH_XH != 1
            #error unsupported earth map DESKTOP resolution
        #endif

        // find lat/lng at this screen location, bale if not over map
        LatLong lls;
        if (!s2ll(s,lls))
            return; 

        /* even though we only draw one application point, s, plotEarth needs points r and d to
         * interpolate to full map resolution.
         *   s - - - r
         *   |
         *   d
         */
        SCoord sr, sd;
        LatLong llr, lld;
        sr.x = s.x + 1;
        sr.y = s.y;
        if (!s2ll(sr,llr))
            llr = lls;
        sd.x = s.x;
        sd.y = s.y + 1;
        if (!s2ll(sd,lld))
            lld = lls;

        // find angle between subsolar point and any visible near this location
        // TODO: actually different at each point, this causes striping
        float clat = cosf(lls.lat);
        float slat = sinf(lls.lat);
        float cos_t = ssslat*slat + csslat*clat*cosf(sun_ss_ll.lng-lls.lng);

        // decide day, night or twilight
        float fract_day;
        if (cos_t > 0) {
            // < 90 deg: sunlit
            fract_day = 1;
        } else if (cos_t > GRAYLINE_COS) {
            // blend from day to night
            fract_day = 1 - powf(cos_t/GRAYLINE_COS, GRAYLINE_POW);
        } else {
            // night side
            fract_day = 0;
        }

        // draw the full res map point
        tft.plotEarth (s.x, s.y, lls.lat_d, lls.lng_d, llr.lat_d - lls.lat_d, llr.lng_d - lls.lng_d,
                    lld.lat_d - lls.lat_d, lld.lng_d - lls.lng_d, fract_day);

        // overlay lat/long grid if enabled
        #define DLAT        (0.98F*180.0F/(EARTH_H*EARTH_XH))                        // about 1 pixel
        #define DLNG        (0.98F*360.0F/(EARTH_W*EARTH_XW)/(azm_on ? clat : 1))    // " with polar spread
        switch (llg_on) {
        case LLG_ALL:

            if (myfmodf (lls.lat_d+90, 15) < DLAT || myfmodf (lls.lng_d+180, 15) < DLNG) {
                uint32_t grid_c = (fabsf (lls.lat_d) < DLAT || fabs (lls.lng_d) < DLNG) ? GRIDC00 : GRIDC;
                tft.drawPixel (s.x, s.y, grid_c);
            }
            break;

        case LLG_TROPICS:

            if (fabsf (fabsf (lls.lat_d) - 23.5F) < DLAT/2) 
                tft.drawPixel (s.x, s.y, GRIDC00);
            break;

        default:
            // none
            break;

        }


    #else // !defined(_USE_DESKTOP)


        // we only support 2x1 to avoid looping for the general case
        #if EARTH_XW != 2 or EARTH_XH != 1
            #error unsupported earth map resolution
        #endif

        // draw one pixel, if over map

        // a latitude cache really helps Mercator time; anything help Azimuthal??
        static float slat_c, clat_c;
        static SCoord s_c;

        // find lat/lng at this screen location, done if not over map
        LatLong lls;
        if (!s2ll(s, lls))
            return;

        // update handy Mercator cache, but always required for Azm.
        if (azm_on || s.y != s_c.y) {
            s_c = s;
            slat_c = sinf(lls.lat);
            clat_c = cosf(lls.lat);
        }

        // draw lat/long grid if enabled
        #define DLAT        0.6F
        #define DLNG        (0.5F/clat_c)
        switch (llg_on) {
        case LLG_ALL:

            if (azm_on) {

                if (myfmodf(lls.lat_d+90, 15) < DLAT || myfmodf (lls.lng_d+180, 15) < DLNG) {
                    uint32_t grid_c = (fabsf (lls.lat_d) < DLAT || fabs (lls.lng_d) < DLNG) ? GRIDC00 : GRIDC;
                    tft.drawPixel (s.x, s.y, grid_c);
                    return;                                         // done
                }

            } else {

                // extra gymnastics are because the pixels-per-line are not integral
                #define _PPLG (EARTH_W*EARTH_XW/(360/15))
                #define _PPLN (EARTH_H*EARTH_XH/(180/15))
                if ((((s.x - map_b.x) - (s.x - map_b.x)/(2*_PPLG)) % _PPLG) == 0
                                    || (((s.y - map_b.y) - (s.y - map_b.y)/(2*_PPLN)) % _PPLN) == 0) {
                    uint32_t grid_c = (fabsf (lls.lat_d) < DLAT || fabs (lls.lng_d) < DLNG) ? GRIDC00 : GRIDC;
                    tft.drawPixel (s.x, s.y, grid_c);
                    return;                                         // done
                }
            }

            break;

        case LLG_TROPICS:

            if (azm_on) {

                if (fabsf (fabsf (lls.lat_d) - 23.5F) < 0.3F) {
                    tft.drawPixel (s.x, s.y, GRIDC00);
                    return;                                         // done
                }

            } else {

                // we already know exactly where the grid lines go.
                if (abs(s.y - (map_b.y+EARTH_H*EARTH_XH/2)) == (uint16_t)((23.5F/180)*(EARTH_H*EARTH_XH))) {
                    tft.drawPixel (s.x, s.y, GRIDC00);
                    return;                                         // done
                }
            }
            break;

        default:

            // none
            break;

        }

        // if get here we did not draw a lat/long grid point

        // we know it's the same color if Mercator and x is one to the right of previous
        static uint16_t prev_clr;
        if (!azm_on && s.y == s_c.y && s.x == s_c.x + 1) {
            tft.drawPixel (s.x, s.y, prev_clr);
            return;
        }

        // find angle between subsolar point and this location
        float cos_t = ssslat*slat_c + csslat*clat_c*cosf(sun_ss_ll.lng-lls.lng);

        uint16_t pix_c = getEarthMapPix (lls, cos_t);
        tft.drawPixel (s.x, s.y, pix_c);

        // preserve for next call
        s_c = s;
        prev_clr = pix_c;

    #endif  // defined _USE_DESKTOP

}

/* draw sun symbol.
 * N.B. we assume sun_c coords insure marker will be wholy within map boundaries.
 */
void drawSun ()
{
    resetWatchdog();

#   define	N_SUN_RAYS	12
    uint16_t body_r = 5*SUN_R/9;
    tft.fillCircle (sun_c.s.x, sun_c.s.y, SUN_R, RA8875_BLACK);
    tft.fillCircle (sun_c.s.x, sun_c.s.y, body_r, RA8875_YELLOW);
    for (uint8_t i = 0; i < N_SUN_RAYS; i++) {
	float a = i*2*M_PIF/N_SUN_RAYS;
	float sa = sinf(a);
	float ca = cosf(a);
	uint16_t x0 = sun_c.s.x + (body_r+2)*ca + 0.5F;
	uint16_t y0 = sun_c.s.y + (body_r+2)*sa + 0.5F;
	uint16_t x1 = sun_c.s.x + (SUN_R)*ca + 0.5F;
	uint16_t y1 = sun_c.s.y + (SUN_R)*sa + 0.5F;
	tft.drawLine (x0, y0, x1, y1, RA8875_YELLOW);
    }
#   undef N_SUN_RAYS
}

/* draw moon symbol.
 * N.B. we assume moon_c coords insure marker will be wholy within map boundaries.
 */
void drawMoon ()
{
    resetWatchdog();

    // rough estimage of phase based on difference in sub-longitude from sun.
    // looking down from north pole this is angle CW from straight behind moon as seen from earth.
    float phase = myfmodf (moon_ss_ll.lng - sun_ss_ll.lng + 4*M_PIF, 2*M_PIF);
    
    #define NEW_HEDGE	0.02				// +- rads considered new

#if defined(_USE_DESKTOP)

    // scan moon face @ full SCALESZ
    const uint16_t mr = MOON_R*tft.SCALESZ;		// moon radius on output device
    for (int16_t dy = -mr; dy <= mr; dy++) {            // scan top to bottom
	float Ry = sqrtf(mr*mr-dy*dy);		        // half-width at y
	int16_t Ryi = floorf(Ry+0.5F);			// " as int
	for (int16_t dx = -Ryi; dx <= Ryi; dx++) {	// scan left to right at y
	    float a = acosf((float)dx/Ryi);	        // looking down from NP CW from right limb
	    tft.drawSubPixel (tft.SCALESZ*moon_c.s.x+dx, tft.SCALESZ*moon_c.s.y+dy,
		    (isnan(a) || a > phase-NEW_HEDGE || a < phase+NEW_HEDGE - M_PIF)
		    	? RA8875_BLACK : RA8875_WHITE);
	}
    }

#else // !defined(_USE_DESKTOP)

    // scan moon face
    for (int16_t y = -MOON_R; y <= MOON_R; y++) {	// scan top to bottom
	float Ry = sqrtf(MOON_R*MOON_R-y*y);		// half-width at y
	int16_t Ryi = floorf(Ry+0.5F);			// " as int
	for (int16_t x = -Ryi; x <= Ryi; x++) {		// scan left to right at y
	    float a = acosf((float)x/Ryi);		// looking down from NP CW from right limb
	    tft.drawPixel (moon_c.s.x+x, moon_c.s.y+y,
		    (isnan(a) || a > phase-NEW_HEDGE || a < phase+NEW_HEDGE - M_PIF)
		    	? RA8875_BLACK : RA8875_WHITE);
	}
    }

#endif

}

/* display some info about DX location in dx_info_b
 */
void drawDXInfo ()
{
    resetWatchdog();

    // skip if dx_info_b being used for sat info
    if (dx_info_for_sat)
	return;

    // divide into 5 rows
    uint16_t vspace = dx_info_b.h/DX_INFO_ROWS;

    // time
    drawDXTime();

    // erase and init
    tft.graphicsMode();
    tft.fillRect (dx_info_b.x, dx_info_b.y+2*vspace, dx_info_b.w, dx_info_b.h-2*vspace+1, RA8875_BLACK);
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (DX_COLOR);

    // lat and long
    char buf[30];
    sprintf (buf, "%.0f%c  %.0f%c",
    		roundf(fabsf(dx_ll.lat_d)), dx_ll.lat_d < 0 ? 'S' : 'N',
		roundf(fabsf(dx_ll.lng_d)), dx_ll.lng_d < 0 ? 'W' : 'E');
    tft.setCursor (dx_info_b.x, dx_info_b.y+3*vspace-8);
    tft.print(buf);
    uint16_t bw, bh;
    getTextBounds (buf, &bw, &bh);

    // maidenhead
    drawMaidenhead(NV_DX_GRID, dx_maid_b, DX_COLOR);

    // compute dist and bearing
    float dist, bearing;
    propDEDXPath (show_lp, &dist, &bearing);
    dist *= ERAD_M;                             // angle to miles
    bearing *= 180/M_PIF;                       // rad -> degrees

    // desired units
    if (show_km)
	dist *= 1.609344F;                      // mi - > km

    // print, capturing where units and deg/path can go
    tft.setCursor (dx_info_b.x, dx_info_b.y+5*vspace-4);
    tft.printf ("%.0f", dist);
    uint16_t units_x = tft.getCursorX()+2;
    tft.setCursor (units_x + 6, tft.getCursorY());
    tft.printf ("@%.0f", bearing);
    uint16_t deg_x = tft.getCursorX() + 3;
    uint16_t deg_y = tft.getCursorY();

    // home-made degree symbol
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setCursor (deg_x, deg_y-bh-bh/5);
    tft.print ('o');

    // path direction
    tft.setCursor (deg_x, deg_y-bh/2-bh/5);
    tft.print (show_lp ? 'L' : 'S');
    tft.setCursor (deg_x, deg_y-bh/3);
    tft.print ('P');

    // distance units
    if (show_km) {
	tft.setCursor (units_x, deg_y-bh/2-bh/5);
	tft.print('k');
	tft.setCursor (units_x, deg_y-bh/3);
	tft.print('m');
    } else {
	tft.setCursor (units_x, deg_y-bh/2-bh/5);
	tft.print('m');
	tft.setCursor (units_x, deg_y-bh/3);
	tft.print('i');
    }

    // sun rise/set or prefix
    if (dxsrss == DXSRSS_PREFIX) {
        char prefix[MAX_PREF_LEN+1];
        tft.fillRect (dxsrss_b.x, dxsrss_b.y, dxsrss_b.w, dxsrss_b.h, RA8875_BLACK);
        if (getDXPrefix (prefix)) {
            tft.setTextColor(DX_COLOR);
            selectFontStyle (LIGHT_FONT, SMALL_FONT);
            bw = getTextWidth (prefix);
            tft.setCursor (dxsrss_b.x+(dxsrss_b.w-bw)/2, dxsrss_b.y + 28);
            tft.print (prefix);
        }
    } else {
        drawDXSunRiseSetInfo();
    }
}

/* return whether s is over DX distance portion of dx_info_b
 */
bool checkDistTouch (const SCoord &s)
{
    uint16_t vspace = dx_info_b.h/DX_INFO_ROWS;

    SBox b;
    b.x = dx_info_b.x;
    b.w = dx_info_b.w/2;
    b.y = dx_info_b.y + 4*vspace;
    b.h = vspace;

    return (inBox (s, b));
}

/* return whether s is over DX path direction portion of dx_info_b
 */
bool checkPathDirTouch (const SCoord &s)
{
    uint16_t vspace = dx_info_b.h/DX_INFO_ROWS;

    SBox b;
    b.x = dx_info_b.x + dx_info_b.w/2;
    b.w = dx_info_b.w/2;
    b.y = dx_info_b.y + 4*vspace;
    b.h = vspace;

    return (inBox (s, b));
}

/* if touch position s is over the latitude portion of Info box b:
 *   adjust ll lat depending on whether the top or bottom half of lat number was touched
 * else if over the longitude portion:
 *   adjust ll lng depending on whether the left or right half of the lng number was touched
 * return true if ll was changed, else false
 * nvert is the number of vertical rows into which b is divided; row is the 0-based row number.
 */
static bool checkLLTouch (const SCoord &s, const SBox &b, uint8_t nvert, uint8_t row, LatLong &ll)
{
    uint16_t vspace = b.h/nvert;			// height of one row
    uint16_t llb = b.x + b.w/3;				// lat - lng boundary
    uint16_t lrb = b.x + 2*b.w/3;			// lng left-right boundary
    uint16_t upd = b.y + row*vspace + vspace/2;		// lat up/down boundary

    if (s.y > upd-vspace/2 && s.y < upd+vspace/2 && s.x >= b.x && s.x < b.x+b.w) {
	if (s.x <= llb) {
	    // touched lat
	    if (s.y < upd) {
		if (ll.lat_d < 89)
		    ll.lat_d += 1;			// 1 deg up = northward, no pole
	    } else {
		if (ll.lat_d > -89)
		    ll.lat_d -= 1;			// 1 deg down = southward, no pole
	    }
	    ll.lat = deg2rad (ll.lat_d);
	} else {
	    // touched lng
	    if (s.x < lrb) {
		if ((ll.lng_d -= 1) < -180)		// 1 deg left = westward, with wrap
		    ll.lng_d += 360;
	    } else {
		if ((ll.lng_d += 1) >= 180)		// 1 deg right = eastward, with wrap
		    ll.lng_d -= 360;
	    }
	    ll.lng = deg2rad (ll.lng_d);
	}
	return (true);
    }
    return (false);
}

/* return whether s touched within DE lat or lng.
 * if so, update ll in place
 */
bool checkDELLTouch (const SCoord &s, LatLong &ll)
{
    return (checkLLTouch (s, de_info_b, 3, 1, ll));
}

/* return whether s touched within DX lat or lng.
 * if so, update ll in place
 */
bool checkDXLLTouch (const SCoord &s, LatLong &ll)
{
    return (checkLLTouch (s, dx_info_b, 5, 2, ll));
}


/* draw DX time unless in sat mode
 */
void drawDXTime()
{
    // skip if dx_info_b being used for sat info
    if (dx_info_for_sat)
	return;

    drawTZ (dx_tz);

    uint16_t vspace = dx_info_b.h/DX_INFO_ROWS;

    time_t utc = nowWO();
    time_t local = utc + dx_tz.tz_secs;
    int hr = hour (local);
    int mn = minute (local);
    int dy = day(local);
    int mo = month(local);

    tft.graphicsMode();
    tft.fillRect (dx_info_b.x, dx_info_b.y+vspace, dx_info_b.w, vspace, RA8875_BLACK);
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (DX_COLOR);
    tft.setCursor (dx_info_b.x, dx_info_b.y+2*vspace-8);

    char buf[32];
    sprintf (buf, "%02d:%02d %s %d", hr, mn, monthShortStr(mo), dy);
    tft.print(buf);
}

/* set `to' to the antipodal location of coords in `from'.
 */
void antipode (LatLong &to, const LatLong &from)
{
    to.lat_d = -from.lat_d;
    to.lng_d = from.lng_d+180;
    normalizeLL(to);
}
