/* simple RPi GPIO access.
 * Can not be instantiated, use RPiGPIO::getRPiGPIO() to gain access.
 */

#include "RPiGPIO.h"

#if defined (_IS_RPI)

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>

#include <bcm_host.h>


/* constructor. will only be called once because it is not public and there is only one instance
 * within a static context. This is the hallmark of a Meyers Singleton.
 */
RPiGPIO::RPiGPIO()
{
        // prepare gpio access
        char ynot[1024];
        ready = mapGPIOAddress(ynot);
        if (ready) {
            // init lock for safe threaded access
            pthread_mutexattr_t lock_attr;
            pthread_mutexattr_init (&lock_attr);
            pthread_mutexattr_settype (&lock_attr, PTHREAD_MUTEX_RECURSIVE);
            pthread_mutex_init (&lock, &lock_attr);
        } else {
            // note why not
            fprintf (stderr, "RPiGPIO: %s\n", ynot);
        }

}

RPiGPIO& RPiGPIO::getRPiGPIO()
{
        static RPiGPIO the_one;         // the only instance, hence only one call to constructor
        return the_one;
}

bool RPiGPIO::isReady()
{
    return (ready);
}

void RPiGPIO::setAsInput(uint8_t p)
{
        if (!ready)
            return;

        pthread_mutex_lock(&lock);

        gbase[p/10] &= ~GPIO_SEL_MASK(p,7);

        // enable pullup -- BCM2835
        gbase[37] = 2;
        usleep(10);
        gbase[38+p/32] = 1UL << (p%32);
        usleep(10);
        gbase[37] = 0;
        gbase[38+p/32] = 0;

        // enable pullup -- BCM2711
        gbase[57+p/16] = (gbase[57+p/16] & ~(3UL << 2*(p%16))) | (1UL << 2*(p%16));

        pthread_mutex_unlock(&lock);
}

void RPiGPIO::setAsOutput(uint8_t p)
{
        if (!ready)
            return;

        pthread_mutex_lock(&lock);
        setAsInput(p);  // TODO?
        gbase[p/10] = (gbase[p/10] & ~GPIO_SEL_MASK(p,7)) | GPIO_SEL_MASK(p,1);
        pthread_mutex_unlock(&lock);
}

void RPiGPIO::setHi(uint8_t p)
{
        if (!ready)
            return;

        pthread_mutex_lock(&lock);
        gbase[7+p/32] = 1UL << (p%32);
        pthread_mutex_unlock(&lock);
}


void RPiGPIO::setLo(uint8_t p)
{
        if (!ready)
            return;

        pthread_mutex_lock(&lock);
        gbase[10+p/32] = 1UL << (p%32);
        pthread_mutex_unlock(&lock);
}

void RPiGPIO::setHiLo (uint8_t p, bool hi)
{
        if (!ready)
            return;

        if (hi)
            setHi (p);
        else
            setLo (p);
}

bool RPiGPIO::readPin (uint8_t p)
{
        if (!ready)
            return(false);

        pthread_mutex_lock(&lock);
        bool hi = (gbase[13+p/32] & (1UL<<(p%32))) != 0;
        pthread_mutex_unlock(&lock);
        return (hi);
}

/* set gbase so it points to the physical address of the GPIO controller.
 * return true if ok, else false with brief excuse in ynot[].
 */
bool RPiGPIO::mapGPIOAddress(char ynot[])
{
        // access kernel physical address
        const char memfile[] = "/dev/mem";
        int fd = open (memfile, O_RDWR|O_SYNC);
        if (fd < 0) {
            sprintf (ynot, "%s: %s", memfile, strerror(errno));
            return (false);
        }

        /* mmap basic GPIO */
        unsigned paddr = 0x200000 + bcm_host_get_peripheral_address();
        // printf ("bcm_host_get_peripheral_address: 0x%X\n", bcm_host_get_peripheral_address());
        gbase = (uint32_t *) mmap (NULL, 4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, paddr);

        // fd not needed after setting up mmap
        close(fd);

        // check for error, leave gbase 0 if so
        if (gbase == MAP_FAILED) {
            gbase = NULL;
            sprintf (ynot, "mmap: %s", strerror(errno));
            return (false);
        }

        // worked
        return (true);
}

#else // !_IS_RPI

// dummy

RPiGPIO::RPiGPIO() { }

RPiGPIO& RPiGPIO::getRPiGPIO()
{
        static RPiGPIO the_one;         // the only instance, hence only one call to constructor
        return the_one;
}

void RPiGPIO::setAsInput(uint8_t p) { } 

void RPiGPIO::setAsOutput(uint8_t p) { }

void RPiGPIO::setHi(uint8_t p) { }

void RPiGPIO::setLo(uint8_t p) { }

void RPiGPIO::setHiLo (uint8_t p, bool hi) { }

bool RPiGPIO::readPin (uint8_t p) { return false; }

#endif
