/* manage selection and display of one earth sat.
 *
 * we call "pass" the overhead view shown in dx_info_b, "path" the orbit shown on the map.
 *
 * N.B. our satellite info server changes blanks to underscores in sat names.
 */


#include "HamClock.h"

bool dx_info_for_sat;			// global to indicate whether dx_info_b is for DX info or sat info

#define	MAX_TLE_AGE	7.0F		// max age to use a TLE, days (except moon)
#define SAT_MIN_EL      1.0F            // minimum sat elevation for event
#define	TLE_REFRESH	(3600*6)	// freshen TLEs this often, seconds
#define	SAT_TOUCH_R	20U		// touch radius, pixels
#define	SAT_UP_R	2		// dot radius when up
#define	PASS_STEP	10.0F           // pass step size, seconds
#define MAX_PATH	2000		// max number of points in orbit path
#define	MAX_FOOT	1625		// max number of points in viewing footprint; must be 13*
#define	TBORDER		50		// top border
#define	FONT_H		(dx_info_b.h/6)	// font height
#define	FONT_D		5		// font descent
#define	SAT_COLOR	RA8875_RED	// path color
#define GIMBALWRAP_COL  RA8875_CYAN     // gimbal wrap az marker color
#define	SOON_COLOR	RA8875_GREEN	// text color for pass soon
#define	SOON_MINS	10      	// "soon", minutes
#define	FP_COLOR	RA8875_RED      // foot print color
#define	TRACK_COLOR	RGB565(128,0,0) // track color
#define	PASS_COLOR	FP_COLOR	// pass color
#define	CB_SIZE		20		// size of selection check box
#define	CELL_H		32		// display cell height
#define	N_COLS		4		// n cols in name table
#define	Q_TO		5		// question timeout
#define	CELL_W		(tft.width()/N_COLS)			// display cell width
#define	N_ROWS		((tft.height()-TBORDER)/CELL_H)	        // n rows in name table
#define	MAX_NSAT	(N_ROWS*N_COLS)				// max names we can display
#define MAX_PASS_STEPS  30              // max lines to draw for pass map

static const char sat_get_all[] = "/ham/HamClock/esats.pl?getall=";	// command to get all TLE
static const char sat_one_page[] = "/ham/HamClock/esats.pl?tlename=%s";	// command to get one TLE
static Satellite *sat;			// satellite definition, if any
static Observer *obs;			// DE
static DateTime rise_time, set_time;	// next pass info
static bool rise_ok, set_ok;		// whether rise_time and set_time are valid
static float rise_az, set_az;           // rise and set az, degrees, if valid
static bool ever_up, ever_down;         // whether sat is ever above or below SAT_MIN_EL in next day
static SCoord *sat_path;		// mallocd screen coords for orbit, first always now, Moon only 1
static SCoord *sat_foot;		// mallocd screen coords for footprint
static uint16_t n_path, n_foot;		// actual number in use
static SBox map_name_b;		        // location of sat name on map
static SBox ok_b = {730,10,55,35};	// Ok button
static char sat_name[NV_SATNAME_LEN];	// NV_SATNAME cache (spaces are underscores)
#define	SAT_NAME_IS_SET()		(sat_name[0])		// whether there is a sat name defined
static time_t tle_refresh;		// last TLE update
static bool new_pass;                   // set when new pass is ready

/* completely undefine the current sat
 */
static void unsetSat()
{
    if (sat) {
	delete sat;
	sat = NULL;
    }
    if (sat_path) {
        free (sat_path);
        sat_path = NULL;
    }
    if (sat_foot) {
        free (sat_foot);
        sat_foot = NULL;
    }
    sat_name[0] = '\0';
    NVWriteString (NV_SATNAME, sat_name);
    dx_info_for_sat = false;
}

/* copy strings up to maxlen, changing all from_char to to_char
 */
static void strncpySubChar (char to_str[], const char from_str[], char to_char, char from_char, int maxlen)
{
    int l = 0;
    char c;

    while (l++ < maxlen && (c = *from_str++) != '\0')
        *to_str++ = c == from_char ? to_char : c;
    *to_str = '\0';
}

/* fill sat_foot with loci of points that see the sat at various viewing altitudes.
 * N.B. call this before updateSatPath malloc's its memory
 */
static void updateFootPrint (float satlat, float satlng)
{
    resetWatchdog();

    // complement of satlat
    float cosc = sinf(satlat);
    float sinc = cosf(satlat);

    #define N_ALTS 3			// number of altitudes to show
    float alts[N_ALTS];			// the altitudes
    uint16_t n_dots[N_ALTS];		// max dots for each

    // show 0, 30 and 60 viewing altitudes, using more dots for the larger loci
    alts[0] = 0; alts[1] = 30; alts[2] = 60;
    n_dots[0] = 9*MAX_FOOT/13; n_dots[1] = 3*MAX_FOOT/13; n_dots[2] = 1*MAX_FOOT/13;

    // start max size, then reduce when know
    sat_foot = (SCoord *) realloc (sat_foot, MAX_FOOT*sizeof(SCoord));
    if (!sat_foot) {
	Serial.println (F("Failed to malloc sat_foot"));
	while (1);			// timeout
    }

    // sat_foot index
    n_foot = 0;

    for (uint8_t alt_i = 0; alt_i < N_ALTS; alt_i++) {

	// satellite viewing altitude
	float valt = deg2rad(alts[alt_i]);

	// great-circle radius from subsat point to viewing circle at altitude valt
	float vrad = sat->viewingRadius(valt);

	// compute each point around viewing circle
	float cosa, B;
	for (uint16_t foot_i = 0; foot_i < n_dots[alt_i]; foot_i++) {
	    float A = foot_i*2*M_PI/n_dots[alt_i];
	    solveSphere (A, vrad, cosc, sinc, &cosa, &B);
	    float vlat = M_PIF/2-acosf(cosa);
	    float vlng = myfmodf(B+satlng+5*M_PIF,2*M_PIF)-M_PIF;	// require [-180.180)
	    ll2s (vlat, vlng, sat_foot[n_foot], 2);
	    if (n_foot == 0 || memcmp (&sat_foot[n_foot], &sat_foot[n_foot-1], sizeof(SCoord)))
		n_foot++;
	}
    }
    // Serial.printf ("n_foot %u / %u\n", n_foot, MAX_FOOT);

    // reduce
    sat_foot = (SCoord *) realloc (sat_foot, n_foot*sizeof(SCoord));
    if (!sat_foot) {
	Serial.println (F("Failed to realloc sat_foot"));
	while (1);			// timeout
    }
}

