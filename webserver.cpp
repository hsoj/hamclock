/* service the port 80 commands
 */

#include "HamClock.h"


// persistent server for listening for remote connections
static WiFiServer remoteServer(HTTPPORT);


/* replace all "%20" with blank, IN PLACE
 */
static void replaceBlankEntity (char *from)
{
    char *to = from;
    while (*from) {
        if (strncmp (from, "%20", 3) == 0) {
            *to++ = ' ';
            from += 3;
        } else
            *to++ = *from++;
    }
    *to = '\0';
}

/* send initial response indicating body will be plain text
 */
static void startPlainText (WiFiClient &client)
{
    resetWatchdog();

    FWIFIPRLN (client, F("HTTP/1.0 200 OK"));
    sendUserAgent (client);
    FWIFIPRLN (client, F("Content-Type: text/plain; charset=us-ascii"));
    FWIFIPRLN (client, F("Connection: close\r\n"));
    resetWatchdog();
}

/* send the given HTTP error response
 */
static void sendHTTPError (WiFiClient &client, const char *errmsg)
{
    resetWatchdog();

    Serial.println (errmsg);

    FWIFIPR (client, F("HTTP/1.0 "));
    client.println (errmsg);
    sendUserAgent (client);
    FWIFIPRLN (client, F("Content-Type: text/html"));
    FWIFIPRLN (client, F("Connection: close\r\n"));
    FWIFIPRLN (client, F("<!DOCTYPE HTML>"));
    FWIFIPRLN (client, F("<html><body><h2>"));
    client.println (errmsg);
    FWIFIPRLN (client, F("</h2></body></html>"));
}

/* send screen capture
 */
static bool sendWiFiScreenCapture(WiFiClient &client, char *not_used)
{
    (void) not_used;

    #define BHDRSZ 54				// BMP header size
    uint8_t buf[300];				// any modest size ge BHDRSZ and mult of 3

#if defined(_USE_DESKTOP)
    uint32_t nrows = tft.SCALESZ*tft.height();
    uint32_t ncols = tft.SCALESZ*tft.width();
#else
    uint32_t nrows = tft.height();
    uint32_t ncols = tft.width();
#endif

    // build BMP header 
    resetWatchdog();
    uint32_t npix = nrows*ncols;		// 3 bytes per pixel
    buf[0] = 'B';				// id
    buf[1] = 'M';				// id
    *((uint32_t*)(buf+ 2)) = BHDRSZ+npix*3; 	// total file size: header + 3-byte pixels
    *((uint16_t*)(buf+ 6)) = 0; 		// reserved 0
    *((uint16_t*)(buf+ 8)) = 0; 		// reserved 0
    *((uint32_t*)(buf+10)) = BHDRSZ;		// offset to start of pixels

    *((uint32_t*)(buf+14)) = 40;		// this is a Windows header
    *((uint32_t*)(buf+18)) = ncols;		// width
    *((uint32_t*)(buf+22)) = -nrows;		// height, neg means starting at the top row
    *((uint16_t*)(buf+26)) = 1;			// n planes
    *((uint16_t*)(buf+28)) = 24;		// bits per pixel -- 24 is simple and avoids color table
    *((uint32_t*)(buf+30)) = 0;			// no compression -- again, simple
    *((uint32_t*)(buf+34)) = 3*npix;		// image size in bytes
    *((uint32_t*)(buf+38)) = 0;			// X pixels per meter -- 0 is don't care
    *((uint32_t*)(buf+42)) = 0;			// Y pixels per meter -- 0 is don't care
    *((uint32_t*)(buf+46)) = 0;			// colors used
    *((uint32_t*)(buf+50)) = 0;			// important colors

    // send the web page header
    resetWatchdog();
    FWIFIPRLN (client, F("HTTP/1.0 200 OK"));
    sendUserAgent (client);
    FWIFIPRLN (client, F("Content-Type: image/bmp"));
    FWIFIPR (client, F("Content-Length: ")); client.println (BHDRSZ+3*npix);
    FWIFIPRLN (client, F("Connection: close\r\n"));
    // Serial.println(F("web header sent"));

    // send the image header
    client.write ((uint8_t*)buf, BHDRSZ);
    // Serial.println(F("img header sent"));

    // send the pixels
    resetWatchdog();
    tft.graphicsMode();
    tft.setXY(0,0);
    tft.writeCommand(RA8875_MRWC);
    static bool first = true;
    if (first) {
	// skip first pixel first time
	tft.readData();
	tft.readData();
	first = false;
    }
    uint16_t bufl = 0;
    for (uint32_t i = 0; i < npix; i++) {
	if ((i % tft.width()) == 0)
	    resetWatchdog();
	uint16_t c = (tft.readData() << 8) | tft.readData();	// 16 bit pixel, MSB first
	buf[bufl++] = (c << 3);					// blue in lower 5 bits
	buf[bufl++] = (((c >> 5)&0x3F) << 2);			// green in next 6 bits
	buf[bufl++] = ((c >> 11) << 3);				// red in upper 5 bits
	if (bufl == sizeof(buf) || i == npix-1) {

            // ESP outgoing data can deadlock if incoming buffer fills, so check for largest source.
            // dx cluster is only autonomous incoming connection to check.
            updateDXCluster();

	    client.write ((uint8_t*)buf, bufl);
	    bufl = 0;
	    resetWatchdog();
	}
    }
    // Serial.println(F("pixels sent"));

    return (true);
}

