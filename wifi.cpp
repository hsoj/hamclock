/* use wifi for NTP and scraping web sites.
 * call updateClocks() during lengthy operations, particularly after making new connections.
 */

#include "HamClock.h"



// agent
#if defined(__linux__)
static const char agent[] = "HamClock-linux";
#elif defined (__APPLE__)
static const char agent[] = "MacHamClock-apple";
#else
static const char agent[] = "ESPHamClock";
#endif

// RSS info
#define	RSS_DEFINT	15000			// default update interval, millis()
static uint16_t rss_interval = RSS_DEFINT;	// working interval
static const char rss_page[] = "/ham/HamClock/RSS/web15rss.pl";
#define NRSS            15                      // max number RSS entries to cache

// kp historical and predicted info, new data posted every 3 hours
#define	KP_INTERVAL	3500000UL		// polling period, millis()
#define	KP_COLOR	RA8875_YELLOW		// loading message text color
static const char kp_page[] = "/ham/HamClock/geomag/kindex.txt";

// xray info, new data posted every 10 minutes
#define	XRAY_INTERVAL	600000UL		// polling interval, millis()
#define	XRAY_LCOLOR	RGB565(255,50,50)	// long wavelength plot color, reddish
#define	XRAY_SCOLOR	RGB565(50,50,255)	// short wavelength plot color, blueish
static const char xray_page[] = "/ham/HamClock/xray/xray.txt";

// sunspot info, new data posted daily
#define	SSPOT_INTERVAL	3400000UL		// polling interval, millis()
#define	SSPOT_COLOR	RGB565(100,100,255)	// loading message text color
static const char sspot_page[] = "/ham/HamClock/ssn/ssn.txt";

// solar flux info, new data posted three times a day
#define	FLUX_INTERVAL	3300000UL		// polling interval, millis()
#define	FLUX_COLOR	RA8875_GREEN		// loading message text color
static const char sf_page[] = "/ham/HamClock/solar-flux/solarflux.txt";

// band conditions, voacap model changes every hour
#define	BC_INTERVAL	1800000UL		// polling interval, millis()
static const char bc_page[] = "/ham/HamClock/fetchBandConditions.pl";
static bool bc_reverting;                       // set while waiting for BC after WX
static uint16_t bc_power;                       // VOACAP power setting

// geolocation web page
static const char locip_page[] = "/ham/HamClock/fetchIPGeoloc.pl";

// SDO images
#define	SDO_INTERVAL	3200000UL		// polling interval, millis()
#define	SDO_COLOR	RA8875_MAGENTA		// loading message text color
static struct {
    const char *read_msg;
    const char *file_name;
} sdo_images[] = {
#if defined(_CLOCK_1600x960)
    { "Reading SDO composite",   "/ham/HamClock/SDO/f_211_193_171_340.bmp"},
    { "Reading SDO 6173 A",      "/ham/HamClock/SDO/latest_340_HMIIC.bmp"},
    { "Reading SDO magnetogram", "/ham/HamClock/SDO/latest_340_HMIB.bmp"}
#elif defined(_CLOCK_2400x1440)
    { "Reading SDO composite",   "/ham/HamClock/SDO/f_211_193_171_510.bmp"},
    { "Reading SDO 6173 A",      "/ham/HamClock/SDO/latest_510_HMIIC.bmp"},
    { "Reading SDO magnetogram", "/ham/HamClock/SDO/latest_510_HMIB.bmp"}
#elif defined(_CLOCK_3200x1920)
    { "Reading SDO composite",   "/ham/HamClock/SDO/f_211_193_171_680.bmp"},
    { "Reading SDO 6173 A",      "/ham/HamClock/SDO/latest_680_HMIIC.bmp"},
    { "Reading SDO magnetogram", "/ham/HamClock/SDO/latest_680_HMIB.bmp"}
#else
    { "Reading SDO composite",   "/ham/HamClock/SDO/f_211_193_171_170.bmp"},
    { "Reading SDO 6173 A",      "/ham/HamClock/SDO/latest_170_HMIIC.bmp"},
    { "Reading SDO magnetogram", "/ham/HamClock/SDO/latest_170_HMIB.bmp"}
#endif
};

// web site retry interval, millis()
#define	WIFI_RETRY	10000UL

// millis() of last attempts
static uint32_t last_wifi;
static uint32_t last_flux;
static uint32_t last_ssn;
static uint32_t last_xray;
static uint32_t last_kp;
static uint32_t last_rss;
static uint32_t last_sdo;
static uint32_t last_bc;

// local funcs
static bool updateKp(SBox &b);
static bool updateXRay(void);
static bool updateSDO(void);
static bool updateSunSpots(void);
static bool updateSolarFlux(void);
static bool updateBandConditions(SBox &box);
static uint32_t crackBE32 (uint8_t bp[]);


/* it is MUCH faster to print F() strings in a String than using them directly.
 * see esp8266/2.3.0/cores/esp8266/Print.cpp::print(const __FlashStringHelper *ifsh) to see why.
 */
void FWIFIPR (WiFiClient &client, const __FlashStringHelper *str)
{
    String _sp(str);
    client.print(_sp);
}

void FWIFIPRLN (WiFiClient &client, const __FlashStringHelper *str)
{
    String _sp(str);
    client.println(_sp);
}

// handy wifi health check
bool wifiOk()
{
    if (WiFi.status() == WL_CONNECTED)
	return (true);

    // retry occasionally
    uint32_t t0 = millis();
    if (t0 - last_wifi > WIFI_RETRY) {
	last_wifi = t0;
	initWiFi(false);
	return (WiFi.status() == WL_CONNECTED);
    } else
	return (false);
}

/* reset the wifi retry flags so they all refresh again
 */
void initWiFiRetry()
{
    last_wifi = 0;
    last_flux = 0;
    last_ssn = 0;
    last_xray = 0;
    last_kp = 0;
    last_rss = 0;
    last_sdo = 0;
    last_bc = 0;
}

/* called to potentially update band conditions.
 * if BC is on plot1_b just wait for revert if in progress else update immediately.
 * if BC is on plot2_b arrange to update it immediately.
 */
void newBC()
{
    if ((plot1_ch == PLOT1_BC && !bc_reverting) || plot2_ch == PLOT2_BC)
        last_bc = 0;
}

/* set de_ll.lat_d and de_ll.lng_d from our public ip.
 * report status via tftMsg if verbose.
 */
