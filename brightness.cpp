/* manage display brightness, either automatically via phot sensor or via on/off/idle settings from the user.
 * coordinate pane with ncfdx button and key.
 *
 * ESP circuit:
 *
 *       +3.3V
 *         |
 *        330K
 *         |
 *         --- A0
 *         |
 *   photoresistor
 *         |
 *        Gnd
 */


#include "HamClock.h"

// phot not supported on desktops
#if !defined(_USE_UNIX)
#define _SUPPORT_PHOT
#endif

// on/off supported on all FB0 and ESP
#if defined(_USE_FB0) || defined(_IS_ESP8266)
#define _SUPPORT_DSPLY_ONOFF
#endif


// configuration values
#define BPWM_MAX	255		        // PWM for 100% brightness
#define BPWM_CHG        1.259		        // brightness mult per tap: 1 db = 1 JND
#define	BPWM_MIN_CHG	4		        // smallest value that increases by >= 1 when * BPWM_CHG
#define	PHOT_PIN	A0		        // Arduino name of analog pin with photo detector
#define	PHOT_MAX	1024		        // 10 bits including known range bug
#define	BPWM_BLEND	0.2F		        // fraction of new brightness to keep
#define	PHOT_BLEND	0.5F		        // fraction of new pwm to keep
#define BPWM_COL        RA8875_WHITE            // brightness scale color
#define PHOT_COL        RA8875_CYAN             // phot scale color
#define BRIGHT_COL      RA8875_RED              // dim marker color  
#define DIM_COL         RA8875_BLUE             // dim marker color  
#define N_ROWS          11                      // rows of clock info, including gaps
#define TOP_GAP         4                       // pixels above clock info title
#define MARKER_H        3                       // scaler marker height
#define SCALE_W         5                       // scale width
#define FOLLOW_DT       100                     // read phot this often, ms

static int16_t bpwm, prev_bpwm;                 // current and previous brightness PWM value 0 .. BPWM_MAX
static uint16_t phot;		                // current photorestistor value
static bool phot_connected;			// set if initial read > 1, else manual clock settings

// fast access to what is in NVRAM
static uint16_t fast_phot_bright, fast_phot_dim;
static uint16_t fast_bpwm_bright, fast_bpwm_dim;

// support for timers and idle
static uint16_t mins_on, mins_off;              // user's local on/off times, stored as hr*60 + min
static uint16_t idle_mins;                      // user's idle timeout period, minutes; 0 for none
static uint32_t idle_t0;                        // time of last user action, millis
static bool clock_off;                          // whether clock is now ostensibly off
static uint8_t user_on, user_off;               // user's on and off brightness
#if defined(_USE_FB0)
static bool is_dsi;                             // whether this is an RPi DSI display
static const char dsi_path[] = "/sys/class/backlight/rpi_backlight/brightness";
#endif // _USE_FB0


/* set display brightness to bpwm.
 * on ESP we control backlight, RPi control displays, others systems ignored.
 */
static void setDisplayBrightness(bool log)
{
        if (log)
            Serial.printf ("BR: setting bpwm %d\n", bpwm);

        #if defined(_USE_FB0)

            if (is_dsi) {
                // control backlight
                char cmd[128];
                snprintf (cmd, sizeof(cmd), "echo %d > %s &", bpwm, dsi_path);
                system (cmd);
            } else {
                // control HDMI on or off
                if (bpwm < BPWM_MAX/2)
                    system ("tvservice -o");
                else
                    system ("tvservice -p; sleep 2; fbset -accel true; fbset -accel false");
            }

        #endif

        #if defined(_IS_ESP8266)

            // ESP: control backlight
            tft.PWM1out(bpwm);

        #endif
}


/* return current photo detector value, range [0..PHOT_MAX] increasing with brightness.
 */
