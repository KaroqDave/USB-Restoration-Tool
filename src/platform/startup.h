#pragma once

namespace usbrestore {

// Must run first: before any DLL or shared object loads and before QApplication exists.
void hardenProcessStartup();

} // namespace usbrestore
