#ifndef _WIRE_H
#define _WIRE_H

/* Arduino Wire.cpp implemented for raspberry pi.
 * only what I need.
 */

#include <stdio.h>
#include <stdint.h>
#include <math.h>

#include "Arduino.h"

#define byte uint8_t

#define MAX_TXBUF       64      // max tx buffer
#define MAX_RXBUF       64      // max rx buffer

class TwoWire {
    public:

        TwoWire(void);
        ~TwoWire(void);

        void begin(void);
        void beginTransmission(uint8_t);
        size_t write(uint8_t);
        size_t write(const uint8_t *data, size_t quantity);
        uint8_t endTransmission(bool sendStop=true);
        uint8_t requestFrom(uint8_t, uint8_t);
        int available(void);
        int read(void);


    private:
#if defined(_IS_RPI)

        int i2c_fd;
        uint8_t dev_addr;
        uint8_t txdata[MAX_TXBUF];
        uint8_t rxdata[MAX_RXBUF];
        int n_txdata, n_rxdata, n_retdata;
        bool transmitting;
        void setAddr (uint8_t addr);
        bool openConnection (void);
        void closeConnection (void);
#endif // _IS_RPI
};

extern TwoWire Wire;

#endif // _WIRE_H
