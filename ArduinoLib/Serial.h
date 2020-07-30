#ifndef _SERIAL_H
#define _SERIAL_H

#include <stdio.h>
#include <stdarg.h>

#include "Arduino.h"

class Serial {

    public:

        Serial()
        {
            // synchronous output for debugging
            setbuf (stdout, NULL);
        }

	void begin (int baud)
	{
	}

	void print (char *s)
	{
	    printf ("%s", s);
	}

	void print (const char *s)
	{
	    printf ("%s", s);
	}

	void print (int i)
	{
	    printf ("%d", i);
	}

	void print (String s)
	{
	    printf ("%s", s.c_str());
	}

	void println (char *s)
	{
	    printf ("%s\n", s);
	}

	void println (const char *s)
	{
	    printf ("%s\n", s);
	}

	void println (int i)
	{
	    printf ("%d\n", i);
	}

	int printf (const char *fmt, ...)
	{
	    va_list ap;
	    va_start (ap, fmt);
	    int n = vprintf (fmt, ap);
	    va_end (ap);
	    return (n);
	}

	operator bool()
	{
	    return (true);
	}
};

extern class Serial Serial;

#endif // _SERIAL_H