/* remote command to report the current count down timer value, in seconds
 */
static bool sendWiFiCountdown (WiFiClient &client, char *not_used)
{
    (void) not_used;

    startPlainText(client);
    client.print (getCountdownLeft()/1000);   // ms -> s
    FWIFIPRLN (client, F(" secs"));

    return (true);
}

/* remote report of DE or DX info
 */
static bool sendWiFiDEDXInfo (WiFiClient &client, bool send_dx)
{
    char buf[100];

    // handy which
    TZInfo &dst =        send_dx ? dx_tz : de_tz;
    LatLong &ll =        send_dx ? dx_ll : de_ll;
    const char *prefix = send_dx ? "DX_" : "DE_";

    // start response
    startPlainText(client);

    // report local time
    time_t utc = nowWO();
    time_t local = utc + dst.tz_secs;
    int yr = year (local);
    int mo = month(local);
    int dy = day(local);
    int hr = hour (local);
    int mn = minute (local);
    int sc = second (local);
    snprintf (buf, sizeof(buf), "%stime %d-%02d-%02dT%02d:%02d:%02d", prefix, yr, mo, dy, hr, mn, sc);
    client.println (buf);

    // report lat
    snprintf (buf, sizeof(buf), "%slat %0.2f degs", prefix, ll.lat_d);
    client.println (buf);

    // report lng
    snprintf (buf, sizeof(buf), "%slng %0.2f degs", prefix, ll.lng_d);
    client.println (buf);

    // report grid
    uint32_t mnv;
    NVReadUInt32 (send_dx ? NV_DX_GRID : NV_DE_GRID, &mnv);
    char maid[5];
    memcpy (maid, &mnv, 4);
    maid[4] = '\0';
    snprintf (buf, sizeof(buf), "%sMaidenhead %s", prefix, maid);
    client.println (buf);

    // report path if dx
    if (send_dx) {
        float dist, B;
        propDEDXPath (show_lp, &dist, &B);
        dist *= ERAD_M;                             // radians to miles
        B *= 180/M_PIF;                             // radians to degrees
        if (show_km)
            dist *= 1.609344F;                      // mi - > km
        FWIFIPR (client, F("DX_path "));
        snprintf (buf, sizeof(buf), "%.0f %s @ %.0f degs %s", dist, show_km ? "km" : "mi",
                                    B, show_lp ? "LP" : "SP");
        client.println (buf);
    }

    return (true);
}

/* remote report DE info
 */
static bool sendWiFiDEInfo (WiFiClient &client, char *not_used)
{
    (void) not_used;
    return (sendWiFiDEDXInfo (client, false));
}

/* remote report DX info
 */