/* return a DateTime for the user's notion of current time
 */
static DateTime userNow()
{
    uint32_t t = nowWO();
    int yr = year(t);
    int mo = month(t);
    int dy = day(t);
    int hr = hour(t);
    int mn = minute(t);
    int sc = second(t);

    DateTime dt(yr, mo, dy, hr, mn, sc);

    return (dt);
}

/* find next rise and set times if sat valid.
 * always find rise and set in the future, so set_time will be < rise_time iff pass is in progress.
 * also update flags ever_up, set_ok, ever_down and rise_ok.
 */
static void findNextPass(char *name)
{
    if (!sat || !obs) {
	set_ok = rise_ok = false;
	return;
    }

    // measure how long this takes
    uint32_t t0 = millis();

    #define COARSE_DT	90L		// seconds/step forward for fast search
    #define FINE_DT  	(-2L)		// seconds/step backward for refined search
    float pel;				// previous elevation
    long dt = COARSE_DT;		// search time step size, seconds
    DateTime t_now = userNow();		// user's display time
    DateTime t_srch = t_now + -FINE_DT;	// search time, start beyond any previous solution
    float tel, taz, trange, trate;	// target el and az, degrees

    // init pel and make first step
    sat->predict (t_srch);
    sat->topo (obs, pel, taz, trange, trate);
    t_srch += dt;

    // search up to a few days ahead for next rise and set times (for example for moon)
    set_ok = rise_ok = false;
    ever_up = ever_down = false;
    while ((!set_ok || !rise_ok) && t_srch < t_now + 2.0F) {
	resetWatchdog();

	// find circumstances at time t_srch
	sat->predict (t_srch);
	sat->topo (obs, tel, taz, trange, trate);

	// check for rising or setting events
	if (tel >= SAT_MIN_EL) {
            ever_up = true;
            if (pel < SAT_MIN_EL) {
                if (dt == FINE_DT) {
                    // found a refined set event (recall we are going backwards),
                    // record and resume forward time.
                    set_time = t_srch;
                    set_az = taz;
                    set_ok = true;
                    dt = COARSE_DT;
                    pel = tel;
                } else if (!rise_ok) {
                    // found a coarse rise event, go back slower looking for better set
                    dt = FINE_DT;
                    pel = tel;
                }
            }
	} else {
            ever_down = true;
            if (pel > SAT_MIN_EL) {
                if (dt == FINE_DT) {
                    // found a refined rise event (recall we are going backwards).
                    // record and resume forward time but skip if set is within COARSE_DT because we
                    // would jump over it and find the NEXT set.
                    float check_tel, check_taz;
                    DateTime check_set = t_srch + COARSE_DT;
                    sat->predict (check_set);
                    sat->topo (obs, check_tel, check_taz, trange, trate);
                    if (check_tel >= SAT_MIN_EL) {
                        rise_time = t_srch;
                        rise_az = taz;
                        rise_ok = true;
                    }
                    // regardless, resume forward search
                    dt = COARSE_DT;
                    pel = tel;
                } else if (!set_ok) {
                    // found a coarse set event, go back slower looking for better rise
                    dt = FINE_DT;
                    pel = tel;
                }
            }
	}

	// Serial.printf ("R %d S %d dt %ld from_now %8.3fs tel %g\n", rise_ok, set_ok, dt, 24*3600*(t_srch - t_now), tel);

	// advance time and save tel
	t_srch += dt;
	pel = tel;
    }

    // new pass ready
    new_pass = true;

    Serial.printf ("%s: next rise in %g hrs, set in %g (%ld ms)\n", name,
	rise_ok ? 24*(rise_time - t_now) : 0.0F, set_ok ? 24*(set_time - t_now) : 0.0F,
        millis() - t0);

    printFreeHeap (F("findNextPass"));
}

/* display next pass on sky dome.
 * N.B. we assume findNextPass has been called
 */
