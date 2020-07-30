/* handle each plotting area.
 */


#include "HamClock.h"

#define	BORDER_COLOR	GRAY
#define TICKLEN		2			// length of plot tickmarks, pixels
#define	LGAP		21			// left gap for labels
#define	BGAP		15			// bottom gap for labels
#define	FONTW		6			// font width with gap
#define	FONTH		8			// font height
#define	NBRGAP		15			// large plot overlay number top gap

static int tickmarks (float min, float max, int numdiv, float ticks[]);

/* plot the given data within the given box.
 * return whether had anything to plot.
 * N.B. if both labels are NULL, use same labels and limits as previous call
 */
bool plotXY (const SBox &box, float x[], float y[], int nxy, const char *xlabel, const char *ylabel,
uint16_t color, float label_value)
{
	char buf[32];
        sprintf (buf, "%.*f", label_value >= 1000 ? 0 : 1, label_value);
	return (plotXYstr (box, x, y, nxy, xlabel, ylabel, color, buf));
}

/* same as plotXY but label is a string
 */
bool plotXYstr (const SBox &box, float x[], float y[], int nxy, const char *xlabel, const char *ylabel,
uint16_t color, char *label_str)
{
    resetWatchdog();

    // no labels implies overlay previous plot
    bool overlay = xlabel == NULL && ylabel == NULL;

    // persistent scale info in case of subsequent overlay
#   define MAXTICKS	10
    static float xticks[MAXTICKS+2], yticks[MAXTICKS+2];
    static uint8_t nxt, nyt;
    static float minx, maxx;
    static float miny, maxy;
    static float dx, dy;

    char buf[32];
    uint8_t bufl;

    // set font and color
    selectFontStyle (BOLD_FONT, FAST_FONT);
    tft.setTextColor(color);

    // report if no data
    if (nxy < 1 || !x || !y) {
	plotMessage (box, color, "No data");
	return (false);
    }

    // find new limits unless this is an overlay
    if (!overlay) {

	// find data extrema
	minx = x[0]; maxx = x[0];
	miny = y[0]; maxy = y[0];
	for (int i = 1; i < nxy; i++) {
	    if (x[i] > maxx) maxx = x[i];
	    if (x[i] < minx) minx = x[i];
	    if (y[i] > maxy) maxy = y[i];
	    if (y[i] < miny) miny = y[i];
	}
	minx = floor(minx);
	maxx = ceil(maxx);
	if (maxx < minx + 1) {
	    minx -= 1;
	    maxx += 1;
	}
	miny = floor(miny);
	maxy = ceil(maxy);
	if (maxy < miny + 1) {
	    miny -= 1;
	    maxy += 1;
	}

	// find tickmarks
	nxt = tickmarks (minx, maxx, MAXTICKS, xticks);
	nyt = tickmarks (miny, maxy, MAXTICKS, yticks);

        // handy ends
	minx = xticks[0];
	maxx = xticks[nxt-1];
	miny = yticks[0];
	maxy = yticks[nyt-1];
	dx = maxx-minx;
	dy = maxy-miny;

	// erase all except bottom line which is map border
	tft.fillRect (box.x, box.y, box.w, box.h-1, RA8875_BLACK);

	// y tickmarks just to the left of the plot
	bufl = sprintf (buf, "%d", (int)maxy);
	tft.setCursor (box.x+LGAP-TICKLEN-bufl*FONTW, box.y); tft.print (buf);
	bufl = sprintf (buf, "%d", (int)miny);
	tft.setCursor (box.x+LGAP-TICKLEN-bufl*FONTW, box.y+box.h-BGAP-FONTH); tft.print (buf);
	for (int i = 0; i < nyt; i++) {
	    uint16_t ty = (uint16_t)(box.y + (box.h-BGAP)*(1 - (yticks[i]-miny)/dy) + 0.5);
	    tft.drawLine (box.x+LGAP-TICKLEN, ty, box.x+LGAP, ty, color);
	}

	// y label is down the left side
	uint8_t ylen = strlen(ylabel);
	uint16_t ly0 = box.y + (box.h - BGAP - ylen*FONTH)/2;
	for (uint8_t i = 0; i < ylen; i++) {
	    tft.setCursor (box.x+LGAP/3, ly0+i*FONTH);
	    tft.print (ylabel[i]);
	}

	// x tickmarks just below plot
        uint16_t txty = box.y+box.h-FONTH-2;
	tft.setCursor (box.x+LGAP, txty); tft.print (minx,0);
	bufl = sprintf (buf, "%c%d", maxx > 0 ? '+' : ' ', (int)maxx);
	tft.setCursor (box.x+box.w-2-bufl*FONTW, txty); tft.print (buf);
	for (int i = 0; i < nxt; i++) {
	    uint16_t tx = (uint16_t)(box.x+LGAP + (box.w-LGAP-1)*(xticks[i]-minx)/dx);
	    tft.drawLine (tx, box.y+box.h-BGAP, tx, box.y+box.h-BGAP+TICKLEN, color);
	}

        // always label 0 if within larger range
        if (minx < 0 && maxx > 0) {
	    uint16_t zx = (uint16_t)(box.x+LGAP + (box.w-LGAP)*(0-minx)/dx + 0.5);
            tft.setCursor (zx-FONTW/2, txty); tft.print (0);
        }

	// x label is centered about the plot across the bottom
	uint8_t xlen = strlen(xlabel);
	uint16_t lx0 = box.x + LGAP + (box.w - LGAP - xlen*FONTW)/2;
	for (uint8_t i = 0; i < xlen; i++) {
	    tft.setCursor (lx0+i*FONTW, box.y+box.h-FONTH-2);
	    tft.print (xlabel[i]);
	}

    }

    // draw plot
    uint16_t last_px = 0, last_py = 0;
    resetWatchdog();
    for (int i = 0; i < nxy; i++) {
	uint16_t px = (uint16_t)(box.x+LGAP + (box.w-LGAP-1)*(x[i]-minx)/dx + 0.5);
	uint16_t py = (uint16_t)(box.y + 1 + (box.h-BGAP-2)*(1 - (y[i]-miny)/dy) + 0.5);
	if (i > 0 && (last_px != px || last_py != py))
	    tft.drawLine (last_px, last_py, px, py, color);		// avoid bug with 0-length lines
	else if (nxy == 1)
	    tft.drawLine (box.x+LGAP, py, box.x+box.w-1, py, color);	// one point clear across
	last_px = px;
	last_py = py;
    }

    // draw plot border
    tft.drawRect (box.x+LGAP, box.y, box.w-LGAP, box.h-BGAP, BORDER_COLOR);

    if (!overlay) {

	// overlay large center value on top in gray
	tft.setTextColor(BRGRAY);
	selectFontStyle (BOLD_FONT, LARGE_FONT);
	uint16_t bw, bh;
	getTextBounds (label_str, &bw, &bh);
	uint16_t text_x = box.x+LGAP+(box.w-LGAP-bw)/2;
	uint16_t text_y = box.y+NBRGAP+bh;
	tft.setCursor (text_x, text_y);
	tft.print (label_str);
    }

    // printFreeHeap (F("plotXYstr"));

    // ok
    return (true);
}