static uint16_t readPhot()
{
        #if defined(_SUPPORT_PHOT)

            resetWatchdog();

            uint16_t new_phot = PHOT_MAX - analogRead (PHOT_PIN);           // brighter gives smaller value

            resetWatchdog();

            return (PHOT_BLEND*new_phot + (1-PHOT_BLEND)*phot);             // smoothing

        #else

            return (0);

        #endif  // _SUPPORT_PHOT
}


#if defined(_SUPPORT_PHOT)

/* get dimensions of brightness and phot slider controls
 */
static void getControls (SBox &b, SBox &p)
{

        b.w = SCALE_W;
        b.x = brightness_b.x + (brightness_b.w - b.w)/3;
        b.y = brightness_b.y + brightness_b.h/9;
        b.h = 6*brightness_b.h/10;

        p.w = SCALE_W;
        p.x = brightness_b.x + 2*(brightness_b.w - b.w)/3;
        p.y = b.y;
        p.h = b.h;

}

#endif  // _SUPPORT_PHOT


/* draw a symbol for the photresistor in b
 */
static void drawPhotSensor()
{
    #if defined(_SUPPORT_PHOT)

        uint8_t n = 2;                                                  // number of \/
        uint16_t w = 2*n+6;                                             // n steps with borders and leads
        uint16_t s = brightness_b.w/w;                                  // 1 x step length
        uint16_t x = brightness_b.x + (brightness_b.w-w*s)/2 + s;       // initial x to center
        uint16_t y = brightness_b.y + brightness_b.h - 2*s;             // y center-line

        // lead in from left then up
        tft.drawLine (x, y, x+s, y, PHOT_COL);
        x += s;
        tft.drawLine (x, y, x+s, y-s, PHOT_COL);
        x += s;

        // draw n \/
        for (uint8_t i = 0; i < n; i++) {
            tft.drawLine (x, y-s, x+s, y+s, PHOT_COL);
            x += s;
            tft.drawLine (x, y+s, x+s, y-s, PHOT_COL);
            x += s;
        }

        // down then lead out to right
        tft.drawLine (x, y-s, x+s, y, PHOT_COL);
        x += s;
        tft.drawLine (x, y, x+s, y, PHOT_COL);

        // incoming light arrows
        uint16_t ax = brightness_b.x + 4*s;                     // arrow head location

        tft.drawLine (ax, y-2*s, ax-1*s,   y-3*s,    PHOT_COL); // main shaft
        tft.drawLine (ax, y-2*s, ax-3*s/4, y-19*s/8, PHOT_COL); // lower shaft
        tft.drawLine (ax, y-2*s, ax-3*s/8, y-11*s/4, PHOT_COL); // upper shaft 

        ax += 2*s;                                              // move to second arrow head and repeat

        tft.drawLine (ax, y-2*s, ax-1*s,   y-3*s,    PHOT_COL);
        tft.drawLine (ax, y-2*s, ax-3*s/4, y-19*s/8, PHOT_COL);
        tft.drawLine (ax, y-2*s, ax-3*s/8, y-11*s/4, PHOT_COL);

    #endif  // _SUPPORT_PHOT
}

/* draw current brightness and phot scales
 */
