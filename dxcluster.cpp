/* handle DX cluster display, for now only DXSpider and AR-Cluster are supported.
 * connection only open when plot2 is showing list.
 */

#include "HamClock.h"


/* AR-Cluster has very buggy show/heading results so we will not support until this is fixed.
 * however it seems the author, is SK : https://www.qrz.com/db/ab5k
 */
// #define _ACCEPT_AR_CLUSTER


#define TITLE_COLOR     RA8875_GREEN
#define LISTING_COLOR   RA8875_WHITE

#define DX_TIMEOUT      60000           // send line feed if idle this long, millis
#define MAX_AGE         300000          // max age to restore spot in list, millis
#define TITLE_Y0        27              // title dy, match VOACAP title position
#define HOSTNM_Y0       32              // host name y down from box top
#define LISTING_Y0      47              // first spot y down from box top
#define LISTING_DY      16              // listing row separation
#define FONT_H          7               // listing font height
#define FONT_W          6               // listing font width
#define DWELL_MS        5000            // period to show non-fatal message, ms
#define MAX_SPOT_LEN    12              // longest saved spot call
#define LISTING_N       ((PLOT2_H - LISTING_Y0)/LISTING_DY)         // max n list rows
#define MAX_HOST_LEN    ((plot2_b.w-2)/FONT_W)                      // max host name len
#define LISTING_Y(r)    (plot2_b.y + LISTING_Y0 + (r)*LISTING_DY)   // screen y for listing row r
#define LISTING_R(Y)    (((Y)+LISTING_DY/2-FONT_H/2-plot2_b.y-LISTING_Y0)/LISTING_DY) // row from screen Y

static WiFiClient dx_client;            // persistent connection while displayed
static uint32_t last_action;            // time of most recent cluster connection activity, millis()
static uint8_t n_spots;                 // n spots already displayed
static char spots[LISTING_N][MAX_SPOT_LEN];          // rolling list of calls seen, always starting at top
static float freqs[LISTING_N];          // rolling list of freqs seen, always starting at top, kHz
static uint16_t uts[LISTING_N];         // rolling list of UT seen, always starting at top

typedef enum {
    CT_UNKNOWN,
    CT_ARCLUSTER,
    CT_DXSPIDER
} ClusterType;
static ClusterType cl_type;

/* convert any upper case letter in str to lower case IN PLACE
 */
static void strtolower (char *str)
{
    for (char c = *str; c != '\0'; c = *++str)
        if (isupper(c))
            *str = tolower(c);
}

/* draw a spot at the given row
 */
static void drawSpot (uint8_t row)
{
    char line[40];

    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor(LISTING_COLOR);

    uint16_t x = plot2_b.x+4;
    uint16_t y = LISTING_Y(row);
    tft.fillRect (x, y, plot2_b.w-5, LISTING_DY-1, RA8875_BLACK);
    tft.setCursor (x, y);

    // guard microwave freqs
    char f_str[10];
    if (freqs[row] < 1e6)
        snprintf (f_str, sizeof(f_str), "%8.1f", freqs[row]);
    else if (freqs[row] < 1e8)
        snprintf (f_str, sizeof(f_str), "%8.0f", freqs[row]);
    else
        snprintf (f_str, sizeof(f_str), "%-8s", " ? ");


    snprintf (line, sizeof(line), "%s %-*s %04u", f_str, MAX_SPOT_LEN-1, spots[row], uts[row]);
    tft.print (line);
}

/* display the given error message.
 * then if fatal: discard spots, shut down connection and done.
 * else dwell and restore.
 */
static void showClusterErr (bool fatal, const char *msg)
{
    // erase list area
    tft.fillRect (plot2_b.x+1, HOSTNM_Y0+10, plot2_b.w-2, plot2_b.h-HOSTNM_Y0-10-1, RA8875_BLACK);

    // show message
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor(RA8875_RED);
    uint16_t mw = getTextWidth ((char*)msg);
    tft.setCursor (plot2_b.x + (plot2_b.w-mw)/2, LISTING_Y(2));
    tft.print (msg);

    // log too
    Serial.printf ("DXC: %s\n", msg);

    if (fatal) {
        // shut down connection
        dx_client.stop();
        n_spots = 0;
    } else {
        // restore
        wdDelay (DWELL_MS);
        for (uint8_t i = 0; i < n_spots; i++)
            drawSpot (i);
    }
}