static bool sendWiFiDXInfo (WiFiClient &client, char *not_used)
{
    (void) not_used;
    return (sendWiFiDEDXInfo (client, true));
}

/* report current satellite info to the given WiFi connection.
 * return whether sat is defined.
 */
static bool sendWiFiSatellite (WiFiClient &client, char *not_used)
{
    (void) not_used;

    // start reply
    startPlainText (client);

    // get name and current position
    float az, el, raz, saz, rhrs, shrs;
    char name[NV_SATNAME_LEN];
    if (!getSatAzElNow (name, &az, &el, &raz, &saz, &rhrs, &shrs)) {
        FWIFIPRLN (client, F("No sat"));
        return (false);
    }

    FWIFIPR (client, F("Name ")); client.println(name);
    FWIFIPR (client, F("Alt ")); client.print(el); FWIFIPRLN(client, F(" degs"));
    FWIFIPR (client, F("Az ")); client.print(az); FWIFIPRLN(client, F(" degs"));

    if (raz != SAT_NOAZ) {
        FWIFIPR (client, F("Next rise in "));
        client.print (rhrs*60);
        FWIFIPR (client, F(" mins at "));
        client.print (raz, 2);
        FWIFIPRLN (client, F(" degs"));
    }
    if (saz != SAT_NOAZ) {
        FWIFIPR (client, F("Next set in "));
        client.print (shrs*60);
        FWIFIPR (client, F(" mins at "));
        client.print (saz, 2);
        FWIFIPRLN (client, F(" degs"));
    }

    return (true);
}

/* send the current collection of sensor data to client in CSV format.
 */
static bool sendWiFiSensorInfo (WiFiClient &client, char *not_used)
{
    (void) not_used;

    // send html header
    startPlainText(client);

    // send content header
    if (useMetricUnits())
	FWIFIPRLN (client, F("# UTC ISO 8601, UNIX seconds, Temp C, Pressure hPa, Humidity %, DewPoint C"));
    else
	FWIFIPRLN (client, F("# UTC ISO 8601, UNIX seconds, Temp F, Pressure inHg, Humidity %, DewPoint F"));

    // print each sensor reading
    time_t t;
    float e, p, h, d;
    uint8_t n = 0;
    char buf[80];
    resetWatchdog();
    while (nextBME280Data (&t, &e, &p, &h, &d, &n)) {
	snprintf (buf, sizeof(buf), "%4d-%02d-%02dT%02d:%02d:%02d, %lu, %7.2f, %7.2f, %5.2f, %7.2f",
		year(t), month(t), day(t), hour(t), minute(t), second(t), t, e, p, h, d);
	client.println (buf);
    }

    return (true);
}

/* send some misc operating info
 */
static bool sendWiFiStats (WiFiClient &client, char *not_used)
{
    (void) not_used;

    // send html header
    startPlainText(client);

    // get latest worst stats
    int worst_heap, worst_stack;
    getWorstMem (&worst_heap, &worst_stack);

    // send info
    resetWatchdog();
    FWIFIPR (client, F("Version   ")); client.println(VERSION);
    FWIFIPR (client, F("Max_Stack ")); client.println (worst_stack);
    FWIFIPR (client, F("Min_Heap  ")); client.println (worst_heap);
    FWIFIPR (client, F("Free_Now  ")); client.println (ESP.getFreeHeap());
    FWIFIPR (client, F("Max_WD_DT ")); client.println (max_wd_dt);

    uint16_t days; uint8_t hrs, mins, secs;
    if (getUptime (&days, &hrs, &mins, &secs)) {
        char buf[40];
        snprintf (buf, sizeof(buf), "%d %02d:%02d:%02d", days, hrs, mins, secs);
        FWIFIPR (client, F("Up_time   ")); client.println (buf);
    }

    return (true);
}

/* finish the wifi then reboot
 */
static bool doWiFiReboot (WiFiClient &client, char *not_used)
{
    (void) not_used;

    // send html header then close
    startPlainText(client);
    FWIFIPRLN (client, F("rebooting ... bye for now."));
    wdDelay(100);
    client.flush();
    client.stop();
    wdDelay(1000);

    Serial.println (F("rebooting..."));
    reboot();

    // never returns but compiler doesn't know that
    return (true);
}

