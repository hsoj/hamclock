#include "Arduino.h"
#include "ESP.h"
#include "ESP8266httpUpdate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

class ESPhttpUpdate ESPhttpUpdate;

static const char *err_msg;
static const char *tmp_dir = "/tmp";

/* popen cmd, set err_msg if fail
 */
static bool runCommand (const char *cmd, const char *err)
{
	char rsp[2048];

	printf ("Running: %s\n", cmd);

	FILE *pp = popen (cmd, "r");
	if (!pp) {
	    printf ("Error: %s\n", err);
	    err_msg = err;
	    return false;
	}
	while (fgets (rsp, sizeof(rsp), pp))
	    printf ("%s", rsp);
	int pexit = pclose (pp);
	if (pexit) {
	    printf ("Error: %s\n", err);
	    err_msg = err;
	    return (false);
	}

	return (true);
}

/* given argv[0] return full path and whether successful.
 * set err_msg if trouble
 */
static bool findFullPath (char *me, char fullpath[], size_t fplen)
{
        // require current dir
        char cwd[1024];
        if (!getcwd (cwd, sizeof(cwd))) {
            err_msg = "Could get CWD";
            return (false);
        }

        // open candidate to insure real
        FILE *fp = NULL;
        if (me[0] == '/') {
            // looks like me is a full path already
            strncpy (fullpath, me, fplen-1);
            fullpath[fplen-1] = '\0';
            fp = fopen (fullpath, "r");
        } else if (strchr (me, '/')) {
            // contains / but doesn't start with it so me might be a path relative to PWD
            snprintf (fullpath, fplen, "%s/%s", cwd, me);
            fp = fopen (fullpath, "r");
        } else {
            // no slashes, so look for it at each PATH entry, beware "."
            char *onepath, *path = strdup (getenv ("PATH")), *tofree = path;   // don't clobber the real PATH
            while ((onepath = strsep(&path, ":")) != NULL) {
                if (strcmp (onepath, ".") == 0)
                    snprintf (fullpath, fplen, "%s/%s", cwd, me);
                else
                    snprintf (fullpath, fplen, "%s/%s", onepath, me);
                fp = fopen (fullpath, "r");
                if (fp)
                    break;
            }
            free(tofree);
        }

        // success if opened 
        if (fp) {
            fclose (fp);
            return (true);
        }

        // fail if could not open
        err_msg = "Could not find current executable";
        return (false);
}


/* url is curl path to new zip file
 */
t_httpUpdate_return ESPhttpUpdate::update(const char *url)
{
	char cmd[8192];

	printf ("Update url: %s\n", url);

	// extract zipfile name
	const char *zipfile = strrchr (url, '/');
	if (!zipfile) {
	    printf ("url %s has no zipfile?\n", url);
	    err_msg = "Bad fetch url";
	    return (HTTP_UPDATE_FAILED);
	}
	zipfile += 1;		// skip /

	// download url
	snprintf (cmd, sizeof(cmd), "cd %s; curl --silent '%s' > %s", tmp_dir, url, zipfile);
	if (!runCommand (cmd, "Error fetching new version"))
	    return (HTTP_UPDATE_FAILED);

	// find make dir from base of zipfile
	const char *zip_ext = strrchr (zipfile, '.');
	if (!zip_ext) {
	    printf ("zipfile %s has no extension?\n", zipfile);
	    err_msg = "Bad zip file name";
	    return (HTTP_UPDATE_FAILED);
	}
	char make_dir[128];
	snprintf (make_dir, sizeof(make_dir), "%.*s", (int)(zip_ext - zipfile), zipfile);

	// explode
	snprintf (cmd, sizeof(cmd), "cd %s; rm -fr %s; unzip %s", tmp_dir, make_dir, zipfile);
	if (!runCommand (cmd, "Error unzipping new version"))
	    return (HTTP_UPDATE_FAILED);

	// make the same target we are
	char *me = strrchr (our_name, '/');
	if (me)
	    me += 1;		// skip /
	else
	    me = our_name;	// already basename
	snprintf (cmd, sizeof(cmd), "cd %s/%s; make -j 3 %s", tmp_dir, make_dir, me);
	if (!runCommand (cmd, "Error making new version"))
	    return (HTTP_UPDATE_FAILED);

	// find full path to running version and overwrite executable wih new version
	char full_path[2048];
        if (!findFullPath (our_name, full_path, sizeof(full_path)))
	    return (HTTP_UPDATE_FAILED);                // already set err_msg
        printf ("Found program at %s\n", full_path);
	snprintf (cmd, sizeof(cmd), "cd %s/%s; rm -f %s; mv %s %s",
				tmp_dir, make_dir, full_path, me, full_path);
	if (!runCommand (cmd, "Error overwriting new version"))
	    return (HTTP_UPDATE_FAILED);

	// clean up
	snprintf (cmd, sizeof(cmd), "cd %s; rm -fr %s %s", tmp_dir, make_dir, zipfile);
	if (!runCommand (cmd, "Error cleaning up"))
	    return (HTTP_UPDATE_FAILED);

	// close all connections and execute over ourselves
        ESP.restart();

	// never get here if successful
	err_msg = "Failed to start new version";
	return (HTTP_UPDATE_FAILED);
}

int ESPhttpUpdate::getLastError(void)
{
	return (1);
}

const char *ESPhttpUpdate::getLastErrorString(void)
{
	return (err_msg);
}