/* try to connect to the DX cluster defined by getDXClusterHost():getDXClusterPort().
 * if success: dx_client is live and return true
 * else: dx_client is closed, display error msg, return false.
 * if called while already connected just return true immediately.
 */
static bool connectDXCluster()
{
    char *dxhost = getDXClusterHost();
    int dxport = getDXClusterPort();
    char buf[150];

    // just continue if already connected
    if (dx_client) {
        Serial.printf ("DXC: Resume %s:%d\n", dxhost, dxport);
        return (true);
    }

    Serial.printf ("DXC: Connecting to %s:%d\n", dxhost, dxport);
    resetWatchdog();
    if (wifiOk() && dx_client.connect(dxhost, dxport)) {

        // look alive
        resetWatchdog();
        updateClocks(false);
        Serial.printf ("DXC: connect ok\n");

        // assume we have been asked for our callsign
        dx_client.println (getCallsign());

        // read until find a line ending with '>', looking for clue about type of cluster along the way
        uint16_t bl;
        cl_type = CT_UNKNOWN;
        while (getTCPLine (dx_client, buf, sizeof(buf), &bl) && buf[bl-1] != '>') {
            strtolower(buf);
            if (strstr (buf, "dxspider"))
                cl_type = CT_DXSPIDER;
#if defined(_ACCEPT_AR_CLUSTER)
            if (strstr (buf, "ar-cluster"))
                cl_type = CT_ARCLUSTER;
#endif // _ACCEPT_AR_CLUSTER
        }

        if (cl_type == CT_UNKNOWN) {
            showClusterErr (true, "Unknown cluster type");
            return (false);
        }

        // confirm still ok
        if (!dx_client) {
            showClusterErr (true, "Login failed");
            return (false);
        }

        // all ok so far
        return (true);
    }

    // sorry
    showClusterErr (true, "Connection failed");    // also calls dx_client.stop()
    return (false);
}

/* given a call sign return its lat/long by querying dx_client.
 * strategy depends on cl_type.
 * return whether successful.
 */
