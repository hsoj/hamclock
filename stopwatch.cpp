/* implement a simple stopwatch with lap timer and countdown timer.
 */


#include "HamClock.h"

// only RPi can use the GPIO pins for control
#if defined (_IS_RPI)
#define _CAN_GPIO
#endif // _IS_RPI



// LED purpose
typedef enum {
    LED_OFF,
    LED_OK,
    LED_SOON,
    LED_EXPIRED
} LEDPurpose;


/* only RPi can control LEDs and start switch for countdown control
 */
#if defined (_CAN_GPIO)

#include "RPiGPIO.h"

// GPIO (not header pins), low-active
#define RED_GPIO        13
#define GRN_GPIO        19
#define RUN_GPIO        26


static void setLEDPurpose (LEDPurpose c_code)
{
    RPiGPIO& gpio = RPiGPIO::getRPiGPIO();

    gpio.setAsOutput (RED_GPIO);
    gpio.setAsOutput (GRN_GPIO);

    switch (c_code) {
    case LED_OFF:
        gpio.setHi (RED_GPIO);
        gpio.setHi (GRN_GPIO);
        break;
    case LED_OK:
        gpio.setHi (RED_GPIO);
        gpio.setLo (GRN_GPIO);
        break;
    case LED_SOON:
        gpio.setHi (RED_GPIO);
        gpio.setLo (GRN_GPIO);
        break;
    case LED_EXPIRED:
        gpio.setLo (RED_GPIO);
        gpio.setHi (GRN_GPIO);
        break;
    }
}

static bool isCountdownButtonTrue()
{
    RPiGPIO& gpio = RPiGPIO::getRPiGPIO();
    gpio.setAsInput (RUN_GPIO);
    return (gpio.isReady() && !gpio.readPin(RUN_GPIO));
}

#else // !_CAN_GPIO

// dummies
static void setLEDPurpose (LEDPurpose c_code) { (void)c_code; }
static bool isCountdownButtonTrue() { return (false); }

#endif // _CAN_GPIO


uint32_t countdown_period;                      // set from setup, ms

#define SW_NDIG         9               	// number of display digits
#define SW_BG           RA8875_BLACK    	// bg color
#define SW_X0           105             	// x coord of left-most digit
#define SW_GAP          30              	// gap between digits
#define SW_Y0           150             	// upper left Y of all digits
#define SW_W            40              	// digit width
#define SW_H            100             	// digit heigth
#define SW_DOTR         3               	// dot radius
#define SW_SLANT        8               	// pixel slant, bottom to top
#define SW_BAX          240             	// control button A x
#define SW_BBX          440             	// control button B x
#define SW_EXITX        670             	// exit button x
#define SW_EXITY        420             	// exit button y
#define SW_BY           350             	// control button y
#define SW_BW           120             	// button width
#define SW_BH           40              	// button height
#define SW_CX           SW_BAX          	// color scale x
#define SW_CY           SW_EXITY        	// color scale y
#define SW_CW           (SW_BBX+SW_BW-SW_CX)    // color scale width
#define SW_CH           SW_BH              	// color scale height
#define SW_HSV_S        200                     // color scale HSV saturation, 0..255
#define SW_HSV_V        255                     // color scale HSV value, 0..255
#define SW_CD_X         300                     // countdown button x
#define SW_CD_Y         75                      // countdown button y
#define SW_CD_W         200                     // countdown button width
#define CD_WARNING      60000                   // icon warning color time, ms

// always in one of these states
typedef enum {
    SWS_RESET,                          	// showing 0, ready to run
    SWS_RUN,                            	// running, can Stop or Lap
    SWS_STOP,                           	// holding time, can run or reset
    SWS_LAP,                            	// hold time, can resume or reset
    SWS_COUNTDOWN,                            	// counting down
} SW_State;
static SW_State sw_state;

/* define which segments are used for each of the 10 digits as bit map
 *      0
 *    1   2
 *      3
 *    4   5
 *      6
 */
static const uint8_t segments[10] = {
    0x77, 0x24, 0x5D, 0x6D, 0x2E, 0x6B, 0x7B, 0x25, 0x7F, 0x2F
};

// which segments are already on in each digit position (same bit layout as above)
static uint8_t digits[SW_NDIG];

// millis() at start or stop, when counting down start_t is when begun
static uint32_t start_t, stop_t;