static void drawNextPass()
{
    resetWatchdog();

    // size and center of screen path
    uint16_t r0 = (dx_info_b.h-2*FONT_H)/2 - 1;
    uint16_t xc = dx_info_b.x + dx_info_b.w/2;
    uint16_t yc = dx_info_b.y + dx_info_b.h - r0 - 1;

    // erase
    tft.fillRect (dx_info_b.x+1, dx_info_b.y+2*FONT_H+1,
	    dx_info_b.w-2, dx_info_b.h-2*FONT_H+1, RA8875_BLACK);

    // skip if no sat or never up
    if (!sat || !obs || !ever_up)
	return;

    // find n steps, step duration and starting time
    bool full_pass = false;
    int n_steps = 0;
    float step_dt = 0;
    DateTime t;

    if (rise_ok && set_ok) {

        // find start and pass duration in days
        float pass_duration = set_time - rise_time;
        if (pass_duration < 0) {
            // rise after set means pass is underway so start now for remaining duration
            DateTime t_now = userNow();
            pass_duration = set_time - t_now;
            t = t_now;
        } else {
            // full pass so start at next rise
            t = rise_time;
            full_pass = true;
        }

        // find step size and number of steps
        n_steps = pass_duration/(PASS_STEP/SPD) + 1;
        if (n_steps > MAX_PASS_STEPS)
            n_steps = MAX_PASS_STEPS;
        step_dt = pass_duration/n_steps;

    } else {

        // it doesn't actually rise or set within the next 24 hour but it's up some time 
        // so just show it at its current position (if it's up)
        n_steps = 1;
        step_dt = 0;
        t = userNow();
    }

    // draw horizon and compass points
    #define HGRIDCOL RGB565(50,90,50)
    tft.drawCircle (xc, yc, r0, BRGRAY);
    for (float a = 0; a < 2*M_PIF; a += M_PIF/6) {
        uint16_t xr = lroundf(xc + r0*cosf(a));
        uint16_t yr = lroundf(yc - r0*sinf(a));
        tft.fillCircle (xr, yr, 1, RA8875_WHITE);
        tft.drawLine (xc, yc, xr, yr, HGRIDCOL);
    }

    float gwaz;
    if (getGimbalWrapAz (&gwaz)) {
        uint16_t xr = lroundf(xc + r0*sinf(deg2rad(gwaz)));
        uint16_t yr = lroundf(yc - r0*cosf(deg2rad(gwaz)));
        tft.fillCircle (xr, yr, 2, GIMBALWRAP_COL);
        printf ("az_mnt0 %g\n", gwaz);
    }

    // draw elevations
    for (uint8_t el = 30; el < 90; el += 30)
        tft.drawCircle (xc, yc, r0*(90-el)/90, HGRIDCOL);

    // label sky directions
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (BRGRAY);
    tft.setCursor (xc - r0, yc - r0 + 2);
    tft.print (F("NW"));
    tft.setCursor (xc + r0 - 12, yc - r0 + 2);
    tft.print (F("NE"));
    tft.setCursor (xc - r0, yc + r0 - 8);
    tft.print (F("SW"));
    tft.setCursor (xc + r0 - 12, yc + r0 - 8);
    tft.print (F("SE"));

    // connect several points from t until set_time, find max elevation for labeling
    float max_el = 0;
    uint16_t max_el_x = 0, max_el_y = 0;
    uint16_t prev_x = 0, prev_y = 0;
    for (uint8_t i = 0; i < n_steps; i++) {
        resetWatchdog();

        // find topocentric position @ t
        float el, az, range, rate;
        sat->predict (t);
        sat->topo (obs, el, az, range, rate);
        if (el < 0 && n_steps == 1)
            break;                                      // only showing pos now but it's down

        // find screen postion
        float r = r0*(90-el)/90;			// screen radius, zenith at center 
        uint16_t x = xc + r*sinf(deg2rad(az)) + 0.5F;	// want east right
        uint16_t y = yc - r*cosf(deg2rad(az)) + 0.5F;	// want north up

        // find max el
        if (el > max_el) {
            max_el = el;
            max_el_x = x;
            max_el_y = y;
        }

        // connect if have prev or just dot if only one
        if (i > 0 && (prev_x != x || prev_y != y))      // avoid bug with 0-length line
            tft.drawLine (prev_x, prev_y, x, y, PASS_COLOR);
        else if (n_steps == 1)
            tft.fillCircle (x, y, SAT_UP_R, SAT_COLOR);

        // label the set end if last step of several and full pass
        if (full_pass && i == n_steps - 1) {
            // x,y is very near horizon, try to move inside a little for clarity
            x += x > xc ? -12 : 2;
            y += y > yc ? -8 : 4;
            tft.setCursor (x, y);
            tft.print('S');
        }

        // save
        prev_x = x;
        prev_y = y;

        // next t
        t += step_dt;
    }

    // label max elevation and time up iff we have a full pass
    if (max_el > 0 && full_pass) {

        // max el
        uint16_t x = max_el_x, y = max_el_y;
        bool draw_left_of_pass = max_el_x > xc;
        bool draw_below_pass = max_el_y < yc;
        x += draw_left_of_pass ? -30 : 20;
        y += draw_below_pass ? 5 : -18;
        tft.setCursor (x, y); 
        tft.print(max_el, 0);
        tft.drawCircle (tft.getCursorX()+2, tft.getCursorY(), 1, BRGRAY);       // simple degree symbol

        // pass duration
        int s_up = (set_time - rise_time)*24*3600;
        char tup_str[32];
        if (s_up >= 3600) {
            int h = s_up/3600;
            int m = (s_up - 3600*h)/60;
            snprintf (tup_str, sizeof(tup_str), "%dh%02d", h, m);
        } else {
            int m = s_up/60;
            int s = s_up - 60*m;
            snprintf (tup_str, sizeof(tup_str), "%d:%02d", m, s);
        }
        uint16_t bw = getTextWidth (tup_str);
        if (draw_left_of_pass)
            x = tft.getCursorX() - bw + 4;                                  // account for deg
        y += draw_below_pass ? 12 : -11;
        tft.setCursor (x, y);
        tft.print(tup_str);
    }

    printFreeHeap (F("drawNextPass"));
}

/* draw name of current satellite on the map and also possibly in dx_info box
 */
static void drawSatName()
{
    if (!sat || !obs || !SAT_NAME_IS_SET())
	return;

    resetWatchdog();

    // retrieve saved name without '_'
    char user_name[NV_SATNAME_LEN];
    strncpySubChar (user_name, sat_name, ' ', '_', NV_SATNAME_LEN);

    // draw in dx_info if using it for sat info
    if (dx_info_for_sat) {

	// erase
	tft.fillRect (dx_info_b.x, dx_info_b.y+1, dx_info_b.w, dx_info_b.h-1, RA8875_BLACK);

	// shorten until fits in satname_b
	selectFontStyle (LIGHT_FONT, SMALL_FONT);
	tft.setTextColor (SAT_COLOR);
	uint16_t bw;
	while (1) {
	    bw = getTextWidth (user_name);
	    if (bw <= satname_b.w)
		break;
	    user_name[strlen(user_name) - 1] = '\0';
	}

	// draw
	tft.fillRect (satname_b.x, satname_b.y, satname_b.w, satname_b.h, RA8875_BLACK);
	tft.setCursor (satname_b.x + (satname_b.w - bw)/2, satname_b.y+FONT_H - 2);
	tft.print (user_name);

    }
}

