/* handle the initial setup screen.
 */

#include <ctype.h>

#include "HamClock.h"

/* defaults
 */
#define DEF_SSID        "FiOS-9QRT4-Guest"
#define DEF_PASS        "Veritium2017"
#define DEF_CALL        "WB0OEW"

// feature tests.

// ESP always needs wifi setup, rpi is up to user, others never
#if defined(_IS_ESP8266)
#define _WIFI_ALWAYS
#elif defined(_IS_RPI)
#include <string.h>
#include <errno.h>
#define _WIFI_ASK
#else
#define _WIFI_NEVER
#endif

// Flip screen only on ESP
#if defined(_IS_ESP8266)
#define _SUPPORT_FLIP
#endif

// kx3 on ESP or rpi
#if defined(_IS_ESP8266) || defined(_IS_RPI)
#define _SUPPORT_KX3
#endif

// temp sensor on ESP or Pi
#if defined(_IS_ESP8266) || defined(_IS_RPI)
#define _SUPPORT_ENVSENSOR
#endif


// debug: force all on just for visual testing
// #define _VIS_TEST
#ifdef _VIS_TEST
#undef _WIFI_NEVER
#undef _WIFI_ASK
#define _WIFI_ALWAYS
#define _SUPPORT_FLIP
#define _SUPPORT_KX3 
#define _SUPPORT_ENVSENSOR
#define _SUPPORT_BRCTRL
#endif // _VIS_TEST


// static storage for published setup items
static char ssid[NV_WIFI_SSID_LEN];
static char pass[NV_WIFI_PW_LEN];
static char call[NV_CALLSIGN_LEN];
static char dxhost[NV_DXHOST_LEN];
static char gpsdhost[NV_GPSDHOST_LEN];
static uint8_t bright_min, bright_max;
static uint16_t dxport;
static float temp_corr;
static float pres_corr;


// layout constants
#define NQR             4                       // number of virtual keyboard rows
#define NQC             13                      // max number of keyboard columns
#define KB_NCOLS        14                      // n cols in keyboard layout
#define KB_CHAR_H       60                      // height of box containing 1 keyboard character
#define KB_CHAR_W       59                      // width "
#define KB_SPC_Y        (KB_Y0+NQR*KB_CHAR_H)   // top edge of special keyboard chars
#define KB_SPC_H        40                      // heights of special keyboard chars
#define KB_INDENT       16                      // keyboard indent
#define SBAR_X          (KB_INDENT+3*KB_CHAR_W/2)// space bar x coord
#define SBAR_W          (KB_CHAR_W*10)          // space bar width
#define F_DESCENT       5                       // font descent below baseline
#define F_INDENT        20                      // font indent within square
#define KB_Y0           190                     // y coord of keyboard top
#define PR_W            23                      // width of character
#define PR_A            24                      // ascending height above font baseline
#define PR_D            9                       // descending height below font baseline
#define PR_H            (PR_A+PR_D)             // prompt height
#define ASK_TO          10                      // user option timeout, seconds
#define PAGE_W          120                     // page button width
#define CURSOR_DROP     2                       // pixels to drop cursor rot

// colors
#define TX_C            RA8875_WHITE            // text color
#define BG_C            RA8875_BLACK            // overall background color
#define KB_C            RGB565(80,80,255)       // key border color
#define KF_C            RA8875_WHITE            // key face color
#define PR_C            RGB565(255,125,0)       // prompt color
#define DEL_C           RA8875_RED              // Delete color
#define DONE_C          RA8875_GREEN            // Done color
#define BUTTON_C        RA8875_CYAN             // option buttons color
#define CURSOR_C        RA8875_GREEN            // cursor color
#define ERR_C           RA8875_RED              // err msg color


// macro given row index return screen y
#define R2Y(r)        ((r)*(PR_H+2))


// page management
#define N_PAGES         2
static uint8_t cur_page;                        // 0 .. N_PAGES-1


// define a string prompt
typedef struct {
    uint8_t page;                               // 0 .. N_PAGES-1
    SBox p_box;		                        // prompt box
    SBox v_box;		                        // value box
    const char *p_str;	                        // prompt string
    char *v_str;	                        // value string
    uint8_t v_len;	                        // max size of v_str, including EOS
    uint16_t v_cx;                              // x coord of value string cursor
} StringPrompt;


// N.B. must match string_pr[] order
typedef enum {
    CALL_SPR,
    LAT_SPR,
    LNG_SPR,
    GPSD_SPR,
    SSID_SPR,
    PASS_SPR,
    DXHOST_SPR,
    DXPORT_SPR,
    COUNTDOWN_SPR,
    TEMPCORR_SPR,
    PRESCORR_SPR,
    BRMIN_SPR,
    BRMAX_SPR,
    N_SPR
} SPIds; 

// string prompts for each page. N.B. must match SPIds order
static StringPrompt string_pr[N_SPR] = {

    // page 1

    {0, {10,  R2Y(0), 50, PR_H},  {110, R2Y(0), 270, PR_H}, "Call:",   call, NV_CALLSIGN_LEN, 0}, 
    {0, {380, R2Y(0), 90, PR_H},  {480, R2Y(0), 110, PR_H}, "DE Lat:", NULL, 0, 0},             // shadowed
    {0, {590, R2Y(0), 70, PR_H},  {670, R2Y(0), 129, PR_H}, "Long:",   NULL, 0, 0},             // shadowed

    {0, {460, R2Y(1), 80, PR_H},  {530, R2Y(1), 270, PR_H}, "host:", gpsdhost, NV_GPSDHOST_LEN, 0},

    {0, {110, R2Y(2), 65, PR_H},  {180, R2Y(2), 610, PR_H}, "SSID:", ssid, NV_WIFI_SSID_LEN, 0},
    {0, {110, R2Y(3), 65, PR_H},  {180, R2Y(3), 610, PR_H}, "Pass:", pass, NV_WIFI_PW_LEN, 0},

    // page 2

    {1, {110, R2Y(0), 150, PR_H}, {265, R2Y(0), 330, PR_H}, "Spider host:", dxhost, NV_DXHOST_LEN, 0},
    {1, {595, R2Y(0), 70, PR_H},  {670, R2Y(0), 100, PR_H}, "port:", NULL, 0, 0},               // shadowed

    {1, {355, R2Y(1), 230, PR_H}, {455, R2Y(1), 100, PR_H}, "CntDn:", NULL, 0, 0},              // shadowed

    {1, {10,  R2Y(2), 50, PR_H},  {110, R2Y(2), 100, PR_H}, "dTemp:", NULL, 0, 0},              // shadowed
    {1, {355, R2Y(2), 50, PR_H},  {455, R2Y(2), 100, PR_H}, "dPres:", NULL, 0, 0},              // shadowed

    {1, {10,  R2Y(3), 50, PR_H},  {110, R2Y(3), 100, PR_H}, "brMin:", NULL, 0, 0},              // shadowed
    {1, {355, R2Y(3), 50, PR_H},  {455, R2Y(3), 100, PR_H}, "brMax:", NULL, 0, 0},              // shadowed

};