static bool getSpotLL (const char *call, LatLong &ll)
{
    bool ok = false;
    char buf[100];

    if (cl_type == CT_ARCLUSTER) {

        // set our location
        snprintf (buf, sizeof(buf), "set/station/latlon %.3f %.3f", de_ll.lat_d, de_ll.lng_d);
        Serial.println (buf);
        dx_client.println (buf);

        // ask for heading 
        snprintf (buf, sizeof(buf), "show/heading %s", call);
        Serial.println (buf);
        dx_client.println (buf);

        // read until find heading response
        float heading, miles;
        while (!ok && getTCPLine (dx_client, buf, sizeof(buf), NULL)) {
            strtolower(buf);
            if (strstr (buf, "heading/distance")) {
                char *deg = strstr(buf, " deg/");
                char *mi = strstr(buf, " mi/");
                if (deg != NULL && mi != NULL) {
                    heading = atof (deg-4);
                    miles = atof (mi-6);
                    ok = true;
                    Serial.println (buf);
                }
            }
        }

        if (ok) {

            // compute target using heading and distance from de
            float A = deg2rad(heading);
            float b = miles/ERAD_M;             // 2Pi * miles / (2Pi*ERAD_M)
            float cx = de_ll.lat;               // really (Pi/2 - lat) then exchange sin/cos
            float ca, B;                        // cos polar angle, delta lng
            solveSphere (A, b, sinf(cx), cosf(cx), &ca, &B);
            ll.lat_d = rad2deg(asinf(ca));      // asin(ca) = Pi/2 - acos(ca)
            ll.lng_d = rad2deg(de_ll.lng + B);
            normalizeLL (ll);

            Serial.printf ("%s heading= %g miles= %g ==> lat= %g lng= %g\n", call, heading, miles,
                        ll.lat_d, ll.lng_d);
            // Serial.printf ("%s A= %g b= %g c= %g -> a= %g B= %g\n", A, b, c, acos(ca), B);

            // check for bogus
            if (fabsf(ll.lat_d) < 1 && fabsf (ll.lng_d) < 1) {
                showClusterErr (false, "Unknown location");
                ok = false;
            }

        } else {

            Serial.println (F("No heading"));
        }

    } else if (cl_type == CT_DXSPIDER) {

        // set our grid location
        uint32_t mnv;
        NVReadUInt32 (NV_DE_GRID, &mnv);
        strcpy (buf, "set/qra AA11JJ"); // cluster requires 6 char locator
        memcpy (&buf[8], &mnv, 4);
        dx_client.println(buf);
        Serial.println(buf);

        // absorb prompt
        uint16_t bl;
        while (getTCPLine (dx_client, buf, sizeof(buf), &bl) && buf[bl-1] != '>')
            continue;

        /* issue show/muf, reply will look like this:
         *          1         2         3         4         5         6         7         8
         * 12345678901234567890123456789012345678901234567890123456789012345678901234567890
         *
         * RxSens: -128 dBM SFI:  68   R:   5   Month: 10   Day: 1
         * Power :   26 dBW    Distance:   921 km    Delay:  3.8 ms
         * Location                       Lat / Long           Azim
         * tucson, az                     32 41 N 111 3 W        33
         * Colorado-K                     39 30 N 105 12 W      217
         * UT LT  MUF Zen  1.8  3.5  7.0 10.1 14.0 18.1 21.0 24.9 28.0 50.0
         * 19 12 11.6  49   S3+  S5+  S7
         * 20 13 11.6  47   S3+  S5+  S7
         * WB0OEW de EA4RCH-5  1-Oct-2019 1950Z dxspider >
         */
        uint8_t nmatch = 0;

        snprintf (buf, sizeof(buf), "show/muf %s", call);
        dx_client.println (buf);
        Serial.println(buf);

        // always read the full response until find prompt line ending with '>'
        while (getTCPLine (dx_client, buf, sizeof(buf), &bl) && buf[bl-1] != '>') {

            Serial.println(buf);

            // look for _second_ location line
            int lat_deg, lat_min, lng_deg, lng_min;
            char lat_ns, lng_ew;
            if (sscanf (buf+31, "%d %d %c %d %d %c",
                            &lat_deg, &lat_min, &lat_ns, &lng_deg, &lng_min, &lng_ew) == 6 && ++nmatch == 2) {
                ll.lat_d = lat_deg + lat_min/60.0F;
                if (lat_ns == 'S')
                    ll.lat_d = -ll.lat_d;
                ll.lng_d = lng_deg + lng_min/60.0F;
                if (lng_ew == 'W')
                    ll.lng_d = -ll.lng_d;
                Serial.printf ("DXC: found %s at %g %g\n", call, ll.lat_d, ll.lng_d);
                normalizeLL(ll);
                ok = true;
            }
        }

        if (!ok)
            Serial.println (F("No muf location"));

    } else {

        Serial.printf ("Bug! cl_type= %d\n", cl_type);
    }

    // return whether ok
    return (ok);
}

/* add and display a new spot, scrolling list if already full
 */
static void addSpot (float kHz, const char call[], uint16_t ut)
{
    // skip if identical to last
    uint8_t si = n_spots - 1;
    if (n_spots > 0 && fabsf(kHz-freqs[si]) < 0.1F && !strcmp (call, spots[si]) && ut == uts[si])
        return;

    // insure row n_spots is vacant
    if (n_spots == LISTING_N) {
        // scroll up, discarding top (first) entry
        for (uint8_t i = 0; i < LISTING_N-1; i++) {
            freqs[i] = freqs[i+1];
            strcpy (spots[i], spots[i+1]);
            uts[i] = uts[i+1];
            drawSpot (i);
        }
        n_spots = LISTING_N-1;
    }

    // store in next row, known to be empty
    freqs[n_spots] = kHz;
    memcpy (spots[n_spots], call, MAX_SPOT_LEN-1);      // preserve existing EOS
    uts[n_spots] = ut;

    // draw
    drawSpot (n_spots++);
}

/* display the current cluster host and port in the given color
 */