/* fill map_name_b with where sat name should go on map
 */
static void setSatMapNameLoc()
{
    // retrieve saved name without '_'
    char user_name[NV_SATNAME_LEN];
    strncpySubChar (user_name, sat_name, ' ', '_', NV_SATNAME_LEN);

    // get size
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    uint16_t bw, bh;
    getTextBounds (user_name, &bw, &bh);
    map_name_b.w = bw;
    map_name_b.h = bh;

    if (azm_on) {
	// easy: just print between hemispheres
	map_name_b.x = map_b.x + (map_b.w - map_name_b.w)/2 ;
	map_name_b.y = map_b.y + 10;
    } else {
	// locate name far from current location and potential obstacles.
	// N.B. start choice above RSS and below sun and moon
	SCoord loc = sat_path[0];
	if (loc.x < map_b.x + map_b.w/2) {
	    // Indian ocean
	    map_name_b.x = map_b.x + 5*map_b.w/8;
	    map_name_b.y = rss_bnr_b.y - map_name_b.h - 5;
	} else {
	    // south pacific
	    map_name_b.x = map_b.x + map_b.w/10;
	    map_name_b.y = rss_bnr_b.y - map_name_b.h - 5;
	}

	// check it's not on the path 
	for (uint16_t p = 0; p < n_path; p++) {
	    SCoord s = sat_path[p];
	    if (inBox (s, map_name_b))
		map_name_b.x = s.x + 20;
	}

	// avoid antipodal and markers
	if (inBox (deap_c.s, map_name_b))
	    map_name_b.x = deap_c.s.x + DEAP_R + 20;
	if (inBox (de_c.s, map_name_b))
	    map_name_b.x = de_c.s.x + DE_R + 20;
	if (inBox (dx_c.s, map_name_b))
	    map_name_b.x = dx_c.s.x + DX_R + 20;

	// check if any of these corrections went too far right
	if (map_name_b.x + map_name_b.w > map_b.x + map_b.w)
	    map_name_b.x = map_b.x + 20;
    }
}

/* mark current sat pass location 
 */
static void drawSatNow()
{
    resetWatchdog();

    float az, el, raz, saz;
    getSatAzElNow (NULL, &az, &el, &raz, &saz, NULL, NULL);

    // size and center of screen path
    uint16_t r0 = (dx_info_b.h-2*FONT_H)/2;
    uint16_t x0 = dx_info_b.x + dx_info_b.w/2;
    uint16_t y0 = dx_info_b.y + dx_info_b.h - r0;

    float r = r0*(90-el)/90;				// screen radius, zenith at center 
    uint16_t x = x0 + r*sinf(deg2rad(az)) + 0.5F;	// want east right
    uint16_t y = y0 - r*cosf(deg2rad(az)) + 0.5F;	// want north up

    tft.fillCircle (x, y, SAT_UP_R, SAT_COLOR);
}

/* draw event title and time t in the dx_info box unless t < 0 then just show title.
 * t is in days: if > 1 hour show HhM else M:S
 */
static void drawSatTime (const char *title, float t)
{
    if (!sat)
	return;

    resetWatchdog();

    static char prev_title[30];
    static uint8_t prev_a, prev_b;

    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (SAT_COLOR);

    // erase if different title
    if (strcmp (title, prev_title)) {
	tft.fillRect (dx_info_b.x, dx_info_b.y+FONT_H, dx_info_b.w, FONT_H, RA8875_BLACK);
	strncpy (prev_title, title, sizeof(prev_title)-1);
    }

    // draw title (even if the same, in case it was erased for some other reason)
    tft.setCursor (dx_info_b.x+1, dx_info_b.y+2*FONT_H-5);
    tft.print (title);
    uint16_t timex = tft.getCursorX();

    // draw time unless coded to be ignored
    if (t >= 0) {
	// assume H:M
        char sep = 'h';
	t *= 24;
	uint8_t a = t, b;
	if (a == 0) {
	    // change to M:S
            sep = ':';
	    t *= 60;
	    a = t;
	}
	b = (t - a)*60;

	// erase if different time
	if (a != prev_a || b != prev_b) {
	    tft.fillRect (timex, dx_info_b.y+FONT_H, dx_info_b.w-(timex-dx_info_b.x), FONT_H, RA8875_BLACK);
	    prev_a = a;
	    prev_b = b;
	}

	// draw time (even if the same, like title, in case it was erased for some other reason)
	tft.print(a);
	tft.print(sep);
	if (b < 10)
	    tft.print('0');
	tft.print(b);
	// tft.printf ("%d:%02d", a, b);		often causes panic

    } else {
	// erase time
	tft.fillRect (timex, dx_info_b.y+FONT_H, dx_info_b.w-(timex-dx_info_b.x), FONT_H, RA8875_BLACK);
    }
}

/* return whether the given line appears to be a valid TLE
 * only count digits and '-' counts as 1
 */
static bool tleHasValidChecksum (const char *line)
{
    // sum first 68 chars
    int sum = 0;
    for (uint8_t i = 0; i < 68; i++) {
	char c = *line++;
	if (c == '-')
	    sum += 1;
	else if (c == '\0')
	    return (false);         // too short
	else if (c >= '0' && c <= '9')
	    sum += c - '0';
    }

    // last char is sum of previous modulo 10
    return ((*line - '0') == (sum%10));
}

/* clear screen, show the given message then restart operation without a sat
 */
