/* Arduino Wire.cpp implemented for Raspberry Pi.
 * This is not meant to be comprehensive, only what we need for now.
 * Compiles on any UNIX but methods all return 0 unless _IS_RPI is defined.
 * see https://www.kernel.org/doc/Documentation/i2c/dev-interface
 */

#include "Wire.h"

#if defined(_IS_RPI)

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>

#endif // _IS_RPI

/* the traditional global Wire object
 */
TwoWire Wire;


/* constructor
 */
TwoWire::TwoWire()
{
#if defined(_IS_RPI)

        memset (rxdata, 0, sizeof(rxdata));
        memset (txdata, 0, sizeof(txdata));
        i2c_fd = -1;
        dev_addr = 0;
        n_txdata = 0;
        n_rxdata = 0;
        n_retdata = 0;
        transmitting = false;

#endif // _IS_RPI
}

/* destructor
 */
TwoWire::~TwoWire()
{
#if defined(_IS_RPI)
        closeConnection();
#endif // _IS_RPI
}


#if defined(_IS_RPI)
/* open connection if not already
 */
bool TwoWire::openConnection()
{
	const char filename[] = "/dev/i2c-1";
        if (i2c_fd < 0) {
            i2c_fd = ::open(filename, O_RDWR);
            if (i2c_fd < 0)
                fprintf (stderr, "I2C %s: %s\n", filename, strerror(errno));
        }

        return (i2c_fd >= 0);
}



/* close connection
 */
void TwoWire::closeConnection()
{
        if (i2c_fd >= 0) {
            fprintf (stderr, "I2C resetting bus\n");
            ::close (i2c_fd);
            i2c_fd = -1;
        }
}

#endif // _IS_RPI



#if defined(_IS_RPI)
/* set addr if different
 */
void TwoWire::setAddr (uint8_t addr)
{
        if (addr == dev_addr)
            return;

        dev_addr = addr;
        // printf ("I2C: setAddr(%02X)\n", addr);
        if (ioctl(i2c_fd, I2C_SLAVE, dev_addr) < 0) {
            fprintf (stderr, "I2C setAddr(%02X): %s\n", addr, strerror(errno));
            // mark as failed for subsequent use
            closeConnection ();
        }
}
#endif // _IS_RPI




/* start an I2C session
 */
void TwoWire::begin()
{
#if defined(_IS_RPI)
        (void) openConnection();
#endif // _IS_RPI
}



/* prepare to send bytes to the I2C slave at the given address
 */
void TwoWire::beginTransmission(uint8_t addr)
{
#if defined(_IS_RPI)

        // check for ready
        if (!openConnection()) {
            fprintf (stderr, "I2C beginTransmission(%02X): driver not open\n", addr);
            return;
        }

        // insure correct addr
        setAddr(addr);

        // init
        // printf ("I2C: beginTransmission(%02X)\n", addr);
        transmitting = true;
        n_txdata = 0;

#endif // _IS_RPI
}



/* buffer another byte to send.
 * returns number so buffered.
 */
size_t TwoWire::write(uint8_t datum)
{
#if defined(_IS_RPI)

        if (!transmitting) {
            fprintf (stderr, "I2C write called but not transmitting\n");
            return (1);         // yes, this is what the real Wire.cpp does
        }

        // printf ("I2C: write %02X\n", datum);

        // buffer if more room
        if (n_txdata < MAX_TXBUF) {
            txdata[n_txdata++] = datum;
            return (1);
        } else {
            fprintf (stderr, "I2C write buffer full\n");
            return (0);
        }

#else
        return (0);
#endif // _IS_RPI
}




/* buffer more bytes to send.
 * returns number so buffered
 */
size_t TwoWire::write(const uint8_t *data, size_t quantity)
{
#if defined(_IS_RPI)

        if (transmitting) {
            // printf ("I2C: transmitting %d\n", quantity);
            for(size_t i = 0; i < quantity; i++) {
                if(!write(data[i])) {
                    return (i);
                }
            }
        }

        return (quantity);

#else
        return (0);
#endif // _IS_RPI
}


/* if sendStop is true, don't do anything, requestFrom() will send n_txdata.
 * if !sendStop then send all buffered bytes to the I2C device specified in beginTransmission() then STOP.
 * see twi_writeTO() for return codes:
 * return 0: ok
 * return 1: ?
 * return 2: received NACK on transmit of address
 * return 3: received NACK on transmit of data
 * return 4: line busy
 */
