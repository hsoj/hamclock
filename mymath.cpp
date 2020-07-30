#include "HamClock.h"



/* fmod() is broken on ESP8266
 */

float
myfmodf (float a, float n)
{
    return (a - (n * (int32_t)(a/n)));
}


/* atof() was broken at least up through 2.2.0 then was fixed by 2.4.0.
 * it failed parsing strings with sci notation.
 * Rather than become dependent on a lib version, this hack works regardless.
 */
float myatof (const char *s)
{
    // make a local copy so we can split at the exponent
    char buf[32];
    strncpy (buf, s, sizeof(buf));
    buf[sizeof(buf)-1] = '\0';

    // find the exponent, if any, in either upper or lower case
    char *ep = strchr (buf, 'e');
    if (!ep)
        ep = strchr (buf, 'E');

    // replace exponent with EOS so atof only sees the mantissa
    if (ep) 
        *ep = '\0';

    // crack the mantissa
    float v = atof(buf);

    // multiply by exponent, if any
    if (ep)
        v *= powf (10.0F, (float)atoi(ep+1));

    // that should do it.
    return (v);
}
