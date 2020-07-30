/* this is the same as Adafruit_RA8875 but runs on Rasp Pi using /dev/fb0 or any UNIX using X Windows.
 * N.B. we only remimplented the functions we use, we may have missed a few.
 */

#ifndef _Adafruit_RA8875_H
#define _Adafruit_RA8875_H

#define	PROGMEM

#include <stdint.h>
#include <pthread.h>

#ifdef _USE_X11

#include <sys/time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

// simplest to just recreate the same fb structure
struct fb_var_screeninfo {
    int xres, yres;
};

#endif // _USE_X11

#ifdef _USE_FB0

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <linux/input.h>

#endif	// _USE_FB0

#include "gfxfont.h"
extern const GFXfont Courier_Prime_Sans6pt7b;

/* the original class uses RGB 565 16 bit colors, the real frame buffer uses TRGB 32 bit true color,
 * although it seems the transparency byte is ignored.
 */

#ifndef RGB565
#define RGB565(R,G,B)   ((((uint16_t)(R) & 0xF8) << 8) | (((uint16_t)(G) & 0xFC) << 3) | ((uint16_t)(B) >> 3))
#endif

#define RGB565_R(c)     (((c) & 0xF800) >> 8)
#define RGB565_G(c)     (((c) & 0x07E0) >> 3)
#define RGB565_B(c)     (((c) & 0x001F) << 3)

#define	RGB1632(C16)	((((uint32_t)(C16)&0xF800)<<8) | (((uint32_t)(C16)&0x07E0)<<5) | (((C16)&0x001F)<<3))
#define	RGB3216(C32)	RGB565(((C32)>>16)&0xFF, ((C32)>>8)&0xFF, ((C32)&0xFF))

#define	RA8875_BLACK	RGB565(0,0,0)
#define	RA8875_WHITE	RGB565(255,255,255)
#define RA8875_RED	RGB565(255,0,0)
#define	RA8875_GREEN	RGB565(0,255,0)
#define	RA8875_BLUE	RGB565(0,0,255)
#define	RA8875_CYAN	RGB565(0,255,255)
#define	RA8875_MAGENTA	RGB565(255,0,255)
#define	RA8875_YELLOW	RGB565(255,255,0)

#define	RA8875_800x480 1
#define RA8875_PWM_CLK_DIV1024 1
#define	RA8875_MRWC 1


class Adafruit_RA8875 {

    public:

	Adafruit_RA8875(uint8_t CS, uint8_t RST);

	void displayOn (int o)
	{
	}

	void GPIOX (int x)
	{
	}

	void PWM1config(bool t, int x)
	{
	}

	void graphicsMode(void)
	{
	}


	void writeCommand (uint8_t c)
	{
	}

	void setRotation (int r)
	{
	    rotation = r;
	}

	void textSetCursor(uint16_t x, uint16_t y)
	{
	}

	void PWM1out(uint16_t bpwm)
	{
	}

	void touchEnable (bool b)
	{
	}

	bool begin (int x);
	uint16_t width(void);
	uint16_t height(void);
	void fillScreen (uint16_t color16);
	void setTextColor(uint16_t color16);
	void setCursor(uint16_t x, uint16_t y);
	void getTextBounds(char *string, int16_t x, int16_t y,
		int16_t *x1, int16_t *y1, uint16_t *w, uint16_t *h);
	void print (char c);
	void print (char *s);
	void print (const char *s);
	void print (int i, int base = 10);
	void print (float f, int p = 2);
	void print (long l);
	void println (void);
	void println (char *s);
	void println (const char *s);
	void println (int i, int base = 10);
	void setXY (int16_t x, int16_t y);
	uint16_t readData(void);
	void setFont (const GFXfont *f);
	int16_t getCursorX(void);
	int16_t getCursorY(void);
	bool touched(void);
	void touchRead (uint16_t *x, uint16_t *y);
	void drawPixel(int16_t x, int16_t y, uint16_t color16);
        void drawPixels(uint16_t * p, uint32_t count, int16_t x, int16_t y);
	void drawSubPixel(int16_t x, int16_t y, uint16_t color16);
	void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color16);
	void drawRect(int16_t x0, int16_t y0, int16_t w, int16_t h, uint16_t color16);
	void fillRect(int16_t x0, int16_t y0, int16_t w, int16_t h, uint16_t color16);
	void drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color16);
	void fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color16);
	void drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2,
	    uint16_t color16);
	void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2,
	    uint16_t color16);

	// special method to draw hi res earth pixel
	void plotEarth (uint16_t x0, uint16_t y0, float lat0, float lng0,
            float dlatr, float dlngr, float dlatd, float dlngd, float fract_day);

        // methods to implement a protected rectangle drawn only with drawPR()
        void setPR (uint16_t x, uint16_t y, uint16_t w, uint16_t h);
        void drawPR(void);
        uint16_t pr_x, pr_y, pr_w, pr_h;
        volatile char pr_flag;
	void setStagingArea(void);

	// real/app display size
	int SCALESZ;

        // get next keyboard character
        char getChar(void);

    protected:

	// 0: normal 2: 180 degs
	int rotation;

    private:

