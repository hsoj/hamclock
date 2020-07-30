/* HamClock
 */


// glue
#include "HamClock.h"

// host server
const char svr_host[] = "clearskyinstitute.com";

// clock and stopwatch box
SBox clock_b = { 0, 65, 230, 80};
SBox stopwatch_b = {149, 93, 38, 22};

// DE and DX map boxes
SBox dx_info_b;                         // dx info location
SBox satname_b;				// satellite name
SBox de_info_b;                         // de info location
SBox de_title_b;                        // de title location
SBox map_b;                             // entire map, pixels only, not border
SBox dx_maid_b;                         // dx maindenhead 
SBox de_maid_b;                         // de maindenhead 

// DST boxes
TZInfo de_tz = {{85, 158, 50, 17}, DE_COLOR, 0};
TZInfo dx_tz = {{85, 307, 50, 17}, DX_COLOR, 0};


// multi-purpose plotting boxes, bottom matches with map_b.y border
SBox plot1_b = {235, 0, 160, PLOT1_H};
SBox plot2_b = {405, 0, 160, PLOT2_H};
SBox plot3_b = {575, 0, 160, PLOT3_H};

// NCDFX box
SBox NCDXF_b = {745, 0, 54, 147};

// brightness control and mode control
SBox brightness_b = {745, 30, 54, 147-30};	// lower portion of NCDXF_b
uint8_t brb_mode;                       // one of BRB_MODE

// graphical button sizes
#define	GB_W	14
#define	GB_H	34

// plot choices
uint8_t plot1_ch, plot2_ch, plot3_ch;

// RSS banner and button boxes
SBox rss_bnr_b;
SBox rss_btn_b;
uint8_t rss_on;

// Azimuthal-Mercator button box and flag
SBox azm_btn_b;
uint8_t azm_on;

// lat/long grid button box and flag
SBox llg_btn_b;
uint8_t llg_on;

// callsign info
typedef struct {
    char *call;                         // callsign from clockSetup()
    uint16_t fg_color;                  // fg color
    uint16_t bg_color;                  // bg color unless ..
    uint8_t bg_rainbow;                 // .. bg rainbow?
    SBox box;                           // size and location
} CallsignInfo;
static CallsignInfo cs_info;
static SBox version_b;			// check for new version

// de and dx sun rise/set boxes, dxsrss_b also used for DX prefix depending on dxsrss
SBox desrss_b, dxsrss_b;

// screen lock control
SBox lkscrn_b = {217, 117, 13, 20};

// WiFi touch control
TouchType wifi_tt;
SCoord wifi_tt_s;

// set up TFT display controller RA8875 instance on hardware SPI plus reset and chip select
#define RA8875_RESET    16
#define RA8875_CS       2
Adafruit_RA8875_R tft(RA8875_CS, RA8875_RESET);

// manage the great circle path through DE and DX points
#define MAX_GPATH		1500    // max number of points to draw in great circle path
static SCoord *gpath;			// malloced path points
static uint16_t n_gpath;		// actual number in use
static uint32_t gpath_time;		// millis() when great path was drawn
#define	GPATH_LINGER	15000		// time for great path to remain on screen, millis()
#define	GPATH_COLOR	RA8875_WHITE	// path color
static SBox prefix_b;                   // where to show DX prefix text

// manage using DX cluster prefix or one from nearestPrefix()
static bool dx_prefix_use_override;     // whether to use dx_override_prefixp[] or nearestPrefix()
static char dx_override_prefix[MAX_PREF_LEN];

// longest interval between calls to resetWatchdog(), ms
uint32_t max_wd_dt;

/* free gpath.
 */
void setDXPathInvalid()
{
    if (gpath) {
	free (gpath);
	gpath = NULL;
    }
    n_gpath = 0;
}

// these are needed for a normal C++ compiler
static void drawCallsign (bool all);
static void drawVersion(bool force);
static void checkTouch(void);
static void drawUptime(bool force);
static void drawWiFiInfo(void);
static void drawScreenLock(void);
static void toggleLockScreen(void);
static void toggleDETimeFormat(void);
static bool checkRSSTouch(SCoord &s);
static bool checkCallsignTouchFG (SCoord &b);
static bool checkCallsignTouchBG (SCoord &b);
static void eraseDXPath(void);
static void eraseDXMarker(void);
static void drawOneTimeDX(void);
static void drawDXPath(void);
static uint16_t getNextColor(uint16_t current);
static void drawRainbow (SBox &box);
static void drawDXCursorPrefix (void);
static void setDXPrefixOverride (const char *ovprefix);
static void unsetDXPrefixOverride (void);
#if defined(_USE_FB0)
static void shutdown(void);
#endif // _USE_FB0


// initial stack
char *stack_start;

