#pragma once
#include "ScannerCore.h"
namespace scanner {
struct ScanSource { std::wstring name; std::wstring manufacturer; };
struct ScanOptions {
	std::wstring source;
	int mode = 2;
	double dpi = 300;
	double widthMm = 210, heightMm = 297;
	double xMm = 0, yMm = 0;
	bool maximumArea = false;
	double brightness = 0;
	bool feeder = false, duplex = false, driverDialog = true;
	// -1: prefer supported memory transfer; 0: native; 2: memory.
	int transfer = -1;
	uint32_t timeoutMs = 90000;
};
struct CapabilityRange { double minimum = 0, maximum = 0, step = 0, current = 0, defaultValue = 0; bool available = false; };
struct ScanCapabilities {
	bool feeder = false, duplex = false, brightness = false;
	int flatbed = -1;
	int feederLoaded = -1;
	bool uiControllable = false;
	double maxWidthMm = 0, maxHeightMm = 0;
	std::vector<double> resolutions;
	std::vector<double> yResolutions, transferMechanisms, pixelTypes;
	CapabilityRange xResolutionRange, yResolutionRange;
};
struct ScanResult {
	std::vector<Image> pages;
	bool completed = false, cancelled = false;
	std::string error;
	std::filesystem::path diagnostics;
	std::filesystem::path recoveryDirectory;
};
std::vector<ScanSource> TwainSources(HWND owner);
ScanCapabilities TwainCapabilities(HWND owner, const std::wstring& source, bool enableFeeder = false);
ScanResult TwainScan(HWND owner, const ScanOptions& options);
int RunTwainTests(std::wstring& report);
int TwainHelperMain(const std::filesystem::path& directory, const std::wstring& operation);
}
