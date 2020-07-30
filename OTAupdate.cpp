/* handle remote firmware updating
 */

#include <ESP8266httpUpdate.h>
#include <WiFiUdp.h>


#include "HamClock.h"

static const char v_page[] = "/ham/HamClock/version.pl";

#if defined(_USE_UNIX)
static const char v_bin[] = "/ham/HamClock/ESPHamClock.zip";
#else
static const char v_bin[] = "/ham/HamClock/ESPHamClock.ino.bin";
#endif

#define	UPDATE_TO	10000U			// timeout, millis()
#define	BOX_W		120			// box width
#define	BOX_H		40			// box height
#define	INDENT		50			// indent
#define	Q_Y		100			// question y
#define	B_Y		200			// box y
#define INFO_X          50                      // info x
#define INFO_Y          390                     // info y


/* return whether a new version is available.
 * if so, and we care, pass back the name in new_ver[new_verl]
 * default no if error.
 */
bool isNewVersionAvailable (char *new_ver, uint16_t new_verl)
{
    WiFiClient v_client;
    bool found_newer = false;

    Serial.print (svr_host); Serial.println (v_page);
    if (wifiOk() && v_client.connect (svr_host, HTTPPORT)) {
	resetWatchdog();

	// query page
	httpGET (v_client, svr_host, v_page);

	// skip header
	if (!httpSkipHeader (v_client)) {
	    Serial.println (F("Version query header is short"));
	    goto out;
	}

	// next line is new version number
        char nv[100];
	if (!getTCPLine (v_client, nv, sizeof(nv), NULL)) {
	    Serial.println (F("Version query timed out"));
	    goto out;
        }

	// compare; always give option to overwrite the current release candidate
        Serial.printf ("found version %s\n", nv);
	float this_v = atof(VERSION);
	float new_v = atof(nv);
	if (new_v > this_v || strstr(VERSION,"rc")) {
            found_newer = true;
            if (new_ver)
                strncpy (new_ver, nv, new_verl);
        }
    }

out:

    // finished with connection
    v_client.stop();

    return (found_newer);
}

/* ask and return whether to install the given (presumably newer) version.
 * default no if trouble of no user response.
 */
bool askOTAupdate(char *new_ver)
{
    // ask whether to install
    eraseScreen();
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    tft.setTextColor (RA8875_WHITE);
    tft.setCursor (INDENT, Q_Y);
    char line[128];
    sprintf (line, "New version %s is available. Update now?  ... ", new_ver);
    tft.print (line);
    uint16_t count_x = tft.getCursorX();
    uint16_t count_y = tft.getCursorY();
    uint8_t count_s = UPDATE_TO/1000U;
    tft.print(count_s);

    // draw yes/no boxes
    SBox no_b =  {INDENT, B_Y, BOX_W, BOX_H};
    SBox yes_b = {(uint16_t)(tft.width()-INDENT-BOX_W), B_Y, BOX_W, BOX_H};
    drawStringInBox ("No", no_b, true, RA8875_WHITE);
    drawStringInBox ("Yes", yes_b, false, RA8875_WHITE);

    // draw URL
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setCursor (INFO_X, INFO_Y);
    tft.print (F("Changes may be reviewed at clearskyinstitute.com/ham/HamClock"));

    // prep for potentially long wait
    closeDXCluster();       // prevent inbound msgs from clogging network
    closeGimbal();          // avoid dangling connection

    // wait for response or time out
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    drainTouch();
    uint32_t t0 = millis();
    while (count_s > 0) {

        // update countdown
        wdDelay(100);
        if (millis() - t0 >= 1000) {
            t0 = millis();
            tft.fillRect (count_x, count_y-30, 50, 40, RA8875_BLACK);
            tft.setCursor (count_x, count_y);
            tft.print(--count_s);
        }

        // check buttons
        SCoord s;
        if (readCalTouch(s) != TT_NONE) {
            if (inBox (s, yes_b)) {
                drawStringInBox ("Yes", yes_b, true, RA8875_WHITE);
                return (true);
            }
            if (inBox (s, no_b)) {
                drawStringInBox ("No", no_b, false, RA8875_WHITE);
                return (false);
            }
        }
    }

    // if get here we timed out
    return (false);
}

/* reload HamClock with the binary file above.
 * if successful HamClock is rebooted with new image so we never return.
 */
void doOTAupdate()
{
    // inform user
    eraseScreen();
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    tft.setCursor (0, 100);
    tft.setTextColor (RA8875_WHITE);
    tft.println (F("Beginning remote update..."));
    tft.println (F("  This may take a minute or so."));
    tft.println (F("  Do not interrupt power or WiFi during this process."));

    // go
    resetWatchdog();
    char url[128];
    sprintf (url, "http://%s%s", svr_host, v_bin);
    t_httpUpdate_return ret = ESPhttpUpdate.update(url);
    resetWatchdog();

    // should not get here if update worked
    switch (ret) {
    case HTTP_UPDATE_FAILED:
	tft.print(F("Update failed: "));
	    tft.print(ESPhttpUpdate.getLastError());
	    tft.print(' ');
	    tft.println (ESPhttpUpdate.getLastErrorString());
	break;
    case HTTP_UPDATE_NO_UPDATES:
	tft.println (F("No updates found"));
	break;
    case HTTP_UPDATE_OK:
	tft.println (F("Update Ok?!"));
	break;
    default:
	tft.print (F("Unknown failure code: "));
	tft.println (ret);
	break;
    }

    // message dwell
    wdDelay(5000);
}