// called once
void setup()
{
    // init record of stack
    char stack;
    stack_start = &stack;

    // life
    pinMode(LIFE_LED, OUTPUT);
    digitalWrite (LIFE_LED, HIGH);

    // this just reset the soft timeout, the hard timeout is still 6 seconds
    ESP.wdtDisable();

    // start debug trace
    resetWatchdog();
    Serial.begin(115200);
    while (!Serial);
    Serial.print(F("HamClock version "));
    Serial.println (F(VERSION));

    // random seed, not critical
    randomSeed(micros());

    // Initialise the display
    if (!tft.begin(RA8875_800x480)) {
	Serial.println("RA8875 Not Found!");
	while (1);
    }

    // set rotation from nvram setting .. too early to query setup options
    uint8_t rot = 0;
    (void) NVReadUInt8 (NV_ROTATE_SCRN, &rot);
    tft.setRotation(rot ? 2 : 0);

    // Adafruit assumed ESP8266 would run at 80 MHz, but we run it at 160
    extern uint32_t spi_speed;
    spi_speed *= 2;

    // turn display full on
    tft.displayOn(true);
    tft.GPIOX(true); 
    tft.PWM1config(true, RA8875_PWM_CLK_DIV1024); // PWM output for backlight
    initBrightness();

// #define _GFX_COORD_TEST
#if defined(_GFX_COORD_TEST)
    // just used to confirm our posix porting layer graphics agree with Adafruit
    tft.fillScreen(RA8875_BLACK);
    tft.fillRect (100, 100, 6, 6, RA8875_RED);
    tft.drawRect (100, 100, 8, 8, RA8875_RED);
    tft.drawPixel (100,108,RA8875_RED);
    tft.drawPixel (102,108,RA8875_RED);
    tft.drawPixel (104,108,RA8875_RED);
    tft.drawPixel (106,108,RA8875_RED);
    tft.drawPixel (108,108,RA8875_RED);
    tft.drawCircle (100, 200, 1, RA8875_RED);
    tft.drawCircle (100, 200, 5, RA8875_RED);
    tft.fillCircle (110, 200, 3, RA8875_RED);
    tft.drawPixel (100,207,RA8875_RED);
    tft.drawPixel (100,208,RA8875_RED);
    tft.drawPixel (102,207,RA8875_RED);
    tft.drawPixel (104,207,RA8875_RED);
    tft.drawPixel (106,207,RA8875_RED);
    tft.drawPixel (108,207,RA8875_RED);
    tft.drawPixel (110,207,RA8875_RED);
    tft.drawPixel (110,208,RA8875_RED);
    tft.drawPixel (112,207,RA8875_RED);
    tft.drawPixel (114,207,RA8875_RED);
    tft.drawPixel (114,200,RA8875_RED);
    while(1)
        wdDelay(100);
#endif // _GFX_COORD_TEST

    // enable touch screen system
    tft.touchEnable(true);

    // get info from user at full brighness, then commence with user's desired brightness
    clockSetup();
    setupBrightness();

    // do not display time until all set up
    hideClocks();

    // draw initial callsign
    tft.fillScreen(RA8875_BLACK);
    cs_info.call = getCallsign();
    cs_info.box.x = (tft.width()-512)/2;
    cs_info.box.y = 10;                 // coordinate with tftMsg()
    cs_info.box.w = 512;
    cs_info.box.h = 50;
    if (!NVReadUInt16 (NV_CALL_FG_COLOR, &cs_info.fg_color))
	cs_info.fg_color = RA8875_BLACK;
    if (!NVReadUInt16 (NV_CALL_BG_COLOR, &cs_info.bg_color))
	cs_info.bg_color = RA8875_WHITE;
    if (!NVReadUInt8 (NV_CALL_BG_RAINBOW, &cs_info.bg_rainbow))
	cs_info.bg_rainbow = 1;
    drawCallsign (true);

    // position the map box in lower right -- border is drawn outside
    map_b.w = EARTH_W*EARTH_XW;
    map_b.h = EARTH_H*EARTH_XH;
    map_b.x = tft.width() - map_b.w - 1;        // 1 in from edge for border
    map_b.y = tft.height() - map_b.h - 1;       // 1 in from edge for border

    // redefine callsign for main screen
    cs_info.box.x = 0;
    cs_info.box.y = 0;
    cs_info.box.w = 230;
    cs_info.box.h = 51;

    // version box
    version_b.w = 46;
    version_b.x = cs_info.box.x+cs_info.box.w-version_b.w;
    version_b.y = cs_info.box.y+cs_info.box.h+4;
    version_b.h = 8;

    // start WiFi and set de_ll.lat_d/de_ll.lng_d from geolocation if desired -- uses tftMsg()
    initWiFi(true);

    // ask to update if new version available -- never returns if update succeeds
    char nv[32];
    if (isNewVersionAvailable(nv, sizeof(nv)) && askOTAupdate (nv))
        doOTAupdate();

    // get lat/long from gpsd if desired -- setup insures that this and geolocate won't both be active
    getGPSDDELatLong();

    // retrieve plot choices, insure plot2 is not DX if no cluster set, plot3 is not GIMBAL if none
    if (!NVReadUInt8 (NV_PLOT_1, &plot1_ch)) {
        plot1_ch = PLOT1_SSN;
        NVWriteUInt8 (NV_PLOT_1, plot1_ch);
    }
    if (!NVReadUInt8 (NV_PLOT_2, &plot2_ch) || (plot2_ch == PLOT2_DX && !useDXCluster())) {
        plot2_ch = PLOT2_XRAY;
        NVWriteUInt8 (NV_PLOT_2, plot2_ch);
    }
    if (!NVReadUInt8 (NV_PLOT_3, &plot3_ch) || (plot3_ch == PLOT3_GIMBAL && !haveGimbal())) {
        plot3_ch = PLOT3_SDO_1;
        NVWriteUInt8 (NV_PLOT_3, plot3_ch);
    }

    // init rest of de info
    de_info_b.x = 1;
    de_info_b.y = 185;
    de_info_b.w = map_b.x - 2;          // just inside border
    de_info_b.h = 110;
    uint16_t devspace = de_info_b.h/DE_INFO_ROWS;
    desrss_b.x = de_info_b.x + de_info_b.w/2;
    desrss_b.y = de_info_b.y + 2*devspace;
    desrss_b.w = de_info_b.w/2;
    desrss_b.h = devspace;
    de_maid_b.x = de_info_b.x;
    de_maid_b.y = de_info_b.y+(DE_INFO_ROWS-1)*de_info_b.h/DE_INFO_ROWS;
    de_maid_b.w = de_info_b.w/2;
    de_maid_b.h = de_info_b.h/DE_INFO_ROWS;
    if (!NVReadUInt8(NV_DE_SRSS, &desrss))
	desrss = false;
    if (!NVReadUInt8(NV_DE_TIMEFMT, &de_time_fmt))
	de_time_fmt = DETIME_INFO;
    normalizeLL (de_ll);
    if (!NVReadInt32(NV_DE_TZ, &de_tz.tz_secs))
	de_tz.tz_secs = getTZ (de_ll);
    sdelat = sinf(de_ll.lat);
    cdelat = cosf(de_ll.lat);
    antipode (deap_ll, de_ll);
    ll2s (de_ll, de_c.s, DE_R);
    ll2s (deap_ll, deap_c.s, DEAP_R);
    setSatObserver (de_ll.lat_d, de_ll.lng_d);
    de_title_b.x = de_info_b.x;
    de_title_b.y = de_tz.box.y-5;
    de_title_b.w = 30;
    de_title_b.h = 30;

    // init dx info
    if (!NVReadUInt8 (NV_DIST_KM, &show_km)) {
        if (!NVReadUInt8 (NV_METRIC_ON, &show_km))
            show_km = false;
        NVWriteUInt8 (NV_DIST_KM, show_km);
    }
    if (!NVReadUInt8 (NV_LP, &show_lp))
	show_lp = false;
    dx_info_b.x = 1;
    dx_info_b.y = 295;
    dx_info_b.w = de_info_b.w;
    dx_info_b.h = 181;
    uint16_t dxvspace = dx_info_b.h/DX_INFO_ROWS;
    dxsrss_b.x = dx_info_b.x + dx_info_b.w/2;
    dxsrss_b.y = dx_info_b.y + 3*dxvspace;
    dxsrss_b.w = dx_info_b.w/2;
    dxsrss_b.h = dxvspace;
    dx_maid_b.x = dx_info_b.x;
    dx_maid_b.y = dx_info_b.y+(DX_INFO_ROWS-2)*dx_info_b.h/DX_INFO_ROWS;
    dx_maid_b.w = dx_info_b.w/2;
    dx_maid_b.h = dx_info_b.h/DX_INFO_ROWS;
    if (!NVReadUInt8(NV_DX_SRSS, &dxsrss))
	dxsrss = DXSRSS_INAGO;
    if (!NVReadFloat(NV_DX_LAT,&dx_ll.lat_d) || !NVReadFloat(NV_DX_LNG,&dx_ll.lng_d)) {
        // if never set, default to 0/0
	dx_ll.lat_d = 0;
	dx_ll.lng_d = 0;
	NVWriteFloat (NV_DX_LAT, dx_ll.lat_d);
	NVWriteFloat (NV_DX_LNG, dx_ll.lng_d);
        setMaidenhead(NV_DX_GRID, dx_ll);
	dx_tz.tz_secs = getTZ (dx_ll);
        NVWriteInt32(NV_DX_TZ, dx_tz.tz_secs);
    }
    dx_ll.lat = deg2rad(dx_ll.lat_d);
    dx_ll.lng = deg2rad(dx_ll.lng_d);
    ll2s (dx_ll, dx_c.s, DX_R);
    if (!NVReadInt32(NV_DX_TZ, &dx_tz.tz_secs))
	dx_tz.tz_secs = getTZ (dx_ll);

    // init portion of dx info used for satellite name
    satname_b.x = dx_info_b.x;
    satname_b.y = dx_info_b.y+1U;
    satname_b.w = dx_info_b.w;
    satname_b.h = dx_info_b.h/6;	// match FONT_H in earthsat.cpp

    // set up RSS button and banner boxes, box is over map pixels, not over the map_b border
    rss_btn_b.x = map_b.x;
    rss_btn_b.y = map_b.y + map_b.h - GB_H;
    rss_btn_b.w = GB_W;
    rss_btn_b.h = GB_H;
    rss_bnr_b.x = map_b.x;
    rss_bnr_b.y = map_b.y + map_b.h - 2*GB_H;
    rss_bnr_b.w = map_b.w;
    rss_bnr_b.h = 2*GB_H;
    NVReadUInt8 (NV_RSS_ON, &rss_on);

    // set up az-merc button
    azm_btn_b.x = map_b.x;
    azm_btn_b.y = map_b.y;
    azm_btn_b.w = GB_W;
    azm_btn_b.h = GB_H;
    NVReadUInt8 (NV_AZIMUTHAL_ON, &azm_on);

    // set up grid button
    llg_btn_b.x = azm_btn_b.x;
    llg_btn_b.y = azm_btn_b.y + azm_btn_b.h;
    llg_btn_b.w = GB_W;
    llg_btn_b.h = GB_H;
    NVReadUInt8 (NV_LLGRID, &llg_on);

    // start the internl clock running but don't display yet
    initTime();

    // init sensors
    initBME280();

    // check for saved satellite
    dx_info_for_sat = initSatSelection();

    // perform inital screen layout
    initScreen();

    // now start checking repetative wd
    max_wd_dt = 0;
}

