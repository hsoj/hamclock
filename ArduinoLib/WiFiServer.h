#ifndef _WIFI_SERVER_H
#define _WIFI_SERVER_H

#include "WiFiClient.h"

class WiFiServer {

    public:

	WiFiServer(int newport);
	bool begin();
	WiFiClient available();

    private:

	int port;
	int socket;
};


#endif // _WIFI_SERVER_H