// define a boolean prompt
typedef struct {
    uint8_t page;                               // 0 .. N_PAGES-1
    SBox p_box;		                        // prompt box
    SBox s_box;		                        // state box, if t/f_str
    bool state;		                        // on or off
    const char *p_str;	                        // prompt string
    const char *f_str;                          // "false" string, or NULL
    const char *t_str;	                        // "true" string, or NULL
} BoolPrompt;


// N.B. must match bool_pr[] order
typedef enum {
    GEOIP_BPR,
    GPSD_BPR,
    WIFI_BPR,
    CLUSTER_BPR,
    UNITS_BPR,
    KX3ON_BPR,
    KX3BAUD_BPR,
    FLIP_BPR,
    N_BPR
} BPIds;

// bool prompts. N.B. must match BPIds order
static BoolPrompt bool_pr[N_BPR] = {

    // page 1

    {0, {10,  R2Y(1), 160, PR_H},  {190, R2Y(1), 50, PR_H},  false, "IP Geolocate?", "No", "Yes"},
    {0, {380, R2Y(1),  80, PR_H},  {460, R2Y(1), 50, PR_H},  false, "gpsd?", "No", NULL},
    {0, {10,  R2Y(2),  70, PR_H},  {110, R2Y(2), 50, PR_H},  false, "WiFi?", "No", NULL},

    // page 2

    {1, {10,  R2Y(0), 99, PR_H},   {110, R2Y(0), 110, PR_H}, false, "Cluster?", "No", NULL},
    {1, {10,  R2Y(1), 80, PR_H},   {110, R2Y(1), 110, PR_H}, false, "Units?", "Imperial", "Metric"},

    {1, {10,  R2Y(4), 80, PR_H},   {110, R2Y(4), 110, PR_H}, false, "KX3?", "No", NULL},        // on/off
    {1, {110, R2Y(4), 80, PR_H},   {190, R2Y(4), 110, PR_H}, false, "baud:", "4800", "38400"},  // baud
    {1, {355, R2Y(4), 140, PR_H},  {455, R2Y(4), 50, PR_H},  false, "Flip?", "No", "Yes"},
};



// store info about a given focus field
typedef struct {
    // N.B. always one, the other NULL
    StringPrompt *sp;
    BoolPrompt *bp;
} Focus;

// current focus
static Focus cur_focus;



// virtual qwerty keyboard
typedef struct {
    char n, s;                                  // normal and shifted char
} Key;
static const Key qwerty[NQR][NQC] PROGMEM = {
    { {'`', '~'}, {'1', '!'}, {'2', '@'}, {'3', '#'}, {'4', '$'}, {'5', '%'}, {'6', '^'},
      {'7', '&'}, {'8', '*'}, {'9', '('}, {'0', ')'}, {'-', '_'}, {'=', '+'}
    },
    { {'Q', 'q'}, {'W', 'w'}, {'E', 'e'}, {'R', 'r'}, {'T', 't'}, {'Y', 'y'}, {'U', 'u'},
      {'I', 'i'}, {'O', 'o'}, {'P', 'p'}, {'[', '{'}, {']', '}'}, {'\\', '|'},
    },
    { {'A', 'a'}, {'S', 's'}, {'D', 'd'}, {'F', 'f'}, {'G', 'g'}, {'H', 'h'}, {'J', 'j'},
      {'K', 'k'}, {'L', 'l'}, {';', ':'}, {'\'', '"'},
    },
    { {'Z', 'z'}, {'X', 'x'}, {'C', 'c'}, {'V', 'v'}, {'B', 'b'}, {'N', 'n'}, {'M', 'm'},
      {',', '<'}, {'.', '>'}, {'/', '?'},
    }
};


// horizontal pixel offset of each virtual keyboard row then follow every KB_CHAR_W
static const uint8_t qroff[NQR] = {
    KB_INDENT,
    KB_INDENT,
    KB_INDENT+KB_CHAR_W,
    KB_INDENT+3*KB_CHAR_W/2
};

// special virtual keyboard chars
static const SBox delete_b  = {KB_INDENT, KB_SPC_Y, SBAR_X-KB_INDENT+1, KB_SPC_H};
static const SBox space_b   = {SBAR_X, KB_SPC_Y, SBAR_W, KB_SPC_H};
static const SBox done_b    = {SBAR_X+SBAR_W, KB_SPC_Y, SBAR_X-KB_INDENT+1, KB_SPC_H};
static const SBox page_b    = {800-PAGE_W-KB_INDENT-1, KB_Y0-37, PAGE_W, 35};
static const SBox skip_b    = {730,10,55,35};      // skip box, nice if same as sat ok


static void drawPageButton()
{
    char buf[32];
    snprintf (buf, sizeof(buf), "Page %d ...", cur_page+1);
    drawStringInBox (buf, page_b, false, DONE_C);
}

/* cycle cur_page.
 * N.B. don't draw, leave that up to drawCurrentPage()
 */
static void nextPage()
{
    cur_page = (cur_page + 1) % N_PAGES;
}

/* return whether the given bool prompt is currently relevant
 */
static bool boolIsRelevant (BoolPrompt *bp)
{
    if (bp->page != cur_page)
        return (false);

    if (bp == &bool_pr[WIFI_BPR]) {
        #if defined(_WIFI_ALWAYS) || defined(_WIFI_NEVER)
            return (false);
        #endif
        #if defined(_WIFI_ASK)
            return (true);
        #endif
    }

    if (bp == &bool_pr[FLIP_BPR]) {
        #if !defined(_SUPPORT_FLIP)
            return (false);
        #endif
    }

    if (bp == &bool_pr[KX3ON_BPR]) {
        #if !defined(_SUPPORT_KX3)
            return (false);
        #endif
    }

    if (bp == &bool_pr[KX3BAUD_BPR]) {
        #if !defined(_SUPPORT_KX3)
            return (false);
        #else
        if (!bool_pr[KX3ON_BPR].state)
            return (false);
        #endif
    }


    return (true);
}

/* return whether the given string prompt is currently relevant
 */
static bool stringIsRelevant (StringPrompt *sp)
{
    if (sp->page != cur_page)
        return (false);

    if (sp == &string_pr[SSID_SPR] || sp == &string_pr[PASS_SPR]) {
        #if defined(_WIFI_NEVER)
            return (false);
        #endif
        #if defined(_WIFI_ASK)
            if (!bool_pr[WIFI_BPR].state)
                return (false);
        #endif
    }

    if (sp == &string_pr[DXHOST_SPR] || sp == &string_pr[DXPORT_SPR]) {
        if (!bool_pr[CLUSTER_BPR].state)
            return (false);
    }

    if (sp == &string_pr[LAT_SPR] || sp == &string_pr[LNG_SPR]) {
        if (bool_pr[GEOIP_BPR].state || bool_pr[GPSD_BPR].state)
            return (false);
    }

    if (sp == &string_pr[GPSD_SPR]) {
        if (!bool_pr[GPSD_BPR].state)
            return (false);
    }

    if (sp == &string_pr[TEMPCORR_SPR]) {
        #if !defined(_SUPPORT_ENVSENSOR)
            return (false);
        #endif
    }

    if (sp == &string_pr[PRESCORR_SPR]) {
        #if !defined(_SUPPORT_ENVSENSOR)
            return (false);
        #endif
    }

    if (sp == &string_pr[BRMIN_SPR] || sp == &string_pr[BRMAX_SPR]) {
        #if defined(_SUPPORT_BRCTRL)
            return (true);
        #else
            return (brControlOk());
        #endif
    }

    return (true);
}