// called repeatedly forever
void loop()
{
    // update stopwatch exclusively, if active
    if (runStopwatch())
        return;

    // check on wifi
    updateWiFi();

    // update clocks
    updateClocks(false);

    // update sat pass (this is just the pass; the path is recomputed before each map sweep)
    updateSatPass();

    // update NCFDX beacons, don't erase if holding path
    updateBeacons(!waiting4DXPath(), false, false);

    // display more of earth map unless we are leaving a path up temporarily
    if (!waiting4DXPath())
	drawMoreEarth();

    // collect and plot new sensor data occasionally
    updateBME280();

    // other goodies
    drawUptime(false);
    drawWiFiInfo();
    drawVersion(false);
    followBrightness();

    // check for touch events
    checkTouch();
}


/* assuming basic hw init is complete setup everything for the screen.
 * called once at startup and after each time returning from other full-screen options.
 * The overall layout is establihed by setting the various SBox values.
 * Some are initialized statically in setup() some are then set relative to these.
 */
void initScreen()
{
    resetWatchdog();

    // erase entire screen
    eraseScreen();

    // set protected region
    tft.setPR (map_b.x, map_b.y, map_b.w, map_b.h);

    // us
    drawVersion(true);
    drawCallsign(true);

    // DE info
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    tft.setTextColor(DE_COLOR);
    tft.setCursor(de_info_b.x, de_tz.box.y+18);
    tft.print(F("DE:"));
    de_c.s.x = de_info_b.x+62;
    de_c.s.y = de_tz.box.y+8;
    drawDEMarker(true);
    drawDEInfo();

    // DX info
    drawOneTimeDX();
    drawDXInfo();

    resetWatchdog();

    // draw section borders
    tft.drawLine (0, map_b.y-1, tft.width()-1, map_b.y-1, GRAY);                        // top
    tft.drawLine (0, tft.height()-1, tft.width()-1, tft.height()-1, GRAY);              // bottom
    tft.drawLine (0, map_b.y-1, 0, tft.height()-1, GRAY);                               // left
    tft.drawLine (tft.width()-1, map_b.y-1, tft.width()-1, tft.height()-1, GRAY);       // right
    tft.drawLine (map_b.x-1, map_b.y-1, map_b.x-1, tft.height()-1, GRAY);               // left of map
    tft.drawLine (0, dx_info_b.y, map_b.x-1, dx_info_b.y, GRAY);                        // de/dx divider

    // set up empty plot boxes but start DX and gimbal now if selected
    plotXY (plot1_b, NULL, NULL, 0, NULL, NULL, RA8875_WHITE, 0);
    if (plot2_ch == PLOT2_DX)
        initDXCluster();
    else
        plotXY (plot2_b, NULL, NULL, 0, NULL, NULL, RA8875_WHITE, 0);
    if (plot3_ch == PLOT3_GIMBAL && haveGimbal())
        initGimbalGUI();
    else
        plotXY (plot3_b, NULL, NULL, 0, NULL, NULL, RA8875_WHITE, 0);
    resetWatchdog();

    // set up beacon box
    resetWatchdog();
    drawBeaconBox();

    // draw all the clocks
    showClocks();
    updateClocks(true);
    drawMainPageStopwatch(true);

    // start 
    initEarthMap();
    initWiFiRetry();
    initBME280Retry();
    displaySatInfo();
    drawUptime(true);
    drawScreenLock();
    drawBrightness();

    // flush any stale touchs
    drainTouch();
}

/* draw the one-time portion of dx_info either because we just booted or because
 * we are transitioning back from being in sat mode
 */
static void drawOneTimeDX()
{
    if (dx_info_for_sat)
	return;

    tft.fillRect (dx_info_b.x, dx_info_b.y+1, dx_info_b.w, dx_info_b.h-1, RA8875_BLACK);

    selectFontStyle (BOLD_FONT, SMALL_FONT);
    tft.setTextColor(DX_COLOR);
    tft.setCursor(dx_info_b.x, dx_tz.box.y+18);
    tft.print(F("DX:"));

    // save/restore dx_c so it can be used to erase
    SCircle dx_c_save = dx_c;
    dx_c.s.x = dx_info_b.x+62;
    dx_c.s.y = dx_tz.box.y+8;
    drawDXMarker(true);
    dx_c = dx_c_save;
}

/* monitor for touch events
 */
static void checkTouch()
{
    resetWatchdog();

    TouchType tt;
    SCoord s;

    // check for remote and local touch, else get out fast
    if (wifi_tt != TT_NONE) {
	s = wifi_tt_s;
	tt = wifi_tt;
	wifi_tt = TT_NONE;
        // remote touches never turn on brightness
    } else {
	tt = readCalTouch (s);
	if (tt == TT_NONE)
	    return;
        // don't do anything else if this tap just turned on brightness
        if (brightnessOn()) {
            drainTouch();
            return;
        }
    }
    Serial.printf("Touch:\t%4d %4d\ttype %d\n", s.x, s.y, (int)tt);


    // check for lock screen first
    if (inBox (s, lkscrn_b)) {
        if (tt == TT_HOLD) {
            if (!screenIsLocked()) {
#if defined(_USE_FB0)
                shutdown();
#else
                reboot();
#endif // _USE_FB0
            }
        } else {
            toggleLockScreen();
            drawScreenLock();
            return;
        }
    }
    if (screenIsLocked())
	return;

    drainTouch();

    // check all touch locations, ones that can be over map checked first
    LatLong ll;
    if (checkRSSTouch(s)) {
	updateRSSNow();
    } else if (inBox (s, azm_btn_b)) {
	setAzmMerc(!azm_on);		// toggle
    } else if (inBox (s, llg_btn_b)) {
	changeLLGrid ();
    } else if (checkSatTouch (s)) {
        dx_info_for_sat = true;
        displaySatInfo();
        eraseDXPath();                  // declutter to emphasize sat track
        drawAllSymbols(false);
    } else if (inCircle (s, moon_c)) {
        setSatFromName ("Moon");
    } else if (s2ll (s, ll)) {
	// over map but still avoid RSS, including symbol radius
	if (tt == TT_HOLD) {
	    // map hold, update DE
	    s.x -= DE_R+1;
	    s.y += DE_R+1;
	    if (!overRSS(s)) {
                // remove spurious fractions
                roundLL(ll);
		newDE (ll);
            }
	} else {
	    // just an ordinary map location, update DX
	    s.x -= DX_R+1;
	    s.y += DX_R+1;
	    if (!overRSS(s)) {
                // remove spurious fractions
                roundLL(ll);
		newDX (ll, NULL);
            }
	}
    } else if (inBox (s, de_title_b)) {
        toggleDETimeFormat();
    } else if (inBox (s, stopwatch_b)) {
        // check this before checkClockTouch
        checkStopwatchTouch(tt);
    } else if (checkClockTouch(s)) {
	updateClocks(false);
    } else if (checkTZTouch (s, de_tz, de_ll)) {
	NVWriteInt32 (NV_DE_TZ, de_tz.tz_secs);
	drawTZ (de_tz);
	drawDEInfo();
    } else if (checkTZTouch (s, dx_tz, dx_ll)) {
        if (!dx_info_for_sat) {
            NVWriteInt32 (NV_DX_TZ, dx_tz.tz_secs);
            drawTZ (dx_tz);
            drawDXInfo();
        }
    } else if (checkCallsignTouchFG(s)) {
	NVWriteUInt16 (NV_CALL_FG_COLOR, cs_info.fg_color);
	drawCallsign (false);	// just foreground
    } else if (checkCallsignTouchBG(s)) {
	NVWriteUInt16 (NV_CALL_BG_COLOR, cs_info.bg_color);
	NVWriteUInt8 (NV_CALL_BG_RAINBOW, cs_info.bg_rainbow);
	drawCallsign (true);	// fg and bg
    } else if (checkDistTouch(s)) {
        if (!dx_info_for_sat) {
            show_km = !show_km;
            NVWriteUInt8 (NV_DIST_KM, show_km);
            drawDXInfo ();
        }
    } else if (checkPathDirTouch(s)) {
        if (!dx_info_for_sat) {
            show_lp = !show_lp;
            NVWriteUInt8 (NV_LP, show_lp);
            drawDXInfo ();
            newBC();
        }
    } else if (checkDELLTouch(s, ll = de_ll)) {
        if (de_time_fmt == DETIME_INFO)
            newDE (ll);
    } else if (checkDXLLTouch(s, ll = dx_ll)) {
        if (!dx_info_for_sat)
            newDX (ll, NULL);
    } else if (checkPlot1Touch(s)) {
	updateWiFi();
    } else if (checkPlot2Touch(s)) {
	updateWiFi();
    } else if (checkPlot3Touch(s)) {
        updateWiFi();
    } else if (inBox (s, brightness_b)) {
	changeBrightness(s);
    } else if (checkBeaconTouch(s)) {
	drawBeaconBox();
	updateBeacons(true, true, true);
    } else if (checkSatNameTouch(s)) {
        closeDXCluster();       // prevent inbound msgs from clogging network
        closeGimbal();          // avoid dangling connection
	hideClocks();
	dx_info_for_sat = querySatSelection(false);
	initScreen();
    } else if (inBox(s, de_maid_b)) {
        toggleMaidenhead (NV_DE_GRID, de_ll);
        drawMaidenhead (NV_DE_GRID, de_maid_b, DE_COLOR);
    } else if (inBox(s, dx_maid_b)) {
        if (!dx_info_for_sat) {
            toggleMaidenhead (NV_DX_GRID, dx_ll);
            drawMaidenhead (NV_DX_GRID, dx_maid_b, DX_COLOR);
        }
    } else if (inBox (s, version_b)) {
        char nv[32];
        if (isNewVersionAvailable(nv, sizeof(nv))) {
            if (askOTAupdate (nv))
                doOTAupdate();
        } else {
            eraseScreen();
	    tft.setTextColor (RA8875_WHITE);
	    tft.setCursor (tft.width()/8, tft.height()/3);
	    selectFontStyle (BOLD_FONT, SMALL_FONT);
	    tft.print ("You're up to date!");
	    wdDelay(3000);
        }
	initScreen();
    } else if (inBox (s, desrss_b)) {
        desrss = !desrss;
        NVWriteUInt8 (NV_DE_SRSS, desrss);
        drawDEInfo();
    } else if (inBox (s, dxsrss_b)) {
        dxsrss = (dxsrss+1)%DXSRSS_N;
        NVWriteUInt8 (NV_DX_SRSS, dxsrss);
        drawDXInfo();
    } else if (checkDXTouch (s)) {
        ;       // nothing more to do
    }

}