static void fatalSatError (const char *fmt, ...)
{
    char buf[256];
    va_list ap;

    int l = sprintf (buf, "Sat error: ");

    va_start (ap, fmt);
    vsnprintf (buf+l, sizeof(buf)-l, fmt, ap);
    va_end (ap);

    Serial.println (buf);

    selectFontStyle (BOLD_FONT, SMALL_FONT);
    uint16_t bw = getTextWidth (buf);

    eraseScreen();
    tft.setTextColor (RA8875_WHITE);
    tft.setCursor ((tft.width()-bw)/2, tft.height()/2);
    tft.print (buf);

    wdDelay (5000);

    resetWatchdog();
    unsetSat();
    initScreen();
}

static void showSelectionBox (uint16_t x, uint16_t y, bool on)
{
    uint16_t fill_color = on ? SAT_COLOR : RA8875_BLACK;
    tft.fillRect (x, y+(CELL_H-CB_SIZE)/2+3, CB_SIZE, CB_SIZE, fill_color);
    tft.drawRect (x, y+(CELL_H-CB_SIZE)/2+3, CB_SIZE, CB_SIZE, RA8875_WHITE);
}


/* look up sat_name. if found set up sat, else inform user and remove sat altogether.
 * return whether found it.
 */
static bool satLookup ()
{
    Serial.printf ("Looking up %s\n", sat_name);

    if (!SAT_NAME_IS_SET())
	return (false);

    // delete then restore if found
    if (sat) {
        delete sat;
        sat = NULL;
    }

    WiFiClient tle_client;
    char t1[TLE_LINEL], t2[TLE_LINEL];
    char name[100];
    bool ok = false;

    resetWatchdog();
    if (wifiOk() && tle_client.connect (svr_host, HTTPPORT)) {
	resetWatchdog();

	// query
	snprintf (name, sizeof(name), sat_one_page, sat_name);
	httpGET (tle_client, svr_host, name);
	if (!httpSkipHeader (tle_client)) {
	    fatalSatError ("Bad http header");
	    goto out;
	}

	// first response line is sat name, should match query
	if (!getTCPLine (tle_client, name, sizeof(name), NULL)) {
	    fatalSatError ("Satellite %s not found", sat_name);
	    goto out;
	}
	if (strcasecmp (name, sat_name)) {
	    fatalSatError ("Name does not match: '%s' '%s'", sat_name, name);
	    goto out;
	}

	// next two lines are TLE
	if (!getTCPLine (tle_client, t1, sizeof(t1), NULL)) {
	    fatalSatError ("Error reading line 1");
	    goto out;
	}
	if (!tleHasValidChecksum (t1)) {
	    fatalSatError ("Bad checksum for %s in line 1", sat_name);
	    goto out;
	}
	if (!getTCPLine (tle_client, t2, sizeof(t2), NULL)) {
	    fatalSatError ("Error reading line 2");
	    goto out;
	}
	if (!tleHasValidChecksum (t2)) {
	    fatalSatError ("Bad checksum for %s in line 2", sat_name);
	    goto out;
	}

	// TLE looks good, update name so cases match, define new sat
        memcpy (sat_name, name, sizeof(sat_name)-1);    // retain EOS
	sat = new Satellite (t1, t2);
	tle_refresh = nowWO();
	ok = true;

    } else {

	fatalSatError ("network error");
    }

out:

    tle_client.stop();

    printFreeHeap (F("satLookup"));

    return (ok);
}

/* show all names and allow op to choose one, wait forever unless timeout.
 * save selection in sat_name, even if empty for no selection.
 * return whether sat was selected.
 */