/* move cur_focus to the next tab position
 */
static void nextTabFocus()
{
#if defined(_USE_DESKTOP)

    /* table of ordered fields for moving to next focus with each tab.
     * N.B. group and order within to their respective pages
     */
    static const Focus tab_fields[] = {
        // page 1
        {       &string_pr[CALL_SPR], NULL},
        {       &string_pr[LAT_SPR], NULL},
        {       &string_pr[LNG_SPR], NULL},
        { NULL, &bool_pr[GEOIP_BPR] },
        { NULL, &bool_pr[GPSD_BPR] },
        {       &string_pr[GPSD_SPR], NULL},
        { NULL, &bool_pr[WIFI_BPR] },
        {       &string_pr[SSID_SPR], NULL},
        {       &string_pr[PASS_SPR], NULL},

        // page 2
        { NULL, &bool_pr[CLUSTER_BPR] },
        {       &string_pr[DXHOST_SPR], NULL},
        {       &string_pr[DXPORT_SPR], NULL},
        { NULL, &bool_pr[UNITS_BPR] },
        {       &string_pr[COUNTDOWN_SPR], NULL},
        {       &string_pr[TEMPCORR_SPR], NULL},
        {       &string_pr[PRESCORR_SPR], NULL},
        {       &string_pr[BRMIN_SPR], NULL},
        {       &string_pr[BRMAX_SPR], NULL},
        { NULL, &bool_pr[KX3ON_BPR] },
        { NULL, &bool_pr[KX3BAUD_BPR] },
        { NULL, &bool_pr[FLIP_BPR] },
    };
    #define N_TAB_FIELDS    (sizeof(tab_fields)/sizeof(tab_fields[0]))

    // find current position in table
    unsigned f;
    for (f = 0; f < N_TAB_FIELDS; f++)
        if (memcmp (&cur_focus, &tab_fields[f], sizeof(cur_focus)) == 0)
            break;
    if (f == N_TAB_FIELDS) {
        printf ("cur_focus not found\n");
        return;
    }

    // move to next relevant field, wrapping if necessary
    for (unsigned i = 1; i <= N_TAB_FIELDS; i++) {
        const Focus *fp = &tab_fields[(f+i)%N_TAB_FIELDS];
        if (fp->sp) {
            if (stringIsRelevant(fp->sp)) {
                cur_focus = *fp;
                return;
            }
        } else {
            if (boolIsRelevant(fp->bp)) {
                cur_focus = *fp;
                return;
            }
        }
    }
    printf ("new focus not found\n");

#endif //_USE_DESKTOP
}

/* set focus to the given string or bool prompt, opposite assumed to be NULL.
 * N.B. effect of setting both or neither is undefined
 */
static void setFocus (StringPrompt *sp, BoolPrompt *bp)
{
    if (!sp != !bp) {
        cur_focus.sp = sp;
        cur_focus.bp = bp;
    }
}

/* set focus to the first relevant prompt in the current page
 */
static void setInitialFocus()
{
    StringPrompt *sp0 = NULL;
    BoolPrompt *bp0 = NULL;

    for (uint8_t i = 0; i < N_SPR; i++) {
        StringPrompt *sp = &string_pr[i];
        if (stringIsRelevant(sp)) {
            sp0 = sp;
            break;
        }
    }

    if (!sp0) {
        for (uint8_t i = 0; i < N_BPR; i++) {
            BoolPrompt *bp = &bool_pr[i];
            if (boolIsRelevant(bp)) {
                bp0 = bp;
                break;
            }
        }
    }

    setFocus (sp0, bp0);
}

/* draw cursor for cur_focus
 */
static void drawCursor()
{
    uint16_t y, x1, x2;

    if (cur_focus.sp) {
        StringPrompt *sp = cur_focus.sp;
        y = sp->v_box.y+sp->v_box.h-CURSOR_DROP;
        x1 = sp->v_cx;
        x2 = sp->v_cx+PR_W;
    } else {
        BoolPrompt *bp = cur_focus.bp;
        y = bp->p_box.y+bp->p_box.h-CURSOR_DROP;
        x1 = bp->p_box.x;
        x2 = bp->p_box.x+PR_W;
    }

    tft.drawLine (x1, y, x2, y, CURSOR_C);
    tft.drawLine (x1, y+1, x2, y+1, CURSOR_C);
}

/* erase cursor for cur_focus
 */
static void eraseCursor()
{
    uint16_t y, x1, x2;

    if (cur_focus.sp) {
        StringPrompt *sp = cur_focus.sp;
        y = sp->v_box.y+sp->v_box.h-CURSOR_DROP;
        x1 = sp->v_cx;
        x2 = sp->v_cx+PR_W;
    } else {
        BoolPrompt *bp = cur_focus.bp;
        y = bp->p_box.y+bp->p_box.h-CURSOR_DROP;
        x1 = bp->p_box.x;
        x2 = bp->p_box.x+PR_W;
    }

    tft.drawLine (x1, y, x2, y, BG_C);
    tft.drawLine (x1, y+1, x2, y+1, BG_C);
}

/* draw the prompt of the given StringPrompt
 */
static void drawSPPrompt (StringPrompt *sp)
{
    tft.setTextColor (PR_C);
    tft.setCursor (sp->p_box.x, sp->p_box.y+sp->p_box.h-PR_D);
    tft.print(sp->p_str);
}

/* draw the value of the given StringPrompt and set v_cx
 * N.B. we will overwrite prev char to shorten v_str by 1 if length overflows v_box
 */
static void drawSPValue (StringPrompt *sp)
{
    tft.setTextColor (TX_C);

    // reduce value string until it fits within box, overwriting previous last char if necessary
    bool chop = false;
    uint8_t sl = strlen (sp->v_str);
    while (getTextWidth (sp->v_str) >= sp->v_box.w - PR_W) {
        sp->v_str[sl-2] = sp->v_str[sl-1];
        sp->v_str[--sl] = '\0';
        chop = true;
    }

    tft.setCursor (sp->v_box.x, sp->v_box.y+sp->v_box.h-PR_D);
    if (sp->v_len == sl+1 || chop) {
        // no more room, set cursor position under last char
        tft.printf ("%.*s", sl - 1, sp->v_str);
        sp->v_cx = tft.getCursorX();
        tft.fillRect (sp->v_cx, sp->v_box.y, sp->v_box.x+sp->v_box.w-sp->v_cx, sp->v_box.h, BG_C);
        tft.print(sp->v_str[sl-1]);
    } else {
        // more room available, cursor follows string
        tft.print(sp->v_str);
        sp->v_cx = tft.getCursorX();
    }
}

