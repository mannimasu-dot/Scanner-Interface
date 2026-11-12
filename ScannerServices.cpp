#include "ScannerServices.h"
#include <commdlg.h>
#include <winspool.h>
#include <mapi.h>
#include <shellapi.h>
#include <sti.h>
#include <wia.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <climits>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "winspool.lib")
#pragma comment(lib, "sti.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "wiaguid.lib")
#pragma comment(lib, "shell32.lib")

namespace scanner {
namespace {
using Microsoft::WRL::ComPtr;

void CheckDeviceEvent(HRESULT hr, const char* operation) {
	if (FAILED(hr)) {
		std::ostringstream message;
		message << operation << " (HRESULT 0x" << std::hex << static_cast<unsigned long>(hr)
			<< "). Windows-WIA-Dienst, Geraetetreiber und Ereignisunterstuetzung pruefen. Kein WIA-Scanersatz.";
		throw std::runtime_error(message.str());
	}
}
struct EventComSession {
	HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	EventComSession() { if (result != RPC_E_CHANGED_MODE) CheckDeviceEvent(result, "COM fuer Geraeteereignisse nicht verfuegbar"); }
	~EventComSession() { if (SUCCEEDED(result)) CoUninitialize(); }
	EventComSession(const EventComSession&) = delete;
	EventComSession& operator=(const EventComSession&) = delete;
};
struct EventString {
	BSTR value;
	explicit EventString(const std::wstring& text) : value(SysAllocString(text.c_str())) {
		if (!value) throw std::bad_alloc();
	}
	~EventString() { SysFreeString(value); }
	EventString(const EventString&) = delete;
	EventString& operator=(const EventString&) = delete;
};
struct EventProperties {
	PROPVARIANT values[3]{};
	~EventProperties() { for (auto& value : values) PropVariantClear(&value); }
};
struct EventCapability {
	WIA_DEV_CAP value{};
	~EventCapability() { SysFreeString(value.bstrName); SysFreeString(value.bstrDescription); SysFreeString(value.bstrIcon); }
};
ComPtr<IWiaDevMgr> EventManager() {
	ComPtr<IWiaDevMgr> manager;
	CheckDeviceEvent(CoCreateInstance(CLSID_WiaDevMgr, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(manager.GetAddressOf())),
		"Windows-Geraeteereignisverwaltung nicht verfuegbar");
	return manager;
}
ComPtr<IEnumWIA_DEV_CAPS> DeviceCapabilities(IWiaDevMgr* manager, const std::wstring& deviceId) {
	EventString id(deviceId);
	ComPtr<IWiaItem> device;
	CheckDeviceEvent(manager->CreateDevice(id.value, device.GetAddressOf()), "Geraet fuer Ereignisverwaltung nicht verfuegbar");
	if (!device) throw std::runtime_error("Der Treiber stellt kein Windows-Ereignisgeraet bereit.");
	ComPtr<IEnumWIA_DEV_CAPS> capabilities;
	CheckDeviceEvent(device->EnumDeviceCapabilities(WIA_DEVICE_EVENTS, capabilities.GetAddressOf()), "Treiber stellt keine Windows-Geraeteereignisse bereit");
	if (!capabilities) throw std::runtime_error("Der Treiber stellt keine Ereignisauflistung bereit.");
	return capabilities;
}
std::wstring EventGuid(const GUID& guid) {
	wchar_t text[39]{};
	if (!StringFromGUID2(guid, text, 39)) throw std::runtime_error("Ereignis-GUID konnte nicht formatiert werden.");
	return text;
}
}

std::vector<DeviceEvent> EnumerateDeviceEvents() {
	EventComSession session;
	auto manager = EventManager();
	ComPtr<IEnumWIA_DEV_INFO> devices;
	CheckDeviceEvent(manager->EnumDeviceInfo(WIA_DEVINFO_ENUM_LOCAL, devices.GetAddressOf()), "Windows-Geraete konnten nicht aufgelistet werden");
	if (!devices) throw std::runtime_error("Windows stellt keine Geraeteauflistung bereit.");
	std::vector<DeviceEvent> result;
	for (;;) {
		ComPtr<IWiaPropertyStorage> properties;
		ULONG fetched = 0;
		HRESULT hr = devices->Next(1, properties.GetAddressOf(), &fetched);
		CheckDeviceEvent(hr, "Windows-Geraeteauflistung fehlgeschlagen");
		if (!fetched) break;
		if (!properties) throw std::runtime_error("Der Treiber liefert keine Geraeteeigenschaften.");
		PROPSPEC specs[3]{};
		const PROPID ids[] = { WIA_DIP_DEV_ID, WIA_DIP_DEV_NAME, WIA_DIP_DEV_TYPE };
		for (int n = 0; n < 3; ++n) { specs[n].ulKind = PRSPEC_PROPID; specs[n].propid = ids[n]; }
		EventProperties data;
		CheckDeviceEvent(properties->ReadMultiple(3, specs, data.values), "Geraeteeigenschaften konnten nicht gelesen werden");
		if (data.values[2].vt != VT_I4) throw std::runtime_error("Der Treiber liefert keinen gueltigen Geraetetyp.");
		if (GET_STIDEVICE_TYPE(data.values[2].lVal) != StiDeviceTypeScanner) continue;
		if (data.values[0].vt != VT_BSTR || !data.values[0].bstrVal || !*data.values[0].bstrVal ||
			data.values[1].vt != VT_BSTR || !data.values[1].bstrVal)
			throw std::runtime_error("Der Scannertreiber liefert keine gueltige Geraete-ID oder keinen Geraetenamen.");
		std::wstring deviceId(data.values[0].bstrVal), deviceName(data.values[1].bstrVal);
		auto capabilities = DeviceCapabilities(manager.Get(), deviceId);
		for (;;) {
			EventCapability capability;
			fetched = 0;
			CheckDeviceEvent(capabilities->Next(1, &capability.value, &fetched), "Geraeteereignisse konnten nicht gelesen werden");
			if (!fetched) break;
			result.push_back({deviceId, deviceName, EventGuid(capability.value.guid), capability.value.bstrName ? capability.value.bstrName : L""});
		}
	}
	if (result.empty()) throw std::runtime_error("Kein Scanner mit Windows-Geraeteereignissen gefunden. Passenden Ereignistreiber installieren; TWAIN bleibt der Scanweg.");
	return result;
}

void RegisterDeviceEvent(const std::wstring& deviceId, const std::wstring& eventId, bool remove) {
	if (deviceId.empty() || deviceId.find(L'\0') != std::wstring::npos)
		throw std::runtime_error("Eine konkrete, gueltige Geraete-ID ist erforderlich; keine globale Registrierung.");
	GUID guid{};
	if (eventId.size() != 38 || eventId.front() != L'{' || eventId.back() != L'}' ||
		eventId.find(L'\0') != std::wstring::npos || FAILED(CLSIDFromString(eventId.c_str(), &guid)) || IsEqualGUID(guid, GUID_NULL))
		throw std::runtime_error("Die Ereignis-ID muss eine gueltige, nicht leere GUID in geschweiften Klammern sein.");
	EventComSession session;
	auto manager = EventManager();
	auto capabilities = DeviceCapabilities(manager.Get(), deviceId);
	bool found = false;
	for (;;) {
		EventCapability capability;
		ULONG fetched = 0;
		CheckDeviceEvent(capabilities->Next(1, &capability.value, &fetched), "Geraeteereignis konnte nicht geprueft werden");
		if (!fetched) break;
		if (!IsEqualGUID(capability.value.guid, guid)) continue;
		if (!(capability.value.ulFlags & WIA_ACTION_EVENT)) throw std::runtime_error("Dieses Geraeteereignis unterstuetzt keinen Programmstart.");
		found = true;
		break;
	}
	if (!found) throw std::runtime_error("Der Geraetetreiber unterstuetzt die angegebene Ereignis-GUID nicht.");
	std::vector<wchar_t> path(32768);
	DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
	if (!length || length >= path.size()) throw std::runtime_error("Anwendungspfad fuer Ereignisregistrierung konnte nicht ermittelt werden.");
	EventString id(deviceId);
	EventString command(std::wstring(L"\"") + path.data() + L"\" /StiDevice:\"%1\" /StiEvent:\"%2\"");
	EventString name(L"Scanner");
	EventString description(L"Scanner-Geraeteereignisse (TWAIN-Bilderwerb)");
	EventString icon(std::wstring(L"\"") + path.data() + L"\",0");
	CheckDeviceEvent(manager->RegisterEventCallbackProgram(remove ? WIA_UNREGISTER_EVENT_CALLBACK : WIA_REGISTER_EVENT_CALLBACK,
		id.value, &guid, command.value, name.value, description.value, icon.value),
		remove ? "Geraeteereignis konnte nicht deregistriert werden" : "Geraeteereignis konnte nicht registriert werden");
}

std::vector<std::wstring> PrintPrinters() {
	DWORD needed = 0, count = 0; const DWORD flags = PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS;
	if (!EnumPrintersW(flags, nullptr, 4, nullptr, 0, &needed, &count) && GetLastError() != ERROR_INSUFFICIENT_BUFFER) throw std::runtime_error("Druckerliste konnte nicht gelesen werden.");
	std::vector<BYTE> buffer(needed); std::vector<std::wstring> printers;
	if (needed) { if (!EnumPrintersW(flags, nullptr, 4, buffer.data(), needed, &needed, &count)) throw std::runtime_error("Druckerliste konnte nicht gelesen werden."); auto entries = reinterpret_cast<const PRINTER_INFO_4W*>(buffer.data()); for (DWORD i = 0; i < count; ++i) printers.emplace_back(entries[i].pPrinterName); }
	DWORD length = 0; if (!GetDefaultPrinterW(nullptr, &length)) { DWORD error = GetLastError(); if (error != ERROR_INSUFFICIENT_BUFFER && error != ERROR_FILE_NOT_FOUND) throw std::runtime_error("Standarddrucker konnte nicht gelesen werden."); }
	if (length) { std::vector<wchar_t> name(length); if (!GetDefaultPrinterW(name.data(), &length)) throw std::runtime_error("Standarddrucker konnte nicht gelesen werden."); auto found = std::find(printers.begin(), printers.end(), name.data()); if (found != printers.end()) std::rotate(printers.begin(), found, found + 1); }
	return printers;
}
std::wstring DefaultPrinter() {
	DWORD length = 0;
	if (!GetDefaultPrinterW(nullptr, &length) && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
		throw std::runtime_error("Kein Windows-Standarddrucker verfuegbar. Bitte einen Standarddrucker in Windows festlegen.");
	if (!length) throw std::runtime_error("Windows meldet keinen Standarddrucker.");
	std::vector<wchar_t> name(length);
	if (!GetDefaultPrinterW(name.data(), &length) || !name[0]) throw std::runtime_error("Windows-Standarddrucker konnte nicht gelesen werden.");
	return name.data();
}
static DEVMODEW* PrintDeviceMode(std::vector<BYTE>& settings) {
	if (settings.size() < sizeof(DEVMODEW)) throw std::runtime_error("Ungueltige Druckereinstellungen.");
	auto mode = reinterpret_cast<DEVMODEW*>(settings.data()); if (mode->dmSize < sizeof(DEVMODEW) || size_t(mode->dmSize) + mode->dmDriverExtra > settings.size()) throw std::runtime_error("Unvollstaendige Druckereinstellungen."); return mode;
}
bool ConfigurePrinter(HWND owner, const std::wstring& printer, std::vector<BYTE>& deviceMode, bool prompt) {
	HANDLE handle = nullptr; if (!OpenPrinterW(const_cast<wchar_t*>(printer.c_str()), &handle, nullptr)) throw std::runtime_error("Drucker konnte nicht geoeffnet werden.");
	struct Printer { HANDLE value; ~Printer() { ClosePrinter(value); } } cleanup{handle};
	LONG size = DocumentPropertiesW(owner, handle, const_cast<wchar_t*>(printer.c_str()), nullptr, nullptr, 0);
	if (size < static_cast<LONG>(sizeof(DEVMODEW))) throw std::runtime_error("Druckertreiber meldet ungueltige Einstellungen.");
	std::vector<BYTE> result(size); auto mode = reinterpret_cast<DEVMODEW*>(result.data());
	if (DocumentPropertiesW(owner, handle, const_cast<wchar_t*>(printer.c_str()), mode, nullptr, DM_OUT_BUFFER) != IDOK) throw std::runtime_error("Druckereinstellungen konnten nicht gelesen werden.");
	if (prompt) {
		auto input = deviceMode.empty() ? PrintDeviceMode(result) : PrintDeviceMode(deviceMode);
		LONG status = DocumentPropertiesW(owner, handle, const_cast<wchar_t*>(printer.c_str()), mode, input, DM_IN_BUFFER | DM_OUT_BUFFER | DM_IN_PROMPT);
		if (status == IDCANCEL) return false; if (status != IDOK) throw std::runtime_error("Druckereinstellungen konnten nicht geaendert werden.");
	}
	PrintDeviceMode(result); deviceMode = std::move(result); return true;
}
bool PrintImages(HWND owner, const std::vector<Image>& images, const PrintOptions& options) {
	if (images.empty()) throw std::runtime_error("Keine Seiten zum Drucken.");
	if (options.copies < 1 || options.copies > 999) throw std::runtime_error("Exemplare muessen zwischen 1 und 999 liegen.");
	PRINTDLGW dialog{}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = owner; dialog.Flags = PD_RETURNDC | PD_NOPAGENUMS | PD_NOSELECTION | PD_USEDEVMODECOPIESANDCOLLATE; dialog.nCopies = 1;
	if (options.printer.empty()) { if (!PrintDlgW(&dialog)) { if (CommDlgExtendedError()) throw std::runtime_error("Druckerdialog konnte nicht geoeffnet werden."); return false; } }
	else {
		if (!options.output.empty() && std::filesystem::exists(options.output)) throw std::runtime_error("Druckausgabedatei existiert bereits.");
		std::vector<BYTE> settings = options.deviceMode;
		if (settings.empty()) ConfigurePrinter(owner, options.printer, settings, false);
		auto mode = PrintDeviceMode(settings); mode->dmFields |= DM_COPIES | DM_COLLATE; mode->dmCopies = 1; mode->dmCollate = DMCOLLATE_FALSE;
		dialog.hDC = CreateDCW(L"WINSPOOL", options.printer.c_str(), nullptr, mode); if (!dialog.hDC) throw std::runtime_error("Angegebener Drucker ist nicht verfuegbar.");
	}
	struct Cleanup { PRINTDLGW& d; ~Cleanup() { if (d.hDC) DeleteDC(d.hDC); if (d.hDevMode) GlobalFree(d.hDevMode); if (d.hDevNames) GlobalFree(d.hDevNames); } } cleanup{dialog};
	if (dialog.hDevMode) { auto mode = static_cast<DEVMODEW*>(GlobalLock(dialog.hDevMode)); if (mode) { mode->dmFields |= DM_COPIES | DM_COLLATE; mode->dmCopies = 1; mode->dmCollate = DMCOLLATE_FALSE; auto dc = ResetDCW(dialog.hDC, mode); GlobalUnlock(dialog.hDevMode); if (!dc) throw std::runtime_error("Druckertreiber konnte nicht auf anwendungsgesteuerte Exemplare umgestellt werden."); dialog.hDC = dc; } }
	std::wstring prn = options.output.wstring();
	if (dialog.Flags & PD_PRINTTOFILE) {
		std::vector<wchar_t> file(32768); wcscpy_s(file.data(), file.size(), L"Ausgabe.prn"); OPENFILENAMEW save{}; save.lStructSize = sizeof(save); save.hwndOwner = owner; save.lpstrFilter = L"Druckdatei (*.prn)\0*.prn\0\0"; save.lpstrFile = file.data(); save.nMaxFile = static_cast<DWORD>(file.size()); save.lpstrDefExt = L"prn"; save.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
		if (!GetSaveFileNameW(&save)) return false; prn = file.data();
	}
	DOCINFOW info{}; info.cbSize = sizeof(info); info.lpszDocName = L"Scanner-Dokument"; info.lpszOutput = prn.empty() ? nullptr : prn.c_str();
	const int job = StartDocW(dialog.hDC, &info);
	if (job <= 0) throw std::runtime_error("Druckauftrag konnte nicht begonnen werden.");
	if (options.jobId) *options.jobId = job;
	try {
		auto printPage = [&](const Image& source) {
			PixelReadScope release{source.pixels};
			source.Validate(); Image image = source;
			const int pw = GetDeviceCaps(dialog.hDC, HORZRES), ph = GetDeviceCaps(dialog.hDC, VERTRES); double dx = GetDeviceCaps(dialog.hDC, LOGPIXELSX), dy = GetDeviceCaps(dialog.hDC, LOGPIXELSY);
			if (pw <= 0 || ph <= 0 || dx <= 0 || dy <= 0) throw std::runtime_error("Drucker meldet ungueltige bedruckbare Flaeche.");
			double w = image.width * dx / image.dpiX, h = image.height * dy / image.dpiY;
			if (options.rotate && std::min(pw / h, ph / w) > std::min(pw / w, ph / h) && (w > pw || h > ph)) { Transform(image, Edit::RotateRight); w = image.width * dx / image.dpiX; h = image.height * dy / image.dpiY; }
			double scale = options.fit ? std::min(pw / w, ph / h) : 1.; int width = static_cast<int>(w * scale), height = static_cast<int>(h * scale);
			if (StartPage(dialog.hDC) <= 0) throw std::runtime_error("Druckseite konnte nicht begonnen werden.");
			BITMAPINFO bitmap{}; bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bitmap.bmiHeader.biWidth = image.width; bitmap.bmiHeader.biHeight = -static_cast<LONG>(image.height); bitmap.bmiHeader.biPlanes = 1; bitmap.bmiHeader.biBitCount = 32; bitmap.bmiHeader.biCompression = BI_RGB;
			SetStretchBltMode(dialog.hDC, HALFTONE); SetBrushOrgEx(dialog.hDC, 0, 0, nullptr);
			if (StretchDIBits(dialog.hDC, options.fit ? (pw - width) / 2 : 0, options.fit ? (ph - height) / 2 : 0, width, height, 0, 0, image.width, image.height, image.pixels.data(), &bitmap, DIB_RGB_COLORS, SRCCOPY) == GDI_ERROR) throw std::runtime_error("Druckbild konnte nicht ausgegeben werden.");
			if (EndPage(dialog.hDC) <= 0) throw std::runtime_error("Druckseite konnte nicht abgeschlossen werden.");
		};
		if (options.collate) for (int c = 0; c < options.copies; ++c) for (const auto& page : images) printPage(page);
		else for (const auto& page : images) for (int c = 0; c < options.copies; ++c) printPage(page);
		if (EndDoc(dialog.hDC) <= 0) throw std::runtime_error("Druckauftrag konnte nicht abgeschlossen werden.");
	} catch (...) { AbortDoc(dialog.hDC); throw; }
	return true;
}
namespace {
[[noreturn]] void MailIoError(const char* operation) {
	throw std::runtime_error(std::string(operation) + " (Windows-Fehler " + std::to_string(GetLastError()) + ").");
}
struct MailHandle {
	HANDLE value = INVALID_HANDLE_VALUE;
	explicit MailHandle(HANDLE handle = INVALID_HANDLE_VALUE) : value(handle) {}
	~MailHandle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
	MailHandle(MailHandle&& other) noexcept : value(std::exchange(other.value, INVALID_HANDLE_VALUE)) {}
	MailHandle(const MailHandle&) = delete;
	MailHandle& operator=(const MailHandle&) = delete;
};
std::string MailUtf8(const std::wstring& text) {
	if (text.empty()) return {};
	if (text.size() > INT_MAX) throw std::runtime_error("Mailtext ist zu lang.");
	int length = static_cast<int>(text.size());
	int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length, nullptr, 0, nullptr, nullptr);
	if (!size) throw std::runtime_error("Ungueltiges Unicode in Mailtext oder Dateipfad.");
	std::string result(size, '\0');
	if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length, result.data(), size, nullptr, nullptr) != size)
		MailIoError("UTF-8-Konvertierung fehlgeschlagen");
	return result;
}
std::filesystem::path MailPath(const std::filesystem::path& path) {
	const auto& text = path.native();
	if (text.empty() || text.find_first_of(L"\"<>|*?") != std::wstring::npos ||
		std::any_of(text.begin(), text.end(), [](wchar_t c) { return c < 32 || c == 127; }))
		throw std::runtime_error("Ungueltiger Mail-Dateipfad (Steuerzeichen oder Sonderpfad).");
	MailUtf8(text);
	auto absolute = std::filesystem::absolute(path).lexically_normal();
	const auto& full = absolute.native();
	if (full.rfind(L"\\\\.\\", 0) == 0 || full.find(L':', 2) != std::wstring::npos || absolute.filename().empty())
		throw std::runtime_error("Geraetepfade und alternative Datenstroeme sind als Maildatei nicht erlaubt.");
	return absolute;
}
struct MailAttachment {
	std::filesystem::path path;
	std::wstring name;
	std::string utf8Name;
	MailHandle file;
};
std::vector<MailAttachment> MailAttachments(const std::vector<std::filesystem::path>& paths) {
	if (paths.empty() || paths.size() > 1000) throw std::runtime_error("Keine oder zu viele Mailanhaenge (maximal 1000).");
	std::vector<MailAttachment> result;
	result.reserve(paths.size());
	for (const auto& input : paths) {
		auto path = MailPath(input);
		auto name = path.filename().wstring();
		MailHandle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
		if (file.value == INVALID_HANDLE_VALUE) MailIoError("Mailanhang konnte nicht gelesen werden");
		BY_HANDLE_FILE_INFORMATION info{};
		if (GetFileType(file.value) != FILE_TYPE_DISK || !GetFileInformationByHandle(file.value, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			throw std::runtime_error("Mailanhaenge muessen lesbare regulaere Dateien sein.");
		result.push_back({std::move(path), name, MailUtf8(name), std::move(file)});
	}
	return result;
}
std::wstring MailId() {
	GUID guid{};
	if (FAILED(CoCreateGuid(&guid))) throw std::runtime_error("Keine eindeutige Maildatei-ID verfuegbar.");
	wchar_t text[39]{};
	if (!StringFromGUID2(guid, text, 39)) throw std::runtime_error("Maildatei-ID konnte nicht formatiert werden.");
	return std::wstring(text + 1, 36);
}
void MailWrite(HANDLE file, std::string_view bytes) {
	while (!bytes.empty()) {
		DWORD written = 0;
		DWORD count = static_cast<DWORD>(std::min<size_t>(bytes.size(), 65536));
		if (!WriteFile(file, bytes.data(), count, &written, nullptr)) MailIoError("Maildatei konnte nicht geschrieben werden");
		if (!written) throw std::runtime_error("Maildatei konnte nicht vollstaendig geschrieben werden.");
		bytes.remove_prefix(written);
	}
}
DWORD MailRead(HANDLE file, unsigned char* buffer, DWORD capacity) {
	DWORD total = 0;
	while (total < capacity) {
		DWORD count = 0;
		if (!ReadFile(file, buffer + total, capacity - total, &count, nullptr)) MailIoError("Mailanhang konnte nicht vollstaendig gelesen werden");
		if (!count) break;
		total += count;
	}
	return total;
}
std::string MailBase64(std::string_view bytes) {
	static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string result;
	for (size_t n = 0; n < bytes.size(); n += 3) {
		unsigned a = static_cast<unsigned char>(bytes[n]);
		unsigned b = n + 1 < bytes.size() ? static_cast<unsigned char>(bytes[n + 1]) : 0;
		unsigned c = n + 2 < bytes.size() ? static_cast<unsigned char>(bytes[n + 2]) : 0;
		result += alphabet[a >> 2]; result += alphabet[((a & 3) << 4) | (b >> 4)];
		result += n + 1 < bytes.size() ? alphabet[((b & 15) << 2) | (c >> 6)] : '=';
		result += n + 2 < bytes.size() ? alphabet[c & 63] : '=';
	}
	return result;
}
std::string MailFilename(const std::string& utf8) {
	static constexpr char hex[] = "0123456789ABCDEF";
	std::string encoded;
	for (unsigned char c : utf8) { encoded += '%'; encoded += hex[c >> 4]; encoded += hex[c & 15]; }
	if (encoded.size() <= 48) return " filename*=UTF-8''" + encoded + "\r\n";
	std::string result;
	for (size_t n = 0, part = 0; n < encoded.size(); n += 42, ++part) {
		result += " filename*" + std::to_string(part) + "*=" + (part == 0 ? "UTF-8''" : "") + encoded.substr(n, 42);
		result += n + 42 < encoded.size() ? ";\r\n" : "\r\n";
	}
	return result;
}
struct MailPendingFile {
	std::filesystem::path path;
	MailHandle file;
	bool committed = false;
	MailPendingFile(const std::filesystem::path& directory, const std::wstring& extension) {
		for (int attempt = 0; attempt < 16; ++attempt) {
			path = directory / (L"Scanner-Mail-" + MailId() + extension);
			file.value = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file.value != INVALID_HANDLE_VALUE) return;
			if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) MailIoError("Neue Maildatei konnte nicht angelegt werden");
		}
		throw std::runtime_error("Keine kollisionsfreie Maildatei verfuegbar.");
	}
	~MailPendingFile() {
		if (file.value != INVALID_HANDLE_VALUE) CloseHandle(std::exchange(file.value, INVALID_HANDLE_VALUE));
		if (!committed) DeleteFileW(path.c_str());
	}
	MailPendingFile(const MailPendingFile&) = delete;
	MailPendingFile& operator=(const MailPendingFile&) = delete;
};
void MailSave(std::vector<MailAttachment>& attachments, const std::filesystem::path& target) {
	MailPendingFile pending(target.parent_path(), L".tmp");
	auto boundary = "Scanner_" + MailUtf8(MailId());
	SYSTEMTIME now{}; GetSystemTime(&now);
	static constexpr const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
	static constexpr const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
	char date[80]{};
	sprintf_s(date, "%s, %02u %s %04u %02u:%02u:%02u +0000", days[now.wDayOfWeek],
		static_cast<unsigned>(now.wDay), months[now.wMonth - 1], static_cast<unsigned>(now.wYear),
		static_cast<unsigned>(now.wHour), static_cast<unsigned>(now.wMinute), static_cast<unsigned>(now.wSecond));
	MailWrite(pending.file.value, std::string("Date: ") + date + "\r\nFrom: Scanner <scanner@localhost>\r\n"
		"Subject: =?UTF-8?B?" + MailBase64(MailUtf8(L"Gescannte Dokumente")) + "?=\r\n"
		"MIME-Version: 1.0\r\nX-Unsent: 1\r\nContent-Type: multipart/mixed;\r\n boundary=\"" + boundary + "\"\r\n\r\n"
		"--" + boundary + "\r\nContent-Type: text/plain; charset=UTF-8\r\nContent-Transfer-Encoding: 7bit\r\n\r\n"
		"Gescannte Dokumente im Anhang.\r\n");
	for (auto& attachment : attachments) {
		MailWrite(pending.file.value, "--" + boundary + "\r\nContent-Type: application/octet-stream\r\n"
			"Content-Disposition: attachment;\r\n" + MailFilename(attachment.utf8Name) + "Content-Transfer-Encoding: base64\r\n\r\n");
		std::array<unsigned char, 57> buffer{};
		for (;;) {
			DWORD count = MailRead(attachment.file.value, buffer.data(), static_cast<DWORD>(buffer.size()));
			if (!count) break;
			MailWrite(pending.file.value, MailBase64(std::string_view(reinterpret_cast<const char*>(buffer.data()), count)) + "\r\n");
		}
		MailWrite(pending.file.value, "\r\n");
	}
	MailWrite(pending.file.value, "--" + boundary + "--\r\n");
	if (!FlushFileBuffers(pending.file.value)) MailIoError("Mailentwurf konnte nicht gesichert werden");
	if (!CloseHandle(std::exchange(pending.file.value, INVALID_HANDLE_VALUE))) MailIoError("Mailentwurf konnte nicht abgeschlossen werden");
	// Kein REPLACE_EXISTING: Auch ein konkurrierend angelegtes Ziel bleibt unveraendert.
	if (!MoveFileExW(pending.path.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) MailIoError("Mailentwurf konnte nicht ohne Ueberschreiben veroeffentlicht werden");
	pending.committed = true;
}
}
void SaveMailDraft(const std::vector<std::filesystem::path>& attachments, const std::filesystem::path& path) {
	auto target = MailPath(path);
	auto files = MailAttachments(attachments);
	MailSave(files, target);
}
namespace {
bool MailAnsi(const std::wstring& text, std::string& result) {
	MailUtf8(text);
	if (text.empty()) { result.clear(); return true; }
	BOOL used = FALSE;
	bool utf8Acp = GetACP() == CP_UTF8;
	DWORD flags = utf8Acp ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
	int length = static_cast<int>(text.size());
	int size = WideCharToMultiByte(CP_ACP, flags, text.data(), length, nullptr, 0, nullptr, utf8Acp ? nullptr : &used);
	if (!size || used) return false;
	std::string converted(size, '\0');
	used = FALSE;
	if (WideCharToMultiByte(CP_ACP, flags, text.data(), length, converted.data(), size, nullptr, utf8Acp ? nullptr : &used) != size || used) return false;
	int wideSize = MultiByteToWideChar(CP_ACP, 0, converted.data(), size, nullptr, 0);
	if (wideSize != length) return false;
	std::wstring roundtrip(wideSize, L'\0');
	if (MultiByteToWideChar(CP_ACP, 0, converted.data(), size, roundtrip.data(), wideSize) != wideSize || roundtrip != text) return false;
	result = std::move(converted);
	return true;
}
std::filesystem::path MailTempDirectory() {
	std::vector<wchar_t> text(32768);
	DWORD length = GetTempPathW(static_cast<DWORD>(text.size()), text.data());
	if (!length || length >= text.size()) MailIoError("Temporaeres Mailverzeichnis nicht verfuegbar");
	return MailPath(std::filesystem::path(text.data()) / L"placeholder").parent_path();
}
struct MailStaging {
	std::filesystem::path directory;
	std::vector<std::filesystem::path> files;
	~MailStaging() {
		for (const auto& path : files) DeleteFileW(path.c_str());
		if (!directory.empty()) RemoveDirectoryW(directory.c_str());
	}
	bool Create() {
		auto base = MailTempDirectory();
		std::string ansi;
		if (!MailAnsi(base.wstring(), ansi)) {
			std::vector<wchar_t> shortPath(32768);
			DWORD length = GetShortPathNameW(base.c_str(), shortPath.data(), static_cast<DWORD>(shortPath.size()));
			if (!length || length >= shortPath.size() || !MailAnsi(shortPath.data(), ansi)) return false;
			base = shortPath.data();
		}
		for (int attempt = 0; attempt < 16; ++attempt) {
			auto candidate = base / (L"Scanner-Mail-" + MailId());
			if (CreateDirectoryW(candidate.c_str(), nullptr)) { directory = std::move(candidate); return true; }
			if (GetLastError() != ERROR_ALREADY_EXISTS) return false;
		}
		return false;
	}
};
ULONG MailUnicode(LPMAPISENDMAILW send, HWND owner, std::vector<MailAttachment>& attachments) {
	std::vector<MapiFileDescW> files(attachments.size());
	std::vector<std::wstring> paths;
	paths.reserve(attachments.size());
	for (const auto& attachment : attachments) paths.push_back(attachment.path.wstring());
	for (size_t n = 0; n < files.size(); ++n) {
		files[n].nPosition = static_cast<ULONG>(-1);
		files[n].lpszPathName = paths[n].data();
		files[n].lpszFileName = attachments[n].name.data();
	}
	wchar_t subject[] = L"Gescannte Dokumente";
	MapiMessageW message{};
	message.lpszSubject = subject;
	message.nFileCount = static_cast<ULONG>(files.size()); message.lpFiles = files.data();
	return send(0, reinterpret_cast<ULONG_PTR>(owner), &message, MAPI_LOGON_UI | MAPI_DIALOG | MAPI_FORCE_UNICODE, 0);
}
ULONG MailLegacy(LPMAPISENDMAIL send, HWND owner, std::vector<MailAttachment>& attachments) {
	std::vector<std::string> paths(attachments.size()), names(attachments.size());
	for (size_t n = 0; n < attachments.size(); ++n)
		if (!MailAnsi(attachments[n].name, names[n])) return MAPI_E_NOT_SUPPORTED;
	MailStaging staging;
	try {
		if (!staging.Create()) return MAPI_E_NOT_SUPPORTED;
		for (size_t n = 0; n < attachments.size(); ++n) {
			MailPendingFile copy(staging.directory, L".dat");
			if (!MailAnsi(copy.path.wstring(), paths[n])) return MAPI_E_NOT_SUPPORTED;
			std::vector<unsigned char> buffer(65536);
			for (;;) {
				DWORD count = MailRead(attachments[n].file.value, buffer.data(), static_cast<DWORD>(buffer.size()));
				if (!count) break;
				MailWrite(copy.file.value, std::string_view(reinterpret_cast<const char*>(buffer.data()), count));
			}
			if (!FlushFileBuffers(copy.file.value)) MailIoError("MAPI-Staging konnte nicht gesichert werden");
			staging.files.push_back(copy.path);
			copy.committed = true;
		}
	} catch (const std::runtime_error&) {
		return MAPI_E_NOT_SUPPORTED;
	}
	std::vector<MapiFileDesc> files(attachments.size());
	for (size_t n = 0; n < files.size(); ++n) {
		files[n].nPosition = static_cast<ULONG>(-1);
		files[n].lpszPathName = paths[n].data(); files[n].lpszFileName = names[n].data();
	}
	char subject[] = "Gescannte Dokumente";
	MapiMessage message{}; message.lpszSubject = subject;
	message.nFileCount = static_cast<ULONG>(files.size()); message.lpFiles = files.data();
	return send(0, reinterpret_cast<ULONG_PTR>(owner), &message, MAPI_LOGON_UI | MAPI_DIALOG, 0);
}
template<typename UnicodeCall, typename AnsiCall>
ULONG MailDispatch(bool hasUnicode, UnicodeCall unicode, bool hasAnsi, AnsiCall ansi) {
	ULONG result = hasUnicode ? unicode() : MAPI_E_NOT_SUPPORTED;
	if (hasAnsi && (!hasUnicode || result == MAPI_E_NOT_SUPPORTED || result == MAPI_E_UNICODE_NOT_SUPPORTED)) result = ansi();
	return result;
}
void MailOpenDraft(HWND owner, std::vector<MailAttachment>& attachments) {
	for (auto& attachment : attachments) {
		LARGE_INTEGER start{};
		if (!SetFilePointerEx(attachment.file.value, start, nullptr, FILE_BEGIN)) MailIoError("Mailanhang konnte nicht fuer MIME zurueckgesetzt werden");
	}
	auto path = MailTempDirectory() / (L"Scanner-Entwurf-" + MailId() + L".eml");
	MailSave(attachments, path);
	// Nur oeffnen: kein mailto:, kein send-Verb, kein versendender MAPI-Fallback.
	auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(owner, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
	if (result <= 32) throw std::runtime_error("Der MIME-Entwurf wurde gespeichert, konnte aber nicht geoeffnet werden: " +
		MailUtf8(path.wstring()) + " (Shell-Fehler " + std::to_string(result) + "). Bitte mit einem Mailprogramm als Entwurf oeffnen; es wurde nichts automatisch versendet.");
}
}
void PrepareMail(HWND owner, const std::vector<std::filesystem::path>& attachments) {
	auto files = MailAttachments(attachments);
	struct MailLibrary {
		HMODULE value = LoadLibraryExW(L"mapi32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
		~MailLibrary() { if (value) FreeLibrary(value); }
	} library;
	ULONG result = MAPI_E_NOT_SUPPORTED;
	if (library.value) {
		auto unicode = reinterpret_cast<LPMAPISENDMAILW>(GetProcAddress(library.value, "MAPISendMailW"));
		auto ansi = reinterpret_cast<LPMAPISENDMAIL>(GetProcAddress(library.value, "MAPISendMail"));
		result = MailDispatch(unicode != nullptr, [&] { return MailUnicode(unicode, owner, files); },
			ansi != nullptr, [&] { return MailLegacy(ansi, owner, files); });
	}
	if (result == SUCCESS_SUCCESS || result == MAPI_USER_ABORT) return;
	// Auch Provider-/Architekturfehler werden ausschliesslich als Dateientwurf abgefangen.
	MailOpenDraft(owner, files);
}
int RunServiceTests(const std::filesystem::path& directory, std::wstring& report) {
	int failures = 0, count = 0;
	auto test = [&](bool ok, const wchar_t* name) {
		++count; if (!ok) ++failures;
		report += (ok ? L"PASS " : L"FAIL ") + std::wstring(name) + L"\n";
	};
	auto rejects = [](auto&& action) {
		try { action(); } catch (const std::exception&) { return true; }
		return false;
	};
	{
		std::vector<BYTE> mode; test(rejects([&] { PrintDeviceMode(mode); }), L"Print settings reject empty driver buffer");
		mode.resize(sizeof(DEVMODEW) - 1); test(rejects([&] { PrintDeviceMode(mode); }), L"Print settings reject truncated public driver buffer");
		mode.resize(sizeof(DEVMODEW)); auto settings = reinterpret_cast<DEVMODEW*>(mode.data()); settings->dmSize = sizeof(DEVMODEW); settings->dmDriverExtra = 1;
		test(rejects([&] { PrintDeviceMode(mode); }), L"Print settings reject missing private driver bytes");
		mode.resize(sizeof(DEVMODEW) + 1); test(PrintDeviceMode(mode)->dmDriverExtra == 1, L"Print settings preserve private driver bytes");
		PrintDeviceMode(mode)->dmSize = 1; test(rejects([&] { PrintDeviceMode(mode); }), L"Print settings reject invalid public structure size");
	}
	try {
		for (ULONG result : {static_cast<ULONG>(MAPI_E_NOT_SUPPORTED), static_cast<ULONG>(MAPI_E_UNICODE_NOT_SUPPORTED)}) {
			int unicodeCalls = 0, ansiCalls = 0;
			auto returned = MailDispatch(true, [&] { ++unicodeCalls; return result; }, true, [&] { ++ansiCalls; return static_cast<ULONG>(SUCCESS_SUCCESS); });
			test(returned == SUCCESS_SUCCESS && unicodeCalls == 1 && ansiCalls == 1, L"Mail MAPI: unsupported Unicode retries ANSI once");
		}
		for (ULONG result : {static_cast<ULONG>(SUCCESS_SUCCESS), static_cast<ULONG>(MAPI_USER_ABORT), static_cast<ULONG>(MAPI_E_FAILURE)}) {
			int ansiCalls = 0;
			auto returned = MailDispatch(true, [&] { return result; }, true, [&] { ++ansiCalls; return static_cast<ULONG>(SUCCESS_SUCCESS); });
			test(returned == result && ansiCalls == 0, L"Mail MAPI: success, cancellation and other errors never open a second dialog");
		}
		int ansiCalls = 0; auto returned = MailDispatch(false, [] { return static_cast<ULONG>(MAPI_E_FAILURE); }, true, [&] { ++ansiCalls; return static_cast<ULONG>(SUCCESS_SUCCESS); });
		test(returned == SUCCESS_SUCCESS && ansiCalls == 1, L"Mail MAPI: missing Unicode entry uses ANSI");
		test(MailBase64("").empty() && MailBase64("f") == "Zg==" && MailBase64("fo") == "Zm8=" &&
			MailBase64("foo") == "Zm9v" && MailBase64("foob") == "Zm9vYg==" &&
			MailBase64("fooba") == "Zm9vYmE=" && MailBase64("foobar") == "Zm9vYmFy", L"Mail Base64 RFC vectors and padding");
		test(MailBase64(std::string("\0\xff\x80", 3)) == "AP+A", L"Mail Base64 unsigned binary bytes");
		const std::wstring unicode = L"\u00e4\u4e2d\U0001f4c4";
		const std::string utf8 = "\xc3\xa4\xe4\xb8\xad\xf0\x9f\x93\x84";
		test(MailUtf8(unicode) == utf8, L"Mail strict UTF8 including surrogate pair");
		test(rejects([&] { MailUtf8(std::wstring(1, static_cast<wchar_t>(0xd800))); }), L"Mail rejects unpaired surrogate");
		std::string ansi;
		test(MailAnsi(L"ASCII-123.txt", ansi) && ansi == "ASCII-123.txt", L"Mail ANSI ASCII roundtrip");
		bool represented = MailAnsi(unicode, ansi);
		std::wstring decoded;
		if (represented) {
			int length = MultiByteToWideChar(CP_ACP, 0, ansi.data(), static_cast<int>(ansi.size()), nullptr, 0);
			decoded.resize(length);
			if (length) MultiByteToWideChar(CP_ACP, 0, ansi.data(), static_cast<int>(ansi.size()), decoded.data(), length);
		}
		test(!represented || decoded == unicode, L"Mail ANSI never substitutes Unicode (also UTF8 ACP)");
		test(MailFilename("a\";\r\n") == " filename*=UTF-8''%61%22%3B%0D%0A\r\n", L"Mail RFC2231 cannot inject headers");
		test(MailFilename(utf8).find("%C3%A4%E4%B8%AD%F0%9F%93%84") != std::string::npos, L"Mail RFC2231 original UTF8 bytes");
		std::filesystem::create_directories(directory);
		auto root = MailPath(directory / (L"service-tests-" + MailId()));
		if (!std::filesystem::create_directory(root)) throw std::runtime_error("Mail-Testverzeichnis existiert bereits.");
		struct Cleanup {
			std::filesystem::path path;
			~Cleanup() { std::error_code error; std::filesystem::remove_all(path, error); }
		} cleanup{root};
		auto write = [&](const std::filesystem::path& path, const std::string& bytes) {
			MailHandle file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
			if (file.value == INVALID_HANDLE_VALUE) MailIoError("Mail-Testdatei konnte nicht angelegt werden");
			MailWrite(file.value, bytes);
		};
		auto read = [](const std::filesystem::path& path) {
			std::ifstream input(path, std::ios::binary);
			if (!input) throw std::runtime_error("Mail-Testdatei konnte nicht gelesen werden.");
			return std::string(std::istreambuf_iterator<char>(input), {});
		};
		auto unbase64 = [](const std::string& encoded) {
			const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
			std::string bytes;
			unsigned accumulator = 0; int bits = 0; bool padding = false;
			for (char c : encoded) {
				if (c == '\r' || c == '\n') continue;
				if (c == '=') { padding = true; continue; }
				auto value = alphabet.find(c);
				if (padding || value == std::string::npos) throw std::runtime_error("Ungueltiges Base64 im Mailtest.");
				accumulator = (accumulator << 6) | static_cast<unsigned>(value); bits += 6;
				if (bits >= 8) { bits -= 8; bytes += static_cast<char>((accumulator >> bits) & 255); }
			}
			return bytes;
		};
		std::string binary;
		for (int n = 0; n < 1025; ++n) binary += static_cast<char>(n & 255);
		auto source = root / (unicode + L".bin");
		auto empty = root / L"empty.bin";
		std::wstring longName(90, L'\u4e2d'); longName += L".bin";
		auto longSource = root / longName;
		write(source, binary); write(empty, ""); write(longSource, "long-name");
		auto draft = root / L"draft.eml";
		SaveMailDraft({source, empty, longSource, source}, draft);
		auto mime = read(draft);
		test(mime.find("Subject: =?UTF-8?B?R2VzY2FubnRlIERva3VtZW50ZQ==?=\r\n") != std::string::npos &&
			mime.find("MIME-Version: 1.0\r\nX-Unsent: 1\r\n") != std::string::npos &&
			mime.find("Content-Type: multipart/mixed;") != std::string::npos, L"Mail RFC5322 MIME draft and encoded subject");
		bool linesOk = true, fullBase64Line = false;
		for (size_t start = 0; start < mime.size();) {
			auto end = mime.find("\r\n", start);
			if (end == std::string::npos || end - start > 78) { linesOk = false; break; }
			auto line = mime.substr(start, end - start);
			if (line.find_first_of("\r\n") != std::string::npos) linesOk = false;
			if (line.size() == 76 && line.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=") == std::string::npos)
				fullBase64Line = true;
			start = end + 2;
		}
		test(linesOk && fullBase64Line, L"Mail CRLF and folded headers/Base64 76 columns");
		test(mime.find(MailFilename(MailUtf8(source.filename().wstring()))) != std::string::npos &&
			mime.find(" filename*0*=UTF-8''") != std::string::npos && mime.find(" filename*1*=") != std::string::npos,
			L"Mail Unicode filename and RFC2231 long continuation");
		auto longHeader = MailFilename(MailUtf8(longName));
		std::string recoveredName;
		for (size_t start = 0; start < longHeader.size();) {
			auto equal = longHeader.find('=', start);
			if (equal == std::string::npos) break;
			auto end = longHeader.find("\r\n", equal);
			std::string segment = longHeader.substr(equal + 1, end - equal - 1);
			if (start == 0) segment.erase(0, 7);
			if (!segment.empty() && segment.back() == ';') segment.pop_back();
			for (size_t n = 0; n < segment.size(); n += 3)
				recoveredName += static_cast<char>(std::stoul(segment.substr(n + 1, 2), nullptr, 16));
			start = end + 2;
		}
		test(recoveredName == MailUtf8(longName), L"Mail RFC2231 long filename byte roundtrip");
		auto boundaryStart = mime.find(" boundary=\"");
		if (boundaryStart == std::string::npos) throw std::runtime_error("MIME-Grenze fehlt im Mailtest.");
		boundaryStart += 11;
		auto boundary = mime.substr(boundaryStart, mime.find('"', boundaryStart) - boundaryStart);
		std::vector<std::string> bodies;
		size_t position = 0;
		const std::string transfer = "Content-Transfer-Encoding: base64\r\n\r\n";
		while ((position = mime.find(transfer, position)) != std::string::npos) {
			position += transfer.size();
			auto end = mime.find("\r\n--" + boundary, position);
			if (end == std::string::npos) throw std::runtime_error("MIME-Abschluss fehlt im Mailtest.");
			bodies.push_back(unbase64(mime.substr(position, end - position))); position = end;
		}
		test(bodies == std::vector<std::string>{binary, "", "long-name", binary} &&
			mime.ends_with("--" + boundary + "--\r\n"), L"Mail binary, empty, repeated attachments and closing boundary");
		for (size_t length : {size_t(1), size_t(2), size_t(3), size_t(56), size_t(57), size_t(58), size_t(114), size_t(115)})
			test(unbase64(MailBase64(binary.substr(0, length))) == binary.substr(0, length), L"Mail Base64 block-edge roundtrip");
		auto rejectedTarget = root / L"rejected.eml";
		test(rejects([&] { SaveMailDraft({}, rejectedTarget); }), L"Mail rejects empty attachment set");
		test(rejects([&] { SaveMailDraft(std::vector<std::filesystem::path>(1001, source), rejectedTarget); }), L"Mail rejects more than 1000 attachments");
		test(rejects([&] { SaveMailDraft({root / L"missing.bin"}, rejectedTarget); }), L"Mail rejects missing attachment");
		test(rejects([&] { SaveMailDraft({root}, rejectedTarget); }), L"Mail rejects directory attachment");
		test(rejects([&] { SaveMailDraft({source}, draft); }) && read(draft) == mime, L"Mail CREATE_NEW commit never overwrites existing draft");
		test(rejects([&] { SaveMailDraft({source}, source); }) && read(source) == binary, L"Mail draft never overwrites an attachment");
		test(rejects([&] { SaveMailDraft({source}, root / L"missing-parent" / L"draft.eml"); }), L"Mail rejects unavailable destination");
		std::wstring nulPath = source.wstring(); nulPath += L'\0'; nulPath += L".ignored";
		test(rejects([&] { SaveMailDraft({std::filesystem::path(nulPath)}, rejectedTarget); }), L"Mail rejects embedded NUL attachment path");
		test(rejects([&] { SaveMailDraft({source}, std::filesystem::path(nulPath)); }), L"Mail rejects embedded NUL target path");
		test(rejects([&] { SaveMailDraft({source}, root / L"evil\r\nBcc: victim.eml"); }), L"Mail rejects CRLF path injection");
		test(rejects([&] { SaveMailDraft({std::filesystem::path(source.wstring() + L":stream")}, rejectedTarget); }), L"Mail rejects alternate data streams");
		test(rejects([&] { MailPath(std::filesystem::path(L"\\\\.\\NUL")); }), L"Mail rejects device namespace");
		bool noTemporaryFiles = !std::filesystem::exists(rejectedTarget);
		for (const auto& entry : std::filesystem::directory_iterator(root))
			if (entry.path().filename().wstring().rfind(L"Scanner-Mail-", 0) == 0) noTemporaryFiles = false;
		test(noTemporaryFiles, L"Mail negative tests leave no partial draft or staging files");
	} catch (const std::exception& error) {
		std::string message = error.what();
		test(false, L"Mail test exception");
		report += std::wstring(message.begin(), message.end()) + L"\n";
	}
	report += L"Services: " + std::to_wstring(count) + L" tests, " + std::to_wstring(failures) + L" failures\n";
	return failures;
}
void RegisterDeviceApplication() {
	Microsoft::WRL::ComPtr<IStillImageW> image; HRESULT hr = StiCreateInstanceW(GetModuleHandleW(nullptr), STI_VERSION, image.GetAddressOf(), nullptr);
	if (FAILED(hr) || !image) throw std::runtime_error("Windows Still Image ist nicht verfuegbar. Die Geraetetastenfunktion benoetigt Treiberunterstuetzung.");
	std::vector<wchar_t> path(32768); DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
	if (!length || length >= path.size()) throw std::runtime_error("Anwendungspfad fuer Still-Image-Registrierung konnte nicht ermittelt werden.");
	wchar_t name[] = L"Scanner"; hr = image->RegisterLaunchApplication(name, path.data());
	if (FAILED(hr)) throw std::runtime_error("Registrierung als Still-Image-Anwendung fehlgeschlagen. Windows-Geraeteeinstellungen und Berechtigungen pruefen.");
}
}