/* plot values of geomagnetic Kp index in boxy form in box b.
 * 8 values per day, nhkp historical followed by npkp predicted.
 * ala http://www.swpc.noaa.gov/products/planetary-k-index
 */
void plotKp (SBox &box, uint8_t kp[], uint8_t nhkp, uint8_t npkp, uint16_t color)
{
    resetWatchdog();

#   define	MAXKP	9
    // N.B. null font origin is upper left
    selectFontStyle (BOLD_FONT, FAST_FONT);
    tft.setTextColor(color);

    // erase all except bottom line which is map border
    tft.fillRect (box.x, box.y, box.w, box.h-1, RA8875_BLACK);

    // y tickmarks just to the left of the plot
    tft.setCursor (box.x+LGAP-TICKLEN-2-FONTW, box.y); tft.print (MAXKP);
    tft.setCursor (box.x+LGAP-TICKLEN-2-FONTW, box.y+box.h-BGAP-FONTH); tft.print (0);
    for (uint8_t k = 0; k <= MAXKP; k++) {
	uint16_t h = k*(box.h-BGAP)/MAXKP;
	uint16_t ty = box.y + box.h - BGAP - h;
	tft.drawLine (box.x+LGAP-TICKLEN, ty, box.x+LGAP, ty, color);
    }

    // y label is down the left side
    static const char ylabel[] = "Planetary Kp";
    uint8_t ylen = sizeof(ylabel)-1;
    uint16_t ly0 = box.y + (box.h - BGAP - ylen*FONTH)/2;
    for (uint8_t i = 0; i < ylen; i++) {
	tft.setCursor (box.x+LGAP/3, ly0+i*FONTH);
	tft.print (ylabel[i]);
    }

    // x labels
    uint8_t nkp = nhkp + npkp;
    tft.setCursor (box.x+LGAP, box.y+box.h-FONTH-2);
    tft.print (-nhkp/8);
    tft.setCursor (box.x+box.w-2*FONTW, box.y+box.h-FONTH-2);
    tft.print('+'); tft.print (npkp/8);
    tft.setCursor (box.x+LGAP+(box.w-LGAP)/2-2*FONTW, box.y+box.h-FONTH-2);
    tft.print (F("Days"));

    // label now if within wider range
    if (nhkp > 0 && npkp > 0) {
	uint16_t zx = box.x + LGAP + nhkp*(box.w-LGAP)/nkp;
        tft.setCursor (zx-FONTW/2, box.y+box.h-FONTH-2);
        tft.print(0);
    }

    // x ticks
    for (uint8_t i = 0; i < nkp/8; i++) {
	uint16_t tx = box.x + LGAP + 8*i*(box.w-LGAP)/nkp;
	tft.drawLine (tx, box.y+box.h-BGAP, tx, box.y+box.h-BGAP+TICKLEN, color);
    }

    // plot Kp values as colored vertical bars depending on strength
    resetWatchdog();
    for (uint8_t i = 0; i < nkp; i++) {
	int8_t k = kp[i];
	uint16_t c = k < 4 ? RA8875_GREEN : k == 4 ? RA8875_YELLOW : RA8875_RED;
	uint16_t x = box.x + LGAP + i*(box.w-LGAP)/nkp;
	uint16_t w = (box.w-LGAP)/nkp-1;
	uint16_t h = k*(box.h-BGAP)/MAXKP;
	uint16_t y = box.y + box.h - BGAP - h;
	if (w > 0 || h > 0)
	    tft.fillRect (x, y, w, h, c);
    }

    // data border
    tft.drawRect (box.x+LGAP, box.y, box.w-LGAP, box.h-BGAP, BORDER_COLOR);

    // overlay large current value on top in gray
    tft.setTextColor(BRGRAY);
    selectFontStyle (BOLD_FONT, LARGE_FONT);
    char buf[32];
    sprintf (buf, "%d", kp[nhkp-1]);
    uint16_t bw, bh;
    getTextBounds (buf, &bw, &bh);
    uint16_t text_x = box.x+LGAP+(box.w-LGAP-bw)/2;
    uint16_t text_y = box.y+NBRGAP+bh;
    tft.setCursor (text_x, text_y);
    tft.print (buf);

    // printFreeHeap (F("plotKp"));
}