/* draw both prompt and value of the given StringPrompt
 */
static void drawSPPromptValue (StringPrompt *sp)
{
    drawSPPrompt (sp);
    drawSPValue (sp);
}

/* erase the prompt of the given StringPrompt
 */
static void eraseSPPrompt (StringPrompt *sp)
{
    tft.fillRect (sp->p_box.x, sp->p_box.y, sp->p_box.w, sp->p_box.h, BG_C);
}

/* erase the value of the given StringPrompt
 */
static void eraseSPValue (StringPrompt *sp)
{
    tft.fillRect (sp->v_box.x, sp->v_box.y, sp->v_box.w, sp->v_box.h, BG_C);
}

/* erase both prompt and value of the given StringPrompt
 */
static void eraseSPPromptValue (StringPrompt *sp)
{
    eraseSPPrompt (sp);
    eraseSPValue (sp);
}

/* draw the prompt of the given BoolPrompt
 */
static void drawBPPrompt (BoolPrompt *bp)
{
    tft.setTextColor (BUTTON_C);                // usually a question but ...

    #ifdef _WIFI_ALWAYS
        if (bp == &bool_pr[WIFI_BPR])
            tft.setTextColor (PR_C);            // ... required wifi is just a prompt 
    #endif

    tft.setCursor (bp->p_box.x, bp->p_box.y+bp->p_box.h-PR_D);
    tft.print(bp->p_str);
}

/* draw the state of the given BoolPrompt, if any
 */
static void drawBPState (BoolPrompt *bp)
{
    bool show_t = bp->state && bp->t_str;
    bool show_f = !bp->state && bp->f_str;

    if (show_t || show_f) {
        tft.setTextColor (TX_C);
        tft.setCursor (bp->s_box.x, bp->s_box.y+bp->s_box.h-PR_D);
        tft.fillRect (bp->s_box.x, bp->s_box.y, bp->s_box.w, bp->s_box.h, BG_C);
        if (show_t)
            tft.print(bp->t_str);
        if (show_f)
            tft.print(bp->f_str);
    }
}

/* erase state of the given BoolPrompt, if any
 */
static void eraseBPState (BoolPrompt *bp)
{
    tft.fillRect (bp->s_box.x, bp->s_box.y, bp->s_box.w, bp->s_box.h, BG_C);
}


/* draw both prompt and state of the given BoolPrompt
 */
static void drawBPPromptState (BoolPrompt *bp)
{
    drawBPPrompt (bp);
    drawBPState (bp);
}


#if defined(_SUPPORT_KX3)


/* erase prompt of the given BoolPrompt 
 */
static void eraseBPPrompt (BoolPrompt *bp)
{
    tft.fillRect (bp->p_box.x, bp->p_box.y, bp->p_box.w, bp->p_box.h, BG_C);
}

/* erase both prompt and state of the given BoolPrompt
 */
static void eraseBPPromptState (BoolPrompt *bp)
{
    eraseBPPrompt (bp);
    eraseBPState (bp);
}

#endif // _SUPPORT_KX3)


/* draw the virtual keyboard
 */
static void drawKeyboard()
{
    tft.fillRect (0, KB_Y0, tft.width(), tft.height()-KB_Y0-1, BG_C);
    tft.setTextColor (KF_C);

    for (uint8_t r = 0; r < NQR; r++) {
        resetWatchdog();
        uint16_t y = r * KB_CHAR_H + KB_Y0 + KB_CHAR_H;
        const Key *row = qwerty[r];
        for (uint8_t c = 0; c < NQC; c++) {
            const Key *kp = &row[c];
            char n = (char)pgm_read_byte(&kp->n);
            if (n) {
                uint16_t x = qroff[r] + c * KB_CHAR_W;

                // shifted on top
                tft.setCursor (x+F_INDENT, y-KB_CHAR_H/2-F_DESCENT);
                tft.print((char)pgm_read_byte(&kp->s));

                // non-shifted below
                tft.setCursor (x+F_INDENT, y-F_DESCENT);
                tft.print(n);

                // key border
                tft.drawRect (x, y-KB_CHAR_H, KB_CHAR_W, KB_CHAR_H, KB_C);
            }
        }
    }

    drawStringInBox ("", space_b, false, KF_C);
    drawStringInBox ("Delete", delete_b, false, DEL_C);
    drawStringInBox ("Done", done_b, false, DONE_C);
}


/* remove blanks from s IN PLACE.
 */
static void noBlanks (char *s)
{
    char c, *s_to = s;
    while ((c = *s++) != '\0')
        if (c != ' ')
            *s_to++ = c;
    *s_to = '\0';
}


/* convert a screen coord on the virtual keyboard to its char value, if any.
 * N.B. this does NOT handle Delete or Done.
 */
static bool s2char (SCoord &s, char *cp)
{
    // check main qwerty
    if (s.y >= KB_Y0) {
        uint16_t kb_y = s.y - KB_Y0;
        uint8_t row = kb_y/KB_CHAR_H;
        if (row < NQR) {
            uint8_t col = (s.x-qroff[row])*KB_NCOLS/tft.width();
            if (col < NQC) {
                const Key *kp = &qwerty[row][col];
                char n = (char)pgm_read_byte(&kp->n);
                if (n) {
                    *cp = kb_y-row*KB_CHAR_H < KB_CHAR_H/2 ? (char)pgm_read_byte(&kp->s) : n;
                    return(true);
                }
            }
        }
    }

    // check space bar
    if (inBox (s, space_b)) {
        *cp = ' ';
        return (true);
    }

    // s is not on the virtual keyboard
    return (false);
}


/* find whether s is in any string_pr.
 * if so return true and set *spp, else return false.
 */
static bool tappedStringPrompt (SCoord &s, StringPrompt **spp)
{
    for (uint8_t i = 0; i < N_SPR; i++) {
        StringPrompt *sp = &string_pr[i];
        if (stringIsRelevant(sp) && inBox (s, sp->v_box)) {
            *spp = sp;
            return (true);
        }
    }
    return (false);
}

/* find whether s is in any relevant bool prompt.
 * if so return true and set *bpp, else return false.
 */
static bool tappedBool (SCoord &s, BoolPrompt **bpp)
{
    for (uint8_t i = 0; i < N_BPR; i++) {
        BoolPrompt *bp = &bool_pr[i];
        if (boolIsRelevant(bp) && inBox (s, bp->p_box)) {
            *bpp = bp;
            return (true);
        }
    }
    return (false);
}



/* draw the current page of prompts and their current values
 */
static void drawCurrentPage()
{
    // erase
    tft.fillRect (0, 0, tft.width(), KB_Y0-1, BG_C);

    // draw page button first for fast feedback afer tap
    drawPageButton();

    // draw relevant string prompts on this page
    for (uint8_t i = 0; i < N_SPR; i++) {
        StringPrompt *sp = &string_pr[i];
        if (stringIsRelevant(sp))
            drawSPPromptValue(&string_pr[i]);
    }

    // draw relevant bool prompts on this page
    for (uint8_t i = 0; i < N_BPR; i++) {
        BoolPrompt *bp = &bool_pr[i];
        if (boolIsRelevant(bp))
            drawBPPromptState (&bool_pr[i]);
    }

    #if defined(_WIFI_ALWAYS)
        // show prompt but otherwise is not relevant
        if (bool_pr[WIFI_BPR].page == cur_page)
            drawBPPrompt (&bool_pr[WIFI_BPR]);
    #endif

    // set initial focus
    setInitialFocus ();
    drawCursor ();
}


