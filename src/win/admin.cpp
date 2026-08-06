#include "win/admin.h"

#include "platform/startup.h"

#include <Windows.h>

namespace usbrestore {

bool isProcessElevated()
{
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(
            &ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        if (!CheckTokenMembership(nullptr, adminGroup, &isAdmin)) {
            isAdmin = FALSE;
        }
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}

void hardenProcessStartup()
{
    // System32, the directory the executable was loaded from, and directories
    // added explicitly. Notably not the current directory and not %PATH%,
    // which are the two an unprivileged user can most easily influence for a
    // process that will be running as Administrator.
    if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
                                  LOAD_LIBRARY_SEARCH_USER_DIRS)) {
        // Older systems without SetDefaultDllDirectories still honour this,
        // which at least takes the current directory out of the search.
        SetDllDirectoryW(L"");
    }
}

} // namespace usbrestore
