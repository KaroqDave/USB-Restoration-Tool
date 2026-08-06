#pragma once

namespace usbrestore {

// Whether the process token is a member of the local Administrators group with
// elevation in effect. The manifest already asks Windows for elevation, so a
// false here means the app is running in a way it was not built to run and
// should stop rather than fail later with an access-denied halfway through a
// disk operation.
bool isProcessElevated();

// Removes the current directory and the PATH from the DLL search order, so a
// planted DLL sitting next to a data file cannot be loaded into a process that
// runs as Administrator. Must be called before any other DLL is loaded, which
// means before the Qt application object exists.
bool enableSecureDllSearch();

} // namespace usbrestore