static void showHostPort (uint16_t c)
{
    char *dxhost = getDXClusterHost();
    int dxport = getDXClusterPort();

    char name[MAX_HOST_LEN];
    snprintf (name, sizeof(name), "%s:%d", dxhost, dxport);

    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor(c);
    uint16_t nw = getTextWidth (name);
    tft.setCursor (plot2_b.x + (plot2_b.w-nw)/2, plot2_b.y + HOSTNM_Y0);
    tft.print (name);
}

/* prep plot2_b and connect dx_client to a dx cluster.
 */
void initDXCluster()
{
    if (!useDXCluster())
        return;

    // erase all except bottom line which is map border
    tft.fillRect (plot2_b.x, plot2_b.y, plot2_b.w, PLOT2_H-1, RA8875_BLACK);
    tft.drawRect (plot2_b.x, plot2_b.y, plot2_b.w, PLOT2_H, GRAY);

    // title
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor(TITLE_COLOR);
    tft.setCursor (plot2_b.x + 27, plot2_b.y + TITLE_Y0);
    tft.print ("DX Cluster");

    // show cluster host busy
    showHostPort (RA8875_YELLOW);

    // connect to dx cluster
    if (connectDXCluster()) {

        // ok: show host in green
        showHostPort (RA8875_GREEN);

        // restore known spots if not too old else reset list
        if (millis() - last_action < MAX_AGE) {
            for (uint8_t i = 0; i < n_spots; i++)
                drawSpot (i);
        } else {
            n_spots = 0;
        }

        // init time
        last_action = millis();

    } // else already displayed error message

    printFreeHeap(F("initDXCluster"));
}

/* called repeatedly by main loop to show another DXCluster entry if any.
 */
void updateDXCluster()
{
    // skip if we are not up
    if (plot2_ch != PLOT2_DX || !useDXCluster() || !dx_client)
        return;

    // roll any new spots into list
    bool gotone = false;
    char line[120];
    char call[30];
    float kHz;
    while (dx_client.available() && getTCPLine (dx_client, line, sizeof(line), NULL)) {
        // DX de KD0AA:     18100.0  JR1FYS       FT8 LOUD in FL!                2156Z EL98
        Serial.printf ("DXC: %s\n", line);

        updateClocks(false);
        resetWatchdog();

        if (sscanf (line, "DX de %*s %f %10s", &kHz, call) == 2) {
            // looks like a spot, extract time also
            char *utp = &line[70];
            uint16_t ut = atoi(utp);
            if (ut > 2400)
                ut = 2400;

            // note and display
            gotone = true;
            addSpot (kHz, call, ut);
        }
    }

    // check for lost connection
    if (!dx_client.connected()) {
        showClusterErr (true, "Lost connection");
        return;
    }

    // send NL if it's been a while
    uint32_t t = millis();
    if (gotone) {
        last_action = t;
    } else if (t - last_action > DX_TIMEOUT) {
        last_action = t;
        Serial.println (F("DXC: feeding"));
        dx_client.print("\r\n");
    }
}

/* insure cluster connection is closed
 */
void closeDXCluster()
{
    // make sure connection is closed
    if (dx_client) {
        dx_client.stop();
        Serial.printf ("DXC: disconnect %s\n", dx_client ? "failed" : "ok");
    }
}

/* try to set DX from the touched spot.
 * return true if looks like user is interacting with the cluster, false if wants to change pane.
 */
bool checkDXTouch (const SCoord &s)
{
    // ours at all?
    if (plot2_ch != PLOT2_DX || !inBox (s, plot2_b))
        return (false);

    // tapping title always leaves this pane
    if (s.y < plot2_b.y + TITLE_Y0) {
        closeDXCluster();               // insure disconnected
        return (false);
    }

    // reconnect if off
    if (!dx_client.connected())
        initDXCluster();

    // find call on row, if any
    int click_row = LISTING_R(s.y);
    if (click_row >= 0 && click_row < n_spots && spots[click_row][0] != '\0' && dx_client) {
        LatLong ll;
        if (getSpotLL (spots[click_row], ll)) {
            setRadioSpot(freqs[click_row]);
            newDX (ll, spots[click_row]);
        }
    }

    // ours
    return (true);
}
