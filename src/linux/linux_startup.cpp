#include "platform/startup.h"

#include <sys/prctl.h>
#include <sys/stat.h>

namespace usbrestore {

void hardenProcessStartup()
{
    // Called by both binaries: the GUI, reached through sudo from an ordinary
    // session, and the helper, reached through pkexec from one. Either way a
    // core dump would land in a place that session can read, carrying whatever
    // was in memory at the time.
    prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);

    // Files the app creates — the log, above all — belong to root and should
    // not be readable or writable by anyone else by default.
    umask(S_IRWXG | S_IRWXO);
}

} // namespace usbrestore
