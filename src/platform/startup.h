#pragma once

namespace usbrestore {

// Process-level hardening that has to happen before anything else runs: before
// the first DLL or shared object is loaded and before the Qt application
// object exists. What it does differs per platform; that it runs first does
// not.
void hardenProcessStartup();

} // namespace usbrestore
