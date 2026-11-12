#pragma once
#include "ScannerTwain.h"

namespace scanner {
// WIA only: name is the friendly name; manufacturer stores the device ID.
std::vector<ScanSource> WiaSources();
// Explicit WIA acquisition; source and transfer are TWAIN-specific and unused.
ScanResult WiaScan(HWND owner, const std::wstring& deviceId, const ScanOptions& options);
int RunWiaTests(std::wstring& report);
}