// controls
static SBox countdown_b = {SW_CD_X, SW_CD_Y, SW_CD_W, SW_BH};
static SBox A_b = {SW_BAX, SW_BY, SW_BW, SW_BH};
static SBox B_b = {SW_BBX, SW_BY, SW_BW, SW_BH};
static SBox exit_b = {SW_EXITX, SW_EXITY, SW_BW, SW_BH};
static SBox color_b = {SW_CX, SW_CY, SW_CW, SW_CH};
static uint8_t sw_hue;                          // hue 0..255
static uint16_t sw_col;                         // color pixel
static bool sw_isup, sw_wasup;                  // whether visible now and previously



/* decide visuals depending on current state:
 *   main_color is the count down text on the Main page (only used if showing Main page and counting down)
 *   led_purpose is the LED state to display
 *   button_state is the Stopwatch Count down button normal or reverse (only used if showing SW page)
 * return whether any changed from previous call.
 */
static bool getVisuals (uint16_t *main_color, LEDPurpose *led_purpose, bool *button_state)
{   
    static uint16_t prev_mc;
    static LEDPurpose prev_lp;
    static bool prev_bs;
    
    // decide current state
    uint16_t mc;
    LEDPurpose lp;
    bool bs;
    if (sw_state == SWS_COUNTDOWN) {
        uint32_t ms_left = getCountdownLeft();
        if (ms_left >= CD_WARNING) {
            // plenty of time
            mc = RA8875_GREEN;
            lp = LED_OK;
            bs = true;
        } else { 
            bool flash_state = (millis()%500) < 250;            // flip at 2 Hz
            if (ms_left > 0) {
                // timeout soon
                mc = RGB565(255,212,112);                       // real YELLOW looks too pale
                lp = flash_state ? LED_SOON : LED_OFF;
                bs = flash_state;
            } else {
                // timed out
                mc = flash_state ? RA8875_RED : RA8875_BLACK;
                lp = flash_state ? LED_EXPIRED : LED_OFF;
                bs = flash_state;
            }
        }
    } else {
        // count down not running
        mc = RA8875_GREEN;
        lp = LED_OFF;
        bs = false;
    }

    // pass back
    *main_color = mc;
    *led_purpose = lp;
    *button_state = bs;
    
    // check if any changed 
    bool new_vis = mc != prev_mc || lp != prev_lp || bs != prev_bs; 

    // persist
    prev_mc = mc; 
    prev_lp = lp; 
    prev_bs = bs; 
    
    // whether any changed
    return (new_vis);
}



/* set sw_col from sw_hue
 */
static void setOurColor()
{
    uint8_t r, g, b;
    hsvtorgb (&r, &g, &b, sw_hue, SW_HSV_S, SW_HSV_V);
    sw_col = RGB565 (r, g, b);
}

/* draw the color control box
 */
static void showColorScale()
{
    // rainbow
    for (uint16_t dx = 0; dx < color_b.w; dx++) {
        uint8_t r, g, b;
        uint8_t h = 255*dx/color_b.w;
        hsvtorgb (&r, &g, &b, h, SW_HSV_S, SW_HSV_V);
        uint16_t c = RGB565 (r, g, b);
        // tft.drawLine (color_b.x + dx, color_b.y, color_b.x + dx, color_b.y + color_b.h, c);
        tft.drawPixel (color_b.x + dx, color_b.y + color_b.h/2, c);
    }

    // mark it
    uint16_t hue_x = color_b.x + sw_hue*color_b.w/255;
    tft.drawLine (hue_x, color_b.y+3*color_b.h/8, hue_x, color_b.y+5*color_b.h/8, RA8875_WHITE);
}

/* draw changed segments of given digit 0-9 in the given position 0..SW_NDIG-1
 */