static void drawBrightnessScale()
{
    #if defined(_SUPPORT_PHOT)

	resetWatchdog();

        SBox b, p;
        getControls (b, p);

        // draw bpwm scale
        // N.B. we assume setup has insured user_on < user_off
        int16_t bh = b.h*(bpwm-user_off)/(user_on - user_off);
        if (bh < MARKER_H)
            bh = MARKER_H;
        tft.fillRect (b.x+1, b.y+1, b.w-2, b.h-2, RA8875_BLACK);        // leave border avoids flicker
        tft.drawRect (b.x, b.y, b.w, b.h, BPWM_COL);
        tft.fillRect (b.x, b.y+b.h-bh, b.w, MARKER_H, BPWM_COL);

        // draw phot scale
        int16_t ph = b.h*phot/PHOT_MAX;
        if (ph < MARKER_H)
            ph = MARKER_H;
        tft.fillRect (p.x+1, p.y+1, p.w-1, p.h-1, RA8875_BLACK);    // leave border to avoid flicker
        tft.drawRect (p.x, p.y, p.w, p.h, PHOT_COL);
        tft.fillRect (p.x, p.y+p.h-ph, p.w, MARKER_H, PHOT_COL);

        // overlay bpwm limits, avoid top and bottom
        bh = b.h*fast_bpwm_bright/BPWM_MAX - 1;
        if (bh < 1)
            bh = 1;
        tft.drawLine (b.x+1, b.y+b.h-bh, b.x+b.w-1, b.y+b.h-bh, BRIGHT_COL);
        bh = b.h*fast_bpwm_dim/BPWM_MAX - 1;
        if (bh < 1)
            bh = 1;
        tft.drawLine (b.x+1, b.y+b.h-bh, b.x+b.w-1, b.y+b.h-bh, DIM_COL);

        // overlay phot limits, avoid top and bottom
        ph = b.h*fast_phot_bright/PHOT_MAX - 1;                     // avoid top
        if (ph < 1)
            ph = 1;                                                 // avoid bottom
        tft.drawLine (p.x+1, p.y+p.h-ph, p.x+p.w-1, p.y+p.h-ph, BRIGHT_COL);
        ph = b.h*fast_phot_dim/PHOT_MAX - 1;
        if (ph < 1)
            ph = 1;                                                 // avoid bottom
        tft.drawLine (p.x+1, p.y+p.h-ph, p.x+p.w-1, p.y+p.h-ph, DIM_COL);

    #endif  // _SUPPORT_PHOT
}

/* draw clock and idle controls
 */
static void drawClockControls()
{
    #if defined(_SUPPORT_DSPLY_ONOFF)

	resetWatchdog();

        tft.fillRect (brightness_b.x+1, brightness_b.y+1, brightness_b.w-2, brightness_b.h-2, RA8875_BLACK);
        tft.drawLine (brightness_b.x, brightness_b.y, brightness_b.x+brightness_b.w, brightness_b.y, GRAY);
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor (RA8875_WHITE);

        // left x values
        uint16_t xl = brightness_b.x + 7;               // label indent
        uint16_t xn = brightness_b.x + 12;              // number indent

        // walk down by dy each time
        uint16_t y = brightness_b.y + TOP_GAP;
        uint8_t dy = (brightness_b.h - TOP_GAP)/N_ROWS;

        // title
        tft.setCursor (xl, y);
        tft.print ("Display");

        // gap
        y += dy;

        // idle
        tft.setCursor (xl-3, y+=dy);
        tft.print ("Idle in:");
        tft.setCursor (xn-3, y+=dy);
        tft.print (idle_mins);
        tft.print (F(" min"));

        // gap
        y += dy;

        // time on
        tft.setCursor (xl, y+=dy);
        tft.print ("On at:");
        tft.setCursor (xn, y+=dy);
        int hr_on = mins_on/60;
        int mn_on = mins_on%60;
        if (hr_on < 10)
            tft.print('0');
        tft.print(hr_on);
        tft.print(':');
        if (mn_on < 10)
            tft.print('0');
        tft.print(mn_on);

        // gap
        y += dy;

        // time off
        tft.setCursor (xl, y+=dy);
        #if defined(_USE_FB0)
            if (is_dsi)
                tft.print ("Dim at:");
            else
                tft.print ("Off at:");          // HDMI don't dim, just turn off or normal
        #else
                tft.print ("Dim at:");
        #endif
        tft.setCursor (xn, y+=dy);
        int hr_off = mins_off/60;
        int mn_off = mins_off%60;
        if (hr_off < 10)
            tft.print('0');
        tft.print(hr_off);
        tft.print(':');
        if (mn_off < 10)
            tft.print('0');
        tft.print(mn_off);

    #endif // _SUPPORT_DSPLY_ONOFF
}

