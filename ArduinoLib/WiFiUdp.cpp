#include "WiFiUdp.h"


WiFiUDP::WiFiUDP()
{
	sockfd = -1;
}

WiFiUDP::~WiFiUDP()
{
	stop();
}

bool WiFiUDP::begin(int port)
{
        // create UDP socket
	sockfd = ::socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0)  {
	    fprintf (stderr, "socket: %s\n", strerror(errno));
	    return (false);
	}
	return (true);
}

bool WiFiUDP::beginMulticast (IPAddress ifIP, IPAddress mcIP, int port)
{
        // not used
        (void)(ifIP);

        // create UDP socket
	sockfd = ::socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0)  {
	    fprintf (stderr, "socket: %s\n", strerror(errno));
	    return (false);
	}

        // bind to mcIP
        struct sockaddr_in mcast_group;
        char mca[32];
        snprintf (mca, sizeof(mca), "%u.%u.%u.%u", mcIP[0], mcIP[1], mcIP[2], mcIP[3]);
        memset(&mcast_group, 0, sizeof(mcast_group));
        mcast_group.sin_family = AF_INET;
        mcast_group.sin_port = htons(port);
        mcast_group.sin_addr.s_addr = inet_addr(mca);
        if (bind(sockfd, (struct sockaddr*)&mcast_group, sizeof(mcast_group)) < 0) {
            ::close (sockfd);
            sockfd = -1;
	    fprintf (stderr, "bind: %s\n", strerror(errno));
	    return (false);
	}

        // join mcIP from anywhere
        struct ip_mreq mreq;
        mreq.imr_multiaddr = mcast_group.sin_addr;
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        if (setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
            ::close (sockfd);
            sockfd = -1;
	    fprintf (stderr, "IP_ADD_MEMBERSHIP: %s\n", strerror(errno));
	    return (false);
        }

        // ok
        return (true);
}


IPAddress WiFiUDP::remoteIP()
{
        IPAddress rip;
        unsigned a[4];
        char *string = inet_ntoa (remoteip.sin_addr);
        sscanf (string, "%u.%u.%u.%u", &a[0], &a[1], &a[2], &a[3]);
        rip[0] = a[0];
        rip[1] = a[1];
        rip[2] = a[2];
        rip[3] = a[3];
        return (rip);
}


void WiFiUDP::beginPacket (const char *host, int port)
{
        // get host
        struct hostent *server;
	server = ::gethostbyname(host);
	if (server == NULL) {
	    fprintf (stderr, "%s:%d: %s\n", host, port, strerror(errno));
	    return;
	}

        // connect
        struct sockaddr_in serveraddr;
	memset ((char *) &serveraddr, 0, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	memcpy((char *)&serveraddr.sin_addr.s_addr, (char *)server->h_addr, server->h_length);
	serveraddr.sin_port = htons(port);
	if (::connect( sockfd, (struct sockaddr *) &serveraddr, sizeof(serveraddr) ) < 0 ) {
	    fprintf (stderr, "Can not connect to %s:%d: %s\n", host, port, strerror(errno));
	    return;
	}
}

void WiFiUDP::write (uint8_t *buf, int n)
{
	w_n = n;	// save original count

	sendto_n = ::write(sockfd, buf, n);
	if (sendto_n < 0) {
	    fprintf (stderr, "sendto: %s\n", strerror(errno));
	    return;
	}
}

bool WiFiUDP::endPacket()
{
	// compare n sent to original count
	return (sendto_n == w_n);
}

int WiFiUDP::parsePacket()
{
	struct timeval tv;
	fd_set rset;
	tv.tv_sec = 0;		// don't block
	tv.tv_usec = 0;
	FD_ZERO (&rset);
	FD_SET (sockfd, &rset);

	// use select() so we can time out, just using read could hang forever
	int s = ::select (sockfd+1, &rset, NULL, NULL, &tv);
	if (s < 0) {
	    fprintf (stderr, "UDP select error: %s\n", strerror(errno));
	    return (false);
	}
	if (s == 0)
	    return (false);

        socklen_t rlen = sizeof(remoteip);
	r_n = ::recvfrom(sockfd, r_buf, sizeof(r_buf), 0, (struct sockaddr *)&remoteip, &rlen);
	if (r_n < 0) {
	    fprintf (stderr, "recvfrom: %s\n", strerror(errno));
	    return (false);
	}
	return (r_n);
}

int WiFiUDP::read(uint8_t *buf, int n)
{
	memcpy (buf, r_buf, n > r_n ? r_n : n);
	return (r_n);
}

void WiFiUDP::stop()
{
	if (sockfd >= 0) {
	    ::close (sockfd);
	    sockfd = -1;
	}
}
