#include "win/Admin.h"

#include <QString>
#include <Windows.h>
#include <shellapi.h>

namespace usbrestore {

bool isProcessElevated()
{
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}

bool relaunchElevated()
{
    wchar_t modulePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) == 0) {
        return false;
    }

    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.lpVerb = L"runas";
    info.lpFile = modulePath;
    info.nShow = SW_SHOWNORMAL;

    return ShellExecuteExW(&info) == TRUE;
}

}