/* given screen tap location known to be within brightness_b, allow user to change a clock setting
 */
static void changeClockSetting (const SCoord &s)
{
    #if defined(_SUPPORT_DSPLY_ONOFF)

        // decide which row and which left-right half
        uint8_t row = (s.y - (brightness_b.y+TOP_GAP))/((brightness_b.h - TOP_GAP)/N_ROWS);
        bool left_half = s.x - brightness_b.x < brightness_b.w/2;

        switch (row) {
        case 2:
            // increase idle time
            idle_mins += 5;
            NVWriteUInt16 (NV_BR_IDLE, idle_mins);
            break;

        case 4:
            // decrease idle time but never below 0
            if (idle_mins > 0) {
                idle_mins = idle_mins < 5 ? 0 : idle_mins - 5;
                NVWriteUInt16 (NV_BR_IDLE, idle_mins);
            }
            break;

        case 5:
            if (left_half) {
                // increase on time hour
                mins_on += 60;
                if (mins_on >= 24*60)
                    mins_on -= 24*60;
            } else {
                // increase on time minutes
                mins_on += 5;
                if (mins_on >= 24*60)
                    mins_on -= 24*60;
            }
            NVWriteUInt16 (NV_DPYON, mins_on);
            break;

        case 7:
            if (left_half) {
                // decrease on time hour
                if (mins_on < 60)
                    mins_on += 24*60;
                mins_on -= 60;
            } else {
                // decrease on time minutes
                if (mins_on < 5)
                    mins_on += 24*60;
                mins_on -= 5;
            }
            NVWriteUInt16 (NV_DPYON, mins_on);
            break;

        case 8:
            if (left_half) {
                // increase off time hour
                mins_off += 60;
                if (mins_off >= 24*60)
                    mins_off -= 24*60;
            } else {
                // increase off time minutes
                mins_off += 5;
                if (mins_off >= 24*60)
                    mins_off -= 24*60;
            }
            NVWriteUInt16 (NV_DPYOFF, mins_off);
            break;

        case 10:
            if (left_half) {
                // decrease off time hour
                if (mins_off < 60)
                    mins_off += 24*60;
                mins_off -= 60;
            } else {
                // decrease off time minutes
                if (mins_off < 5)
                    mins_off += 24*60;
                mins_off -= 5;
            }
            NVWriteUInt16 (NV_DPYOFF, mins_off);
            break;

        default:
            return;             // avoid needless redraw
        }

        // redraw with new settings
        drawClockControls();

    #endif // _SUPPORT_DSPLY_ONOFF
}

/* check whether time to turn display on or off from the timers or idle timeout
 * check idle timeout first, then honor on/off settings
 */
static void checkClockTimers()
{
    #if defined(_SUPPORT_DSPLY_ONOFF)

        // check idle timeout first, if enabled
        if (idle_mins > 0) {
            uint16_t im = (millis() - idle_t0)/60000;   // ms -> mins
            if (im >= idle_mins && !clock_off) {
                Serial.println (F("BR: Idle timed out"));
                bpwm = user_off;
                setDisplayBrightness(true);
                clock_off = true;
            }
        }

        // only check on/off times at top of each minute
        static time_t check_mins;
        time_t utc = nowWO();
        time_t utc_mins = utc/60;
        if (utc_mins == check_mins)
            return;
        check_mins = utc_mins;

        // check for time to turn on or off.
        // get local time
        time_t local = utc + de_tz.tz_secs;
        int hr = hour (local);
        int mn = minute (local);
        uint16_t mins_now = hr*60 + mn;

        // Serial.printf("idle %d now %d on %d off %d,bpwm %d\n",idle_mins,mins_now,mins_on,mins_off,bpwm);

        // change when its time, give priority to turning on in case both are equal
        if (mins_now == mins_on) {
            if (bpwm != user_on) {
                Serial.println (F("BR: on"));
                bpwm = user_on;
                setDisplayBrightness(true);
                clock_off = false;
                idle_t0 = millis();             // reset idle!
            }
        } else if (mins_now == mins_off) {
            if (bpwm != user_off) {
                Serial.println (F("BR: off"));
                bpwm = user_off;
                setDisplayBrightness(true);
                clock_off = true;
            }
        }

    #endif // _SUPPORT_DSPLY_ONOFF
}

