/* this supports the Adafruit BME280 humidity, temperature & pressure sensor connected in I2C mode.
 */

#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

#include "HamClock.h"


static Adafruit_BME280 bme;			// I2C is default
bool bme280_connected;			        // set whether begin() succeeds

#define	I2CADDR		0x77			// sensor I2C address

// polling management. display period is ultimately N_SENS * SLOWEST_DT
#define N_SENS          100			// number of sensor data points to collect
#define GOSLOWER 	(5*60000L)     		// take data more slowly after up this long, millis()
#define GOSLOWEST 	(60*60000L)    		// take data even more slowly after up this long, millis()
#define INITIAL_DT	(5*1000L)      		// initial sensing period until GOSLOWER, millis()
#define SLOWER_DT 	(72*1000L)		// sensing period after GOSLOWER, millis()
#define SLOWEST_DT 	(900*1000L)    		// sensing period after GOSLOWEST, millis()

// data management
static float qTemp[N_SENS];			// circular queue of temperature values
static float qPres[N_SENS];			// circular queue of pressure values
static float qHum[N_SENS];			// circular queue of humidity values
static time_t qTime[N_SENS];			// circular queue of sensor read times
static uint8_t qhead;				// index of next q entries to use

// time management.
static uint32_t readDT = INITIAL_DT;		// period between readings, millis();
static uint32_t last_reading;			// last time sensors were read, millis()

/* try to connect to sensors.
 * set bme280_connected if succeed.
 */
static void connectSensors()
{
    if (bme.begin(I2CADDR)) {

	// Forced mode sleeps until read; normal mode runs continuously and warms the sensor
	bme.setSampling(Adafruit_BME280::MODE_FORCED,
                    Adafruit_BME280::SAMPLING_X1, // temperature
                    Adafruit_BME280::SAMPLING_X1, // pressure
                    Adafruit_BME280::SAMPLING_X1, // humidity
                    Adafruit_BME280::FILTER_OFF,
                    Adafruit_BME280::STANDBY_MS_1000);
	// note
	bme280_connected = true;

	// initial readings are a little flakey, read until fairly stable
	int nsmall = 0;
	float prev_t = 1e6;
	for (uint16_t i = 0; i < 50 && nsmall < 10; i++) {
	    float t = bme.readTemperature();
	    if (fabsf(t-prev_t) < 1)
		nsmall++;
	    else
		nsmall = 0;
	    prev_t = t;
	    wdDelay(100);
	}
    }

    Serial.print(F("BME280 ")); 
    if (bme280_connected)
        Serial.println(F("found"));
    else
        Serial.println(F("not found"));
}

/* read current temperature, pressure and humidity in units determined by useMetricUnits() into
 *   next q enttry. if ok advance q and return true.
 */
static bool readSensor ()
{
    bool ok = false;

    if (bme280_connected) {
	resetWatchdog();
	bme.takeForcedMeasurement();
	float t = bme.readTemperature();                        // C
	float p = bme.readPressure();                           // Pascals
	float h = bme.readHumidity();                           // percent
	if (isnan(t) || t < -40 || isnan(p) || isnan(h)) {
	    // try restarting
	    connectSensors();
	} else {
	    // all good
	    if (useMetricUnits()) {
		// want C and hPa
		qTemp[qhead] = t + getBMETempCorr();            // already C
		qPres[qhead] = p/100 + getBMEPresCorr();        // Pascals to hPa
	    } else {
		// want F and inches Hg
		qTemp[qhead] = 1.8*t + 32.0 + getBMETempCorr(); // C to F
		qPres[qhead] = p / 3386.39 + getBMEPresCorr();  // Pascals to inches Hg
	    }
	    qHum[qhead]  = h;
	    qTime[qhead] = now();

	    // ok
	    qhead = (qhead+1)%N_SENS;
	    ok = true;
	}
    }

    // record update time
    last_reading = millis();

    // return whether success
    return (ok);
}

/* convert temperature and relative humidity to dewpoint.
 * both temp units are as per useMetricUnits().
 * http://irtfweb.ifa.hawaii.edu/~tcs3/tcs3/Misc/Dewpoint_Calculation_Humidity_Sensor_E.pdf
 */
static float dewPoint (float T, float RH)
{
    // want C
    if (!useMetricUnits())
        T = 5.0F/9.0F*(T-32);           // F to C
    float H = (log10f(RH)-2)/0.4343F + (17.62F*T)/(243.12F+T);
    float Dp = 243.12F*H/(17.62F-H);
    if (!useMetricUnits())
        Dp = 9.0F/5.0F*Dp + 32;         // C to F
    return (Dp);
}

/* see whether a sensor is attached and init plot3_ch
 */
void initBME280()
{
    // try to connect to sensors
    connectSensors();

    // if no sensor, insure plot3_ch is not for a sensor
    if (!bme280_connected) {
	switch (plot3_ch) {
	case PLOT3_TEMP:        // fallthru
	case PLOT3_PRESSURE:	// fallthru
	case PLOT3_HUMIDITY:    // fallthru
	case PLOT3_DEWPOINT:
	    plot3_ch = PLOT3_SDO_1;
	    break;
	}
    }

}