static bool askSat (bool timeout)
{
    #define NO_SAT		(-1)		// cookie when op has chosen not to display a sat

    resetWatchdog();

    // don't inherit anything lingering after the tap that got us here
    drainTouch();

    // erase screen and set font
    eraseScreen();
    tft.setTextColor (RA8875_WHITE);

    // show title, prompt and save end cursor location if running a countdown
    uint16_t count_x = 0, count_y = 3*TBORDER/4;
    uint8_t count_s = 5;		// count down seconds
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    tft.setCursor (5, count_y);
    tft.print (F("Select satellite, or none"));
    if (timeout) {
	tft.print (F(" ... "));
	count_x = tft.getCursorX();
	tft.print(count_s);
    }
    uint32_t count_t0 = millis();	// start of count down

    // show what SOON_COLOR means
    tft.setTextColor (SOON_COLOR);
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setCursor (tft.width()-435, count_y);
    tft.printf ("<%d Mins", SOON_MINS);

    // show what SAT_COLOR means
    tft.setTextColor (SAT_COLOR);
    tft.setCursor (tft.width()-315, count_y);
    tft.print (F("Now"));

    // show rise units
    tft.setTextColor (RA8875_WHITE);
    tft.setCursor (tft.width()-250, count_y);
    tft.print (F("Rise in HH:MM"));
    selectFontStyle (BOLD_FONT, SMALL_FONT);

    // show Ok button
    drawStringInBox ("Ok", ok_b, false, RA8875_WHITE);

    /// setup
    char sat_names[MAX_NSAT][NV_SATNAME_LEN];
    uint16_t prev_sel_x = 0, prev_sel_y = 0;
    int8_t sel_idx = NO_SAT;
    uint8_t n_sat = 0;

    // open connection
    WiFiClient sat_client;
    resetWatchdog();
    if (!wifiOk() || !sat_client.connect (svr_host, HTTPPORT))
        goto out;

    // query page and skip header
    resetWatchdog();
    httpGET (sat_client, svr_host, sat_get_all);
    if (!httpSkipHeader (sat_client))
        goto out;

    // read and display each sat
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    for (n_sat = 0; n_sat < MAX_NSAT; n_sat++) {

        // read name and 2 lines, done when eof
        char t1[TLE_LINEL], t2[TLE_LINEL];
        if (!getTCPLine (sat_client, &sat_names[n_sat][0], NV_SATNAME_LEN, NULL)
                         || !getTCPLine (sat_client, t1, sizeof(t1), NULL)
                         || !getTCPLine (sat_client, t2, sizeof(t2), NULL)) {
            break;
        }

        // find row and column, col-major order
        uint8_t r = n_sat % N_ROWS;
        uint8_t c = n_sat / N_ROWS;

        // ul corner
        uint16_t x = c*CELL_W;
        uint16_t y = TBORDER + r*CELL_H;

        // show tick box, pre-select if saved before
        if (strcmp (sat_name, sat_names[n_sat]) == 0) {
            sel_idx = n_sat;
            showSelectionBox (x, y, true);
            prev_sel_x = x;
            prev_sel_y = y;
        } else {
            showSelectionBox (x, y, false);
        }

        // display next rise time of this sat
        if (sat)
            delete sat;
	sat = new Satellite (t1, t2);
        findNextPass(sat_names[n_sat]);
        tft.setTextColor (RA8875_WHITE);
        tft.setCursor (x + CB_SIZE + 8, y + FONT_H);
        if (rise_ok) {
            DateTime t_now = userNow();
            if (rise_time < set_time) {
                // pass lies ahead
                float hrs_to_rise = (rise_time - t_now)*24.0;
                if (hrs_to_rise*60 < SOON_MINS)
                    tft.setTextColor (SOON_COLOR);
                uint8_t mins_to_rise = (hrs_to_rise - (uint16_t)hrs_to_rise)*60;
                if (hrs_to_rise < 1 && mins_to_rise < 1)
                    mins_to_rise = 1;   // 00:00 looks wrong
                if (hrs_to_rise < 10)
                    tft.print ('0');
                tft.print ((uint16_t)hrs_to_rise);
                tft.print (':');
                if (mins_to_rise < 10)
                    tft.print ('0');
                tft.print (mins_to_rise);
                tft.print (' ');
            } else {
                // pass in progress
                tft.setTextColor (SAT_COLOR);
                tft.print (F("Up "));
            }
        } else if (!ever_up) {
            tft.setTextColor (GRAY);
            tft.print (F("NoR "));
        } else if (!ever_down) {
            tft.setTextColor (SAT_COLOR);
            tft.print (F("NoS "));
        }

        // followed by scrubbed name
        char user_name[NV_SATNAME_LEN];
        strncpySubChar (user_name, sat_names[n_sat], ' ', '_', NV_SATNAME_LEN);
        tft.print (user_name);
    }

    // close connection
    sat_client.stop();

    // bale if no satellites found
    if (n_sat == 0)
	goto out;

    // follow touches to make selection, done when tap Ok or timeout after SEL_TO
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    while (true) {

	// wait for tap or time out
	SCoord s;
	while (readCalTouch(s) == TT_NONE) {
	    resetWatchdog();
	    if (timeout && count_s > 0) {
		uint32_t t = millis();
		if (t - count_t0 > 1000) {
		    if (--count_s == 0) {
			// timed out: retain current selection
			return (true);
		    }
                    // draw counter
		    tft.fillRect (count_x, count_y-30, 20, 40, RA8875_BLACK);
		    tft.setCursor (count_x, count_y);
                    tft.setTextColor (RA8875_WHITE);
		    tft.print(count_s);
		    count_t0 = t;
		}
	    }
	    wdDelay(50);
	}

	// get here if user tapped something: abort countdown if using a timeout
	if (timeout && count_s > 0) {
	    count_s = 0;
	    tft.fillRect (count_x, count_y-30, 20, 40, RA8875_BLACK);
	}

	// tap Ok?
	if (inBox (s, ok_b)) {
	    // show Ok button toggle
	    drawStringInBox ("Ok", ok_b, true, RA8875_WHITE);
	    break;
	}

	// which satellite was tapped?
	resetWatchdog();
	uint8_t r = (s.y - TBORDER)/CELL_H;
	uint8_t c = s.x/CELL_W;
	if (s.x - c*CELL_W > CELL_W/2)
	    continue;				// require tapping in left half of cell
	uint8_t tap_idx = c*N_ROWS + r;		// column major order
	if (tap_idx < n_sat) {
	    // toggle
	    uint16_t x = c * CELL_W;
	    uint16_t y = TBORDER + r * CELL_H;
	    if (tap_idx == sel_idx) {
		// already on: forget and toggle off
		// Serial.printf ("forget %s\n", sat_names[sel_idx]);
		showSelectionBox (x, y, false);
		sel_idx = NO_SAT;
	    } else {
		// toggle previous selection off (if any) and show selected
		if (prev_sel_y > 0)
		    showSelectionBox (prev_sel_x, prev_sel_y, false);
		sel_idx = tap_idx;
		prev_sel_x = x;
		prev_sel_y = y;
		// Serial.printf ("select %s\n", sat_names[sel_idx]);
		showSelectionBox (x, y, true);
	    }
	}
    }

  out:

    // close connection
    sat_client.stop();

    printFreeHeap (F("askSat"));

    if (n_sat == 0) {
	fatalSatError ("No satellites found");
	return (false);
    }

    // set sat_name and whether any selected
    if (sel_idx != NO_SAT) {
	strcpy (sat_name, sat_names[sel_idx]);
	return (true);
    } else {
	unsetSat();
	return (false);
    }
}

/* return whether sat epoch is known to be good.
 */
static bool checkSatEpoch()
{
    if (!sat)
	return (false);

    DateTime t_now = userNow();
    DateTime t_epo = sat->epoch();
    if (isSatMoon())
        return (t_epo + 1.5F > t_now && t_now + 1.5F > t_epo);
    else
        return (t_epo + MAX_TLE_AGE > t_now && t_now + MAX_TLE_AGE > t_epo);
}

/* set the satellite observing location
 */
void setSatObserver (float lat, float lng)
{
    resetWatchdog();

    if (obs)
	delete obs;
    obs = new Observer (lat, lng, 0);
}

/* if a satellite is currently in play, return its name, current az and el, az of next rise and set,
 *    and hours until next rise and set. name and times may be NULL if no interested.
 * even if return true, rise and set az may be SAT_NOAZ, for example geostationary, in which case *rdtp
 *    and *sdtp are not set even if not NULL.
 */
