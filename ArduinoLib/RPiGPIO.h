#ifndef _RPiGPIO_H
#define _RPiGPIO_H

/* Class to read and write the GPIO pins on Raspberry Pi.
 * N.B. methods take GPIO number, not header pin number.
 * This is a Meyers Singleton, it can not be instantiated, use RPiGPIO::getRPiGPIO() to gain access.
 *
 * data sheet: https://www.raspberrypi.org/documentation/hardware/raspberrypi/bcm2835/BCM2835-ARM-Peripherals.pdf
 * errata: https://elinux.org/BCM2835_datasheet_errata
 */

#include <stdint.h>
#include <pthread.h>

#include "Arduino.h"

class RPiGPIO {

    public:

        static RPiGPIO& getRPiGPIO(void);
        bool isReady(void);
        void setAsInput(uint8_t p);
        void setAsOutput(uint8_t p);
        void setHi(uint8_t p);
        void setHiLo (uint8_t p, bool hi);
        void setLo(uint8_t p);
        bool readPin (uint8_t p);

#if __cplusplus > 199711L

        // enforce no copy or move, only possible in c++11
        RPiGPIO(const RPiGPIO&) = delete;             // Copy ctor
        RPiGPIO(RPiGPIO&&) = delete;                  // Move ctor
        RPiGPIO& operator=(const RPiGPIO&) = delete;  // Copy assignment
        RPiGPIO& operator=(RPiGPIO&&) = delete;       // Move assignment

#endif


    private:

        RPiGPIO(void);

#if defined(_IS_RPI)
        bool ready;
        volatile uint32_t *gbase;
        inline uint32_t GPIO_SEL_MASK (uint8_t p, uint32_t m) {
            return (m<<(3*(p%10)));
        }
        pthread_mutex_t lock;
        bool mapGPIOAddress(char ynot[]);
#endif

};

#endif // _RPiGPIO_H