/* return whether this is a RPi connected to a DSI display
 */
static bool isRPiDSI()
{
        #if defined(_USE_FB0)
            static bool we_know;

            // test only if we don't already know
            if (!we_know) {

                // test for DSI
                resetWatchdog();
                int dsifd = open (dsi_path, O_WRONLY);
                if (dsifd >= 0) {
                    Serial.printf ("BR: found DSI display\n");
                    is_dsi = true;
                    close (dsifd);
                } else
                    Serial.printf ("BR: assuming HDMI display\n");

                // now we know
                we_know = true;
            }

            return (is_dsi);

        #else
            return (false);

        #endif
}

/* return whether the display hardware can be set on or off
 */
bool brOnOffOk()
{
        #if defined(_SUPPORT_DSPLY_ONOFF)
            return (true);
        #else
            return (false);
        #endif
}

/* return whether the display hardware brightness can be controlled
 */
bool brControlOk()
{
        #if defined(_IS_ESP8266)
            return (true);

        #elif defined(_USE_FB0)
            return (isRPiDSI());

        #else
            return (false);

        #endif
}

/* call this once to determine hardware and set full on.
 * then call setupBrightness() to commence with user's brightness settings.
 */
void initBrightness()
{
	resetWatchdog();

        // establish display
        (void) isRPiDSI();

        // full on for now
	bpwm = BPWM_MAX;
	setDisplayBrightness(true);
}

/* call this once after initBrightness() to commence with user's brightness controls.
 */
void setupBrightness()
{
	resetWatchdog();

	// init to user's full brightness
        user_on = getBrMax()*BPWM_MAX/100;
        user_off = getBrMin()*BPWM_MAX/100;
	bpwm = user_on;
	setDisplayBrightness(true);
        clock_off = false;

        // init idle time and period
        idle_t0 = millis();
        if (!NVReadUInt16 (NV_BR_IDLE, &idle_mins)) {
            idle_mins = 0;
            NVWriteUInt16 (NV_BR_IDLE, idle_mins);
        }

        // check whether connected, discard first read
        phot = readPhot();
        wdDelay(100);
        phot = readPhot();
        phot_connected = phot > 1;  // in case they ever fix the range bug

        // retrieve fast copies, init if first time, honor user settings

        if (!NVReadUInt16 (NV_BPWM_BRIGHT, &fast_bpwm_bright) || fast_bpwm_bright > user_on)
            fast_bpwm_bright = user_on;
        if (!NVReadUInt16 (NV_BPWM_DIM, &fast_bpwm_dim) || fast_bpwm_dim < user_off)
            fast_bpwm_dim = user_off;
        if (fast_bpwm_bright <= fast_bpwm_dim) {
            // new user range is completely outside 
            fast_bpwm_bright = user_on;
            fast_bpwm_dim = user_off;
        }
        NVWriteUInt16 (NV_BPWM_BRIGHT, fast_bpwm_bright);
        NVWriteUInt16 (NV_BPWM_DIM, fast_bpwm_dim);

        if (!NVReadUInt16 (NV_PHOT_BRIGHT, &fast_phot_bright)) {
            fast_phot_bright = PHOT_MAX;
            NVWriteUInt16 (NV_PHOT_BRIGHT, fast_phot_bright);
        }
        if (!NVReadUInt16 (NV_PHOT_DIM, &fast_phot_dim)) {
            fast_phot_dim = 0;
            NVWriteUInt16 (NV_PHOT_DIM, fast_phot_dim);
        }

        // get display mode
        if (!NVReadUInt8 (NV_BRB_MODE, &brb_mode))
            brb_mode = BRB_SHOW_BEACONS;

        // retrieve clock settings
        NVReadUInt16 (NV_DPYON, &mins_on);
        NVReadUInt16 (NV_DPYOFF, &mins_off);
}