/* shorten str IN PLACE as needed to be less that maxw pixels wide.
 * return final width in pixels.
 */
static uint16_t maxStringW (char *str, uint16_t maxw)
{
    uint8_t strl = strlen (str);
    uint16_t bw;

    while ((bw = getTextWidth(str)) > maxw)
	str[strl--] = '\0';

    return (bw);
}

/* print weather info in the given box
 */
void plotWX (const SBox &box, uint16_t color, const WXInfo &wi)
{
    resetWatchdog();

    // erase all except bottom line which is map border then add border
    tft.fillRect (box.x, box.y, box.w, box.h-1, RA8875_BLACK);
    tft.drawRect (box.x, box.y, box.w, box.h, GRAY);

    const uint8_t indent = FONTW+2;	// allow for attribution
    uint16_t dy = box.h/3;
    uint16_t ddy = box.h/5;
    float f;
    char buf[32];
    uint16_t w;

    // large temperature with degree symbol and units
    tft.setTextColor(color);
    selectFontStyle (BOLD_FONT, LARGE_FONT);
    f = useMetricUnits() ? wi.temperature_c : 9*wi.temperature_c/5+32;
    sprintf (buf, "%.0f %c", f, useMetricUnits() ? 'C' : 'F');
    w = maxStringW (buf, box.w-indent);
    tft.setCursor (box.x+indent+(box.w-indent-w)/2-8, box.y+dy);
    tft.print(buf);
    uint16_t bw, bh;
    getTextBounds (buf+strlen(buf)-2, &bw, &bh);
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    tft.setCursor (tft.getCursorX()-bw, tft.getCursorY()-2*bh/3);
    tft.print('o');
    dy += ddy;


    // remaining info smaller
    selectFontStyle (LIGHT_FONT, SMALL_FONT);

    // humidity
    sprintf (buf, "%.0f%% RH", wi.humidity_percent);
    w = maxStringW (buf, box.w-indent);
    tft.setCursor (box.x+indent+(box.w-indent-w)/2, box.y+dy);
    tft.print (buf);
    dy += ddy;

    // wind
    f = (useMetricUnits() ? 3.6 : 2.237) * wi.wind_speed_mps; // kph or mph
    sprintf (buf, "%s @ %.0f %s", wi.wind_dir_name, f, useMetricUnits() ? "kph" : "mph");
    w = maxStringW (buf, box.w-indent);
    tft.setCursor (box.x+indent+(box.w-indent-w)/2, box.y+dy);
    tft.print (buf);
    dy += ddy;

    // nominal conditions
    strcpy (buf, wi.conditions);
    w = maxStringW (buf, box.w-indent);
    tft.setCursor (box.x+indent+(box.w-indent-w)/2, box.y+dy);
    tft.print(buf);

    // attribution very small down the left side
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    uint8_t ylen = strlen(wi.attribution);
    uint16_t ly0 = box.y + (box.h - ylen*FONTH)/2;
    for (uint8_t i = 0; i < ylen; i++) {
	tft.setCursor (box.x+2, ly0+i*FONTH);
	tft.print (wi.attribution[i]);
    }

    // printFreeHeap (F("plotWX"));
}