/* update firmware if available
 */
static bool doWiFiUpdate (WiFiClient &client, char *not_used)
{
    (void) not_used;

    // prep for response but won't be one if we succeed with update
    startPlainText(client);

    // proceed if newer version is available
    if (isNewVersionAvailable (NULL, 0)) {
        FWIFIPRLN (client, F("updating..."));
        doOTAupdate();                  // never returns if successful
        FWIFIPRLN (client, F("update failed"));
    } else
        FWIFIPRLN (client, F("already up to date"));

    return (true);
}

/* send current clock time
 */
static bool sendWiFiTime (WiFiClient &client, char *not_used)
{
    (void)not_used;

    // send html header
    startPlainText(client);

    // report time
    char buf[100];
    time_t utc = nowWO();
    int yr = year (utc);
    int mo = month(utc);
    int dy = day(utc);
    int hr = hour (utc);
    int mn = minute (utc);
    int sc = second (utc);
    snprintf (buf, sizeof(buf), "Clock_UTC: %d-%02d-%02dT%02d:%02d:%02d", yr, mo, dy, hr, mn, sc);
    if (utc == now())
        strcat (buf, "Z");      // append Z if time really is UTC
    client.println (buf);

    return (true);
}

/* remote command to set and start the count down timer.
 */
static bool setWiFiCountdown (WiFiClient &client, char line[])
{
    // crack
    int minutes = atoi(line);
    if (minutes < 1)
        return (false);

    // engage
    startCountdown(minutes * 60000);            // mins -> ms

    // ack
    startPlainText (client);
    client.println (minutes);
    return (true);
}

/* remote command to set display on or off
 */
static bool setWiFiDisplayOnOff (WiFiClient &client, char line[])
{
    // parse
    if (strncmp (line, "on ", 3) == 0)
        brightnessOn();
    else if (strncmp (line, "off ", 4) == 0)
        brightnessOff();
    else
        return (false);

    // ack
    char *blank = strchr (line, ' ');
    if (!blank)
        return (false);
    *blank = '\0';
    startPlainText (client);
    FWIFIPR (client, F("display "));
    client.println (line);

    // ok
    return (true);
}

/* remote command to set display on/off times
 */
static bool setWiFiDisplayTimes (WiFiClient &client, char line[])
{
    // parse
    int on_hr, on_mn, off_hr, off_mn;
    if (sscanf (line, "on=%d:%d&off=%d:%d", &on_hr, &on_mn, &off_hr, &off_mn) != 4)
        return (false);

    // set
    if (!setDisplayTimes (on_hr*60+on_mn, off_hr*60+off_mn))
        return (false);

    // ack
    uint16_t on_mins, off_mins;
    if (!getDisplayTimes (&on_mins, &off_mins))
        return (false);
    char ack[100];
    snprintf (ack, sizeof(ack), "display on %02d:%02d off %02d:%02d", on_mins/60, on_mins%60,
                off_mins/60, off_mins%60);
    startPlainText (client);
    client.println (ack);

    // ok
    return (true);

}

/* remote command to set dx cluster on or off.
 * what this really does is change plot2_ch to either PLOT2_DX (on) or PLOT2_XRAY (off)
 */
static bool setWiFiDXClusterOnOff (WiFiClient &client, char line[])
{
    // parse and ack
    bool changed = false;
    bool turn_on = false;
    bool configured = false;
    if (strncmp (line, "on ", 3) == 0) {
        // set if not already and configured
        if (useDXCluster())
            configured = true;
        if (configured && plot2_ch != PLOT2_DX) {
            plot2_ch = PLOT2_DX;
            initDXCluster();
            changed = true;
        }
        turn_on = true;
    } else if (strncmp (line, "off ", 4) == 0) {
        // change to something else if not already
        if (plot2_ch == PLOT2_DX) {
            plot2_ch = PLOT2_XRAY;
            closeDXCluster();
            changed = true;
        }
        turn_on = false;
    } else {
        // unknown command
        return (false);
    }

    // force update and persist if changed
    if (changed) {
        initWiFiRetry();
        NVWriteUInt8 (NV_PLOT_2, plot2_ch);
    }

    // ack
    startPlainText (client);
    if (turn_on && !configured) {
        FWIFIPRLN (client, F("dxcluster not configured"));
    } else {
        char buf[60];
        snprintf (buf, sizeof(buf), "dxcluster %s%s", turn_on ? "on" : "off", changed ? "" : " already");
        client.println (buf);
    }

    // ok
    return (true);
}