/* round to whole degrees.
 * used to remove spurious fractions
 */
void roundLL (LatLong &ll)
{
    ll.lat_d = roundf (ll.lat_d);
    ll.lng_d = roundf (ll.lng_d);
    normalizeLL(ll);
}

/* given the degree members:
 *   clamp lat to [-90,90];
 *   modulo lng to [-180,180).
 * then fill in the radian members.
 */
void normalizeLL (LatLong &ll)
{
    ll.lat_d = fminf(fmaxf(ll.lat_d,-90),90);                   // truncate
    ll.lat = deg2rad(ll.lat_d);

    ll.lng_d = myfmodf(ll.lng_d+(2*360+180),360)-180;           // wrap
    ll.lng = deg2rad(ll.lng_d);
}

/* set new DX location from degs in dx_info.
 * also set override prefix unless NULL
 */
void newDX (LatLong &ll, const char *ovprefix)
{
    // disable the sat info 
    if (dx_info_for_sat) {
	dx_info_for_sat = false;
	drawOneTimeDX();
    }

    // set grid and TZ from full precision then match display precision
    setMaidenhead (NV_DX_GRID, ll);
    normalizeLL (ll);
    dx_tz.tz_secs = getTZ (ll);

    // erase previous DX info
    eraseDXPath ();
    eraseDXMarker ();

    // set new location
    dx_ll = ll;
    ll2s (dx_ll, dx_c.s, DX_R);

    // set DX prefix
    if (ovprefix)
        setDXPrefixOverride (ovprefix);
    else
        unsetDXPrefixOverride();

    // draw in new location and update info
    drawDXPath ();
    drawDXInfo ();
    drawAllSymbols(false);
    drawDXCursorPrefix();

    // show DX weather and update band conditions if showing
    showDXWX();
    newBC();

    // persist
    NVWriteFloat (NV_DX_LAT, dx_ll.lat_d);
    NVWriteFloat (NV_DX_LNG, dx_ll.lng_d);
}

/* set new DE location from degs
 */
void newDE (LatLong &ll)
{
    // set grid and TZ from full precision then match display precision
    setMaidenhead(NV_DE_GRID, ll);
    normalizeLL (ll);
    de_tz.tz_secs = getTZ (ll);

    // sat path will change, stop gimbal and require op to start
    stopGimbalNow();

    // if azm must start over because everything moves to keep new DE centered
    if (azm_on) {
	drawDEMarker(false);
	de_ll = ll;
	NVWriteFloat (NV_DE_LAT, de_ll.lat_d);
	NVWriteFloat (NV_DE_LNG, de_ll.lng_d);

        // inform satellite subsystem and update pass if showing sat
        setSatObserver (de_ll.lat_d, de_ll.lng_d);
        displaySatInfo();

        // show new wx and update band conditions if showing
        showDEWX();
        newBC();

	initEarthMap();
	return;
    }

    // for mercator we try harder to just update the minimum

    // erase current markers
    eraseDXPath();
    eraseDEMarker();
    eraseDEAPMarker();
    eraseDXMarker();

    // set new
    de_ll = ll;
    sdelat = sinf(de_ll.lat);
    cdelat = cosf(de_ll.lat);
    ll2s (de_ll, de_c.s, DE_R);
    antipode (deap_ll, de_ll);
    ll2s (deap_ll, deap_c.s, DEAP_R);

    // draw in new location and update info
    drawDEInfo();
    drawDXInfo();	// heading changes
    drawAllSymbols(false);

    // show DE weather and update band conditions if showing
    showDEWX();
    newBC();

    // inform satellite subsystem and update pass if showing sat
    setSatObserver (de_ll.lat_d, de_ll.lng_d);
    displaySatInfo();

    // persist
    NVWriteFloat (NV_DE_LAT, de_ll.lat_d);
    NVWriteFloat (NV_DE_LNG, de_ll.lng_d);
}
 
/* given a touch location check if Op wants to change callsign fg.
 * if so then update cs_info and return true else false.
 */
static bool checkCallsignTouchFG (SCoord &b)
{
    SBox left_half = cs_info.box;
    left_half.w /=2;

    if (inBox (b, left_half)) {
	// change fg
	do {
	    cs_info.fg_color = getNextColor (cs_info.fg_color);
	} while (!cs_info.bg_rainbow && cs_info.fg_color == cs_info.bg_color);
	return (true);
    }
    return (false);
}


/* given a touch location check if Op wants to change callsign bg.
 * if so then update cs_info and return true else false.
 */
static bool checkCallsignTouchBG (SCoord &b)
{
    SBox right_half = cs_info.box;
    right_half.w /=2;
    right_half.x += right_half.w;

    if (inBox (b, right_half)) {
	// change bg cycling through rainbow when current color is white
	if (cs_info.bg_rainbow) {
	    cs_info.bg_rainbow = false;
	    do {
		cs_info.bg_color = getNextColor (cs_info.bg_color);
	    } while (cs_info.bg_color == cs_info.fg_color);
	} else {
	    // TODO: can never turn on Rainbow if FG is white because BG will never be set to white
	    if (cs_info.bg_color == RA8875_WHITE) {
		cs_info.bg_rainbow = true;
	    } else {
		do {
		    cs_info.bg_color = getNextColor (cs_info.bg_color);
		} while (cs_info.bg_color == cs_info.fg_color);
	    }
	}
	return (true);
    }
    return (false);
}

/* return next color in basic set of primary colors.
 */
static uint16_t getNextColor(uint16_t current)
{
    static uint16_t colors[] = {
	RA8875_RED, RA8875_GREEN, RA8875_BLUE, RA8875_CYAN,
	RA8875_MAGENTA, RA8875_YELLOW, RA8875_WHITE, RA8875_BLACK
    };
    #define NCOLORS (sizeof(colors)/sizeof(colors[0]))

    for (uint8_t i = 0; i < NCOLORS; i++)
	if (colors[i] == current)
	    return (colors[(i+1)%NCOLORS]);
    return (colors[0]);		// default if current is unknown
}