/* plotBandConditions helper to plot one band condition
 */
static void BChelper (uint8_t band, uint16_t b_col, uint16_t r_col, uint16_t y, float rel)
{
    #define RELCOL(r)       ((r) < 0.33 ? RA8875_RED : ((r) < 0.66 ? RA8875_YELLOW : RA8875_GREEN))

    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor(GRAY);
    tft.setCursor (b_col, y);
    tft.print(band);

    tft.setTextColor(RELCOL(rel));
    tft.setCursor (r_col, y);

    if (rel > 0.99F)
        rel = 0.99F;             // 100 doesn't fit
    char relbuf[10];
    if (band == 80)
        snprintf (relbuf, sizeof(relbuf), "%2.0f%%", 100*rel);
    else
        snprintf (relbuf, sizeof(relbuf), "%3.0f", 100*rel);
    tft.print(relbuf);
}

/* print the band conditions in the given box.
 * response is CSV short-path reliability 80-10.
 * if response does not match expected format print it as an error message.
 */
bool plotBandConditions (const SBox &box, char response[], char config[])
{
    resetWatchdog();

    // erase all then draw border
    tft.fillRect (box.x, box.y, box.w, box.h, RA8875_BLACK);
    tft.drawRect (box.x, box.y, box.w, box.h, GRAY);

    // crack
    float bc80, bc60, bc40, bc30, bc20, bc17, bc15, bc12, bc10;
    if (sscanf (response, "%f,%f,%f,%f,%f,%f,%f,%f,%f",
                &bc80, &bc60, &bc40, &bc30, &bc20, &bc17, &bc15, &bc12, &bc10) != 9) {
        plotMessage (box, RA8875_RED, response);
        return (false);
    }

    // title
    tft.setTextColor(RA8875_WHITE);
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    uint16_t h = box.h/5-2;                             // text row height
    char *title = (char *) "VOACAP DE-DX";
    uint16_t bw = getTextWidth (title);
    tft.setCursor (box.x+(box.w-bw)/2, box.y+h);
    tft.print (title);

    // column locations
    uint16_t bcol1 = box.x + 7;                         // band column 1
    uint16_t rcol1 = bcol1 + box.w/4-10;                // path reliability column 1
    uint16_t bcol2 = rcol1 + box.w/4+14;                // band column 2
    uint16_t rcol2 = bcol2 + box.w/4-10;                // path reliability column 2
    uint16_t y = box.y + 2*h;                           // text y

    // 8 bands, skip 60m
    BChelper (80, bcol1, rcol1, y, bc80);
    BChelper (40, bcol1, rcol1, y+h, bc40);
    BChelper (30, bcol1, rcol1, y+2*h, bc30);
    BChelper (20, bcol1, rcol1, y+3*h, bc20);

    BChelper (17, bcol2, rcol2, y, bc17);
    BChelper (15, bcol2, rcol2, y+h, bc15);
    BChelper (12, bcol2, rcol2, y+2*h, bc12);
    BChelper (10, bcol2, rcol2, y+3*h, bc10);

    // notes
    tft.setTextColor(GRAY);
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    bw = maxStringW (config, box.w);
    tft.setCursor (box.x+(box.w-bw)/2, box.y+box.h-10); // beware comma descender
    tft.print (config);

    // printFreeHeap (F("plotBC"));

    // ok
    return (true);
}