/* set DE or DX from GET command: newDX?long=XXX&lat=YYY (or lng)
 * return whether all ok.
 */
static bool setWiFiNewDEDX (WiFiClient &client, bool new_dx, char line[])
{
    LatLong ll;
    char *p;

    // crack longitude
    if (!(p = strstr (line, "long")) && !(p = strstr (line, "lng")))    // friendly
        return (false);
    p = strchr (p, '=');
    if (!p)
	return (false);
    ll.lng_d = myatof(p+1);
    if (ll.lng_d < -180 || ll.lng_d >= 180)
        return (false);

    // crack latitude
    p = strstr (line, "lat");
    if (!p)
	return (false);
    p = strchr (p, '=');
    if (!p)
	return (false);
    ll.lat_d = myatof(p+1);
    if (ll.lat_d < -90 || ll.lat_d > 90)
        return (false);

    // both newDE/DX will call normalizeLL() to clean up and set radian versions

    // engage -- including normalization
    if (new_dx)
	newDX (ll, NULL);
    else
	newDE (ll);

    // echo
    sendWiFiDEDXInfo (client, new_dx);

    // ok
    return (true);
}

/* set DE from GET command: newDE?long=XXX&lat=YYY
 * return whether all ok.
 */
static bool setWiFiNewDE (WiFiClient &client, char line[])
{
    return (setWiFiNewDEDX (client, false, line));
}

/* set DX from GET command: newDX?long=XXX&lat=YYY
 * return whether all ok.
 */
static bool setWiFiNewDX (WiFiClient &client, char line[])
{
    return (setWiFiNewDEDX (client, true, line));
}

/* set DE or DX from maidenhead locator, eg, AB12
 * return whether all ok
 */
static bool setWiFiNewGrid (WiFiClient &client, bool new_dx, char line[])
{
    Serial.println (line);

    // check
    char m1, m2, m3, m4;
    if (strlen (line) < 5 || line[4] != ' '
                        || sscanf (line, "%c%c%c%c", &m1, &m2, &m3, &m4) != 4
                        || m1 < 'A' || m1 > 'R' || m2 < 'A' || m2 > 'R'
                        || !isdigit(m3) || !isdigit(m4))
	return (false);

    // convert
    line[4] = '\0';
    LatLong ll;
    maidenhead2ll (ll, line);

    // engage
    if (new_dx)
	newDX (ll, NULL);
    else
	newDE (ll);

    // echo
    sendWiFiDEDXInfo (client, new_dx);

    // ok
    return (true);
}

/* set DE from maidenhead locator, eg, AB12
 * return whether all ok
 */
static bool setWiFiNewDEGrid (WiFiClient &client, char line[])
{
    return (setWiFiNewGrid (client, false, line));
}

/* set DX from maidenhead locator, eg, AB12
 * return whether all ok
 */
static bool setWiFiNewDXGrid (WiFiClient &client, char line[])
{
    return (setWiFiNewGrid (client, true, line));
}

/* try to set the satellite to the given name
 */
static bool setWiFiSatName (WiFiClient &client, char line[])
{
    resetWatchdog();

    // replace any %20
    replaceBlankEntity (line);

    // remove trailing HTTP, if any (curl sends it, chrome doesn't)
    char *http = strstr (line, " HTTP");
    if (*http)
        *http = '\0';

    // do it
    setSatFromName (line);
    sendWiFiSatellite (client, NULL);

    // already reported results
    return (true);
}