bool getSatAzElNow (char *name, float *azp, float *elp, float *razp, float *sazp,
float *rdtp, float *sdtp)
{
    // get out fast if nothing to do or no info
    if (!obs || !sat || !SAT_NAME_IS_SET())
	return (false);

    // public name, if interested
    if (name)
        strncpySubChar (name, sat_name, ' ', '_', NV_SATNAME_LEN);

    // compute now
    DateTime t_now = userNow();
    float range, rate;
    sat->predict (t_now);
    sat->topo (obs, *elp, *azp, range, rate);

    // horizon info, if available
    *razp = rise_ok ? rise_az : SAT_NOAZ;
    *sazp = set_ok  ? set_az  : SAT_NOAZ;

    // times, if interested and available
    if (rdtp && rise_ok)
        *rdtp = (rise_time - t_now)*24;
    if (sdtp && set_ok)
        *sdtp = (set_time - t_now)*24;

    // ok
    return (true);
}


/* called by main loop() to update pass info.
 * once per second is enough, not needed at all if no sat named or !dx_info_for_sat
 * the _path_ is updated much less often in updateSatPath().
 */
void updateSatPass()
{
    // get out fast if nothing to do or don't care
    if (!obs || !dx_info_for_sat || !SAT_NAME_IS_SET())
	return;

    // run once per second is fine
    static uint32_t last_run;
    if (!timesUp(&last_run, 1000))
        return;

    // look up if first time or time to refresh
    if (!sat) {
	if (!clockTimeOk()) {
	    // network error, wait longer next time to give a chance to recover
	    last_run += 60000UL;
	    return;
	}
	if (!satLookup()) {
	    return;
	}
	if (!checkSatEpoch()) {
	    // got it but epoch is out of date, give up
	    fatalSatError ("Epoch for %s is out of date", sat_name);
	    return;
	}
	// ok, update all info
	displaySatInfo();
    }

    resetWatchdog();

    // check edge cases
    if (!ever_up) {
        drawSatTime ("     No rise", -1);
	return;
    }
    if (!ever_down) {
        drawSatTime ("      No set", -1);
	return;
    }

    // update pass and process key events

    DateTime t_now = userNow();
    float days_to_rise = rise_time - t_now;
    float days_to_set = set_time - t_now;

    if (rise_time < set_time) {
	if (t_now < rise_time) {
	    // pass lies ahead
	    drawSatTime ("Rise in ", days_to_rise);
	} else if (t_now < set_time) {
	    // pass in progress
	    drawSatTime (" Set in ", days_to_set);
	    drawSatNow();
	} else {
	    // just set, time to find next pass
	    displaySatInfo();
	}
    } else {
	if (t_now < set_time) {
	    // pass in progress
	    drawSatTime (" Set in ", days_to_set);
	    drawSatNow();
	} else {
	    // just set, time to find next pass
	    displaySatInfo();
	}
    }

}

/* compute satellite geocentric path into sat_path[] and footprint into sat_foot[].
 * called once at the top of each map sweep so we can afford more extenstive checks than updateSatPass().
 * just skip if no named satellite or time is not confirmed.
 * the _pass_ is updated in updateSatPass().
 * we also update map_name_b to avoid the current sat location.
 */
void updateSatPath()
{
    if (!obs || !SAT_NAME_IS_SET() || !clockTimeOk())
	return;

    resetWatchdog();

    // look up if first time
    if (!sat) {
	if (!satLookup())
	    return;
	// init pass info for updateSatPass()
	findNextPass(sat_name);
    }

    // confirm epoch is still valid
    if (!checkSatEpoch()) {
	// not valid, maybe a fresh element set will be ok
        Serial.printf ("%s out of date\n", sat_name);
	if (!satLookup())
	    return;
	if (!checkSatEpoch()) {
	    // no update or still bad epoch, give up on this sat
	    fatalSatError ("Epoch for %s is out of date", sat_name);
	    return;
	}
	// init pass info for updateSatPass()
	findNextPass(sat_name);
    }

    // from here we have a valid sat to report

    // free sat_path first since it was last to be malloced
    if (sat_path) {
	free (sat_path);
	sat_path = NULL;
    }

    // fill sat_foot
    DateTime t = userNow();
    float satlat, satlng;
    sat->predict (t);
    sat->geo (satlat, satlng);
    updateFootPrint(satlat, satlng);

    // start sat_path max size, then reduce when know size needed
    sat_path = (SCoord *) malloc (MAX_PATH * sizeof(SCoord));
    if (!sat_path) {
	Serial.println (F("Failed to malloc sat_path"));
	while (1);	// timeout
    }

    // fill sat_path
    float period = sat->period();
    n_path = 0;
    uint16_t max_path = isSatMoon() ? 1 : MAX_PATH;         // N.B. only set the current location if Moon
    for (uint16_t p = 0; p < max_path; p++) {
	ll2s (satlat, satlng, sat_path[n_path], 2);
	if (n_path == 0 || memcmp (&sat_path[n_path], &sat_path[n_path-1], sizeof(SCoord)))
	    n_path++;
	t += period/max_path;	// show 1 rev
	sat->predict (t);
	sat->geo (satlat, satlng);
    }
    // Serial.printf ("n_path %u / %u\n", n_path, MAX_PATH);

    // reduce
    sat_path = (SCoord *) realloc (sat_path, n_path * sizeof(SCoord));
    if (!sat_path) {
	Serial.println (F("Failed to realloc sat_path"));
	while (1);	// timeout
    }

    // set map name to avoid current location
    setSatMapNameLoc();
}

/* draw all sat path points on the given screen row
 */
