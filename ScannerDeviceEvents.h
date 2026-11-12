#pragma once
#include <string>
#include <vector>

namespace scanner {
struct DeviceEvent {
	std::wstring deviceId, deviceName, eventId, eventName;
};
// WIA is used only for Windows device events, never for image acquisition.
// Throws std::runtime_error if the driver/service cannot provide device events.
std::vector<DeviceEvent> EnumerateDeviceEvents();
// Registers only the specified device/event for this executable; remove unregisters it.
void RegisterDeviceEvent(const std::wstring& deviceId, const std::wstring& eventId, bool remove);
}
