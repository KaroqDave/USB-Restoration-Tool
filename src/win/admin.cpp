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
    // Excludes the current directory and %PATH%, the easiest for an unprivileged user to plant in.
    if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
                                  LOAD_LIBRARY_SEARCH_USER_DIRS)) {
        // Fallback for older systems: at least drops the current directory.
        SetDllDirectoryW(L"");
    }
}

} // namespace usbrestore