void drawSatPointsOnRow (uint16_t y0)
{
    if (!sat)
	return;

    resetWatchdog();

    // draw fat pixel on row above to avoid next row erasing it

    for (uint16_t p = 0; p < n_path; p++) {
        SCoord s = sat_path[p];
        if (y0 == s.y && overMap(s)) {
            tft.drawPixel (s.x, s.y, TRACK_COLOR);
            s.y -= 1;
            if (overMap(s)) tft.drawPixel (s.x, s.y, TRACK_COLOR);
            s.x += 1;
            if (overMap(s)) tft.drawPixel (s.x, s.y, TRACK_COLOR);
            s.y += 1;
            if (overMap(s)) tft.drawPixel (s.x, s.y, TRACK_COLOR);
        }
    }

    for (uint16_t f = 0; f < n_foot; f++) {
	SCoord s = sat_foot[f];
	if (y0 == s.y && overMap(s)) {
	    tft.drawPixel (s.x, s.y, FP_COLOR);
            s.y -= 1;
	    if (overMap(s)) tft.drawPixel (s.x, s.y, FP_COLOR);
            s.x += 1;
	    if (overMap(s)) tft.drawPixel (s.x, s.y, FP_COLOR);
            s.y += 1;
	    if (overMap(s)) tft.drawPixel (s.x, s.y, FP_COLOR);
	}
    }
}

/* draw sat name on map if it includes row y0 unless already showing in dx_info.
 * also draw if y0 == 0 as a way to draw regardless.
 */
void drawSatNameOnRow(uint16_t y0)
{
    // done if nothing to do or name is not using row y0
    if (dx_info_for_sat || !sat || !obs || !SAT_NAME_IS_SET())
	return;
    if (y0 != 0 && (y0 < map_name_b.y || y0 >= map_name_b.y + map_name_b.h))
	return;

    resetWatchdog();

    // retrieve saved name without '_'
    char user_name[NV_SATNAME_LEN];
    strncpySubChar (user_name, sat_name, ' ', '_', NV_SATNAME_LEN);

    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (SAT_COLOR);
    tft.setCursor (map_name_b.x, map_name_b.y + map_name_b.h - 1);
    tft.print (user_name);
}

/* return whether user has tapped near the head of the satellite path or in the map name
 */
bool checkSatTouch (const SCoord &s)
{
    if (!sat || !sat_path)
	return (false);

    SBox sat_b;
    sat_b.x = sat_path[0].x-SAT_TOUCH_R;
    sat_b.y = sat_path[0].y-SAT_TOUCH_R;
    sat_b.w = 2*SAT_TOUCH_R;
    sat_b.h = 2*SAT_TOUCH_R;

    return (inBox (s, sat_b) || (!dx_info_for_sat && inBox (s, map_name_b)));
}

/* return whether user has tapped the satellite name box
 */
bool checkSatNameTouch (const SCoord &s)
{
    return (inBox (s, satname_b));
}

/* something effecting the satellite has changed such as time or observer so get fresh info then
 * display it in dx_info_b, if care
 */
void displaySatInfo()
{
    if (!obs || !sat || !dx_info_for_sat)
	return;

    // confirm epoch still valid
    if (!checkSatEpoch()) {
	fatalSatError ("Epoch for %s is out of date", sat_name);
	return;
    }

    // freshen elements if stale
    if (nowWO() - tle_refresh > TLE_REFRESH) {
	if (!satLookup())
	    return;
    }

    findNextPass(sat_name);
    drawSatName();
    drawNextPass();
}

/* retrieve list of satellites and let user select up to one, preselecting last known if any.
 * save name in sat_name and NVRAM, even if empty to signify no satellite.
 * return whether a sat was chosen or not.
 */
bool querySatSelection(bool timeout)
{
    resetWatchdog();

    // stop any tracking
    stopGimbalNow();

    NVReadString (NV_SATNAME, sat_name);
    if (askSat(timeout)) {
        Serial.printf ("Selected sat '%s'\n", sat_name);
	if (!satLookup())
	    return (false);
	findNextPass(sat_name);
    } else {
	delete sat;
	sat = NULL;
    }

    NVWriteString (NV_SATNAME, sat_name);	// persist name even if empty

    printFreeHeap (F("querySatSelection"));

    return (SAT_NAME_IS_SET());
}

/* install new satellite, if possible, or remove if "none"
 */
bool setSatFromName (const char *new_name)
{
    // remove if "none"
    if (strcmp (new_name, "none") == 0) {
        if (SAT_NAME_IS_SET()) {
            unsetSat();
            initScreen();
        }
        return (true);
    }


    strncpySubChar (sat_name, new_name, '_', ' ', NV_SATNAME_LEN);

    if (satLookup()) {
	// found

        // stop any tracking
        stopGimbalNow();

	// make permanent and redraw
	dx_info_for_sat = true;
	NVWriteString (NV_SATNAME, sat_name);
	initScreen();
	return (true);
    } else {
	// failed
	return (false);
    }
}

/* install a new satellite from its TLE.
 * return whether all good.
 * N.B. not saved in NV_SATNAME because we won't have the tle
 */
bool setSatFromTLE (const char *name, const char *t1, const char *t2)
{
    if (!tleHasValidChecksum(t1) || !tleHasValidChecksum(t2))
        return(false);

    // stop any tracking
    stopGimbalNow();

    sat = new Satellite (t1, t2);
    if (!checkSatEpoch()) {
        delete sat;
	sat = NULL;
        fatalSatError ("Elements out of date");
        return (false);
    }
    tle_refresh = nowWO();
    dx_info_for_sat = true;
    strncpySubChar (sat_name, name, '_', ' ', NV_SATNAME_LEN);
    initScreen();
    return (true);
}

/* return whether there is a valid sat in NV
 */
bool initSatSelection()
{
    NVReadString (NV_SATNAME, sat_name);
    return (SAT_NAME_IS_SET());
}

/* return whether new_pass has been set since last call, and always reset.
 */
bool isNewPass()
{
    bool np = new_pass;
    new_pass = false;
    return (np);
}

/* return whether the current satellite is in fact the moon
 */
bool isSatMoon()
{
    return (sat && !strcmp_P (sat_name, PSTR("Moon")));
}