#if defined(_CLOCK_1600x960)

	#define FB_XRES 1600
	#define FB_YRES 960
	#define EARTH_BIG_W 1320
	#define EARTH_BIG_H 660

#elif defined(_CLOCK_2400x1440)

	#define FB_XRES 2400
	#define FB_YRES 1440
	#define EARTH_BIG_W 1980
	#define EARTH_BIG_H 990

#elif defined(_CLOCK_3200x1920)

	#define FB_XRES 3200
	#define FB_YRES 1920
	#define EARTH_BIG_W 2640
	#define EARTH_BIG_H 1320

#else   // original size

	#define FB_XRES 800
	#define FB_YRES 480
	#define EARTH_BIG_W 660
	#define EARTH_BIG_H 330

#endif

#ifdef _USE_X11

	Display *display;
	Window win;
	GC gc;
	XImage *img;
	Pixmap pixmap;

#endif // _USE_X11

#ifdef _USE_FB0

	// mouse and/or touch screen is read in separate thread protected by mouse_lock
	static void *mouseThreadHelper(void *me);
	void mouseThread (void);
        void findMouse(void);
	int mouse_fd, touch_fd;
        struct timespec mouse_ts;
        #define MOUSE_FADE 2000    // ms

	// kb is read in separate thread protected by kb_lock
	static void *kbThreadHelper(void *me);
	void kbThread ();
        void findKeyboard(void);
        int kb_fd;

        void setCursorIfVis (uint16_t row, uint16_t col, uint32_t color);

	int fb_fd;
	int FB_CURSOR_SZ;
	#define FB_CURSOR_W 15         // APP units
	#define FB_BPP_RQD 32
	uint32_t *fb_fb;
        uint32_t fb_pixlen;
	uint32_t *fb_cursor;

#endif	// _USE_FB0

	pthread_mutex_t mouse_lock;
	volatile int16_t mouse_x, mouse_y;
	volatile int mouse_ups, mouse_downs;
	pthread_mutex_t kb_lock;
        char kb_cq[20];
        int kb_cqhead, kb_cqtail;

	// frame buffer is drawn in separate thread protected by fb_lock
	static void *fbThreadHelper(void *me);
	#define APP_WIDTH  800
	#define APP_HEIGHT 480
	void fbThread ();
	pthread_mutex_t fb_lock;
	struct fb_var_screeninfo fb_si;
	volatile bool fb_dirty;
	uint32_t *fb_canvas;
	uint32_t *fb_stage;
	int fb_nbytes;
	void plotLineLow(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint32_t color32);
	void plotLineHigh(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint32_t color32);
	void plotLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint32_t color32);
	void plot32 (int16_t x, int16_t y, uint32_t color32);
	void plotChar (char c);
	uint32_t text_color32;
	uint16_t cursor_x, cursor_y;
	uint16_t read_x, read_y;
	bool read_msb, read_first;
	const GFXfont *current_font;
	int FB_X0;
	int FB_Y0;

	// big earth maps
	static const uint16_t DEARTH_BIG[EARTH_BIG_H][EARTH_BIG_W];
	static const uint16_t NEARTH_BIG[EARTH_BIG_H][EARTH_BIG_W];

};

#endif // _Adafruit_RA8875_H