/* validate all string fields, temporarily indicate ones in error if on current page.
 * return whether all ok.
 */
static bool validateStringPrompts()
{
    // collect bad ids to flag
    SPIds badsid[N_SPR];
    uint8_t n_badsid = 0;

    // check lat/long unless using geolocation
    if (!bool_pr[GEOIP_BPR].state) {

        uint8_t n_bad = n_badsid;
        float v;
        char c;

        char *lat_str = string_pr[LAT_SPR].v_str;
        int n_lats = sscanf (lat_str, "%f%c", &v, &c);
        if (n_lats == 1 && v >= -90 && v <= 90)
            de_ll.lat_d = v;
        else if (n_lats == 2 && v >= 0 && v <= 90 && (c == 'N' || c == 'n' || c == 'S' || c == 's'))
            de_ll.lat_d = v * (c == 'N' || c == 'n' ? 1 : -1);
        else
            badsid[n_badsid++] = LAT_SPR;

        char *lng_str = string_pr[LNG_SPR].v_str;
        int n_lngs = sscanf (lng_str, "%f%c", &v, &c);
        if (n_lngs == 1 && v >= -180 && v <= 180)
            de_ll.lng_d = v;
        else if (n_lngs == 2 && v >= 0 && v <= 180 && (c == 'W' || c == 'w' || c == 'E' || c == 'e'))
            de_ll.lng_d = v * (c == 'W' || c == 'w' ? -1 : 1);
        else
            badsid[n_badsid++] = LNG_SPR;

        // use to set grid if no trouble
        if (n_bad == n_badsid)
            setMaidenhead(NV_DE_GRID, de_ll);
    }

    // check cluster host and port if used
    if (bool_pr[CLUSTER_BPR].state) {

        char *host_str = string_pr[DXHOST_SPR].v_str;
        noBlanks(host_str);
        char *dot = strchr (host_str, '.');
        if (!dot || dot == host_str || dot[1] == '\0')  // require dot surrounded by chars
            badsid[n_badsid++] = DXHOST_SPR;

        char *port_str = string_pr[DXPORT_SPR].v_str;
        char *first_bad;
        long portn = strtol (port_str, &first_bad, 10);
        if (*first_bad != '\0' || portn < 23 || portn > 65535)  // 23 is telnet
            badsid[n_badsid++] = DXPORT_SPR;
        else
            dxport = portn;
    }

    // check for plausible temperature correction
    char *tc_str = string_pr[TEMPCORR_SPR].v_str;
    temp_corr = atof (tc_str);
    if (temp_corr < -10 || temp_corr > 10)
        badsid[n_badsid++] = TEMPCORR_SPR;

    // check for plausible pressure correction
    char *pc_str = string_pr[PRESCORR_SPR].v_str;
    pres_corr = atof (pc_str);
    if (pres_corr < -10 || pres_corr > 10)
        badsid[n_badsid++] = PRESCORR_SPR;

    // check for plausible countdown value
    char *cd_str = string_pr[COUNTDOWN_SPR].v_str;
    char *first_bad;
    int cd_min = strtol (cd_str, &first_bad, 10);
    if (cd_min > 0 && *first_bad == '\0')               // 1 minute minimum
        countdown_period = cd_min * 60000;              // mins -> ms
    else
        badsid[n_badsid++] = COUNTDOWN_SPR;

    // require ssid and pw if wifi
    if (bool_pr[WIFI_BPR].state) {
        if (strlen (string_pr[SSID_SPR].v_str) == 0)
            badsid[n_badsid++] = SSID_SPR;
        if (strlen (string_pr[PASS_SPR].v_str) == 0)
            badsid[n_badsid++] = PASS_SPR;
    }

    // allow no spaces in call sign
    if (strchr (string_pr[CALL_SPR].v_str, ' ')) {
        badsid[n_badsid++] = CALL_SPR;
    }

    // require finite gpsd host name if used
    if (bool_pr[GPSD_BPR].state) {
        char *str = string_pr[GPSD_SPR].v_str;
        noBlanks(str);
        if (strlen(str) == 0)
            badsid[n_badsid++] = GPSD_SPR;
    }

    // require both brightness 0..100 and min < max.
    if (brControlOk()) {
        // Must use ints to check for < 0
        int brmn = atoi (string_pr[BRMIN_SPR].v_str);
        int brmx = atoi (string_pr[BRMAX_SPR].v_str);
        bool brmn_ok = brmn >= 0 && brmn <= 100;
        bool brmx_ok = brmx >= 0 && brmx <= 100;
        bool order_ok = brmn < brmx;
        if (!brmn_ok || (!order_ok && brmx_ok))
            badsid[n_badsid++] = BRMIN_SPR;
        if (!brmx_ok || (!order_ok && brmn_ok))
            badsid[n_badsid++] = BRMAX_SPR;
        if (brmn_ok && brmx_ok && order_ok) {
            bright_min = brmn;
            bright_max = brmx;
        }
    }

    // indicate any values in error, changing pages if necessary to find first
    if (n_badsid > 0) {

        bool show_bad = false;

        do {

            // flag each erroneous value on current page
            for (uint8_t i = 0; i < n_badsid; i++) {
                StringPrompt *sp = &string_pr[badsid[i]];
                if (sp->page == cur_page) {
                    eraseSPValue (sp);
                    tft.setTextColor (ERR_C);
                    tft.setCursor (sp->v_box.x, sp->v_box.y+sp->v_box.h-PR_D);
                    tft.print ("Err");
                    show_bad = true;
                }
            }

            // next page if no bad fields on this one
            if (!show_bad) {

                // no bad fields on this page, try next
                nextPage();
                drawCurrentPage();

            } else {

                // found bad field on this page

                // dwell error flag(s)
                wdDelay(2000);

                // restore values
                for (uint8_t i = 0; i < n_badsid; i++) {
                    StringPrompt *sp = &string_pr[badsid[i]];
                    if (sp->page == cur_page) {
                        eraseSPValue (sp);
                        drawSPValue (sp);
                    }
                }

                // redraw cursor in case it's value was flagged
                drawCursor();
            }

        } while (show_bad == false);

        // at least one bad field
        return (false);
    }

    // all good
    return (true);
}


/* if RPi try to set NV_WIFI_SSID and NV_WIFI_PASSWD from wpa_supplicant.conf 
 */
