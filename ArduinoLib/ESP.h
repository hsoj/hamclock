#ifndef _ESP_H
#define _ESP_H

#include "Arduino.h"

class ESP {

    public:

        ESP();

	void wdtDisable(void)
	{
            // noop
	}

	void wdtFeed(void)
	{
            // noop
	}

        void restart(void);

	uint32_t getFreeHeap(void);

        uint32_t getChipId(void);

    private:

        uint32_t sn;
};

extern class ESP ESP;

extern void yield(void);

#endif // _ESP_H