/* erase the DX marker by restoring map
 */
static void eraseDXMarker()
{
    eraseSCircle (dx_c);

    // restore sat name in case hit
    for (int16_t dy = -dx_c.r; dy <= dx_c.r; dy++)
        drawSatNameOnRow (dx_c.s.y+dy);
}

/* erase great circle through DE and DX by restoring map at each entry in gpath[] then forget.
 * we also erase the box used to display the prefix
 */
static void eraseDXPath()
{
    // get out fast if nothing to do
    if (!n_gpath)
        return;

    // erase the prefix box
    for (uint16_t dy = 0; dy < prefix_b.h; dy++)
        for (uint16_t dx = 0; dx < prefix_b.w; dx++)
            drawMapCoord (prefix_b.x + dx, prefix_b.y + dy);

    // erase the great path
    for (uint16_t i = 0; i < n_gpath; i++) {
	drawMapCoord (gpath[i]);			// draws x and x+1, y
	drawMapCoord (gpath[i].x, gpath[i].y+1);	//        "       , y+1
    }

    // mark no longer active
    setDXPathInvalid();
}

/* find long- else short-path angular distance and east-of-north bearing from DE to DX, both in radians.
 * both will be in range 0..2pi
 */
void propDEDXPath (bool long_path, float *distp, float *bearp)
{
    // cdist will be cos of short-path anglar separation in radians, so acos is 0..pi
    // *bearp will be bearing from DE to DX east-to-north in radians, -pi..pi
    float cdist;
    solveSphere (dx_ll.lng-de_ll.lng, M_PI_2F-dx_ll.lat, sdelat, cdelat, &cdist, bearp);

    if (long_path) {
        *distp = 2*M_PIF - acosf(cdist);                // long path can be anywhere 0..2pi
        *bearp = myfmodf (*bearp + 3*M_PIF, 2*M_PIF);   // +180 then clamp to 0..2pi
    } else {
        *distp = acosf(cdist);                          // short part always 0..pi
        *bearp = myfmodf (*bearp + 2*M_PIF, 2*M_PIF);   // shift -pi..pi to 0..2pi
    }
}

/* draw great circle through DE and DX.
 * save screen coords in gpath[]
 */
static void drawDXPath ()
{
    // find short-path bearing and distance from DE to DX
    float dist, bear;
    propDEDXPath (false, &dist, &bear);

    // start with max nnumber of points, then reduce
    gpath = (SCoord *) realloc (gpath, MAX_GPATH * sizeof(SCoord));
    if (!gpath) {
	Serial.println (F("Failed to malloc gpath"));
	reboot();
    }

    // walk great circle path from DE through DX, storing each point
    float ca, B;
    SCoord s;
    n_gpath = 0;
    for (float b = 0; b < 2*M_PIF; b += 2*M_PIF/MAX_GPATH) {
	solveSphere (bear, b, sdelat, cdelat, &ca, &B);
	ll2s (asinf(ca), myfmodf(de_ll.lng+B+5*M_PIF,2*M_PIF)-M_PIF, s, 1);
	if (overMap(s) && (n_gpath == 0 || memcmp (&s, &gpath[n_gpath-1], sizeof(SCoord)))) {
	    uint16_t c = b < dist ? DE_COLOR : RA8875_WHITE;
	    gpath[n_gpath++] = s;

            // beware drawing the fat pixel off the edge of earth because eraseDXPath won't erase it
            LatLong ll;
	    tft.drawPixel (s.x, s.y, c);
            if (s2ll(s.x+1, s.y, ll))
                tft.drawPixel (s.x, s.y, c);
            if (s2ll(s.x+1, s.y+1, ll))
                tft.drawPixel (s.x, s.y, c);
            if (s2ll(s.x, s.y+1, ll))
                tft.drawPixel (s.x, s.y, c);
	}
    }

    // reduce to actual number of points used
    Serial.printf ("n_gpath %u -> %u\n", MAX_GPATH, n_gpath);
    gpath = (SCoord *) realloc (gpath, n_gpath * sizeof(SCoord));
    if (!gpath) {
	Serial.println (F("Failed to realloc gpath"));
	reboot();
    }

    // record time
    gpath_time = millis();

    printFreeHeap (F("drawDXPath"));
}

/* return whether we are waiting for a DX path to linger.
 * also erase path if its linger time just expired.
 */
bool waiting4DXPath()
{
    if (n_gpath == 0)
	return (false);

    if (millis() - gpath_time > GPATH_LINGER) {
	eraseDXPath();		// sets no longer valid
        drawAllSymbols(false);
	return (false);
    }

    tft.drawPR();

    return (true);
}

void drawDXMarker (bool force)
{
    // test for over visible map unless force, eg might be under RSS now
    if (!force && !overMap(dx_c.s))
	return;

    tft.fillCircle (dx_c.s.x, dx_c.s.y, DX_R, DX_COLOR);
    tft.drawCircle (dx_c.s.x, dx_c.s.y, DX_R, RA8875_BLACK);
    tft.fillCircle (dx_c.s.x, dx_c.s.y, 2, RA8875_BLACK);
}

/* return the bounding box of the given string in the current font.
 */
void getTextBounds (char str[], uint16_t *wp, uint16_t *hp)
{
    int16_t x, y;
    tft.getTextBounds (str, 0, 0, &x, &y, wp, hp);
}


/* return width in pixels of the given string in the current font
 */
uint16_t getTextWidth (char str[])
{
    uint16_t w, h;
    getTextBounds (str, &w, &h);
    return (w);
}


/* draw callsign using cs_info.
 * draw everything if all, else just fg text.
 */
static void drawCallsign (bool all)
{
    tft.graphicsMode();

    if (all) {
	if (cs_info.bg_rainbow)
	    drawRainbow (cs_info.box);
	else
	    tft.fillRect (cs_info.box.x, cs_info.box.y, cs_info.box.w, cs_info.box.h, cs_info.bg_color);
    }

    // keep shrinking font until fits
    uint16_t cw, ch;
    selectFontStyle (BOLD_FONT, LARGE_FONT);
    getTextBounds (cs_info.call, &cw, &ch);

    if (cw >= cs_info.box.w) {
        selectFontStyle (BOLD_FONT, SMALL_FONT);
        getTextBounds (cs_info.call, &cw, &ch);
        if (cw >= cs_info.box.w) {
            selectFontStyle (LIGHT_FONT, SMALL_FONT);
            getTextBounds (cs_info.call, &cw, &ch);
        }
    }

    tft.setTextColor (cs_info.fg_color);
    int cx = cs_info.box.x + (cs_info.box.w-cw)/2;
    int cy = cs_info.box.y + ch + (cs_info.box.h-ch)/2 - 1;
    tft.setCursor (cx, cy);
    tft.print(cs_info.call);
}

/* draw full spectrum in the given box.
 */
static void drawRainbow (SBox &box)
{
    uint8_t h, s = 255, v = 255;
    uint8_t r, g, b;
    uint8_t x0 = random(box.w);

    tft.graphicsMode();
    for (uint16_t x = box.x; x < box.x+box.w; x++) {
	h = 255*((x+x0-box.x)%box.w)/box.w;
	hsvtorgb (&r, &g, &b, h, s, v);
	uint16_t c = RGB565(r,g,b);
	tft.fillRect (x, box.y, 1, box.h, c);
    }
}

/* draw version just below cs_info.
 * N.B. get out fast if nothing new, eg, we assume a new version never disappears.
 */
static void drawVersion(bool force)
{
    // how often to check for new version
    #define VER_CHECK_DT    (6*3600*1000UL)         // millis

    // check for new version occasionally unless we already know
    static uint32_t prev_check_t;
    static bool new_available;
    uint32_t check_t = millis();
    bool check_now = prev_check_t == 0 || (!new_available && check_t - prev_check_t > VER_CHECK_DT);
    if (check_now) {
        new_available = isNewVersionAvailable (NULL, 0);
        prev_check_t = check_t;
    }

    // draw if asked to or we just checked and found a new version
    if (force || (check_now && new_available)) {
        // show current version, but highlight if new version is available
        char ver[32];
        uint16_t col = new_available ? RA8875_RED : GRAY;
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        snprintf (ver, sizeof(ver), "Ver %s", VERSION);
        uint16_t vw = getTextWidth (ver);
        tft.setTextColor (col);
        tft.setCursor (version_b.x+version_b.w-vw, version_b.y);        // right justify
        tft.print (ver);
    }
}