static void geolocateIP (bool verbose)
{
    WiFiClient iploc_client;				// wifi client connection
    float lat, lng;
    char llline[80];
    char ipline[80];
    char credline[80];
    int nlines = 0;

    Serial.println(locip_page);
    resetWatchdog();
    if (wifiOk() && iploc_client.connect(svr_host, HTTPPORT)) {
	httpGET (iploc_client, svr_host, locip_page);
	if (!httpSkipHeader (iploc_client))
            goto out;

        // expect 4 lines: LAT=, LNG=, IP= and CREDIT=, anything else first line is error message
	if (!getTCPLine (iploc_client, llline, sizeof(llline), NULL))
            goto out;
        nlines++;
        lat = myatof (llline+4);
	if (!getTCPLine (iploc_client, llline, sizeof(llline), NULL))
            goto out;
        nlines++;
        lng = myatof (llline+4);
	if (!getTCPLine (iploc_client, ipline, sizeof(ipline), NULL))
            goto out;
        nlines++;
	if (!getTCPLine (iploc_client, credline, sizeof(credline), NULL))
            goto out;
        nlines++;
    }

out:

    if (nlines == 4) {
        // ok

        if (verbose)
            tftMsg ("IP %s geolocation courtesy %s", ipline+3, credline+7);
        de_ll.lat_d = lat;
        de_ll.lng_d = lng;
        normalizeLL (de_ll);
        setMaidenhead(NV_DE_GRID, de_ll);
        NVWriteFloat (NV_DE_LAT, de_ll.lat_d);
        NVWriteFloat (NV_DE_LNG, de_ll.lng_d);

    } else {
        // trouble, error message if 1 line

        if (verbose) {
            if (nlines == 1)
                tftMsg ("IP geolocation err: %s", llline);
            else
                tftMsg ("IP geolocation failed");
        }
    }

    iploc_client.stop();
    resetWatchdog();
    printFreeHeap (F("geolocateIP"));
}

/* init and connect, inform via tftMsg() if verbose.
 * non-verbose is used for automatic retries that should not clobber the display.
 */