static void drawDigit (uint8_t position, uint8_t digit)
{
    // coord of upper left corner
    uint16_t x0 = SW_X0 + (SW_W+SW_GAP)*position;

    // get current and desired segment mask in this position then replace now
    uint8_t mask_now = digits[position];
    uint8_t mask_new = segments[digit];
    digits[position] = mask_new;

    // update each changed segment
    uint8_t mask_chg = mask_now ^ mask_new;

    // segment 0
    if (mask_chg & 0x01) {
        uint16_t c = (mask_new & 0x01) ? sw_col : SW_BG;
        tft.drawRect (x0, SW_Y0, SW_W, 2, c);
        // tft.drawLine (x0, SW_Y0, x0+SW_W, SW_Y0, c);
        // tft.drawLine (x0, SW_Y0+1, x0+SW_W, SW_Y0+1, c);
    }

    // segment 1
    if (mask_chg & 0x02) {
        uint16_t c = (mask_new & 0x02) ? sw_col : SW_BG;
        tft.drawLine (x0, SW_Y0, x0-SW_SLANT/2, SW_Y0+SW_H/2, c);
        tft.drawLine (x0+1, SW_Y0, x0-SW_SLANT/2+1, SW_Y0+SW_H/2, c);
    }

    // segment 2
    if (mask_chg & 0x04) {
        uint16_t c = (mask_new & 0x04) ? sw_col : SW_BG;
        tft.drawLine (x0+SW_W, SW_Y0, x0+SW_W-SW_SLANT/2, SW_Y0+SW_H/2, c);
        tft.drawLine (x0+SW_W+1, SW_Y0, x0+SW_W-SW_SLANT/2+1, SW_Y0+SW_H/2, c);
    }

    // segment 3
    if (mask_chg & 0x08) {
        uint16_t c = (mask_new & 0x08) ? sw_col : SW_BG;
        tft.drawRect (x0-SW_SLANT/2, SW_Y0+SW_H/2, SW_W, 2, c);
        // tft.drawLine (x0-SW_SLANT/2, SW_Y0+SW_H/2, x0+SW_W-SW_SLANT/2, SW_Y0+SW_H/2, c);
        // tft.drawLine (x0-SW_SLANT/2, SW_Y0+SW_H/2+1, x0+SW_W-SW_SLANT/2, SW_Y0+SW_H/2+1, c);
    }

    // segment 4
    if (mask_chg & 0x10) {
        uint16_t c = (mask_new & 0x10) ? sw_col : SW_BG;
        tft.drawLine (x0-SW_SLANT/2, SW_Y0+SW_H/2, x0-SW_SLANT, SW_Y0+SW_H, c);
        tft.drawLine (x0-SW_SLANT/2+1, SW_Y0+SW_H/2, x0-SW_SLANT+1, SW_Y0+SW_H, c);
    }

    // segment 5
    if (mask_chg & 0x20) {
        uint16_t c = (mask_new & 0x20) ? sw_col : SW_BG;
        tft.drawLine (x0+SW_W-SW_SLANT/2, SW_Y0+SW_H/2, x0+SW_W-SW_SLANT, SW_Y0+SW_H, c);
        tft.drawLine (x0+SW_W-SW_SLANT/2+1, SW_Y0+SW_H/2, x0+SW_W-SW_SLANT+1, SW_Y0+SW_H, c);
    }

    // segment 6
    if (mask_chg & 0x40) {
        uint16_t c = (mask_new & 0x40) ? sw_col : SW_BG;
        tft.drawRect (x0-SW_SLANT, SW_Y0+SW_H, SW_W, 2, c);
        // tft.drawLine (x0-SW_SLANT, SW_Y0+SW_H, x0+SW_W-SW_SLANT, SW_Y0+SW_H, c);
        // tft.drawLine (x0-SW_SLANT, SW_Y0+SW_H+1, x0+SW_W-SW_SLANT, SW_Y0+SW_H+1, c);
    }

}

/* display the given time value in millis()
 */
static void drawTime(uint32_t t)
{
    t %= (100UL*60UL*60UL*1000UL);                        // SW_NDIG digits

    uint8_t tenhr = t / (10UL*3600UL*1000UL);
    drawDigit (0, tenhr);
    t -= tenhr * (10UL*3600UL*1000UL);

    uint8_t onehr = t / (3600UL*1000UL);
    drawDigit (1, onehr);
    t -= onehr * (3600UL*1000UL);

    uint8_t tenmn = t / (600UL*1000UL);
    drawDigit (2, tenmn);
    t -= tenmn * (600UL*1000UL);

    uint8_t onemn = t / (60UL*1000UL);
    drawDigit (3, onemn);
    t -= onemn * (60UL*1000UL);

    uint8_t tensec = t / (10UL*1000UL);
    drawDigit (4, tensec);
    t -= tensec * (10UL*1000UL);

    uint8_t onesec = t / (1UL*1000UL);
    drawDigit (5, onesec);
    t -= onesec * (1UL*1000UL);

    uint8_t tenthsec = t / (100UL);
    drawDigit (6, tenthsec);
    t -= tenthsec * (100UL);

    uint8_t hundsec = t / (10UL);
    drawDigit (7, hundsec);
    t -= hundsec * (10UL);

    uint8_t thousec = t / (1UL);
    drawDigit (8, thousec);

    // printf ("%d %d : %d %d : %d %d . %d %d %d\n", tenhr, onehr, tenmn, onemn, tensec, onesec, tenthsec, hundsec, thousec);
}

