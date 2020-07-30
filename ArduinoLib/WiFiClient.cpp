/* implement WiFiClient using normal UNIX sockets
 */


#include "IPAddress.h"
#include "WiFiClient.h"

WiFiClient::WiFiClient()
{
	// printf ("constructing wifi\n");
	socket = -1;
	n_peek = 0;
}

WiFiClient::WiFiClient(int fd)
{
	// printf ("setting socket to %d\n", fd);
	socket = fd;
	n_peek = 0;
}

WiFiClient::operator bool()
{
	// printf ("wifi return %d\n", socket != -1);
	return (socket != -1);
}

int WiFiClient::connect_to (int sockfd, struct sockaddr *serv_addr, int addrlen, int to_ms)
{
        unsigned int len;
        int err;
        int flags;
        int ret;

        /* set socket non-blocking */
        flags = fcntl (sockfd, F_GETFL, 0);
        (void) fcntl (sockfd, F_SETFL, flags | O_NONBLOCK);

        /* start the connect */
        ret = ::connect (sockfd, serv_addr, addrlen);
        if (ret < 0 && errno != EINPROGRESS)
            return (-1);

        /* wait for sockfd to become useable */
        ret = tout (to_ms, sockfd);
        if (ret < 0)
            return (-1);

        /* verify connection really completed */
        len = sizeof(err);
        err = 0;
        ret = getsockopt (sockfd, SOL_SOCKET, SO_ERROR, (char *) &err, &len);
        if (ret < 0)
            return (-1);
        if (err != 0) {
            errno = err;
            return (-1);
        }

        /* looks good - restore blocking */
        (void) fcntl (sockfd, F_SETFL, flags);
        return (0);
}

int WiFiClient::tout (int to_ms, int fd)
{
        fd_set rset, wset;
        struct timeval tv;
        int ret;

        FD_ZERO (&rset);
        FD_ZERO (&wset);
        FD_SET (fd, &rset);
        FD_SET (fd, &wset);

        tv.tv_sec = to_ms / 1000;
        tv.tv_usec = to_ms % 1000;

        ret = select (fd + 1, &rset, &wset, NULL, &tv);
        if (ret > 0)
            return (0);
        if (ret == 0)
            errno = ETIMEDOUT;
        return (-1);
}


bool WiFiClient::connect(const char *host, int port)
{
        struct addrinfo hints, *aip;
        char port_str[16];
        int sockfd;

        /* lookup host address.
         * N.B. must call freeaddrinfo(aip) after successful call before returning
         */
        memset (&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        sprintf (port_str, "%d", port);
        int error = ::getaddrinfo (host, port_str, &hints, &aip);
        if (error) {
            fprintf (stderr, "getaddrinfo(%s:%d): %s\n", host, port, gai_strerror(error));
            return (false);
        }

        /* create socket */
        sockfd = ::socket (aip->ai_family, aip->ai_socktype, aip->ai_protocol);
        if (sockfd < 0) {
            freeaddrinfo (aip);
            fprintf (stderr, "socket(%s:%d): %s\n", host, port, strerror(errno));
	    return (false);
        }

        /* connect */
        if (connect_to (sockfd, aip->ai_addr, aip->ai_addrlen, 5000) < 0) {
            fprintf (stderr, "connect(%s,%d): %s\n", host,port,strerror(errno));
            freeaddrinfo (aip);
            close (sockfd);
            return (false);
        }

        /* ok */
        freeaddrinfo (aip);
	socket = sockfd;
	n_peek = 0;
        return (true);
}

bool WiFiClient::connect(IPAddress ip, int port)
{
        char host[32];
        snprintf (host, sizeof(host), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        return (connect (host, port));
}

void WiFiClient::setNoDelay(bool on)
{
        // control Nagle algorithm
        socklen_t flag = on;
        if (setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (void *) &flag, sizeof(flag)) < 0)
            fprintf (stderr, "TCP_NODELAY(%d): %s\n", on, strerror(errno));     // not fatal
}

void WiFiClient::stop()
{
	if (socket >= 0) {
	    shutdown (socket, SHUT_RDWR);
	    close (socket);
	    socket = -1;
	    n_peek = 0;
	}
}

bool WiFiClient::connected()
{
	return (socket >= 0);
}

int WiFiClient::available()
{
        // none if closed
        if (socket < 0)
            return (0);

        // simple if unread bytes already available
	if (n_peek > 0)
	    return (1);

        // don't block
        struct timeval tv;
        fd_set rset;
        FD_ZERO (&rset);
        FD_SET (socket, &rset);
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        int s = select (socket+1, &rset, NULL, NULL, &tv);
        if (s < 0) {
	    stop();
	    return (0);
	}
        if (s == 0)
            return (0);

        // read more
	int n = ::read(socket, peek, sizeof(peek));
	if (n > 0) {
	    n_peek = n;
	    return (1);
	} else {
	    stop();
	    return (0);
	}
}

int WiFiClient::read()
{
	if (available()) {
	    int ret = peek[0];
	    memmove (peek, peek+1, --n_peek);
	    return (ret);
	}
	return (-1);
}

int WiFiClient::write (const uint8_t *buf, int n)
{
        // can't if closed
        if (socket < 0)
            return (0);

	int nw;
	for (int ntot = 0; ntot < n; ntot += nw) {
	    nw = ::write (socket, buf+ntot, n-ntot);
	    if (nw < 0) {
		fprintf (stderr, "write: %s\n", strerror(errno));
		return (0);
	    }
	    // printf ("%.*s", nw, buf+ntot);
	}
	return (n);
}

void WiFiClient::print (String s)
{
	const uint8_t *sp = (const uint8_t *) s.c_str();
	int n = strlen ((char*)sp);
	write (sp, n);
}

void WiFiClient::println (String s)
{
	const uint8_t *sp = (const uint8_t *) s.c_str();
	int n = strlen ((char*)sp);
	write (sp, n);
	write ((const uint8_t *) "\r\n", 2);
}

void WiFiClient::print (float f)
{
	char buf[32];
	int n = sprintf (buf, "%g", f);
	write ((const uint8_t *) buf, n);
}

void WiFiClient::print (float f, int s)
{
	char buf[32];
	int n = sprintf (buf, "%.*f", s, f);
	write ((const uint8_t *) buf, n);
}

void WiFiClient::println (float f)
{
	char buf[32];
	int n = sprintf (buf, "%g\r\n", f);
	write ((const uint8_t *) buf, n);
}

void WiFiClient::println (float f, int s)
{
	char buf[32];
	int n = sprintf (buf, "%.*f\r\n", s, f);
	write ((const uint8_t *) buf, n);
}

void WiFiClient::println (int i)
{
	char buf[32];
	int n = sprintf (buf, "%d\r\n", i);
	write ((const uint8_t *) buf, n);
}

void WiFiClient::println (uint32_t i)
{
	char buf[32];
	int n = sprintf (buf, "%u\r\n", i);
	write ((const uint8_t *) buf, n);
}

String WiFiClient::remoteIP()
{
	struct sockaddr_in sa;
	socklen_t len = sizeof(sa);

	getpeername(socket, (struct sockaddr *)&sa, &len);
	struct in_addr ipAddr = sa.sin_addr;

	char str[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &ipAddr, str, INET_ADDRSTRLEN);
	return (String(str));
}