uint8_t TwoWire::endTransmission(bool sendStop)
{
#if defined(_IS_RPI)

        // check for ready
        if (!openConnection()) {
            fprintf (stderr, "I2C endTransmission(): driver not open\n");
            return (4);
        }

        // printf ("I2C: endTransmission: %d bytes\n", n_txdata);

        // null case
        if (n_txdata == 0)
            return (0);

        if (!sendStop)
            return (0); // feign success for now

        // send
        int nw = ::write (i2c_fd, txdata, n_txdata);
        bool ok = (nw == n_txdata);

        // check return
        if (!ok) {
            if (nw == 0)
                fprintf (stderr, "I2C endTransmission() EOF, n %d\n", n_txdata);
            else if (nw < 0) {
                fprintf (stderr, "I2C endTransmission() n %d: %s\n", n_txdata, strerror(errno));
                closeConnection ();         // might reset the bus
            } else
                fprintf (stderr, "I2C endTransmission() short: %d of %d\n", nw, n_txdata);
        }

        // regardless, we tried
        n_txdata = 0;

        // done
        transmitting = false;

        return (ok ? 0 : 1);

#else
        return (1);
#endif // _IS_RPI
}



/* ask the I2C slave at the given address to send n bytes.
 * returns the actual number received.
 * N.B. if n_txdata > 0, we send that first without a STOP, then read
 */
uint8_t TwoWire::requestFrom(uint8_t addr, uint8_t nbytes)
{
#if defined(_IS_RPI)

        // check for ready
        if (!openConnection()) {
            fprintf (stderr, "I2C requestFrom(): driver not open\n");
            return (0);
        }

        // printf ("I2C: requestFrom %d bytes\n", nbytes);

        // clamp size
        if (nbytes > MAX_RXBUF) {
            fprintf (stderr, "I2C requestFrom(%02X,%d) too many, clamping to %d\n", addr, nbytes, MAX_RXBUF);
            nbytes = MAX_RXBUF;
        }

        // insure correct addr
        setAddr(addr);

        // n read
        int nr;

        // send then recv without intermediate STOP if txdata still not sent
        if (n_txdata > 0) {

            struct i2c_rdwr_ioctl_data work_queue;
            struct i2c_msg msg[2];

            work_queue.nmsgs = 2;
            work_queue.msgs = msg;

            work_queue.msgs[0].addr = addr;
            work_queue.msgs[0].len = n_txdata;
            work_queue.msgs[0].flags = 0;   // write
            work_queue.msgs[0].buf = txdata;

            work_queue.msgs[1].addr = addr;
            work_queue.msgs[1].len = nbytes;
            work_queue.msgs[1].flags = I2C_M_RD;
            work_queue.msgs[1].buf = rxdata;

            if (ioctl(Wire.i2c_fd,I2C_RDWR,&work_queue) < 0) {
                fprintf (stderr, "I2C_RDWR failed: %s\n", strerror(errno));
                nr = 0;
            } else {
                nr = nbytes;
            }

            // did out best to send
            n_txdata = 0;

        } else {

            // null case
            if (nbytes == 0)
                return (0);

            // rx
            nr = ::read (i2c_fd, rxdata, nbytes);

            // check return
            if (nr < 0) {
                fprintf (stderr, "I2C requestFrom(%02X,%d): %s\n", addr, nbytes, strerror(errno));
                nr = 0;
                closeConnection ();         // might reset the bus
            } else if (nr == 0)
                fprintf (stderr, "I2C requestFrom(%02X,%d) EOF\n", addr, nbytes);
            else if (nr < nbytes)
                fprintf (stderr, "I2C requestFrom(%02X,%d) short: %d\n", addr, nbytes, nr);

        }

        // save
        n_rxdata = nr;

        // prep for reading
        n_retdata = 0;

        // report actual
        return (nr);

#else
        return (0);
#endif // _IS_RPI
}


/* returns number of bytes available to read
 */
int TwoWire::available(void)
{
#if defined(_IS_RPI)

        return (n_rxdata);

#else
        return (0);
#endif // _IS_RPI
}


/* returns the next byte received from an earlier requestFrom()
 */
int TwoWire::read(void)
{
#if defined(_IS_RPI)

        // printf ("I2C: read returning %02X %d/%d\n", rxdata[n_retdata], n_retdata+1, n_rxdata);

        // return in read order
        if (n_retdata < n_rxdata)
            return (rxdata[n_retdata++]);
        else
            return (0x99);

#else
        return (0);
#endif // _IS_RPI
}