void initWiFi (bool verbose)
{
    // N.B. look at the usages and make sure this is "big enough"
    static const char dots[] = ".........................................";

    if (verbose) tftMsg("Starting Network:");
    resetWatchdog();

    // we only want station mode, not access too
    WiFi.mode(WIFI_STA);

    // show connection status
    char *ssid = getWiFiSSID();
    WiFi.begin (ssid, getWiFiPW());		        // non-blocking, poll with status()
    resetWatchdog();
    uint32_t t0 = millis();
    uint32_t timeout = verbose ? 30000UL : 3000UL;	// it is much faster when trying to reconnect
    uint16_t wn = 1;
    if (verbose) tftMsg ("MAC addr: %s", WiFi.macAddress().c_str());
    if (verbose && ssid) tftMsg ("");                   // prep for connection countdown
    do {
	if (verbose && ssid) tftMsg ("Connecting to %s %.*s\r", ssid, wn++, dots);
	resetWatchdog();
	Serial.println (F("Trying network"));
	if (millis() - t0 > timeout || wn == sizeof(dots)) {
	    if (verbose) {
		tftMsg ("Connection failed .. check connections and credentials.");
		tftMsg ("Continuing degraded but will keep trying.");
		wdDelay(5000);
	    }
	    return;
	}
        wdDelay(1000);
    } while (WiFi.status() != WL_CONNECTED);

    // success -- init retry times
    resetWatchdog();
    initWiFiRetry();

    if (verbose) {
	IPAddress ip;
	ip = WiFi.localIP();
	tftMsg ("IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
	ip = WiFi.subnetMask();
	tftMsg ("Mask: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
	ip = WiFi.gatewayIP();
	tftMsg ("GW: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
	ip = WiFi.dnsIP();
	tftMsg ("DNS: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
	tftMsg ("Hostname: %s", WiFi.hostname().c_str());
	if (WiFi.RSSI() < 10) {
	    tftMsg ("Signal strength : %d dBm", WiFi.RSSI());
	    tftMsg ("Channel : %d", WiFi.channel());
	}
	tftMsg ("S/N: %u", ESP.getChipId());
    }

    // set location from IP if desired
    resetWatchdog();
    if (useGeoIP())
        geolocateIP (verbose);

    // start web server for screen captures (no return status to report)
    initWebServer();

    // final report and time to peruse or skip
    if (verbose) {
        static const SBox skip_b = {730,10,55,35};      // skip box, nice if same as sat ok
        selectFontStyle (BOLD_FONT, SMALL_FONT);
        drawStringInBox ("Skip", skip_b, false, RA8875_WHITE);
	tftMsg ("");
	#define	TO_DS 50                                // timeout delay, decaseconds
        uint8_t s_left = TO_DS/10;                      // seconds remaining
        uint32_t t0 = millis();
        drainTouch();
	for (uint8_t ds_left = TO_DS; ds_left > 0; --ds_left) {
            SCoord s;
            if (readCalTouch(s) != TT_NONE && inBox(s, skip_b)) {
                drawStringInBox ("Skip", skip_b, true, RA8875_WHITE);
                break;
            }
            if ((TO_DS - (millis() - t0)/100)/10 < s_left) {
                // just printing every ds_left/10 is too slow due to overhead
                char buf[30];
                sprintf (buf, "Network ready ... %d\r", s_left--);
                tftMsg (buf);
            }
	    wdDelay(100);
	}
    }

    // handy init bc_power
    if (bc_power == 0 && !NVReadUInt16 (NV_BCPOWER, &bc_power))
        bc_power = 100;
}

/* if s is in ll corner of b, rotate bc_power, update NV and return true; else return false.
 * N.B. we assume s is already somewhere within b.
 */
static bool rotateBCPower (const SCoord &s, const SBox &b)
{
    // done if not in ll corner
    if (s.x > b.x + b.w/3 || s.y < b.y + 4*b.h/5)
        return (false);

    // rotate
    switch (bc_power) {
    case 1:   bc_power = 10;   break;
    case 10:  bc_power = 100;  break;
    case 100: bc_power = 1000; break;
    default:  bc_power = 1;    break;
    }

    // persist
    NVWriteUInt16 (NV_BCPOWER, bc_power);

    // good
    return (true);
}

/* arrange to resume plot1 after dt
 */
void revertPlot1 (uint32_t dt)
{
    switch (plot1_ch) {
    case PLOT1_SSN:
	last_ssn = millis() - SSPOT_INTERVAL + dt;
        break;
    case PLOT1_FLUX:
	last_flux = millis() - FLUX_INTERVAL + dt;
        break;
    case PLOT1_BC:
	last_bc = millis() - BC_INTERVAL + dt;
        bc_reverting = true;
        break;
    case PLOT1_N:               // lint
        break;
    }
}

/* check if it is time to update any info via wifi.
 * really should be called updatePlots()
 */
void updateWiFi(void)
{
    resetWatchdog();

    // time now
    uint32_t t0 = millis();

    // proceed even if no wifi to allow subsystems to display their error messages

    // freshen plot1 contents
    switch (plot1_ch) {
    case PLOT1_SSN:
	if (!last_ssn || t0 - last_ssn > SSPOT_INTERVAL) {
	    if (updateSunSpots())
		last_ssn = t0;
	    else
		last_ssn = t0 - SSPOT_INTERVAL + WIFI_RETRY;
	}
        break;

    case PLOT1_FLUX:
	if (!last_flux || t0 - last_flux > FLUX_INTERVAL) {
	    if (updateSolarFlux())
		last_flux = t0;
	    else
		last_flux = t0 - FLUX_INTERVAL + WIFI_RETRY;
	}
        break;

    case PLOT1_BC:
	if (!last_bc || t0 - last_bc > BC_INTERVAL) {
	    if (updateBandConditions(plot1_b))
		last_bc = t0;
	    else
		last_bc = t0 - BC_INTERVAL + WIFI_RETRY;
	}
        break;

    case PLOT1_N:               // lint
        break;
    }

    // freshen plot2 contents
    switch (plot2_ch) {
    case PLOT2_KP:
	if (!last_kp || t0 - last_kp > KP_INTERVAL) {
	    if (updateKp(plot2_b))
		last_kp = t0;
	    else
		last_kp = t0 - KP_INTERVAL + WIFI_RETRY;
	}
        break;

    case PLOT2_XRAY:
	if (!last_xray || t0 - last_xray > XRAY_INTERVAL) {
	    if (updateXRay())
		last_xray = t0;
	    else
		last_xray = t0 - XRAY_INTERVAL + WIFI_RETRY;
	}
        break;

    case PLOT2_BC:
	if (!last_bc || t0 - last_bc > BC_INTERVAL) {
	    if (updateBandConditions(plot2_b))
		last_bc = t0;
	    else
		last_bc = t0 - BC_INTERVAL + WIFI_RETRY;
	}
        break;

    case PLOT2_DX:
        updateDXCluster();
        break;

    case PLOT2_N:               // lint
        break;
    }

    // freshen plot3 contents
    switch (plot3_ch) {
    case PLOT3_SDO_1: // fallthru
    case PLOT3_SDO_2: // fallthru
    case PLOT3_SDO_3:
	if (!last_sdo || t0 - last_sdo > SDO_INTERVAL) {
	    if (updateSDO())
		last_sdo = t0;
	    else
		last_sdo = t0 - SDO_INTERVAL + WIFI_RETRY;
	}
        break;

    case PLOT3_GIMBAL:
        updateGimbal();
        break;

    case PLOT3_KP:
	if (!last_kp || t0 - last_kp > KP_INTERVAL) {
	    if (updateKp(plot3_b))
		last_kp = t0;
	    else
		last_kp = t0 - KP_INTERVAL + WIFI_RETRY;
	}
        break;

    case PLOT3_N:
        break;
    }

    // freshen RSS
    if (!last_rss || t0 - last_rss > rss_interval) {
	if (updateRSS())
	    last_rss = t0;
	else
	    last_rss = t0 - rss_interval + WIFI_RETRY;
    }

    // check for server commands
    checkWebServer();
}

/* check if the given touch coord is inside plot1_b.
 * if so, rotate to next type, reset its new last_X counter and return true, else return false.
 */
bool checkPlot1Touch (const SCoord &s)
{
    if (!inBox (s, plot1_b))
	return (false);

    // rotate
    switch (plot1_ch) {

    case PLOT1_SSN:
        plot1_ch = PLOT1_FLUX;
        last_flux = millis() - FLUX_INTERVAL;	        // force new fetch in next updateWiFi()
        break;

    case PLOT1_FLUX:
        if (plot2_ch == PLOT2_BC) {
            plot1_ch = PLOT1_SSN;                       // dont duplicate BC
            last_ssn = millis() - SSPOT_INTERVAL;	// force new fetch in next updateWiFi()
        } else {
            plot1_ch = PLOT1_BC;
            last_bc = millis() - BC_INTERVAL;	        // force new fetch in next updateWiFi()
        }
        break;

    case PLOT1_BC:
        if (rotateBCPower (s, plot1_b)) {
            // stay with BC, just update with new power setting
            updateBandConditions(plot1_b);
        } else {
            plot1_ch = PLOT1_SSN;
            last_ssn = millis() - SSPOT_INTERVAL;	        // force new fetch in next updateWiFi()
        }
        break;

    case PLOT1_N:               // lint
        break;
    }

    // persist
    NVWriteUInt8 (NV_PLOT_1, plot1_ch);

    // ok
    return (true);
}

/* check if the given touch coord is inside plot2_b.
 * if so, rotate to next type, reset its new last_X counter and return true, else return false.
 */
bool checkPlot2Touch (const SCoord &s)
{
    if (!inBox (s, plot2_b))
        return (false);

    // rotate
    switch (plot2_ch) {

    case PLOT2_KP:
        plot2_ch = PLOT2_XRAY;
        last_xray = millis() - XRAY_INTERVAL;	        // force new fetch in next updateWiFi()
        break;

    case PLOT2_XRAY:
        if (plot1_ch != PLOT1_BC) {
            plot2_ch = PLOT2_BC;
            last_bc = millis() - BC_INTERVAL;	        // force new fetch in next updateWiFi()
        } else if (useDXCluster()) {
            plot2_ch = PLOT2_DX;
            initDXCluster();
        } else if (plot3_ch != PLOT3_KP) {
            plot2_ch = PLOT2_KP;
            last_kp = millis() - KP_INTERVAL;	        // force new fetch in next updateWiFi()
        } // nothing else eligible
        break;

    case PLOT2_BC:
        if (rotateBCPower (s, plot2_b)) {
            // stay with BC, just update with new power setting
            updateBandConditions(plot2_b);
        } else if (useDXCluster()) {
            plot2_ch = PLOT2_DX;
            initDXCluster();
        } else if (plot3_ch != PLOT3_KP) {
            plot2_ch = PLOT2_KP;
            last_kp = millis() - KP_INTERVAL;	        // force new fetch in next updateWiFi()
        } else {
            plot2_ch = PLOT2_XRAY;
            last_xray = millis() - XRAY_INTERVAL;	// force new fetch in next updateWiFi()
        }
        break;

    case PLOT2_DX:
        // first check if user has tapped a dx, else assume they want to roll to next pane
        if (!checkDXTouch(s)) {
            closeDXCluster();
            if (plot3_ch != PLOT3_KP) {
                plot2_ch = PLOT2_KP;
                last_kp = millis() - KP_INTERVAL;	// force new fetch in next updateWiFi()
            } else {
                plot2_ch = PLOT2_XRAY;
                last_xray = millis() - XRAY_INTERVAL;	// force new fetch in next updateWiFi()
            }
        }
        break;

    case PLOT2_N:               // lint
        break;
    }

    // persist
    NVWriteUInt8 (NV_PLOT_2, plot2_ch);

    // ok
    return (true);
}

/* handle plot3 touch, return whether ours.
 * handle all but SDO immediately.
 */
bool checkPlot3Touch (const SCoord &s)
{
    // check whether in box at all
    if (!inBox (s, plot3_b))
	return (false);

    // check gimbal first
    if (checkGimbalTouch(s))
        return (true);

    // handy top half check
    SBox top_b = plot3_b;
    top_b.h /= 2;

    // roll depending on tap and state
    if (bme280_connected && !inBox (s, top_b)) {

	// in bottom half with sensor: roll sensor plot

	switch (plot3_ch) {
	case PLOT3_TEMP:        plot3_ch = PLOT3_PRESSURE; break;
	case PLOT3_PRESSURE:	plot3_ch = PLOT3_HUMIDITY; break;
	case PLOT3_HUMIDITY:	plot3_ch = PLOT3_DEWPOINT; break;
	case PLOT3_DEWPOINT:	plot3_ch = PLOT3_TEMP;     break;
	default:		plot3_ch = PLOT3_TEMP;     break;
	}

        plotBME280();

    } else {

	// in top half or no sensor: roll to next pane type

	switch (plot3_ch) {
	case PLOT3_SDO_1:
            plot3_ch = PLOT3_SDO_2;
            last_sdo = millis() - SDO_INTERVAL;         // force new fetch in next updateWiFi()
            break;

	case PLOT3_SDO_2:
            plot3_ch = PLOT3_SDO_3;
            last_sdo = millis() - SDO_INTERVAL;         // force new fetch in next updateWiFi()
            break;

	case PLOT3_SDO_3:
            if (haveGimbal()) {
                plot3_ch = PLOT3_GIMBAL;
                initGimbalGUI();
            } else if (plot2_ch != PLOT2_KP) {
                plot3_ch = PLOT3_KP;
                last_kp = millis() - KP_INTERVAL;       // force new fetch in next updateWiFi()
            } else {
                plot3_ch = PLOT3_SDO_1;
                last_sdo = millis() - SDO_INTERVAL;     // force new fetch in next updateWiFi()
            }
            break;

	case PLOT3_GIMBAL:
            closeGimbal();
            // fallthru
        default:
            plot3_ch = PLOT3_SDO_1;
            last_sdo = millis() - SDO_INTERVAL;         // force new fetch in next updateWiFi()
            break;
	}
    }

    // persist
    NVWriteUInt8 (NV_PLOT_3, plot3_ch);

    // ours
    return (true);
}

/* NTP time server query.
 * returns UNIX time, or 0 if trouble.
 * for NTP packet description see
 *   http://www.cisco.com
 *	/c/en/us/about/press/internet-protocol-journal/back-issues/table-contents-58/154-ntp.html
 */
time_t getNTPUTC(void)
{
    static const char *ntp_list[] = {
	"time.google.com",
	"pool.ntp.org",
	"time.apple.com",
    };
    #define NNTP (sizeof(ntp_list)/sizeof(ntp_list[0]))
    static uint8_t ntp_i = 0;
    static const uint8_t timeReqA[] = { 0xE3, 0x00, 0x06, 0xEC };
    static const uint8_t timeReqB[] = { 0x31, 0x4E, 0x31, 0x34 };

    // need wifi
    if (!wifiOk())
	return (0);

    // need udp
    WiFiUDP ntp_udp;
    if (!ntp_udp.begin(1234)) {					// any local port
	Serial.println (F("UDP startup failed"));
	return (0);
    }

    // NTP buffer and timers
    uint8_t  buf[48];
    uint32_t tx_ms, rx_ms;

    // Assemble request packet
    memset(buf, 0, sizeof(buf));
    memcpy(buf, timeReqA, sizeof(timeReqA));
    memcpy(&buf[12], timeReqB, sizeof(timeReqB));

    // send
    Serial.print(F("Issuing NTP request to ")); Serial.println(ntp_list[ntp_i]);
    ntp_udp.beginPacket (ntp_list[ntp_i], 123);			// NTP uses port 123
    ntp_udp.write(buf, sizeof(buf));
    tx_ms = millis();						// record when packet sent
    if (!ntp_udp.endPacket()) {
	Serial.println (F("UDP write failed"));
	ntp_i = (ntp_i+1)%NNTP;					// try different server next time
	ntp_udp.stop();
	return (0UL);
    }
    // Serial.print (F("Sent 48 ... "));
    resetWatchdog();

    // receive response
    // Serial.print(F("Awaiting response ... "));
    memset(buf, 0, sizeof(buf));
    uint32_t t0 = millis();
    while (!ntp_udp.parsePacket()) {
	if (millis() - t0 > 3000U) {
	    Serial.println(F("UDP timed out"));
	    ntp_i = (ntp_i+1)%NNTP;					// try different server next time
	    ntp_udp.stop();
	    return (0UL);
	}
	resetWatchdog();
	wdDelay(10);
    }
    rx_ms = millis();						// record when packet arrived
    resetWatchdog();

    // read response
    if (ntp_udp.read (buf, sizeof(buf)) != sizeof(buf)) {
	Serial.println (F("UDP read failed"));
	ntp_i = (ntp_i+1)%NNTP;					// try different server next time
	ntp_udp.stop();
	return (0UL);
    }
    // IPAddress from = ntp_udp.remoteIP();
    // Serial.printf ("received 48 from %d.%d.%d.%d\n", from[0], from[1], from[2], from[3]);

    // only accept server responses which are mode 4
    uint8_t mode = buf[0] & 0x7;
    if (mode != 4) {						// insure server packet
	Serial.print (F("RX mode should be 4 but it is ")); Serial.println (mode);
	ntp_udp.stop();
	return (0UL);
    }

    // crack and advance to next whole second
    time_t unix_s = crackBE32 (&buf[40]) - 2208988800UL;	// packet transmit time - (1970 - 1900)
    if ((uint32_t)unix_s > 0x7FFFFFFFUL) { 			// sanity check beyond unsigned value
	Serial.print (F("crazy large UNIX time: ")); Serial.println ((uint32_t)unix_s);
	ntp_udp.stop();
	return (0UL);
    }
    uint32_t fraction_more = crackBE32 (&buf[44]);		// x / 10^32 additional second
    uint16_t ms_more = 1000UL*(fraction_more>>22)/1024UL;	// 10 MSB to ms
    uint16_t transit_time = (rx_ms - tx_ms)/2;			// transit = half the round-trip time
    if (transit_time < 1) {					// don't trust unless finite
	Serial.println (F("too fast"));
	ntp_udp.stop();
	return (0UL);
    }
    ms_more += transit_time;					// with transit now = unix_s + ms_more
    uint16_t sec_more = ms_more/1000U+1U;			// whole seconds behind rounded up
    wdDelay (sec_more*1000U - ms_more);				// wait to next whole second
    unix_s += sec_more;						// account for delay
    // Serial.print (F("Fraction ")); Serial.print(ms_more);
    // Serial.print (F(", transit ")); Serial.print(transit_time);
    // Serial.print (F(", seconds ")); Serial.print(sec_more);
    // Serial.print (F(", UNIX ")); Serial.print (unix_s); Serial.println();
    resetWatchdog();

    Serial.println (F("NTP ok"));
    ntp_udp.stop();
    printFreeHeap (F("NTP"));
    return (unix_s);
}

/* read next char from client.
 * return whether another character was in fact available.
 */
bool getChar (WiFiClient &client, char *cp)
{
    #define GET_TO 5000	// millis()

    resetWatchdog();

    // wait for char
    uint32_t t0 = millis();
    while (!client.available()) {
	resetWatchdog();
	if (!client.connected())
	    return (false);
	if (millis() - t0 > GET_TO)
	    return (false);
	wdDelay(10);
    }

    // read, which has another way to indicate failure
    int c = client.read();
    if (c < 0) {
	Serial.println (F("bad read"));
	return (false);
    }

    // got one
    *cp = (char)c;
    return (true);
}

/* send User-Agent to client
 */
void sendUserAgent (WiFiClient &client)
{
    // format
    char ua[100];
    snprintf (ua, sizeof(ua), "User-Agent: %s/%s (id %u up %ld)\r\n",
                agent, VERSION, ESP.getChipId(), getUptime(NULL,NULL,NULL,NULL));

    // send
    client.print(ua);
}

/* issue an HTTP Get
 */
void httpGET (WiFiClient &client, const char *server, const char *page)
{
    resetWatchdog();

    FWIFIPR (client, F("GET ")); client.print(page); FWIFIPRLN (client, F(" HTTP/1.0"));
    FWIFIPR (client, F("Host: ")); client.println (server);
    sendUserAgent (client);
    FWIFIPRLN (client, F("Connection: close\r\n"));

    resetWatchdog();
}

/* skip the given wifi client stream ahead to just after the first blank line, return whether ok.
 * this is often used so subsequent stop() on client doesn't slam door in client's face with RST
 */
bool httpSkipHeader (WiFiClient &client)
{
    char line[512];

    do {
	if (!getTCPLine (client, line, sizeof(line), NULL))
	    return (false);
	// Serial.println (line);
    } while (line[0] != '\0');  // getTCPLine absorbs \r\n so this tests for a blank line
    return (true);
}

/* retrieve and plot latest and predicted kp indices, return whether all ok
 */
static bool updateKp(SBox &b)
{
    // data are provided every 3 hours == 8/day. collect 7 days of history + 2 days of predictions
    #define	NHKP		(8*7)			// N historical Kp values
    #define	NPKP		(8*2)			// N predicted Kp values
    #define	NKP		(NHKP+NPKP)		// N total Kp values
    uint8_t kp[NKP];					// kp collection
    uint8_t kp_i = 0;					// next kp index to use
    char line[100];					// text line
    WiFiClient kp_client;				// wifi client connection
    bool ok = false;					// set iff all ok

    plotMessage (b, KP_COLOR, "Reading kpmag data...");

    Serial.println(kp_page);
    resetWatchdog();
    if (wifiOk() && kp_client.connect(svr_host, HTTPPORT)) {
	updateClocks(false);
	resetWatchdog();

	// query web page
	httpGET (kp_client, svr_host, kp_page);

	// skip response header
	if (!httpSkipHeader (kp_client))
	    goto out;

	// read lines into kp array
	// Serial.println(F("reading k indices"));
	for (kp_i = 0; kp_i < NKP && getTCPLine (kp_client, line, sizeof(line), NULL); kp_i++) {
	    // Serial.print(kp_i); Serial.print("\t"); Serial.println(line);
	    kp[kp_i] = myatof(line);
	}

    } else {
	Serial.println (F("connection failed"));
    }

    // require all
    ok = (kp_i == NKP);

out:

    // plot
    if (ok)
	plotKp (b, kp, NHKP, NPKP, KP_COLOR);
    else
	plotMessage (b, KP_COLOR, "No Kp data");

    // clean up
    kp_client.stop();
    resetWatchdog();
    printFreeHeap (F("Kp"));
    return (ok);
}

/* given a GOES XRAY Flux value, return its event level designation in buf.
 */
static char *xrayLevel (float flux, char *buf)
{
    if (flux < 1e-8)
	strcpy (buf, "A0.0");
    else {
	static const char levels[] = "ABCMX";
	int power = floorf(log10f(flux));
	if (power > -4)
	    power = -4;
	float mantissa = flux*powf(10.0F,-power);
	char alevel = levels[8+power];
	sprintf (buf, "%c%.1f", alevel, mantissa);
    }
    return (buf);
}

// retrieve and plot latest xray indices, return whether all ok
static bool updateXRay()
{
    #define NXRAY 150			// n lines to collect = 25 hours @ 10 mins per line
    float lxray[NXRAY], sxray[NXRAY];	// long and short wavelength values
    uint8_t xray_i;			// next index
    char line[100];
    uint16_t ll;
    bool ok = false;
    WiFiClient xray_client;

    plotMessage (plot2_b, XRAY_LCOLOR, "Reading XRay data...");

    Serial.println(xray_page);
    resetWatchdog();
    if (wifiOk() && xray_client.connect(svr_host, HTTPPORT)) {
	updateClocks(false);

	// query web page
	httpGET (xray_client, svr_host, xray_page);

        // soak up remaining header
	(void) httpSkipHeader (xray_client);

	// collect content lines and extract both wavelength intensities
	xray_i = 0;
	float current_flux = 1;
	while (xray_i < NXRAY && getTCPLine (xray_client, line, sizeof(line), &ll)) {
	    // Serial.println(line);
	    if (line[0] == '2' && ll >= 56) {
		float s = myatof(line+35);
		if (s <= 0) 			// missing values are set to -1.00e+05, also guard 0
		    s = 1e-9;
		sxray[xray_i] = log10f(s);
		float l = myatof(line+47);
		if (l <= 0) 			// missing values are set to -1.00e+05, also guard 0
		    l = 1e-9;
		lxray[xray_i] = log10f(l);
		// Serial.print(l); Serial.print('\t'); Serial.println (s);
		xray_i++;
		if (xray_i == NXRAY)
		    current_flux = l;
	    }
	}

	// proceed iff we found all
	if (xray_i == NXRAY) {
	    resetWatchdog();

	    // create x in hours back from 0
	    float x[NXRAY];
	    for (int16_t i = 0; i < NXRAY; i++)
		x[i] = (i-NXRAY)/6.0;		// 6 entries per hour

	    // use two values on right edge to force constant plot scale -2 .. -9
	    x[NXRAY-3] = 0; lxray[NXRAY-3] = lxray[NXRAY-4];
	    x[NXRAY-2] = 0; lxray[NXRAY-2] = -2;
	    x[NXRAY-1] = 0; lxray[NXRAY-1] = -9;

	    // overlay short over long
	    ok = plotXYstr (plot2_b, x, lxray, NXRAY, "Hours", "GOES 16 Xray", XRAY_LCOLOR,
	    			xrayLevel(current_flux, line))
		 && plotXY (plot2_b, x, sxray, NXRAY, NULL, NULL, XRAY_SCOLOR, 0.0);
	} else {
	    Serial.print (F("Only found ")); Serial.print (xray_i); Serial.print(F(" of "));
            Serial.println (NXRAY);
	}
    } else {
	Serial.println (F("connection failed"));
    }

    // clean up xray_client regardless
    if (!ok)
        plotMessage (plot2_b, XRAY_LCOLOR, "No XRay data");
    xray_client.stop();
    resetWatchdog();
    printFreeHeap (F("XRay"));
    return (ok);
}


// retrieve and plot latest sun spot indices, return whether all ok
static bool updateSunSpots()
{
    // collect lines, assume one day
    #define NSUNSPOT 8			// go back 7 days, including 0
    float sspot[NSUNSPOT+1];		// values plus forced 0 at end so plot y axis always starts at 0
    float x[NSUNSPOT+1];		// time axis plus ... "
    char line[100];
    WiFiClient ss_client;
    bool ok = false;

    plotMessage (plot1_b, SSPOT_COLOR, "Reading Sunspot data...");

    Serial.println(sspot_page);
    resetWatchdog();
    if (wifiOk() && ss_client.connect(svr_host, HTTPPORT)) {
	updateClocks(false);

	// query web page
	httpGET (ss_client, svr_host, sspot_page);

	// skip response header
	if (!httpSkipHeader (ss_client))
	    goto out;

	// read lines into sspot array and build corresponding time value
	// Serial.println(F("reading ssn"));
	uint8_t ssn_i;
	for (ssn_i = 0; ssn_i < NSUNSPOT && getTCPLine (ss_client, line, sizeof(line), NULL); ssn_i++) {
	    // Serial.print(ssn_i); Serial.print("\t"); Serial.println(line);
	    sspot[ssn_i] = myatof(line+11);
	    x[ssn_i] = -7 + ssn_i;
	}

	// plot if found all, display current value
	updateClocks(false);
	resetWatchdog();
	if (ssn_i == NSUNSPOT) {
            x[NSUNSPOT] = x[NSUNSPOT-1];        // dup last time
            sspot[NSUNSPOT] = 0;                // set value to 0
	    ok = plotXY (plot1_b, x, sspot, NSUNSPOT+1, "Days", "Sunspot Number",
                                        SSPOT_COLOR, sspot[NSUNSPOT-1]);
        }

    } else {
	Serial.println (F("connection failed"));
    }

    // clean up
out:
    if (!ok)
        plotMessage (plot1_b, SSPOT_COLOR, "No Sunspot data");
    ss_client.stop();
    resetWatchdog();
    printFreeHeap (F("Sunspots"));
    return (ok);
}

/* retrieve and plot latest and predicted solar flux indices, return whether all ok.
 */
static bool updateSolarFlux()
{
    // collect lines, three per day for 10 days
    #define	NSFLUX		30
    float x[NSFLUX], flux[NSFLUX];
    WiFiClient sf_client;
    char line[120];
    bool ok = false;

    plotMessage (plot1_b, FLUX_COLOR, "Reading solar flux ...");

    Serial.println (sf_page);
    resetWatchdog();
    if (wifiOk() && sf_client.connect(svr_host, HTTPPORT)) {
	updateClocks(false);
	resetWatchdog();

	// query web page
	httpGET (sf_client, svr_host, sf_page);

	// skip response header
	if (!httpSkipHeader (sf_client))
	    goto out;

	// read lines into flux array and build corresponding time value
	// Serial.println(F("reading flux"));
	uint8_t flux_i;
	for (flux_i = 0; flux_i < NSFLUX && getTCPLine (sf_client, line, sizeof(line), NULL); flux_i++) {
	    // Serial.print(flux_i); Serial.print("\t"); Serial.println(line);
	    flux[flux_i] = myatof(line);
	    x[flux_i] = -6.667 + flux_i/3.0;	// 7 days history + 3 days predictions
	}

	// plot if found all, display current value
	updateClocks(false);
	resetWatchdog();
	if (flux_i == NSFLUX)
	    ok = plotXY (plot1_b, x, flux, NSFLUX, "Days", "Solar flux", FLUX_COLOR, flux[NSFLUX-10]);

    } else {
	Serial.println (F("connection failed"));
    }

    // clean up
out:
    if (!ok)
        plotMessage (plot1_b, FLUX_COLOR, "No solarflux data");
    sf_client.stop();
    resetWatchdog();
    printFreeHeap (F("SolarFlux"));
    return (ok);
}

/* retrieve and plot latest band conditions, return whether all ok.
 * reset bc_reverting
 */
static bool updateBandConditions(SBox &box)
{
    char response[100];
    char config[100];
    WiFiClient bc_client;
    bool ok = false;

    plotMessage (box, RA8875_YELLOW, "Reading conditions ...");

    // build query
    char query[sizeof(bc_page)+200];
    uint32_t t = nowWO();
    snprintf (query, sizeof(query),
                "%s?YEAR=%d&MONTH=%d&RXLAT=%.0f&RXLNG=%.0f&TXLAT=%.0f&TXLNG=%.0f&UTC=%d&PATH=%d&POW=%d",
                bc_page, year(t), month(t), dx_ll.lat_d, dx_ll.lng_d, de_ll.lat_d, de_ll.lng_d,
                hour(t), show_lp, bc_power);

    Serial.println (query);
    resetWatchdog();
    if (wifiOk() && bc_client.connect(svr_host, HTTPPORT)) {
	updateClocks(false);
	resetWatchdog();

	// query web page
	httpGET (bc_client, svr_host, query);

	// skip response header
	if (!httpSkipHeader (bc_client)) {
            plotMessage (box, RA8875_RED, "No BC header");
	    goto out;
        }

        // next line is CSV short-path reliability, 80-10m
        if (!getTCPLine (bc_client, response, sizeof(response), NULL)) {
            plotMessage (box, RA8875_RED, "No BC response");
            goto out;
        }

        // next line is configuration summary
        if (!getTCPLine (bc_client, config, sizeof(config), NULL)) {
            Serial.println(response);
            plotMessage (box, RA8875_RED, "No BC config");
            goto out;
        }

        // keep time fresh
	updateClocks(false);
	resetWatchdog();

	// plot
        Serial.println (response);
        Serial.println (config);
        ok = plotBandConditions (box, response, config);

    } else {
	plotMessage (box, RA8875_RED, "BC failed");
    }

    // clean up
out:
    bc_reverting = false;
    bc_client.stop();
    resetWatchdog();
    printFreeHeap (F("BandConditions"));
    return (ok);
}

/* read SDO image and display in plot3_b
 */
static bool updateSDO ()
{
    WiFiClient sdo_client;

    // choose file and message if valid
    switch (plot3_ch) {
    case PLOT3_SDO_1: break;
    case PLOT3_SDO_2: break;
    case PLOT3_SDO_3: break;
    default: return(false);
    }
    uint8_t sdoi = plot3_ch - PLOT3_SDO_1;
    const char *sdo_fn = sdo_images[sdoi].file_name;
    const char *sdo_rm = sdo_images[sdoi].read_msg;;

    // inform user
    plotMessage (plot3_b, SDO_COLOR, sdo_rm);

    // assume bad unless proven otherwise
    bool ok = false;

    Serial.println(sdo_fn);
    resetWatchdog();
    if (wifiOk() && sdo_client.connect(svr_host, HTTPPORT)) {
	updateClocks(false);

	// composite types
	union { char c[4]; uint32_t x; } i32;
	union { char c[2]; uint16_t x; } i16;

	// query web page
	httpGET (sdo_client, svr_host, sdo_fn);

	// skip response header
	if (!httpSkipHeader (sdo_client))
	    goto out;

	// keep track of our offset in the image file
	uint32_t byte_os = 0;

	// read first two bytes to confirm correct format
	char c;
	if (!getChar(sdo_client,&c) || c != 'B' || !getChar(sdo_client,&c) || c != 'M') {
	    Serial.println (F("SDO image is not BMP"));
	    goto out;
	}
	byte_os += 2;

	// skip down to byte 10 which is the offset to the pixels offset
	while (byte_os++ < 10) {
	    if (!getChar(sdo_client,&c)) {
		Serial.println (F("SDO header 1 error"));
		goto out;
	    }
	}
	for (uint8_t i = 0; i < 4; i++, byte_os++) {
	    if (!getChar(sdo_client,&i32.c[i])) {
		Serial.println (F("SDO pix_start error"));
		goto out;
	    }
	}
	uint32_t pix_start = i32.x;
	// Serial.printf ("pixels start at %d\n", pix_start);

	// next word is subheader size, must be 40 BITMAPINFOHEADER
	for (uint8_t i = 0; i < 4; i++, byte_os++) {
	    if (!getChar(sdo_client,&i32.c[i])) {
		Serial.println (F("SDO hdr size error"));
		goto out;
	    }
	}
	uint32_t subhdr_size = i32.x;
	if (subhdr_size != 40) {
	    Serial.printf ("SDO DIB must be 40: %d\n", subhdr_size);
	    goto out;
	}

	// next word is width
	for (uint8_t i = 0; i < 4; i++, byte_os++) {
	    if (!getChar(sdo_client,&i32.c[i])) {
		Serial.println (F("SDO width error"));
		goto out;
	    }
	}
	int32_t img_w = i32.x;

	// next word is height
	for (uint8_t i = 0; i < 4; i++, byte_os++) {
	    if (!getChar(sdo_client,&i32.c[i])) {
		Serial.println (F("SDO height error"));
		goto out;
	    }
	}
	int32_t img_h = i32.x;
	Serial.printf ("SDO image is %d x %d = %d\n", img_w, img_h, img_w*img_h);

	// next short is n color planes
	for (uint8_t i = 0; i < 2; i++, byte_os++) {
	    if (!getChar(sdo_client,&i16.c[i])) {
		Serial.println (F("SDO height error"));
		goto out;
	    }
	}
	uint16_t n_planes = i16.x;
	if (n_planes != 1) {
	    Serial.printf ("SDO planes must be 1: %d\n", n_planes);
	    goto out;
	}

	// next short is bits per pixel
	for (uint8_t i = 0; i < 2; i++, byte_os++) {
	    if (!getChar(sdo_client,&i16.c[i])) {
		Serial.println (F("SDO height error"));
		goto out;
	    }
	}
	uint16_t n_bpp = i16.x;
	if (n_bpp != 24) {
	    Serial.printf ("SDO bpp must be 24: %d\n", n_bpp);
	    goto out;
	}

	// next word is compression method
	for (uint8_t i = 0; i < 4; i++, byte_os++) {
	    if (!getChar(sdo_client,&i32.c[i])) {
		Serial.println (F("SDO compression error"));
		goto out;
	    }
	}
	uint32_t comp = i32.x;
	if (comp != 0) {
	    Serial.printf ("SDO compression must be 0: %d\n", comp);
	    goto out;
	}

	// skip down to start of pixels
	while (byte_os++ <= pix_start) {
	    if (!getChar(sdo_client,&c)) {
		Serial.println (F("SDO header 3 error"));
		goto out;
	    }
	}

	// display box depends on actual output size.
	SBox v_b;
#if defined(_USE_DESKTOP)
	v_b.x = plot3_b.x * tft.SCALESZ;
	v_b.y = plot3_b.y * tft.SCALESZ;
	v_b.w = plot3_b.w * tft.SCALESZ;
	v_b.h = plot3_b.h * tft.SCALESZ;
#else
	v_b = plot3_b;
#endif

	// clip and center the image within v_b
	uint16_t xborder = img_w > v_b.w ? (img_w - v_b.w)/2 : 0;
	uint16_t yborder = img_h > v_b.h ? (img_h - v_b.h)/2 : 0;

	// scan all pixels
	for (uint16_t img_y = 0; img_y < img_h; img_y++) {

	    // keep time active
	    resetWatchdog();
	    updateClocks(false);

	    for (uint16_t img_x = 0; img_x < img_w; img_x++) {

		char b, g, r;

		// read next pixel
		if (!getChar (sdo_client, &b) || !getChar (sdo_client, &g) || !getChar (sdo_client, &r)) {
		    Serial.printf ("SDO read error after %d pixels\n", img_y*img_w + img_x);
		    goto out;
		}

		// draw if fits
		if (img_x >= xborder && img_x < xborder + v_b.w 
			    && img_y >= yborder && img_y < yborder + v_b.h) {

		    uint8_t ur = r;
		    uint8_t ug = g;
		    uint8_t ub = b;
		    uint16_t color16 = RGB565(ur,ug,ub);
#if defined(_USE_DESKTOP)
		    tft.drawSubPixel (v_b.x + img_x - xborder,
		    		v_b.y + v_b.h - (img_y - yborder) - 1, color16); // vertical flip
#else
		    tft.drawPixel (v_b.x + img_x - xborder,
		    		v_b.y + v_b.h - (img_y - yborder) - 1, color16); // vertical flip
#endif
		}
	    }

	    // skip padding to bring total row length to multiple of 4
	    uint8_t extra = img_w % 4;
	    if (extra > 0) {
		for (uint8_t i = 0; i < 4 - extra; i++) {
		    if (!getChar(sdo_client,&c)) {
			Serial.println (F("SDO row padding error"));
			goto out;
		    }
		}
	    }
	}

	Serial.println (F("SDO image complete"));
        tft.drawRect (plot3_b.x, plot3_b.y, plot3_b.w, plot3_b.h, GRAY);
	ok = true;

    } else {
	Serial.println (F("connection failed"));
    }

out:
    if (!ok)
	plotMessage (plot3_b, SDO_COLOR, "SDO failed");
    sdo_client.stop();
    printFreeHeap(F("SDO"));
    return (ok);
}

/* get next line from client in line[] then return true, else nothing and return false.
 * line[] will have \r and \n removed and end with \0, optional line length in *ll will not include \0.
 */
bool getTCPLine (WiFiClient &client, char line[], uint16_t line_len, uint16_t *ll)
{
    // keep clocks current
    updateClocks(false);

    // decrement available length so there's always room to add '\0'
    line_len -= 1;

    // read until find \n or time out.
    uint16_t i = 0;
    while (true) {
	char c;
	if (!getChar (client, &c))
	    return (false);
	if (c == '\r')
	    continue;
	if (c == '\n') {
	    line[i] = '\0';
	    if (ll)
		*ll = i;
	    // Serial.println(line);
	    return (true);
	} else if (i < line_len)
	    line[i++] = c;
    }
}

/* convert an array of 4 big-endian network-order bytes into a uint32_t
 */
static uint32_t crackBE32 (uint8_t bp[])
{
    return (((uint32_t)bp[0] << 24) |
	    ((uint32_t)bp[1] << 16) |
	    ((uint32_t)bp[2] <<  8) |
	    ((uint32_t)bp[3] <<  0));
}

/* called when RSS has just been turned on: update now and restart refresh cycle
 */
void updateRSSNow()
{
    if (updateRSS())
	last_rss = millis();
}

/* display next RSS feed item if on, return whether ok
 */
bool updateRSS ()
{
    // persistent list of malloced titles
    static char *titles[NRSS];
    static uint8_t n_titles, title_i;

    // skip and clear cache if off
    if (!rss_on) {
        while (n_titles > 0) {
            free (titles[--n_titles]);
            titles[n_titles] = NULL;
        }
	return (true);
    }

    // prepare background
    tft.fillRect (rss_bnr_b.x, rss_bnr_b.y, rss_bnr_b.w, rss_bnr_b.h, RSS_BG_COLOR);
    tft.drawLine (rss_bnr_b.x, rss_bnr_b.y, rss_bnr_b.x+rss_bnr_b.w, rss_bnr_b.y, GRAY);
    drawRSSButton();

    // fill titles[] if empty
    if (title_i >= n_titles) {

        // reset count and index
        n_titles = title_i = 0;

        // TCP client
        WiFiClient rss_client;
        
        // line buffer
        char line[256];

        Serial.println(rss_page);
        resetWatchdog();
        if (wifiOk() && rss_client.connect(svr_host, HTTPPORT)) {

            resetWatchdog();
            updateClocks(false);

            // fetch feed page
            httpGET (rss_client, svr_host, rss_page);

            // skip response header
            if (!httpSkipHeader (rss_client))
                goto out;

            // get up to NRSS more titles[]
            for (n_titles = 0; n_titles < NRSS; n_titles++) {
                if (!getTCPLine (rss_client, line, sizeof(line), NULL))
                    goto out;
                if (titles[n_titles])
                    free (titles[n_titles]);
                titles[n_titles] = strdup (line);
                // Serial.printf ("RSS[%d] len= %d\n", n_titles, strlen(titles[n_titles]));
            }
        }

      out:
        rss_client.stop();

        // real trouble if still no titles
        if (n_titles == 0) {
            // report error and back off rss_interval 
            selectFontStyle (LIGHT_FONT, SMALL_FONT);
            tft.setTextColor (RSS_FG_COLOR);
            tft.setCursor (rss_bnr_b.x + rss_bnr_b.w/2-100, rss_bnr_b.y + 2*rss_bnr_b.h/3-1);
            tft.print (F("RSS network error"));
            Serial.println (F("RSS failed"));
            if (rss_interval < 2*RSS_DEFINT)
                rss_interval += 5;
            return (false);
        }
        printFreeHeap (F("RSS"));
    }

    // draw next title
    char *title = titles[title_i];
    size_t ll = strlen(title);

    // usable banner drawing x and width
    uint16_t ubx = rss_btn_b.x + rss_btn_b.w + 5;
    uint16_t ubw = rss_bnr_b.x + rss_bnr_b.w - ubx;

    resetWatchdog();

    // find pixel width of title
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (RSS_FG_COLOR);
    uint16_t bw = getTextWidth (title);

    // draw as 1 or 2 lines to fit within ubw
    if (bw < ubw) {
        // title fits on one row, draw centered horizontally and vertically
        tft.setCursor (ubx + (ubw-bw)/2, rss_bnr_b.y + 2*rss_bnr_b.h/3-1);
        tft.print (title);
    } else {
        // title too long, split near center
        char *row2 = strchr (title+ll/2, ' ');
        if (!row2)
            row2 = title+ll/2;	        // no blanks! just split in half?
        *row2++ = '\0';                 // replace with EOS and move to start of row 2
        uint16_t r1w, r2w;	        // row 1 and 2 pixel widths
        r1w = getTextWidth (title);
        r2w = getTextWidth (row2);

        // draw if fits
        if (r1w <= ubw && r2w <= ubw) {
            tft.setCursor (ubx + (ubw-r1w)/2, rss_bnr_b.y + rss_bnr_b.h/2 - 8);
            tft.print (title);
            tft.setCursor (ubx + (ubw-r2w)/2, rss_bnr_b.y + rss_bnr_b.h - 9);
            tft.print (row2);
        } else {
            Serial.printf ("RSS not fit: '%s' '%s'\n", title, row2);
        }
    }

    // remove from list and advance to next title
    free (titles[title_i]);
    titles[title_i++] = NULL;
 
    // ok so restore update interval
    rss_interval = RSS_DEFINT;

    resetWatchdog();
    return (true);
}