/* check our touch controls, update state
 */
static void checkOurTouch()
{
    SCoord s;
    if (readCalTouch(s) == TT_NONE)
        return;

    if (inBox (s, countdown_b)) {
        // (re)start countdown regardless of current state
        startCountdown(countdown_period);
    
    } else if (inBox (s, A_b)) {
        switch (sw_state) {
        case SWS_RESET:
            // clicked Run
            start_t = millis();
            sw_state = SWS_RUN;
            drawStringInBox ("Stop", A_b, false, sw_col);
            drawStringInBox ("Lap", B_b, false, sw_col);
            break;
        case SWS_RUN:
            // clicked Stop
            sw_state = SWS_STOP;
            stop_t = millis() - start_t;        // capture delta
            drawTime(stop_t);            // sync display perfectly
            drawStringInBox ("Run", A_b, false, sw_col);
            drawStringInBox ("Reset", B_b, false, sw_col);
            break;
        case SWS_STOP:
            // clicked Run
            sw_state = SWS_RUN;
            start_t = millis() - stop_t;        // reinstate delta
            drawStringInBox ("Stop", A_b, false, sw_col);
            drawStringInBox ("Lap", B_b, false, sw_col);
            break;
        case SWS_LAP:
            // clicked Reset
            sw_state = SWS_RESET;
            drawTime(0);
            drawStringInBox ("Run", A_b, false, sw_col);
            drawStringInBox ("Reset", B_b, false, sw_col);
            break;
        case SWS_COUNTDOWN:
            // stop counting down
            sw_state = SWS_RESET;
            drawTime(0);
            drawStringInBox ("Run", A_b, false, sw_col);
            drawStringInBox ("Count down", countdown_b, false, sw_col);
            setLEDPurpose(LED_OFF);
            break;
        }

    } else if (inBox (s, B_b)) {
        switch (sw_state) {
        case SWS_RESET:
            // clicked Reset
            sw_state = SWS_RESET;
            drawTime(0);
            drawStringInBox ("Run", A_b, false, sw_col);
            drawStringInBox ("Reset", B_b, false, sw_col);
            break;
        case SWS_RUN:
            // clicked Lap
            sw_state = SWS_LAP;
            stop_t = millis() - start_t;        // capture delta for init
            drawTime(stop_t);            // sync display perfectly
            drawStringInBox ("Reset", A_b, false, sw_col);
            drawStringInBox ("Resume", B_b, false, sw_col);
            break;
        case SWS_STOP:
            // clicked Reset
            sw_state = SWS_RESET;
            drawTime(0);
            drawStringInBox ("Run", A_b, false, sw_col);
            drawStringInBox ("Reset", B_b, false, sw_col);
            break;
        case SWS_LAP:
            // clicked Resume
            sw_state = SWS_RUN;
            drawStringInBox ("Stop", A_b, false, sw_col);
            drawStringInBox ("Lap", B_b, false, sw_col);
            break;
        case SWS_COUNTDOWN:
            // stop counting down
            sw_state = SWS_RESET;
            drawTime(0);
            drawStringInBox ("Run", A_b, false, sw_col);
            drawStringInBox ("Count down", countdown_b, false, sw_col);
            setLEDPurpose(LED_OFF);
            break;
        }

    } else if (inBox (s, exit_b)) {
        sw_isup = false;

    } else if (inBox (s, color_b)) {
        sw_hue = 255*(s.x - color_b.x)/color_b.w;
        NVWriteUInt8 (NV_SWHUE, sw_hue);
        checkStopwatchTouch(TT_NONE);
    }
}

/* draw remaining count down time and manage the state of the count down button and LED.
 * N.B. we assume sw_state == SWS_COUNTDOWN
 */
static void drawOurCountdownTime()
{
    // always draw remaining time left
    uint32_t ms_left = getCountdownLeft();
    drawTime(ms_left);

    // update state if any change
    uint16_t main_color;
    LEDPurpose led_purpose;
    bool button_state;
    if (getVisuals (&main_color, &led_purpose, &button_state)) {
        drawStringInBox ("Count down", countdown_b, button_state, sw_col);
        setLEDPurpose (led_purpose);
    }
}