/* draw wifi signal strength or IP occasionally below cs_info
 */
static void drawWiFiInfo()
{
    resetWatchdog();

    static bool prev_dbm;

    // just once every few seconds, wary about overhead calling RSSI()
    static uint32_t prev_ms;
    if (!timesUp(&prev_ms, 5000))
        return;

    // prep
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (GRAY);
    int16_t x = cs_info.box.x + 68;		// a little past Uptime
    int16_t y = cs_info.box.y+cs_info.box.h+3;
    uint16_t w = cs_info.box.w - version_b.w - 68 - 2;
    tft.fillRect (x, y, w, 11, RA8875_BLACK);
    tft.setCursor (x, y+1);

    // get net info and thence what is ok
    IPAddress ip = WiFi.localIP();
    int16_t rssi = WiFi.RSSI();
    bool net_ok = ip[0] != '\0';
    bool rssi_ok = rssi < 10;

    // show info else error
    char str[20];
    if (net_ok) {
	if (prev_dbm || !rssi_ok)
	    sprintf (str, "IP %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
	else 
	    sprintf (str, "  WiFi %4d dBm", rssi);
    } else {
	strcpy (str, "    No Network");
    }
    tft.print(str);

    // toggle
    prev_dbm = !prev_dbm;

}

static void prepUptime()
{
    resetWatchdog();

    const uint16_t x = cs_info.box.x;
    const uint16_t y = cs_info.box.y+cs_info.box.h+3;
    tft.fillRect (x+11, cs_info.box.y+cs_info.box.h+1, 50, 11, RA8875_BLACK);
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (GRAY);
    tft.setCursor (x, y+1);
    tft.print (F("Up "));
}

/* return uptime in seconds, 0 if not ready yet.
 * already break into components if not NULL
 */
time_t getUptime (uint16_t *days, uint8_t *hrs, uint8_t *mins, uint8_t *secs)
{
    // "up" is elapsed time since first good value
    static time_t start_s;

    // get time now from NTP
    time_t now_s = now();
    if (now_s < 1490000000L)            // March 2017
        return (0);                     // not ready yet

    // get secs since starts_s unless first call or time ran backwards?!
    if (start_s == 0 || now_s < start_s)
        start_s = now_s;
    time_t up0 = now_s - start_s;

    // break out if interested
    if (days && hrs && mins && secs) {
        time_t up = up0;
        *days = up/SPD;
        up -= *days*SPD;
        *hrs = up/3600;
        up -= *hrs*3600;
        *mins = up/60;
        up -= *mins*60;
        *secs = up;
    }

    // return up secs
    return (up0);

}

/* draw time since boot just below cs_info.
 * keep drawing to a minimum and get out fast if no change unless force.
 */
static void drawUptime(bool force)
{
    // only do the real work once per second
    static uint32_t prev_ms;
    if (!timesUp(&prev_ms, 1000))
        return;

    // only redraw if significant chars change
    static uint8_t prev_m = 99, prev_h = 99;

    // get uptime, bail if not ready yet.
    uint16_t days; uint8_t hrs, mins, secs;
    time_t upsecs = getUptime (&days, &hrs, &mins, &secs);
    if (!upsecs)
        return;

    resetWatchdog();

    // draw two most significant units if change
    if (upsecs < 60) {
	prepUptime();
	tft.print(upsecs); tft.print(F("s "));
    } else if (upsecs < 3600) {
	prepUptime();
	tft.print(mins); tft.print(F("m "));
	tft.print(secs); tft.print(F("s "));
    } else if (upsecs < SPD) {
	if (mins != prev_m || force) {
	    prepUptime();
	    tft.print(hrs); tft.print(F("h "));
            tft.print(mins); tft.print(F("m "));
	    prev_m = mins;
	}
    } else {
	if (hrs != prev_h || force) {
	    prepUptime();
	    tft.print(days); tft.print(F("d "));
	    tft.print(hrs); tft.print(F("h "));
	    prev_h = hrs;
	}
    }
}


/* draw the RSS button according to its current on/off state as per rss_on
 */
void drawRSSButton()
{
    // background and colors depend on state
    if (rss_on) {
        tft.fillRect (rss_btn_b.x, rss_btn_b.y, rss_btn_b.w, rss_btn_b.h, RA8875_WHITE);
        tft.setTextColor (RA8875_BLACK);
    } else {
        tft.fillRect (rss_btn_b.x, rss_btn_b.y, rss_btn_b.w, rss_btn_b.h, RA8875_BLACK);
	tft.drawRect (rss_btn_b.x, rss_btn_b.y, rss_btn_b.w, rss_btn_b.h, RA8875_WHITE);
        tft.setTextColor (RA8875_WHITE);
    }

    // text is always the same
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    uint16_t bx = rss_btn_b.x + (rss_btn_b.w-5)/2;
    tft.setCursor (bx, rss_btn_b.y+3);
    tft.print ('R');
    tft.setCursor (bx, rss_btn_b.y+13);
    tft.print ('S');
    tft.setCursor (bx, rss_btn_b.y+23);
    tft.print ('S');
}

/* return whether the given screen location is over the RSS button toggle.
 * if so toggle the button and erase banner if now off.
 */
static bool checkRSSTouch(SCoord &s)
{
    // done if not within button
    if (!inBox (s, rss_btn_b))
        return (false);

    // toggle button state and persist
    rss_on = !rss_on;
    NVWriteUInt8 (NV_RSS_ON, rss_on);

    // erase banner if now off
    if (!rss_on) {

	// erase entire banner if azm mode
	if (azm_on)
	    tft.fillRect (rss_bnr_b.x, rss_bnr_b.y, rss_bnr_b.w, rss_bnr_b.h, RA8875_BLACK);

	// restore map and sat path
	for (uint16_t y = rss_bnr_b.y; y < rss_bnr_b.y+rss_bnr_b.h; y++) {
            updateClocks(false);
	    for (uint16_t x = rss_bnr_b.x; x < rss_bnr_b.x+rss_bnr_b.w; x += 1)
                drawMapCoord (x, y);	// knows to avoid RSS button
	    drawSatPointsOnRow (y);
	}

	// draw in case these were in the banner box
	drawAllSymbols(false);
    }

    // update button appearance
    drawRSSButton();

    // yes, RSS button was touched
    return (true);
}

/* return whether coordinate s is over a usable map location
 */
bool overMap (const SCoord &s)
{
    return (inBox(s, map_b) && !overRSS(s) && !inBox(s,azm_btn_b) && !inBox(s,llg_btn_b));
}

/* return whether coordinate s is over any symbol
 */
bool overAnySymbol (const SCoord &s)
{
    return (inCircle(s, de_c) || inCircle(s, dx_c) || inCircle(s, deap_c)
                || inCircle (s, sun_c) || inCircle (s, moon_c) || overAnyBeacon(s) || inBox(s,santa_b));
}

/* draw all symbols, order establishes layering priority
 * N.B. called by updateBeacons(): beware recursion
 */
void drawAllSymbols(bool erase_too)
{
    updateClocks(false);
    resetWatchdog();

    if (!overRSS(sun_c.s))
        drawSun();
    if (!overRSS(moon_c.s))
        drawMoon();
    updateBeacons(erase_too, true, false);
    drawDEMarker(false);
    drawDXMarker(false);
    if (!overRSS(deap_c.s))
        drawDEAPMarker();
    drawSanta ();

    updateClocks(false);
}

/* return whether coordinate s is over an active RSS region.
 */
bool overRSS (const SCoord &s)
{
	return (inBox (s, rss_btn_b) || (rss_on && inBox (s, rss_bnr_b)));
}

/* return whether box b is over an active RSS region
 */
bool overRSS (const SBox &b)
{
    if (!rss_on)
        return (false);
    if (b.x >= rss_bnr_b.x+rss_bnr_b.w)
        return (false);
    if (b.y >= rss_bnr_b.y+rss_bnr_b.h)
        return (false);
    if (b.x + b.w <= rss_bnr_b.x)
        return (false);
    if (b.y + b.h <= rss_bnr_b.y)
        return (false);
    return (true);
}


/* draw the azm-mercator button according to its current on/off state as per azm_on
 */
void drawAzmMercButton()
{
    // background and colors depend on state
    if (azm_on) {
        tft.fillRect (azm_btn_b.x, azm_btn_b.y, azm_btn_b.w, azm_btn_b.h-1, RA8875_WHITE);
        tft.setTextColor (RA8875_BLACK);
    } else {
        tft.fillRect (azm_btn_b.x, azm_btn_b.y, azm_btn_b.w, azm_btn_b.h, RA8875_BLACK);
	tft.drawRect (azm_btn_b.x, azm_btn_b.y, azm_btn_b.w, azm_btn_b.h, RA8875_WHITE);
        tft.setTextColor (RA8875_WHITE);
    }

    // text is always the same
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    uint16_t bx = azm_btn_b.x + (azm_btn_b.w-5)/2;
    tft.setCursor (bx, azm_btn_b.y+3);
    tft.print ('A');
    tft.setCursor (bx, azm_btn_b.y+13);
    tft.print ('Z');
    tft.setCursor (bx, azm_btn_b.y+23);
    tft.print ('M');
}

/* toggle azm - mercator and restart map
 */
void setAzmMerc(bool on)
{
    // set button state and persist
    azm_on = on;
    NVWriteUInt8 (NV_AZIMUTHAL_ON, azm_on);

    // update button appearance
    drawAzmMercButton();

    // restart map
    initEarthMap();
}


/* draw the lat-long grid button according to its current state as per llg_on
 */
void drawLLGridButton()
{
    char txt[4];

    // deside style
    switch (llg_on) {
    case LLG_ALL:
        tft.fillRect (llg_btn_b.x, llg_btn_b.y, llg_btn_b.w, llg_btn_b.h, RA8875_WHITE);
        tft.setTextColor (RA8875_BLACK);
        strcpy (txt, "LLG");
        break;
    case LLG_TROPICS:
        tft.fillRect (llg_btn_b.x, llg_btn_b.y, llg_btn_b.w, llg_btn_b.h, RA8875_WHITE);
        tft.setTextColor (RA8875_BLACK);
        strcpy (txt, "TRO");
        break;
    default:
        // off
        tft.fillRect (llg_btn_b.x, llg_btn_b.y, llg_btn_b.w, llg_btn_b.h, RA8875_BLACK);
        tft.drawRect (llg_btn_b.x, llg_btn_b.y, llg_btn_b.w, llg_btn_b.h, RA8875_WHITE);
        tft.setTextColor (RA8875_WHITE);
        strcpy (txt, "LLG");
        break;
    }

    // draw text vertically
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    uint16_t bx = llg_btn_b.x + (llg_btn_b.w-5)/2;
    tft.setCursor (bx, llg_btn_b.y+3);
    tft.print (txt[0]);
    tft.setCursor (bx, llg_btn_b.y+13);
    tft.print (txt[1]);
    tft.setCursor (bx, llg_btn_b.y+23);
    tft.print (txt[2]);
}

/* change the lat/long grid control and restart map
 */
void changeLLGrid()
{
    // rotate button state and persist
    switch (llg_on) {
    case LLG_ALL:
        llg_on = LLG_TROPICS;
        break;
    case LLG_TROPICS:
        llg_on = LLG_OFF;
        break;
    default:
        llg_on = LLG_ALL;
        break;
    }
    NVWriteUInt8 (NV_LLGRID, llg_on);

    // update button appearance
    drawLLGridButton();

    // restart map
    initEarthMap();
}


/* write another line to the initial screen.
 * advance row first unless line ends with \r.
 */
void tftMsg (const char *fmt, ...)
{
    static uint16_t y = 65;             // coordinate with initial cs_info
    char buf[128];
    va_list ap;

    va_start(ap, fmt);
    int l = vsnprintf (buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (buf[l-1] == '\r') {
	buf[l-1] = '\0';
	tft.fillRect (200, y-23, tft.width()-200, 28, RA8875_BLACK);
    } else
	y += 30;

    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor(RA8875_WHITE);
    tft.setCursor (150, y);
    tft.print(buf);

    Serial.println(buf);
}

/* toggle the NV_LKSCRN_ON value
 */
static void toggleLockScreen()
{
    uint8_t lock_on = !screenIsLocked();
    Serial.printf ("Screen lock is now %s\n", lock_on ? "On" : "Off");
    NVWriteUInt8 (NV_LKSCRN_ON, lock_on);
}

/* draw the lock screen symbol according to NV_LKSCRN_ON.
 */
static void drawScreenLock()
{
    uint8_t lock_on = screenIsLocked();
    uint16_t hh = lkscrn_b.h/2;
    uint16_t hw = lkscrn_b.w/2;

    tft.fillRect (lkscrn_b.x, lkscrn_b.y, lkscrn_b.w, lkscrn_b.h, RA8875_BLACK);
    tft.fillRect (lkscrn_b.x, lkscrn_b.y+hh, lkscrn_b.w, hh, RA8875_WHITE);
    tft.drawLine (lkscrn_b.x+hw, lkscrn_b.y+hh+2, lkscrn_b.x+hw, lkscrn_b.y+hh+hh/2, RA8875_BLACK);
    tft.drawCircle (lkscrn_b.x+hw, lkscrn_b.y+hh, hw, RA8875_WHITE);

    if (!lock_on)
	tft.fillRect (lkscrn_b.x+hw, lkscrn_b.y, hw+1, hh-1, RA8875_BLACK);
}

static void toggleDETimeFormat()
{
    // cycle and persist
    de_time_fmt = (de_time_fmt + 1) % DETIME_N;
    NVWriteUInt8(NV_DE_TIMEFMT, de_time_fmt);

    // draw new state
    drawDEInfo();
}

/* resume using nearestPrefix
 */
static void unsetDXPrefixOverride ()
{
    dx_prefix_use_override = false;
}

/* set an override prefix for getDXPrefix() to use instead of using nearestPrefix()
 */
static void setDXPrefixOverride (const char *ovprefix)
{
    // clear out old
    memset (dx_override_prefix, 0, sizeof(dx_override_prefix));

    // copy into dx_override_prefix; if contains / usually use the shorter side
    const char *slash = strchr (ovprefix, '/');
    if (slash) {
        const char *right = slash+1;
        size_t llen = slash - ovprefix;
        size_t rlen = strlen (right);
        const char *slash2 = strchr (right, '/');
        if (slash2)
            rlen = slash2 - right;              // don't count past 2nd slash

        if (rlen <= 1 || llen <= rlen || !strcmp(right,"MM") || !strcmp(right,"AM")
                        || !strncmp (right, "QRP", 3) || strspn(right,"0123456789") == rlen)
            memcpy (dx_override_prefix, ovprefix, llen > MAX_PREF_LEN ? MAX_PREF_LEN : llen); 
        else
            memcpy (dx_override_prefix, right, rlen > MAX_PREF_LEN ? MAX_PREF_LEN : rlen); 
    } else
        memcpy (dx_override_prefix, ovprefix, MAX_PREF_LEN);


    // find right-most digit
    int rdig_idx = -1;
    for (int i = 0; i < MAX_PREF_LEN; i++)
        if (isdigit(dx_override_prefix[i]))
            rdig_idx = i;

    // truncate after if room
    if (rdig_idx >= 0 && rdig_idx < MAX_PREF_LEN-1)
        dx_override_prefix[rdig_idx+1] = '\0';

    // flag ready
    dx_prefix_use_override = true;
}

/* return the override prefix else nearest one based on ll, if any
 */
bool getDXPrefix (char p[MAX_PREF_LEN+1])
{
    if (dx_prefix_use_override) {
        memcpy (p, dx_override_prefix, MAX_PREF_LEN);
        p[MAX_PREF_LEN] = '\0';
        return (true);
    } else {
        return (nearestPrefix (dx_ll, p));
    }
}

/* display the DX prefix at dx_c
 */
static void drawDXCursorPrefix()
{
    char p[MAX_PREF_LEN+1];
    if (getDXPrefix(p))
        drawMapTag (p, dx_c.s.x, dx_c.s.y, dx_c.r, prefix_b);
}

/* draw a string over the map taking care not to obscure the given region nor go outside the map.
 * set box with final location and size decision.
 */
void drawMapTag (char *tag, uint16_t x, uint16_t y, uint16_t r, SBox &box)
{
    // get text size
    uint16_t cw, ch;
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    getTextBounds (tag, &cw, &ch);

    // define initial position
    box.x = x - cw/2-2;
    box.y = y + r+2;
    box.w = cw + 4;
    box.h = ch + 2;

    // insure entirely over map
    SCoord rss_test;
    rss_test.x = map_b.x + map_b.w/2U;
    rss_test.y = box.y + box.h;
    if (overRSS(rss_test) || box.y + box.h >= map_b.y + map_b.h) {
        // move label on top of symbol if near bottom 
        box.y = y - r - box.h - 2;
    } else if (azm_on) {
        // check bottom corners against hemisphere edge if in lower half, top half should be ok
        uint16_t center_y = map_b.y + map_b.h/2;
        if (y > center_y) {
            uint32_t r2 = (uint32_t)map_b.h/2 * (uint32_t)map_b.w/4;
            uint32_t dy = box.y + box.h - center_y;
            uint16_t max_dx = sqrtf(r2 - dy*dy);
            uint16_t center_x = map_b.x + ((x < map_b.x + map_b.w/2) ? map_b.w/4 : 3*map_b.w/4);
            if (box.x < center_x - max_dx) {
                box.x = x + r + 2;
                box.y = y - box.h/2;
            } else if (box.x + box.w >= center_x + max_dx) {
                box.x = x - r - box.w - 2;
                box.y = y - box.h/2;
            }
        }
    } else {
        // check left and right edges
        if (box.x < map_b.x)
            box.x = map_b.x + 2;
        else if (box.x + box.w >= map_b.x + map_b.w)
            box.x = map_b.x + map_b.w - box.w - 2;
    }

    // draw
    tft.fillRect (box.x, box.y, box.w, box.h, RA8875_BLACK);
    tft.setCursor (box.x+2, box.y + 1);
    tft.setTextColor (RA8875_WHITE);
    tft.print(tag);
}

/* return whether screen is currently locked
 */
bool screenIsLocked()
{
    uint8_t lock_on;
    if (!NVReadUInt8 (NV_LKSCRN_ON, &lock_on))
        lock_on = 0;
    return (lock_on != 0);
}

/* return whether SCoord is within SBox
 */
bool inBox (const SCoord &s, const SBox &b)
{
    return (s.x >= b.x && s.x < b.x+b.w && s.y >= b.y && s.y < b.y+b.h);
}

/* return whether SCoord is within SCircle.
 * N.B. must match Adafruit_RA8875::fillCircle()
 */
bool inCircle (const SCoord &s, const SCircle &c)
{
    // beware unsigned subtraction
    uint16_t dx = (s.x >= c.s.x) ? s.x - c.s.x : c.s.x - s.x;
    uint16_t dy = (s.y >= c.s.y) ? s.y - c.s.y : c.s.y - s.y;
    return (4*dx*dx + 4*dy*dy <= 4*c.r*(c.r+1)+1);
}

/* erase the given SCircle
 */
void eraseSCircle (const SCircle &c)
{
    // scan a circle of radius r+1/2 to include whole pixel.
    // radius (r+1/2)^2 = r^2 + r + 1/4 so we use 2x everywhere to avoid floats
    uint16_t radius2 = 4*c.r*(c.r + 1) + 1;
    for (int16_t dy = -2*c.r; dy <= 2*c.r; dy += 2) {
        for (int16_t dx = -2*c.r; dx <= 2*c.r; dx += 2) {
            int16_t xy2 = dx*dx + dy*dy;
            if (xy2 <= radius2)
                drawMapCoord (c.s.x+dx/2, c.s.y+dy/2);
        }
    }
}

/* erase entire screen
 */
void eraseScreen()
{
    tft.setPR (0, 0, 0, 0);
    tft.fillScreen(RA8875_BLACK);
    tft.drawPR();
}

void resetWatchdog()
{
    // record longest wd feed interval so far in max_wd_dt
    static uint32_t prev_ms;
    uint32_t ms = millis();
    uint32_t dt = ms - prev_ms;
    if ((dt > max_wd_dt || dt > 5000) && prev_ms > 0) {
        // Serial.printf ("max WD %u\n", dt);
        max_wd_dt = dt;
    }
    prev_ms = ms;

    ESP.wdtFeed();
    yield();
}

/* like delay() but breaks into small chunks so we can call resetWatchdog()
 */
void wdDelay(int ms)
{
    #define WD_DELAY_DT   50
    uint32_t t0 = millis();
    int dt;
    while ((dt = millis() - t0) < ms) {
        resetWatchdog();
        if (dt < WD_DELAY_DT)
            delay (dt);
        else
            delay (WD_DELAY_DT);
    }
}

/* handy utility to return whether now is dt later than prev.
 * if so, update *prev and return true, else return false.
 */
bool timesUp (uint32_t *prev, uint32_t dt)
{
    uint32_t ms = millis();
    if (ms - *prev < dt)
        return (false);
    *prev = ms;
    return (true);
}

#if defined(_USE_FB0)

/* option to power down, only on FB0
 */
static void shutdown(void)
{
    eraseScreen();

    selectFontStyle (BOLD_FONT, SMALL_FONT);

    // define location of buttons (type machinations to avoid narrowing warnings).
    const uint16_t x0 = tft.width()/4;
    const uint16_t w = tft.width()/2;
    const uint16_t h = 50;
    uint16_t y = tft.height()/3;
    SBox can_b = {x0, y, w, h};
    SBox rsh_b = {x0, y+=h, w, h};
    SBox rbp_b = {x0, y+=h, w, h};
    SBox sdp_b = {x0, y+=h, w, h};

    drawStringInBox ("Never mind -- resume", can_b, false, RA8875_GREEN);
    drawStringInBox ("Restart HamClock", rsh_b, false, RA8875_YELLOW);
    drawStringInBox ("Reboot Pi", rbp_b, false, RA8875_RED);
    drawStringInBox ("Shutdown Pi", sdp_b, false, RA8875_RED);

    // wait forever for selection
    for (;;) {
        SCoord s;
        TouchType tt = readCalTouch (s);
        if (tt != TT_NONE) {
            if (inBox (s, can_b)) {
                initScreen();
                return;
            }
            if (inBox (s, rsh_b)) {
                reboot();
            }
            if (inBox (s, rbp_b)) {
                drawStringInBox ("Rebooting Pi...", rbp_b, true, RA8875_RED);
                system ("sudo reboot");
                for(;;);
            }
            if (inBox (s, sdp_b)) {
                drawStringInBox ("Shutting down Pi...", sdp_b, true, RA8875_RED);
                system ("sudo halt");
                for(;;);
            }
        }
    }
}

#endif // _USE_FB0

/* reboot
 */
void reboot()
{
    ESP.restart();
    for(;;);
}

/* return the worst offending heap and stack
 */
static int worst_heap = 900000000;
static int worst_stack;
void getWorstMem (int *heap, int *stack)
{
    *heap = worst_heap;
    *stack = worst_stack;
}

/* log current heap and stack usage, record worst offenders
 */
void printFreeHeap (const __FlashStringHelper *label)
{
    // compute sizes
    char stack_here;
    int free_heap = ESP.getFreeHeap();
    int stack_used = stack_start - &stack_here;

    // log..
    // getFreeHeap() is close to binary search of max malloc
    Serial.printf ("Up %lu s: ", millis()/1000U); // N.B. do not use getUptime here, it loops NTP
    Serial.print (label);
    Serial.printf ("(), free heap %d, stack size %d\n", free_heap, stack_used);

    // record worst
    if (free_heap < worst_heap)
        worst_heap = free_heap;
    if (stack_used > worst_stack)
        worst_stack = stack_used;
}