static bool getWPA()
{
#if defined(_IS_RPI)

    // open
    static const char wpa_fn[] = "/etc/wpa_supplicant/wpa_supplicant.conf";
    FILE *fp = fopen (wpa_fn, "r");
    if (!fp) {
        printf ("%s: %s\n", wpa_fn, strerror(errno));
        return (false);
    }

    // read, looking for ssid and psk
    char buf[1024], wpa_ssid[100], wpa_psk[100];
    bool found_ssid = false, found_psk = false;
    while (fgets (buf, sizeof(buf), fp)) {
        if (sscanf (buf, " ssid=\"%100[^\"]\"", wpa_ssid) == 1)
            found_ssid = true;
        if (sscanf (buf, " psk=\"%100[^\"]\"", wpa_psk) == 1)
            found_psk = true;
    }

    // finished with file
    fclose (fp);

    // save if found both
    if (found_ssid && found_psk) {
        wpa_ssid[NV_WIFI_SSID_LEN-1] = '\0';
        strcpy (ssid, wpa_ssid);
        NVWriteString(NV_WIFI_SSID, ssid);
        wpa_psk[NV_WIFI_PW_LEN-1] = '\0';
        strcpy (pass, wpa_psk);
        NVWriteString(NV_WIFI_PASSWD, pass);
        return (true);
    }

    // nope
    return (false);

#else

    return (false);

#endif // _IS_RPI
}


/* load all setup values from nvram or set default values:
 */
static void initSetup()
{
    // init wifi

    if (!getWPA() && !NVReadString(NV_WIFI_SSID, ssid)) {
        strncpy (ssid, DEF_SSID, NV_WIFI_SSID_LEN-1);
        NVWriteString(NV_WIFI_SSID, ssid);
    }
    if (!NVReadString(NV_WIFI_PASSWD, pass)) {
        strncpy (pass, DEF_PASS, NV_WIFI_PW_LEN-1);
        NVWriteString(NV_WIFI_PASSWD, pass);
    }


    // init call sign

    if (!NVReadString(NV_CALLSIGN, call)) {
        strncpy (call, DEF_CALL, NV_CALLSIGN_LEN-1);
        NVWriteString(NV_CALLSIGN, call);
    }


    // gpsd host and option

    if (!NVReadString (NV_GPSDHOST, gpsdhost)) {
        gpsdhost[0] = '\0';
        NVWriteString (NV_GPSDHOST, gpsdhost);
    }
    bool_pr[GPSD_BPR].state = gpsdhost[0] != '\0';



    // init dx cluster

    if (!NVReadString(NV_DXHOST, dxhost)) {
        memset (dxhost, 0, sizeof(dxhost));
        NVWriteString(NV_DXHOST, dxhost);
    }
    if (!NVReadUInt16(NV_DXPORT, &dxport)) {
        dxport = 0;
        NVWriteUInt16(NV_DXPORT, dxport);
    }
    bool_pr[CLUSTER_BPR].state = dxhost[0] != '\0' && dxport > 0;


    // init de lat/lng

    // if de never set before set to cental US so it differs from default DX which is 0/0.
    if (!NVReadFloat (NV_DE_LAT, &de_ll.lat_d) || !NVReadFloat (NV_DE_LNG, &de_ll.lng_d)) {
        // http://www.kansastravel.org/geographicalcenter.htm
        de_ll.lng_d = -99;
        de_ll.lat_d = 40;
        setMaidenhead(NV_DE_GRID, de_ll);
        NVWriteFloat (NV_DE_LAT, de_ll.lat_d);
        NVWriteFloat (NV_DE_LNG, de_ll.lng_d);
    }


    // init KX3. NV stores actual baud rate, we just toggle between 4800 and 38400, 0 means off

    uint32_t kx3;
    if (!NVReadUInt32 (NV_KX3BAUD, &kx3)) {
        kx3 = 0;                                // off
        NVWriteUInt32 (NV_KX3BAUD, kx3);
    }
    bool_pr[KX3ON_BPR].state = kx3 != 0;
    bool_pr[KX3BAUD_BPR].state = kx3 != 4800;


    // init WiFi

#if defined(_WIFI_ALWAYS)
    bool_pr[WIFI_BPR].state = true;             // always on
    bool_pr[WIFI_BPR].p_str = "WiFi:";          // not a question
#elif defined(_WIFI_ASK)
    bool_pr[WIFI_BPR].state = false;            // default off
#endif


    // init misc

    uint8_t rot;
    if (!NVReadUInt8 (NV_ROTATE_SCRN, &rot)) {
        rot = 0;
        NVWriteUInt8 (NV_ROTATE_SCRN, rot);
    }
    bool_pr[FLIP_BPR].state = rot;

    uint8_t met;
    if (!NVReadUInt8 (NV_METRIC_ON, &met)) {
        met = 0;
        NVWriteUInt8 (NV_METRIC_ON, met);
    }
    bool_pr[UNITS_BPR].state = met;

    if (!NVReadFloat (NV_TEMPCORR, &temp_corr)) {
        temp_corr = 0;
        NVWriteFloat (NV_TEMPCORR, temp_corr);
    }
    if (!NVReadFloat (NV_PRESCORR, &pres_corr)) {
        pres_corr = 0;
        NVWriteFloat (NV_PRESCORR, pres_corr);
    }

    if (!NVReadUInt32 (NV_CD_PERIOD, &countdown_period)) {
        countdown_period = 600000;     // 10 mins default
        NVWriteUInt32 (NV_CD_PERIOD, countdown_period);
    }

    bool_pr[GEOIP_BPR].state = false;

    if (!NVReadUInt8 (NV_BR_MIN, &bright_min)) {
        bright_min = 0;
        NVWriteUInt8 (NV_BR_MIN, bright_min);
    }
    if (!NVReadUInt8 (NV_BR_MAX, &bright_max)) {
        bright_max = 100;
        NVWriteUInt8 (NV_BR_MAX, bright_max);
    }
}


/* return whether user wants to run setup.
 */ 
static bool askRun()
{
    eraseScreen();

    drawStringInBox ("Skip", skip_b, false, TX_C);

    tft.setTextColor (TX_C);
    tft.setCursor (tft.width()/6, tft.height()/5);
#if defined(_USE_DESKTOP)
    tft.print (F("Click anywhere to enter Setup screen ... "));
#else
    tft.print (F("Tap anywhere to enter Setup screen ... "));
#endif // _USE_DESKTOP

    int16_t x = tft.getCursorX();
    int16_t y = tft.getCursorY();
    uint16_t to;
    for (to = ASK_TO*10; to > 0; --to) {
        resetWatchdog();
        if ((to+9)/10 != (to+10)/10) {
            tft.fillRect (x, y-PR_A, 2*PR_W, PR_A+PR_D, BG_C);
            tft.setCursor (x, y);
            tft.print((to+9)/10);
        }

        // check for touch or secret abort box
        SCoord s;
        TouchType tt = readCalTouch (s);
        if (tt != TT_NONE || tft.getChar()) {
            drainTouch();
            if (tt != TT_NONE && inBox (s, skip_b)) {
                drawStringInBox ("Skip", skip_b, true, TX_C);
                return (false);
            }
                
            break;
        }
        wdDelay(100);
    }

    return (to > 0);
}





/* init display and supporting StringPrompt and BoolPrompt data structs
 */