/* draw the main page stopwatch icon or count down time remaining in stopwatch_b depending on sw_state.
 * called often, so get out fast if no change unless force
 */
void drawMainPageStopwatch (bool force)
{
    if (sw_state == SWS_COUNTDOWN) {

        // get time remaining
        uint32_t ms_left = getCountdownLeft();

        // check if same second
        static uint32_t prev_sec;
        uint32_t sec = ms_left/1000;
        bool same_sec = sec == prev_sec;
        prev_sec = sec;

        // skip if no change in colors and same second remaining
        uint16_t main_color;
        LEDPurpose led_purpose;
        bool button_state;
        if (!getVisuals (&main_color, &led_purpose, &button_state) && same_sec && !force)
            return;

        // update LED
        setLEDPurpose (led_purpose);

        // break into H:M:S
        ms_left += 500;         // round
        uint8_t hr = ms_left / 3600000;
        ms_left -= hr * 3600000;
        uint8_t mn = ms_left / 60000;
        ms_left -= mn * 60000;
        uint8_t sc = ms_left/1000;

        // format
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        char buf[32];
        if (hr == 0)
            sprintf (buf, "%d:%02d", mn, sc);
        else
            sprintf (buf, "%dh%02d", hr, mn);
        uint16_t cdw = getTextWidth(buf);

        // erase and draw
        tft.fillRect (stopwatch_b.x, stopwatch_b.y, stopwatch_b.w, stopwatch_b.h, RA8875_BLACK);
        tft.setTextColor (main_color);
        tft.setCursor (stopwatch_b.x + (stopwatch_b.w-cdw)/2, stopwatch_b.y+stopwatch_b.h/4);
        tft.print (buf);

    } else if (force) {

        // draw icon

        // erase
        tft.fillRect (stopwatch_b.x, stopwatch_b.y, stopwatch_b.w, stopwatch_b.h, RA8875_BLACK);

        // radius and step
        uint16_t r = 3*stopwatch_b.h/8;
        uint16_t xc = stopwatch_b.x + stopwatch_b.w/2;
        uint16_t yc = stopwatch_b.y + stopwatch_b.h/2;
        uint16_t dx = r*cosf(deg2rad(45)) + 0.5F;

        // watch
        tft.fillCircle (xc, yc, r, GRAY);

        // top stem
        tft.fillRect (xc-1, yc-r-3, 3, 4, GRAY);

        // 2 side stems
        tft.fillCircle (xc-dx, yc-dx-1, 1, GRAY);
        tft.fillCircle (xc+dx, yc-dx-1, 1, GRAY);

        // face
        tft.drawCircle (xc, yc, 3*stopwatch_b.h/10, RA8875_BLACK);

        // hands
        tft.drawLine (xc, yc, xc, yc-3*stopwatch_b.h/11, RA8875_WHITE);
        tft.drawLine (xc, yc, xc+3*stopwatch_b.h/14, yc, RA8875_WHITE);

        // LED off
        setLEDPurpose (LED_OFF);
    }
}


/* stopwatch_b has been touched from Main page.
 * if tapped while counting down just reset and continue main HamClock page, else start new SW page
 */
