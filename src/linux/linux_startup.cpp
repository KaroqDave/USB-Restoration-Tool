#include "platform/startup.h"

#include <sys/prctl.h>
#include <sys/stat.h>

namespace usbrestore {

void hardenProcessStartup()
{
    // Under sudo or pkexec a core dump would land where the user's session can read it.
    prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);

    // Root-owned files such as the log must not be readable by anyone else.
    umask(S_IRWXG | S_IRWXO);
}

} // namespace usbrestore