/* arrange last read time so next call to updateBME280() always makes a fresh reading
 */
void initBME280Retry()
{
	last_reading = 0;
}

/* retrieve another set of sensor data from the queue, oldest first.
 * first call with *np = 0, then we increment each time and know to stop when it reaches N_SENS.
 * return whether another set is returned.
 */
bool nextBME280Data (time_t *t, float *temp, float *pressure, float *humidity, float *dp, uint8_t *np)
{
    resetWatchdog();

    // first time
    time_t t0 = qTime[qhead];

    // return next valid entry, skipping any initial unset entries and staying within the time limit
    while (*np < N_SENS) {
	uint8_t qi = (qhead + (*np)++) % N_SENS;
	time_t tqi = qTime[qi];
	if (tqi > 0 && (!t0 || tqi - t0 < N_SENS*SLOWEST_DT)) {
	    *t        = tqi;
	    *temp     = qTemp[qi];
	    *pressure = qPres[qi];
	    *humidity = qHum[qi];
	    *dp = dewPoint (qTemp[qi], qHum[qi]);
	    return (true);
	}
    }

    return (false);
}

/* if it is time, add another set of readings to the sensor value arrays at qhead and plot.
 * N.B. ignore if no sensors connected or clock not set.
 * N.B. collect but don't plot unless selected
 */
void updateBME280()
{
    resetWatchdog();

    if (!bme280_connected || !clockTimeOk())
	return;

    uint32_t t0 = millis();

    if (!last_reading || t0 - last_reading >= readDT) {

	// read new values into queues and advance
	if (readSensor ()) {

	    // slow down after a little while
	    if (t0 > GOSLOWEST)
		readDT = SLOWEST_DT;
	    else if (t0 > GOSLOWER)
		readDT = SLOWER_DT;

	    // update current plot if choice is sensor data
	    switch (plot3_ch) {
	    case PLOT3_TEMP:         // fallthru
	    case PLOT3_PRESSURE:     // fallthru
	    case PLOT3_HUMIDITY:     // fallthru
            case PLOT3_DEWPOINT:
		resetWatchdog();
		plotBME280 ();
		break;
	    }
	}
    }
}

/* plot the current sensor choice
 */
void plotBME280 ()
{
    if (!bme280_connected)
	return;

    // prepare the appropriate plot
    float *q;
    const char *ylabel;
    uint16_t color;
    switch (plot3_ch) {
    case PLOT3_TEMP:
	q = qTemp;
	if (useMetricUnits())
	    ylabel = "Temp, C";
	else
	    ylabel = "Temp, F";
	color = 0xFBEF;
	break;
    case PLOT3_PRESSURE:
	q = qPres;
	if (useMetricUnits())
	    ylabel = "Press, hPa";
	else
	    ylabel = "Press, inHg";
	color = RA8875_YELLOW;
	break;
    case PLOT3_HUMIDITY:
	q = qHum;
	ylabel = "Humidity, %";
	color = RA8875_CYAN;
	break;
    case PLOT3_DEWPOINT:
	q = NULL;               // special case, see below
	if (useMetricUnits())
	    ylabel = "Dew point, C";
	else
	    ylabel = "Dew point, F";
	color = RA8875_GREEN;
	break;
    default: 
        // not showing a sensor
        return;
    }

    // x axis depends on time span
    const char *xlabel;
    float time_scale;
    if (readDT >= SLOWEST_DT ) {
	xlabel = "Hours";
	time_scale = -3600.0F;
    } else {
	xlabel = "Minutes";
	time_scale = -60.0F;
    }

    // build x and y
    // N.B. dewpoint is derived from qTemp and qHum
    float x[N_SENS], y[N_SENS];
    time_t t0 = now();
    uint8_t nxy = 0;					// count entries with valid times
    resetWatchdog();
    for (uint8_t i = 0; i < N_SENS; i++) {
	uint8_t qi = (qhead + i) % N_SENS;		// oldest .. newest == qhead .. qhead-1
	if (qTime[qi] > 0) {				// skip if not set
	    x[nxy] = (t0 - qTime[qi])/time_scale;	// minutes ago .. beware unsigned time_t
	    if (plot3_ch == PLOT3_DEWPOINT) {
                y[nxy] = dewPoint (qTemp[qi], qHum[qi]);
            } else {
                y[nxy] = q[qi];
            }
	    nxy++;
	}
    }

    // plot, with a bit more precision for imperial pressure
    if (plot3_ch == PLOT3_PRESSURE && !useMetricUnits()) {
        char buf[32];
        sprintf (buf, "%.2f", y[nxy-1]);
        plotXYstr (plot3_b, x, y, nxy, xlabel, ylabel, color, buf);
    } else {
        plotXY (plot3_b, x, y, nxy, xlabel, ylabel, color, y[nxy-1]);
    }
}