void checkStopwatchTouch(TouchType tt)
{
    // if tapped the stop watch while counting down, just restart
    if (sw_state == SWS_COUNTDOWN && tt == TT_TAP) {
        startCountdown(countdown_period);
        return;
    }

    // get last color, else set default
    if (!NVReadUInt8 (NV_SWHUE, &sw_hue)) {
        sw_hue = 85;    // green
        NVWriteUInt8 (NV_SWHUE, sw_hue);
    }
    setOurColor();

    // init screen and color
    eraseScreen();
    showColorScale();
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    drawStringInBox ("Exit", exit_b, false, sw_col);
    sw_wasup = false;
    sw_isup = true;

    // init segments all off
    memset (digits, 0, sizeof(digits));

    // draw punctuation
    tft.fillCircle (SW_X0 + 2*SW_W + SW_GAP + SW_GAP/2 - SW_SLANT/3, SW_Y0 + SW_H/3, SW_DOTR, sw_col);
    tft.fillCircle (SW_X0 + 2*SW_W + SW_GAP + SW_GAP/2 - 2*SW_SLANT/3, SW_Y0 + 2*SW_H/3, SW_DOTR, sw_col);
    tft.fillCircle (SW_X0 + 4*SW_W + 3*SW_GAP + SW_GAP/2 - SW_SLANT/3, SW_Y0 + SW_H/3, SW_DOTR, sw_col);
    tft.fillCircle (SW_X0 + 4*SW_W + 3*SW_GAP + SW_GAP/2 - 2*SW_SLANT/3, SW_Y0 + 2*SW_H/3, SW_DOTR, sw_col);
    tft.fillCircle (SW_X0 + 6*SW_W + 5*SW_GAP + SW_GAP/2 - SW_SLANT, SW_Y0 + SW_H, SW_DOTR, sw_col);

    // set buttons from state
    switch (sw_state) {
    case SWS_RESET:
        start_t = millis();
        drawTime(0);
        drawStringInBox ("Count down", countdown_b, false, sw_col);
        drawStringInBox ("Run", A_b, false, sw_col);
        drawStringInBox ("Reset", B_b, false, sw_col);
        break;
    case SWS_RUN:
        drawStringInBox ("Count down", countdown_b, false, sw_col);
        drawStringInBox ("Stop", A_b, false, sw_col);
        drawStringInBox ("Lap", B_b, false, sw_col);
        break;
    case SWS_STOP:
        drawTime(stop_t);        // resinstate stopped time
        drawStringInBox ("Count down", countdown_b, false, sw_col);
        drawStringInBox ("Run", A_b, false, sw_col);
        drawStringInBox ("Reset", B_b, false, sw_col);
        break;
    case SWS_LAP:
        drawStringInBox ("Count down", countdown_b, false, sw_col);
        drawTime(stop_t);        // reinstate lap hold
        drawStringInBox ("Reset", A_b, false, sw_col);
        drawStringInBox ("Resume", B_b, false, sw_col);
        break;
    case SWS_COUNTDOWN:
        drawOurCountdownTime();
        drawStringInBox ("Count down", countdown_b, true, sw_col);
        drawStringInBox ("Reset", A_b, false, sw_col);
        drawStringInBox ("Reset", B_b, false, sw_col);
        break;
    }
}

/* run another iteration of the stop watch.
 * we are either not running at all, running but our page is not visible, or we are fully visible.
 * get out fast if not running.
 * we also manage a few other subsystems when transitioning visibility.
 * return whether we are visible now.
 */
bool runStopwatch()
{

    // always check switch
    if (isCountdownButtonTrue())
        startCountdown(countdown_period);

    if (sw_isup) {

        // stopwatch page is up

        resetWatchdog();

        // close down other systems if just coming up
        if (!sw_wasup) {
            Serial.println(F("Starting stopwatch"));
            closeDXCluster();       // prevent inbound msgs from clogging network
            closeGimbal();          // avoid dangling connection
            sw_wasup = true;
        }

        // check for our button taps, updates state so might close
        checkOurTouch();
        if (!sw_isup) {
            Serial.println(F("Exiting stopwatch"));
            sw_wasup = true;
            sw_isup = false;
            initScreen();
            return (false);
        }

        // update time display if running up or down
        if (sw_state == SWS_RUN) {

            drawTime(millis() - start_t);

            #if defined(_USE_DESKTOP)
            // push updated drawing immediately
            tft.setCursor (50,50);
            tft.setTextColor(SW_BG);
            tft.print ('x');
            #endif

        } else if (sw_state == SWS_COUNTDOWN) {

            drawOurCountdownTime();
        }

    } else if (sw_state == SWS_COUNTDOWN) {

        // main page is up and count down is running

        drawMainPageStopwatch (false);

    }

    return (sw_isup);
}

/* start the countdown timer with the given number of millis.
 * can be called any time regardless of Main page, our page or other
 */
void startCountdown(uint32_t ms)
{
    sw_state = SWS_COUNTDOWN;
    countdown_period = ms;
    start_t = millis();

    if (sw_isup) {
        drawStringInBox ("Count down", countdown_b, true, sw_col);
        drawStringInBox ("Reset", A_b, false, sw_col);
        drawStringInBox ("Reset", B_b, false, sw_col);
    }
}

/* return ms countdown time remaining, if any
 */
uint32_t getCountdownLeft()
{
    if (sw_state == SWS_COUNTDOWN) {
        uint32_t since_start = millis() - start_t;
        if (since_start < countdown_period)
            return (countdown_period - since_start);
    }
    return (0);
}