/* refresh brightness display if either mode active
 */
void drawBrightness()
{
        if (phot_connected && brb_mode == BRB_SHOW_BRCTRL) {
            // draw scale and phot symbol
            drawBrightnessScale();
            drawPhotSensor();
        } else if (brb_mode == BRB_SHOW_BRCLOCK) {
            // draw clock settings
            drawClockControls();
        }
}


/* set display brightness according to current photo detector and limits and check clock settings
 */
void followBrightness()
{
	resetWatchdog();

        checkClockTimers();

        if (phot_connected && !clock_off) {

            // not too fast (eg, while not updating map after new DE)
            static uint32_t prev_m;
            if (!timesUp (&prev_m, FOLLOW_DT))
                return;

            // save current phot
            uint16_t prev_phot = phot;

            // update mean with new phot reading if connected
            // linear interpolate between dim and bright limits to find new brightness
            phot = readPhot();
            int32_t del_phot = phot - fast_phot_dim;
            int32_t bpwm_range = fast_bpwm_bright - fast_bpwm_dim;
            int32_t phot_range = fast_phot_bright - fast_phot_dim;
            if (phot_range == 0)
                phot_range = 1;         // avoid /0
            int16_t new_bpwm = fast_bpwm_dim + bpwm_range * del_phot / phot_range;
            if (new_bpwm < 0)
                new_bpwm = 0;
            else if (new_bpwm > BPWM_MAX)
                new_bpwm = BPWM_MAX;
            // smooth update
            bpwm = BPWM_BLEND*new_bpwm + (1-BPWM_BLEND)*bpwm + 0.5F;

            // draw even if bpwm doesn't change but phot changed some, such as going above fast_phot_bright
            bool phot_changed = (phot>prev_phot && phot-prev_phot>30) || (phot<prev_phot && prev_phot-phot>30);

            // engage if changed
            if (bpwm != prev_bpwm || phot_changed) {
                prev_bpwm = bpwm;
                setDisplayBrightness(false);
                if (brb_mode == BRB_SHOW_BRCTRL)
                    drawBrightnessScale();
            }

            // #define _DEBUG_BRIGHTNESS
            #ifdef _DEBUG_BRIGHTNESS

                Serial.print("follow");
                Serial.print ("\tPHOT:\t");
                    Serial.print (phot); Serial.print('\t');
                    Serial.print(fast_phot_dim); Serial.print(" .. "); Serial.print(fast_phot_bright);
                Serial.print ("\tBPWM:\t");
                    Serial.print(bpwm); Serial.print('\t');
                    Serial.print(fast_bpwm_dim); Serial.print(" .. "); Serial.println(fast_bpwm_bright);

            #endif // _DEBUG_BRIGHTNESS
        }
}

/* called on any tap anywhere to insure screen is on and reset idle_t0.
 * return whether we were off prior to tap.
 */
bool brightnessOn()
{
        idle_t0 = millis();

        if (clock_off) {
            Serial.println (F("display on"));
            bpwm = user_on;
            setDisplayBrightness(true);
            clock_off = false;
            return (true);
        } else
            return (false);
}

/* turn screen off.
 */
void brightnessOff()
{
        Serial.println (F("display off"));
        bpwm = user_off;
        setDisplayBrightness(true);
        clock_off = true;
}

/* given a tap within brightness_b, change brightness or clock setting
 */