/* set satellite from given TLE: set_sattle?name=n&t1=line1&t2=line2
 * return whether command is fully recognized.
 */
static bool setWiFiSatTLE (WiFiClient &client, char line[])
{
    resetWatchdog();

    // find components
    char *name = strstr (line, "name=");
    char *t1 = strstr (line, "&t1=");
    char *t2 = strstr (line, "&t2=");
    if (!name || !t1 || !t2) {
        Serial.println (F("missing component"));
        return (false);
    }

    // break into proper separate strings
    name += 5; *t1 = '\0';
    t1 += 4; *t2 = '\0';
    t2 += 4;

    // replace %20 with blanks
    replaceBlankEntity (name);
    replaceBlankEntity (t1);
    replaceBlankEntity (t2);

    // enforce known line lengths
    size_t t1l = strlen(t1);
    if (t1l < TLE_LINEL-1) {
        Serial.println (F("t1 short"));
        return(false);
    }
    t1[TLE_LINEL-1] = '\0';
    size_t t2l = strlen(t2);
    if (t2l < TLE_LINEL-1) {
        Serial.println (F("t2 short"));
        return(false);
    }
    t2[TLE_LINEL-1] = '\0';

    // try to install
    if (setSatFromTLE (name, t1, t2))
	return (sendWiFiSatellite (client, NULL));

    // nope
    Serial.println (F("Bad sat:"));
    Serial.println (name);
    Serial.println (t1);
    Serial.println (t2);
    return (false);
}

/* set clock time from any of three formats:
 *  ISO=YYYY-MM-DDTHH:MM:SS
 *  unix=s
 *  Now
 * return whether command is fully recognized.
 */
static bool setWiFiTime (WiFiClient &client, char line[])
{
    resetWatchdog();

    int yr, mo, dy, hr, mn, sc;

    if (strncmp (line, "Now", 3) == 0) {

	changeTime (0);

    } else if (strncmp (line, "unix=", 5) == 0) {

	// crack and engage
	changeTime (atol(line+5));

    } else if (sscanf (line, "ISO=%d-%d-%dT%d:%d:%d", &yr, &mo, &dy, &hr, &mn, &sc) == 6) {

	// reformat
	tmElements_t tm;
	tm.Year = yr - 1970;
	tm.Month = mo;
	tm.Day = dy;
	tm.Hour = hr;
	tm.Minute = mn;
	tm.Second = sc;

	// convert and engage
	changeTime (makeTime(tm));

    } else {

	return (false);
    }

    startPlainText(client);
    FWIFIPR (client, F("UNIX_time "));
    client.println (nowWO());

    return (true);
}

/* perform a touch screen action based on coordinates received via wifi GET
 * return whether all ok.
 */
static bool setWiFiTouch (WiFiClient &client, char line[])
{
    char *p;

    // crack raw screen x and y
    p = strstr (line, "x=");
    if (!p)
	return (false);
    int x = atoi (p+2);
    p = strstr (line, "y=");
    if (!p)
	return (false);
    int y = atoi (p+2);

    // must be over display
    if (x < 0 || x >= tft.width() || y < 0 || y >= tft.height())
	return (false);

    // inform checkTouch() to use wifi_tt_s; it will reset
    wifi_tt_s.x = x;
    wifi_tt_s.y = y;
    wifi_tt = TT_TAP;

    // ack
    startPlainText (client);
    FWIFIPR (client, F("Touch_x "));
    client.println (wifi_tt_s.x);
    FWIFIPR (client, F("Touch_y "));
    client.println (wifi_tt_s.y);

    // ok
    return (true);
}

/* service remote connection
 */