static void initDisplay()
{
    // erase screen
    eraseScreen();

    // init shadow strings. N.B. free() before leaving
    snprintf (string_pr[LAT_SPR].v_str = (char*)calloc(7,1), string_pr[LAT_SPR].v_len = 7,
                                        "%.2f%c", fabsf(de_ll.lat_d), de_ll.lat_d < 0 ? 'S' : 'N');
    snprintf (string_pr[LNG_SPR].v_str = (char*)calloc(8,1), string_pr[LNG_SPR].v_len = 8,
                                        "%.2f%c", fabsf(de_ll.lng_d), de_ll.lng_d < 0 ? 'W' : 'E');
    snprintf (string_pr[DXPORT_SPR].v_str = (char*)calloc(7,1), string_pr[DXPORT_SPR].v_len = 7,
                                        "%u", dxport);
    snprintf (string_pr[TEMPCORR_SPR].v_str = (char*)calloc(6,1), string_pr[TEMPCORR_SPR].v_len = 6,
                                        "%.2f", temp_corr);
    snprintf (string_pr[PRESCORR_SPR].v_str = (char*)calloc(6,1), string_pr[PRESCORR_SPR].v_len = 6,
                                        "%.3f", pres_corr);
    snprintf (string_pr[COUNTDOWN_SPR].v_str = (char*)calloc(8,1), string_pr[COUNTDOWN_SPR].v_len = 8,
                                        "%u", countdown_period/60000);  // ms -> minutes
    snprintf (string_pr[BRMIN_SPR].v_str = (char*)calloc(4,1), string_pr[BRMIN_SPR].v_len = 4,
                                        "%u", bright_min);
    snprintf (string_pr[BRMAX_SPR].v_str = (char*)calloc(4,1), string_pr[BRMAX_SPR].v_len = 4,
                                        "%u", bright_max);

    // draw virtual keyboard
    drawKeyboard();

    // draw current page
    drawCurrentPage();
}

/* run the setup screen until all fields check ok and user wants to exit
 */
static void runSetup()
{
    drainTouch();

    SCoord s;
    char c;

    do {
        StringPrompt *sp;
        BoolPrompt *bp;

        // wait for next tap or character input
        for (;;) {

            // if touch try to also find what char it might be from virtual kb
            if (readCalTouch(s) != TT_NONE) {
                if (!s2char (s, &c))
                    c = 0;
                break;
            }

            // if real kb input, invalidate touch location
            c = tft.getChar();
            if (c) {
                s.x = s.y = 0;
                break;
            }

            // neither, wait and repeat
            resetWatchdog();
            wdDelay(10);
        }


        // process

        if (c == '\t') {

            // move focus to next tab position
            eraseCursor();
            nextTabFocus();
            drawCursor();

        } else if (inBox (s, page_b) || c == 27) {              // esc

            // show next page
            nextPage();
            drawCurrentPage();

        } else if (cur_focus.sp && (inBox (s, delete_b) || c == '\b' || c == 127)) {

            // tapped Delete or kb equiv while focus is string: remove one char

            StringPrompt *sp = cur_focus.sp;
            uint8_t sl = strlen (sp->v_str);
            if (sl > 0) {
                // erase cursor, shorten string, find new width, erase to end, redraw
                eraseCursor ();
                sp->v_str[sl-1] = '\0';
                uint16_t sw = getTextWidth (sp->v_str);
                tft.fillRect (sp->v_box.x+sw, sp->v_box.y, sp->v_box.w-sw, sp->v_box.h, BG_C);
                drawSPValue (sp);
                drawCursor ();
            }

        } else if (cur_focus.sp && isprint(c)) {

            // received a new char for string in focus

            StringPrompt *sp = cur_focus.sp;
            uint8_t sl = strlen (sp->v_str);

            // cursor will likely move
            eraseCursor ();

            // append or overwrite with c
            if (sl < sp->v_len - 1) {
                // ok, room for 1 more
                sp->v_str[sl++] = c;
                sp->v_str[sl] = '\0';
            } else {
                // no more room, overwrite last char
                eraseSPValue (sp);
                sp->v_str[sl-1] = c;
            }

            // update string, cursor at end
            drawSPValue (sp);
            drawCursor ();

        } else if (tappedBool (s, &bp) || (c == ' ' && cur_focus.bp)) {

            // typing space applies to focus bool
            if (c == ' ')
                bp = cur_focus.bp;

            // ignore tapping on bools not being shown
            if (!boolIsRelevant(bp))
                continue;

            // move focus here
            eraseCursor ();
            setFocus (NULL, bp);
            drawCursor ();

            // toggle and redraw
            bp->state = !bp->state;
            drawBPState (bp);

            // check for possible secondary implications
            if (bp == &bool_pr[GEOIP_BPR]) {
                // show/hide lat/lng prompts, gpsd
                if (bp->state) {
                    // no gpsd host
                    bool_pr[GPSD_BPR].state = false;
                    eraseSPPromptValue (&string_pr[GPSD_SPR]);
                    drawBPState (&bool_pr[GPSD_BPR]);
                    // no lat/long
                    eraseSPPromptValue (&string_pr[LAT_SPR]);
                    eraseSPPromptValue (&string_pr[LNG_SPR]);
                } else {
                    // show lat/long
                    drawSPPromptValue (&string_pr[LAT_SPR]);
                    drawSPPromptValue (&string_pr[LNG_SPR]);
                }
            }
            else if (bp == &bool_pr[CLUSTER_BPR]) {
                // show/hide dx prompts
                if (bp->state) {
                    eraseBPState (&bool_pr[CLUSTER_BPR]);
                    drawSPPromptValue (&string_pr[DXHOST_SPR]);
                    drawSPPromptValue (&string_pr[DXPORT_SPR]);
                } else {
                    eraseSPPromptValue (&string_pr[DXHOST_SPR]);
                    eraseSPPromptValue (&string_pr[DXPORT_SPR]);
                    drawBPState (&bool_pr[CLUSTER_BPR]);
                }
            }

            else if (bp == &bool_pr[GPSD_BPR]) {
                // show/hide gpsd host, geolocate, lat/long
                if (bp->state) {
                    // no lat/long
                    eraseSPPromptValue (&string_pr[LAT_SPR]);
                    eraseSPPromptValue (&string_pr[LNG_SPR]);
                    // no geolocate
                    bool_pr[GEOIP_BPR].state = false;
                    drawBPState (&bool_pr[GEOIP_BPR]);
                    // show gpsd host
                    eraseBPState (&bool_pr[GPSD_BPR]);
                    drawSPPromptValue (&string_pr[GPSD_SPR]);
                } else {
                    // no gpsd host
                    eraseSPPromptValue (&string_pr[GPSD_SPR]);
                    drawBPState (&bool_pr[GPSD_BPR]);
                    // show lat/long
                    drawSPPromptValue (&string_pr[LAT_SPR]);
                    drawSPPromptValue (&string_pr[LNG_SPR]);
                }
            }

          #if defined(_WIFI_ASK)
            else if (bp == &bool_pr[WIFI_BPR]) {
                // show/hide wifi prompts
                if (bp->state) {
                    eraseBPState (&bool_pr[WIFI_BPR]);
                    drawSPPromptValue (&string_pr[SSID_SPR]);
                    drawSPPromptValue (&string_pr[PASS_SPR]);
                } else {
                    eraseSPPromptValue (&string_pr[SSID_SPR]);
                    eraseSPPromptValue (&string_pr[PASS_SPR]);
                    drawBPState (&bool_pr[WIFI_BPR]);
                }
            }
          #endif // _WIFI_ASK

          #if defined(_SUPPORT_KX3)
            else if (bp == &bool_pr[KX3ON_BPR]) {
                // show/hide baud rate
                if (bp->state) {
                    eraseBPState (&bool_pr[KX3ON_BPR]);
                    drawBPPromptState (&bool_pr[KX3BAUD_BPR]);
                } else {
                    eraseBPPromptState (&bool_pr[KX3BAUD_BPR]);
                    drawBPState (&bool_pr[KX3ON_BPR]);
                }
            }
          #endif // _SUPPORT_KX3

        } else if (tappedStringPrompt (s, &sp) && stringIsRelevant (sp)) {

            // move focus here unless already there
            if (cur_focus.sp != sp) {
                eraseCursor ();
                setFocus (sp, NULL);
                drawCursor ();
            }
        }

    } while (!(inBox (s, done_b) || c == '\r' || c == '\n') || !validateStringPrompts());

    // all fields are valid

}

