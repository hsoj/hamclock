/* look up current weather
 */


#include "HamClock.h"

static const char wx_base[] = "/ham/HamClock/wx.pl";

#define	WX_STAYUP	15000		// time for wx display to stay up, millis() -- nice if == GPATH_LINGER

/* look up current weather info for the given location.
 * if wip is filled ok return true, else return false with short reason in ynot[]
 */
static bool getCurrentWX (float lat, float lng, bool is_de, WXInfo *wip, char ynot[])
{
    char line[128];
    WiFiClient wx_client;
    bool ok = false;

    Serial.println(wx_base);
    resetWatchdog();

    // get
    if (wifiOk() && wx_client.connect(svr_host, HTTPPORT)) {
        updateClocks(false);
        resetWatchdog();

        // query web page
	snprintf (line, sizeof(line), "%s?is_de=%d&lat=%g&lng=%g", wx_base, is_de, lat, lng);
	Serial.println (line);
        httpGET (wx_client, svr_host, line);

	// skip response header
	if (!httpSkipHeader (wx_client)) {
	    strcpy_P (ynot, PSTR("WX header error"));
	    goto out;
        }

	// crack response
	uint8_t n_found = 0;
	while (n_found < N_WXINFO_FIELDS && getTCPLine (wx_client, line, sizeof(line), NULL)) {
	    updateClocks(false);

	    // check for error message in which case abandon further search
	    if (sscanf (line, "error=%[^\n]", ynot) == 1)
		goto out;

	    // check for normal messages
	    if (sscanf (line, "city=%[^\n]", wip->city) == 1
			|| sscanf (line, "temperature_c=%f", &wip->temperature_c) == 1
			|| sscanf (line, "humidity_percent=%f", &wip->humidity_percent) == 1
			|| sscanf (line, "wind_speed_mps=%f", &wip->wind_speed_mps) == 1
			|| sscanf (line, "wind_dir_name=%[^\n]", wip->wind_dir_name) == 1
			|| sscanf (line, "clouds=%[^\n]", wip->clouds) == 1
			|| sscanf (line, "conditions=%[^\n]", wip->conditions) == 1
			|| sscanf (line, "attribution=%[^\n]", wip->attribution) == 1)
		n_found++;
	}
	if (n_found < N_WXINFO_FIELDS) {
	    strcpy_P (ynot, PSTR("No WX data"));
	    goto out;
	}

	// ok!
	ok = true;

    } else {

        strcpy_P (ynot, PSTR("WX connection failed"));

    }



    // clean up
out:
    wx_client.stop();
    resetWatchdog();
    printFreeHeap (F("getCurrentWX"));
    return (ok);
}

/* display the current DX weather in plot1_b then arrange to revert after WX_STAYUP
 */
void showDXWX()
{
    char ynot[32];
    WXInfo wi;

    if (getCurrentWX (rad2deg(dx_ll.lat), rad2deg(dx_ll.lng), false, &wi, ynot))
	plotWX (plot1_b, DX_COLOR, wi);
    else
	plotMessage (plot1_b, DX_COLOR, ynot);

    revertPlot1 (WX_STAYUP);
}

/* display the current DE weather in plot1_b then arrange to revert after WX_STAYUP
 */
void showDEWX()
{
    char ynot[32];
    WXInfo wi;

    if (getCurrentWX (rad2deg(de_ll.lat), rad2deg(de_ll.lng), true, &wi, ynot))
	plotWX (plot1_b, DE_COLOR, wi);
    else
	plotMessage (plot1_b, DE_COLOR, ynot);

    revertPlot1 (WX_STAYUP);
}