void changeBrightness (SCoord &s)
{
        #if defined(_SUPPORT_PHOT)
            if (phot_connected && brb_mode == BRB_SHOW_BRCTRL) {

                SBox b, p;
                getControls (b, p);

                // capture before update
                prev_bpwm = bpwm;

                // set brightness directly from tap location
                if (s.y < b.y)
                    bpwm = user_on;
                else if (s.y > b.y + b.h)
                    bpwm = user_off;
                else
                    bpwm = user_off + (user_on-user_off)*(b.y + b.h - s.y)/b.h;

                // redefine upper or lower range, whichever is closer
                if (phot > (fast_phot_bright+fast_phot_dim)/2) {
                    // change bright end
                    fast_bpwm_bright = bpwm;
                    fast_phot_bright = phot;

                    // persist
                    NVWriteUInt16 (NV_BPWM_BRIGHT, fast_bpwm_bright);
                    NVWriteUInt16 (NV_PHOT_BRIGHT, fast_phot_bright);
                 } else {
                    // change dim end
                    fast_bpwm_dim = bpwm;
                    fast_phot_dim = phot;

                    // persist
                    NVWriteUInt16 (NV_BPWM_DIM, fast_bpwm_dim);
                    NVWriteUInt16 (NV_PHOT_DIM, fast_phot_dim);
                }

                Serial.printf ("BR: bpwm: %d .. %d phot: %d .. %d\n", fast_bpwm_dim, fast_bpwm_bright,
                                        fast_phot_dim, fast_phot_bright);

            }
        #endif // _SUPPORT_PHOT

        if (brb_mode == BRB_SHOW_BRCLOCK)
            changeClockSetting (s);
}


/* return whether s is within the beacon on/off control.
 * if so, rotate brb_mode in prep for next refresh.
 */
bool checkBeaconTouch (SCoord &s)
{
        if (inBox (s, NCDXF_b)) {

            switch (brb_mode) {

            case BRB_SHOW_BEACONS:
                #if defined(_SUPPORT_DSPLY_ONOFF)
                    brb_mode = BRB_SHOW_BRCLOCK;
                #elif defined(_SUPPORT_PHOT)
                    if (phot_connected)
                        brb_mode = BRB_SHOW_BRCTRL;
                    else
                        brb_mode = BRB_SHOW_NOTHING;
                #else
                    brb_mode = BRB_SHOW_NOTHING;
                #endif
                break;

            case BRB_SHOW_BRCLOCK:
                // don't use BRB_SHOW_NOTHING if have BRB_SHOW_BRCLOCK
                #if defined(_SUPPORT_PHOT)
                    if (phot_connected)
                        brb_mode = BRB_SHOW_BRCTRL;
                    else
                        brb_mode = BRB_SHOW_BEACONS;
                #else
                    brb_mode = BRB_SHOW_BEACONS;
                #endif
                break;

            case BRB_SHOW_BRCTRL:
                // don't use BRB_SHOW_NOTHING if have BRB_SHOW_BRCTRL
                brb_mode = BRB_SHOW_BEACONS;
                break;

            case BRB_SHOW_NOTHING:
                brb_mode = BRB_SHOW_BEACONS;
                break;

            }

            NVWriteUInt8 (NV_BRB_MODE, brb_mode);
            return (true);
        }
        return (false);
}

/* if supported and within range: set on/off times from remote command and update display.
 * return whether ok.
 */
bool setDisplayTimes (uint16_t on, uint16_t off)
{
        #if defined(_SUPPORT_DSPLY_ONOFF)

            // check
            if (on >= 24*60 || off >= 24*60)
                return (false);

            // keep
            mins_on = on;
            mins_off = off;
            NVWriteUInt16 (NV_DPYON, mins_on);
            NVWriteUInt16 (NV_DPYOFF, mins_off);

            // show
            brb_mode = BRB_SHOW_BRCLOCK;
            drawClockControls();

            return (true);
        #else

            return (false);

        #endif
}

/* return current clock timers and whether supported
 */
bool getDisplayTimes (uint16_t *on, uint16_t *off)
{
        #if defined(_SUPPORT_DSPLY_ONOFF)
            *on = mins_on;
            *off = mins_off;
            return (true);
        #else
            return (false);
        #endif
}