/* draw the given string with border centered inside the given box using the current font.
 */
void drawStringInBox (const char str[], const SBox &b, bool inverted, uint16_t color)
{
    uint16_t sw = getTextWidth ((char*)str);

    uint16_t fg = inverted ? BG_C : color;
    uint16_t bg = inverted ? color : BG_C;

    tft.setCursor (b.x+(b.w-sw)/2, b.y+3*b.h/4);
    tft.fillRect (b.x, b.y, b.w, b.h, bg);
    tft.drawRect (b.x, b.y, b.w, b.h, KB_C);
    tft.setTextColor (fg);
    tft.print(str);
}


void clockSetup()
{
    // must start with a calibrated screen
    calibrateTouch(false);

    // set font used throughout
    selectFontStyle (BOLD_FONT, SMALL_FONT);

    // load values from nvram, else set defaults
    initSetup();

    // ask user whether they want to run setup
    if (!askRun())
        return;

    // ok, user wants to run setup

    // init display prompts and options
    initDisplay();

    // get current rotation state so we can tell whether it changes
    bool rotated = rotateScreen();

    // main interaction loop
    runSetup();

    // persist results 
    NVWriteString(NV_WIFI_SSID, ssid);
    NVWriteString(NV_WIFI_PASSWD, pass);
    NVWriteString(NV_CALLSIGN, call);
    NVWriteFloat(NV_DE_LAT, de_ll.lat_d);
    NVWriteFloat(NV_DE_LNG, de_ll.lng_d);
    NVWriteUInt8 (NV_ROTATE_SCRN, bool_pr[FLIP_BPR].state);
    NVWriteUInt8 (NV_METRIC_ON, bool_pr[UNITS_BPR].state);
    NVWriteUInt32 (NV_KX3BAUD, bool_pr[KX3ON_BPR].state ? (bool_pr[KX3BAUD_BPR].state ? 38400 : 4800) : 0);
    NVWriteFloat (NV_TEMPCORR, temp_corr);
    NVWriteFloat (NV_PRESCORR, pres_corr);
    NVWriteUInt32 (NV_CD_PERIOD, countdown_period);
    NVWriteUInt8 (NV_BR_MIN, bright_min);
    NVWriteUInt8 (NV_BR_MAX, bright_max);

    // TODO: cluster on/off should be separate rather than wiping out the values just to save state
    if (!bool_pr[GPSD_BPR].state)
        gpsdhost[0] = '\0';
    NVWriteString(NV_GPSDHOST, gpsdhost);
    if (!bool_pr[CLUSTER_BPR].state) {
        dxhost[0] = '\0';
        dxport = 0;
    }
    NVWriteString(NV_DXHOST, dxhost);
    NVWriteUInt16(NV_DXPORT, dxport);

    // clean up shadow strings
    free (string_pr[LAT_SPR].v_str);
    free (string_pr[LNG_SPR].v_str);
    free (string_pr[DXPORT_SPR].v_str);
    free (string_pr[TEMPCORR_SPR].v_str);
    free (string_pr[PRESCORR_SPR].v_str);
    free (string_pr[COUNTDOWN_SPR].v_str);
    free (string_pr[BRMIN_SPR].v_str);
    free (string_pr[BRMAX_SPR].v_str);

    // must recalibrate if rotating screen
    if (rotated != rotateScreen()) {
        tft.setRotation(rotateScreen() ? 2 : 0);
        calibrateTouch(true);
    }

}


/* return pointer to static storage containing the WiFi SSID, else NULL if not used
 */
char *getWiFiSSID()
{
    if (bool_pr[WIFI_BPR].state)
        return (ssid);
    else
        return (NULL);
}


/* return pointer to static storage containing the WiFi password, else NULL if not used
 */
char *getWiFiPW()
{
    if (bool_pr[WIFI_BPR].state)
        return (pass);
    else
        return (NULL);
}


/* return pointer to static storage containing the Callsign
 */
char *getCallsign()
{
    return (call);
}

/* return pointer to static storage containing the DX cluster host
 */
char *getDXClusterHost()
{
    return (dxhost);
}

/* return dx cluster node port
 */
int getDXClusterPort()
{
    return (dxport);
}

/* return whether we should be allowing DX cluster
 */
bool useDXCluster()
{
    return (bool_pr[CLUSTER_BPR].state);
}

/* return whether to rotate the screen
 */
bool rotateScreen()
{
    return (bool_pr[FLIP_BPR].state);
}

/* return whether to use metric units
 */
bool useMetricUnits()
{
    return (bool_pr[UNITS_BPR].state);
}

/* return whether to use IP geolocation
 */
bool useGeoIP()
{
    return (bool_pr[GEOIP_BPR].state);
}

/* return temperature correction.
 * at this point it's just a number, caller should interpret according to useMetricUnits()
 */
float getBMETempCorr()
{
    return (temp_corr);
}

/* return pressure correction.
 * at this point it's just a number, caller should interpret according to useMetricUnits()
 */
float getBMEPresCorr()
{
    return (pres_corr);
}

/* return KX3 baud rate, 0 if off
 */
uint32_t getKX3Baud()
{
    return (bool_pr[KX3ON_BPR].state ? (bool_pr[KX3BAUD_BPR].state ? 38400 : 4800) : 0);
}

/* gpsd host and whether to use.
 * if no host, just return whether.
 */
bool getGPSDHost (char *host)
{
    if (bool_pr[GPSD_BPR].state) {
        if (host)
            memcpy (host, gpsdhost, NV_GPSDHOST_LEN);
        return (true);
    }
    return (false);
}

/* return desired maximum brightness, percentage
 */
uint8_t getBrMax()
{
    return (bright_max);
}

/* return desired minimum brightness, percentage
 */
uint8_t getBrMin()
{
    return (bright_min);
}
