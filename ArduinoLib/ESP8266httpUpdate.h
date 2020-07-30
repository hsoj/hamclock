#ifndef _HTTP_UPDATE_H
#define _HTTP_UPDATE_H

typedef int t_httpUpdate_return;

class ESPhttpUpdate
{
    public:

	t_httpUpdate_return update(const char *url);
	int getLastError(void);
	const char *getLastErrorString(void);
};

enum {
	HTTP_UPDATE_OK,
	HTTP_UPDATE_FAILED,
	HTTP_UPDATE_NO_UPDATES,
};

extern ESPhttpUpdate ESPhttpUpdate;

#endif // _HTTP_UPDATE_H
