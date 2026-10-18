#pragma once

namespace usbrestore {

// The manifest requests elevation, so false means stop now rather than fail mid-operation.
bool isProcessElevated();

} // namespace usbrestore