static void serveRemote(WiFiClient &client)
{
    typedef struct {
        PGM_P command;                                  // GET command, including delim
        bool (*funp)(WiFiClient &client, char *line);   // function to implement
        PGM_P help;                                     // more after command, if any
    } CmdTble;
    const CmdTble command_table[] = { // can't use static PROGMEM because PSTR is a runtime expression!
        { PSTR("get_capture.bmp "),   sendWiFiScreenCapture, NULL },
        { PSTR("get_countdown "),     sendWiFiCountdown,     NULL },
        { PSTR("get_de "),            sendWiFiDEInfo,        NULL },
        { PSTR("get_dx "),            sendWiFiDXInfo,        NULL },
        { PSTR("get_satellite "),     sendWiFiSatellite,     NULL },
        { PSTR("get_sensors "),       sendWiFiSensorInfo,    NULL },
        { PSTR("get_stats "),         sendWiFiStats,         NULL },
        { PSTR("get_time "),          sendWiFiTime,          NULL },
        { PSTR("restart "),           doWiFiReboot,          NULL },
        { PSTR("updateVersion "),     doWiFiUpdate,          NULL },
        { PSTR("set_countdown?"),     setWiFiCountdown,      PSTR("minutes") },
        { PSTR("set_displayOnOff?"),  setWiFiDisplayOnOff,   PSTR("on|off") },
        { PSTR("set_displayTimes?"),  setWiFiDisplayTimes,   PSTR("on=HR:MN&off=HR:MN") },
        { PSTR("set_dxclusterOnOff?"),setWiFiDXClusterOnOff, PSTR("on|off") },
        { PSTR("set_newde?"),         setWiFiNewDE,          PSTR("lat=X&lng=Y") },
        { PSTR("set_newdegrid?"),     setWiFiNewDEGrid,      PSTR("AB12") },
        { PSTR("set_newdx?"),         setWiFiNewDX,          PSTR("lat=X&lng=Y") },
        { PSTR("set_newdxgrid?"),     setWiFiNewDXGrid,      PSTR("AB12") },
        { PSTR("set_satname?"),       setWiFiSatName,        PSTR("abc|none") },
        { PSTR("set_sattle?"),        setWiFiSatTLE,         PSTR("name=abc&t1=line1&t2=line2") },
        { PSTR("set_time?"),          setWiFiTime,           PSTR("ISO=YYYY-MM-DDTHH:MM:SS") },
        { PSTR("set_time?"),          setWiFiTime,           PSTR("Now") },
        { PSTR("set_time?"),          setWiFiTime,           PSTR("unix=secs_since_1970") },
        { PSTR("set_touch?"),         setWiFiTouch,          PSTR("x=X&y=Y") },
    };
    #define N_CT (sizeof(command_table)/sizeof(command_table[0]))

    char line[TLE_LINEL*3];     // accommodate longest query, probably set_sattle
    char *skipget = line+5;     // handy skip "GET /"

    // first line should be the GET
    if (!getTCPLine (client, line, sizeof(line), NULL) || strncmp (line, "GET /", 5)) {
        sendHTTPError (client, "405 Method Not Allowed");
        goto out;
    }

    // discard remainder of header
    (void) httpSkipHeader (client);

    Serial.print (F("Command from "));
        Serial.print(client.remoteIP());
        Serial.print(F(": "));
        Serial.println(line);


    // search for command, execute its implementation function if found
    resetWatchdog();
    for (uint8_t i = 0; i < N_CT; i++) {
        const CmdTble *ctp = &command_table[i];
        size_t cl = strlen_P (ctp->command);
        if (strncmp_P (skipget, ctp->command, cl) == 0) {
            // found command, now run its function passing string after command
            if (!(*ctp->funp)(client, skipget+cl))
                sendHTTPError (client, "400 Bad request");
            goto out;
        }
    }

    // if get here, command was not found, list help
    startPlainText(client);
    for (uint8_t i = 0; i < N_CT; i++) {
        const CmdTble *ctp = &command_table[i];
        client.print (FPSTR(ctp->command));
        if (ctp->help)
            client.println (FPSTR(ctp->help));
        else
            client.println ("");
    }

  out:

    client.stop();
    printFreeHeap (F("serveRemote"));
}

void checkWebServer()
{
    // check if someone is trying to tell/ask us something
    WiFiClient client = remoteServer.available();
    if (client)
	serveRemote(client);
}

void initWebServer()
{
    resetWatchdog();
    remoteServer.begin();
}