/* print a message in a box, take care not to go outside
 */
void plotMessage (const SBox &box, uint16_t color, const char *message)
{
    // prep font
    selectFontStyle (BOLD_FONT, FAST_FONT);
    tft.setTextColor(color);

    // make a copy so we can use maxStringW
    int ml = strlen(message);
    char msg_cpy[ml+1];
    strcpy (msg_cpy, message);
    uint16_t msgw = maxStringW (msg_cpy, box.w-2);

    // draw centered
    resetWatchdog();
    tft.fillRect (box.x, box.y, box.w+1, box.h-1, RA8875_BLACK);
    tft.setCursor (box.x+(box.w-msgw)/2, box.y+box.h/2-FONTH);
    tft.print(msg_cpy);

    // check for up to one more line
    int cl = strlen (msg_cpy);
    if (cl < ml) {
        strcpy (msg_cpy, message+cl);
        msgw = maxStringW (msg_cpy, box.w-2);
        tft.setCursor (box.x+(box.w-msgw)/2, box.y+box.h/2+2);
        tft.print(msg_cpy);
    }
}

/* given min and max and an approximate number of divisions desired,
 * fill in ticks[] with nicely spaced values and return how many.
 * N.B. return value, and hence number of entries to ticks[], might be as
 *   much as 2 more than numdiv.
 */
static int
tickmarks (float min, float max, int numdiv, float ticks[])
{
    static int factor[] = { 1, 2, 5 };
#   define NFACTOR    (sizeof(factor)/sizeof(factor[0]))
    float minscale;
    float delta;
    float lo;
    float v;
    int n;

    minscale = fabs(max - min);

    if (minscale == 0) {
	/* null range: return ticks in range min-1 .. min+1 */
	for (n = 0; n < numdiv; n++)
	    ticks[n] = min - 1.0 + n*2.0/numdiv;
	return (numdiv);
    }

    delta = minscale/numdiv;
    for (n=0; n < (int)NFACTOR; n++) {
	float scale;
	float x = delta/factor[n];
	if ((scale = (powf(10.0F, ceilf(log10f(x)))*factor[n])) < minscale)
	    minscale = scale;
    }
    delta = minscale;

    lo = floor(min/delta);
    for (n = 0; (v = delta*(lo+n)) < max+delta; )
	ticks[n++] = v;

    return (n);
}
