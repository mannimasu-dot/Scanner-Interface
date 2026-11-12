#include "ScannerTwain.h"
#include <commctrl.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace scanner {
namespace tw {
#pragma pack(push, 2)
struct Version { uint16_t major, minor, language, country; char info[34]; };
struct Identity { uint32_t id; Version version; uint16_t major, minor; uint32_t groups; char manufacturer[34], family[34], name[34]; };
struct Capability { uint16_t cap, container; HANDLE handle; };
struct One { uint16_t type; uint32_t item; };
struct Fix { int16_t whole; uint16_t fraction; };
struct ImageInfo { Fix xResolution, yResolution; int32_t width, height; int16_t samples, bits[8], bitsPerPixel; uint16_t planar; int16_t pixelType; uint16_t compression; };
struct Frame { Fix left, top, right, bottom; };
struct Layout { Frame frame; uint32_t document, page, frameNumber; };
struct UserInterface { uint16_t show, modal; HWND parent; };
struct Event { void* event; uint16_t message; };
struct Pending { uint16_t count; uint32_t endOfJob; };
struct Status { uint16_t condition, reserved; };
struct SetupMemory { uint32_t minimum, maximum, preferred; };
struct Memory { uint32_t flags, length; void* data; };
struct MemoryTransfer { uint16_t compression; uint32_t bytesPerRow, columns, rows, xOffset, yOffset, bytesWritten; Memory memory; };
#pragma pack(pop)
static_assert(sizeof(Identity) == 156);
static_assert(sizeof(MemoryTransfer) == 26 + sizeof(Memory));
using Entry = uint16_t (WINAPI*)(Identity*, Identity*, uint32_t, uint16_t, uint16_t, void*);
constexpr uint16_t CAP_FEEDERENABLED = 0x1002, CAP_FEEDERLOADED = 0x1003, CAP_DUPLEX = 0x1012, CAP_DUPLEXENABLED = 0x1013;
constexpr uint16_t ICAP_PIXELTYPE = 0x0101, ICAP_UNITS = 0x0102, ICAP_XFERMECH = 0x0103;
constexpr uint16_t ICAP_BITDEPTH = 0x112b;
static constexpr uint32_t BitDepth(int mode) { return mode == 0 ? 1u : mode == 1 ? 8u : 24u; }
static_assert(BitDepth(0) == 1 && BitDepth(1) == 8 && BitDepth(2) == 24);
static constexpr bool UseDuplex(bool feederLoaded, bool supported, bool requestedFeeder, bool requestedDuplex) {
	return feederLoaded && supported && (!requestedFeeder || requestedDuplex);
}
static_assert(UseDuplex(true, true, false, false));
static_assert(UseDuplex(true, true, true, true));
static_assert(!UseDuplex(true, true, true, false));
static_assert(!UseDuplex(false, true, false, true));
static_assert(!UseDuplex(true, false, false, true));
constexpr uint16_t ICAP_BRIGHTNESS = 0x1101, ICAP_XRESOLUTION = 0x1118, ICAP_YRESOLUTION = 0x1119, ICAP_PHYSICALWIDTH = 0x1111, ICAP_PHYSICALHEIGHT = 0x1112;
static uint32_t Fixed(double value) { auto whole = static_cast<int32_t>(std::floor(value)); auto fraction = static_cast<uint32_t>(std::llround((value - whole) * 65536.)); if (fraction == 65536) { ++whole; fraction = 0; } return static_cast<uint16_t>(static_cast<int16_t>(whole)) | (fraction << 16); }
static double Unfixed(uint32_t value) { return static_cast<int16_t>(value & 0xffff) + (value >> 16) / 65536.; }
static double Coordinate(const Fix& f) { return f.whole + f.fraction / 65536.; }
static Frame AreaFrame(const ScanOptions& options) {
	Frame frame{}; uint32_t values[]{Fixed(options.xMm / 25.4), Fixed(options.yMm / 25.4), Fixed((options.xMm + options.widthMm) / 25.4), Fixed((options.yMm + options.heightMm) / 25.4)};
	memcpy(&frame, values, sizeof(frame)); return frame;
}
static void MaximumArea(ScanOptions& options, const ScanCapabilities& caps) {
	if (!std::isfinite(caps.maxWidthMm) || !std::isfinite(caps.maxHeightMm) || caps.maxWidthMm <= 0 || caps.maxHeightMm <= 0) throw std::runtime_error("TWAIN: Maximaler Scanbereich nicht bekannt. Bitte Scanner-Menue oder einen definierten Scanbereich verwenden.");
	options.xMm = options.yMm = 0; options.widthMm = caps.maxWidthMm; options.heightMm = caps.maxHeightMm;
}
static bool SameRasterFrame(const Frame& requested, const Frame& actual, double dpi) {
	auto withinRaster = [&](const Fix& a, const Fix& b) { return std::abs(Coordinate(a) - Coordinate(b)) <= 1. / dpi + 1. / 65536.; };
	return withinRaster(requested.left, actual.left) && withinRaster(requested.top, actual.top) && withinRaster(requested.right, actual.right) && withinRaster(requested.bottom, actual.bottom);
}
static std::string DescribeFrame(const Frame& f) { std::ostringstream text; text << std::setprecision(12) << Coordinate(f.left) << ',' << Coordinate(f.top) << ',' << Coordinate(f.right) << ',' << Coordinate(f.bottom); return text.str(); }
static std::wstring Wide(const char* text) { size_t n = strnlen_s(text, 34); int size = MultiByteToWideChar(CP_ACP, 0, text, static_cast<int>(n), nullptr, 0); std::wstring out(size, L' '); MultiByteToWideChar(CP_ACP, 0, text, static_cast<int>(n), out.data(), size); return out; }
static size_t ItemSize(uint16_t type) { switch (type) { case 0: case 3: return 1; case 1: case 4: case 6: return 2; case 2: case 5: case 7: return 4; default: return 0; } }
static double Number(uint16_t type, uint32_t value) {
	switch (type) { case 0: return static_cast<int8_t>(value); case 1: return static_cast<int16_t>(value); case 2: return static_cast<int32_t>(value); case 3: return value & 255; case 4: case 6: return value & 65535; case 5: return value; case 7: return Unfixed(value); default: throw std::runtime_error("TWAIN: Nichtnumerischer Capability-Typ."); }
}
struct Values { uint16_t type = 0; std::vector<uint32_t> items; CapabilityRange range; uint32_t current = 0; bool hasCurrent = false; };
static Values Decode(uint16_t container, const uint8_t* data, size_t bytes) {
	if (bytes < 2) throw std::runtime_error("TWAIN: Capability-Container zu kurz.");
	Values out; memcpy(&out.type, data, 2); size_t itemSize = ItemSize(out.type);
	if (!itemSize) throw std::runtime_error("TWAIN: Capability-Typ nicht unterstuetzt.");
	auto word = [&](size_t offset) { uint32_t value = 0; if (offset > bytes || bytes - offset < 4) throw std::runtime_error("TWAIN: Capability-Container unvollstaendig."); memcpy(&value, data + offset, 4); return value; };
	if (container == 5) { out.current = word(2); out.items.push_back(out.current); out.hasCurrent = true; }
	else if (container == 6) {
		out.range = {Number(out.type, word(2)), Number(out.type, word(6)), Number(out.type, word(10)), Number(out.type, word(18)), Number(out.type, word(14)), true};
		out.current = word(18); out.hasCurrent = true;
		if (out.range.minimum > out.range.maximum || out.range.step < 0) throw std::runtime_error("TWAIN: Ungueltiger Wertebereich.");
	} else if (container == 3 || container == 4) {
		size_t offset = container == 4 ? 14 : 6; uint32_t count = word(2);
		if (count > 65536 || offset > bytes || count > (bytes - offset) / itemSize) throw std::runtime_error("TWAIN: Ungueltige Capability-Liste.");
		for (uint32_t n = 0; n < count; ++n) { uint32_t item = 0; memcpy(&item, data + offset + n * itemSize, itemSize); out.items.push_back(item); }
		if (container == 4) { uint32_t index = word(6), defaultIndex = word(10); if (index >= count || defaultIndex >= count) throw std::runtime_error("TWAIN: Ungueltiger Capability-Index."); out.current = out.items[index]; out.hasCurrent = true; }
	} else throw std::runtime_error("TWAIN: Unbekannter Capability-Container.");
	return out;
}
class Trace {
	std::ofstream stream;
	ULONGLONG start = GetTickCount64();
public:
	explicit Trace(const std::filesystem::path& path) : stream(path, std::ios::binary) { if (!stream) throw std::runtime_error("TWAIN-Diagnoseprotokoll konnte nicht angelegt werden."); }
	void Write(const std::string& line) { stream << GetTickCount64() - start << "ms " << line << '\n'; stream.flush(); }
};
class Session {
	HMODULE library = nullptr; Entry entry = nullptr; Identity app{}, source{}; HWND parent = nullptr; bool manager = false, opened = false; int state = 3; Trace* trace = nullptr;
	uint16_t lastCondition = 0xffff;
public:
	explicit Session(Entry testEntry) : entry(testEntry), state(4) {}
	explicit Session(HWND owner, Trace* log = nullptr) : parent(owner), trace(log) {
		Log("Session start; pointerBits=" + std::to_string(sizeof(void*) * 8));
		std::vector<wchar_t> windows(32768);
		UINT length = GetWindowsDirectoryW(windows.data(), static_cast<UINT>(windows.size()));
		if (length && length < windows.size()) { auto legacy = std::filesystem::path(windows.data()) / L"twain_32.dll"; library = LoadLibraryExW(legacy.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32); }
		else Log("Windows directory unavailable or too long; trying TWAINDSM.dll");
		if (!library) library = LoadLibraryExW(L"TWAINDSM.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
		if (!library) throw std::runtime_error("TWAIN-Datenquellenverwaltung fehlt fuer diese Prozessarchitektur. Der Brother-Treiber benoetigt den x86-Hilfsprozess TwainHost.exe.");
		Log("DSM loaded");
		entry = reinterpret_cast<Entry>(GetProcAddress(library, "DSM_Entry")); if (!entry) { FreeLibrary(library); library = nullptr; throw std::runtime_error("TWAIN DSM_Entry fehlt."); }
		app.version = {1, 0, 6, 49, "Scanner 1.0"}; app.major = 1; app.minor = 9; app.groups = 3;
		strcpy_s(app.manufacturer, "Scanner Project"); strcpy_s(app.family, "Document Scanner"); strcpy_s(app.name, "Scanner");
		Log("BEGIN OPENDSM"); auto openedManager = entry(&app, nullptr, 1, 4, 0x301, &parent); Log("END OPENDSM RC=" + std::to_string(openedManager));
		if (openedManager != 0) { FreeLibrary(library); library = nullptr; throw std::runtime_error("TWAIN-Verwaltung konnte nicht geoeffnet werden."); } manager = true;
	}
	~Session() {
		if (state == 7) { Pending p{}; if (Call(1, 5, 0x701, &p) == 0) state = p.count ? 6 : 5; }
		if (state == 6) { Pending p{}; if (Call(1, 5, 7, &p) == 0) state = 5; }
		if (state == 5) { UserInterface ui{0, 0, parent}; if (Call(1, 9, 0x501, &ui) == 0) state = 4; }
		if (opened) entry(&app, nullptr, 1, 3, 0x402, &source);
		if (manager) entry(&app, nullptr, 1, 4, 0x302, &parent);
		if (library) FreeLibrary(library);
	}
	std::vector<Identity> List() { std::vector<Identity> result; Identity id{}; auto r = entry(&app, nullptr, 1, 3, 4, &id); while (r == 0) { result.push_back(id); if (result.size() > 1000) throw std::runtime_error("TWAIN meldet zu viele Quellen."); r = entry(&app, nullptr, 1, 3, 5, &id); } if (r != 7) throw std::runtime_error("TWAIN-Quellenliste konnte nicht vollstaendig gelesen werden."); return result; }
	void Open(const std::wstring& name) { Log("BEGIN source enumeration/open"); auto list = List(); auto it = std::find_if(list.begin(), list.end(), [&](const Identity& id) { return Wide(id.name) == name; }); if (it == list.end()) throw std::runtime_error("Die ausgewaehlte TWAIN-Quelle ist nicht mehr verfuegbar."); source = *it; Log("BEGIN OPENDS source=" + std::string(source.name, strnlen_s(source.name, 34))); auto rc = entry(&app, nullptr, 1, 3, 0x401, &source); Log("END OPENDS RC=" + std::to_string(rc)); if (rc != 0) { Status status{}; auto sr = entry(&app, nullptr, 1, 8, 1, &status); throw std::runtime_error("TWAIN OPENDS: RC=" + std::to_string(rc) + ", CC=" + (sr == 0 ? std::to_string(status.condition) : std::string("unbekannt"))); } opened = true; state = 4; Log("Source opened; state=4"); }
	void Log(const std::string& text) { if (trace) trace->Write(text); }
	uint16_t Call(uint32_t group, uint16_t data, uint16_t message, void* value) {
		const bool event = group == 1 && data == 2;
		std::ostringstream label; label << "state=" << state << " DG=" << group << " DAT=0x" << std::hex << data << " MSG=0x" << message;
		if (!event) Log("BEGIN " + label.str());
		auto result = entry(&app, &source, group, data, message, value); lastCondition = 0xffff;
		if (result == 1 || result == 2) { Status status{}; if (entry(&app, &source, 1, 8, 1, &status) == 0) lastCondition = status.condition; }
		if (!event || result != 5) Log("END " + label.str() + " RC=" + std::to_string(result) + ((result == 1 || result == 2) ? " CC=" + std::to_string(lastCondition) : ""));
		return result;
	}
	std::runtime_error Error(const std::string& operation, uint16_t result) { return std::runtime_error(operation + ": RC=" + std::to_string(result) + ", CC=" + (lastCondition == 0xffff ? std::string("unbekannt") : std::to_string(lastCondition)) + ", state=" + std::to_string(state) + "."); }
	bool OperationFailed() const { return lastCondition == 5; }
	bool Query(uint16_t cap, uint16_t message, Values& values) {
		Capability c{cap, 0, nullptr}; Log("Capability=" + std::to_string(cap)); auto result = Call(1, 1, message, &c);
		if (result != 0 || !c.handle) { if (c.handle) GlobalFree(c.handle); return false; }
		auto memory = static_cast<const uint8_t*>(GlobalLock(c.handle));
		try { if (!memory) throw std::runtime_error("TWAIN: Capability konnte nicht gesperrt werden."); values = Decode(c.container, memory, GlobalSize(c.handle)); }
		catch (...) { if (memory) GlobalUnlock(c.handle); GlobalFree(c.handle); throw; }
		GlobalUnlock(c.handle); GlobalFree(c.handle);
		std::ostringstream text; text << "Capability=" << cap << " container=" << c.container << " type=" << values.type;
		for (auto v : values.items) text << " value=" << Number(values.type, v);
		if (values.range.available) text << " range=" << values.range.minimum << ':' << values.range.maximum << ':' << values.range.step << " default=" << values.range.defaultValue << " current=" << values.range.current;
		Log(text.str()); return true;
	}
	bool Get(uint16_t cap, uint32_t& value) {
		Values values; if (!Query(cap, 2, values) || !values.hasCurrent) return false; value = values.current; auto size = ItemSize(values.type); if (size == 1) value &= 255; else if (size == 2) value &= 65535; return true;
	}
	void Set(uint16_t cap, uint16_t type, uint32_t value, bool required = true) {
		HANDLE memory = GlobalAlloc(GHND, sizeof(One)); if (!memory) throw std::bad_alloc(); auto one = static_cast<One*>(GlobalLock(memory)); if (!one) { GlobalFree(memory); throw std::bad_alloc(); } *one = One{type, value}; GlobalUnlock(memory);
		Capability c{cap, 5, memory}; Log("SET Capability=" + std::to_string(cap) + " type=" + std::to_string(type) + " value=" + std::to_string(Number(type, value))); auto r = Call(1, 1, 6, &c); GlobalFree(memory);
		if (required && r != 0) throw Error("TWAIN SET Capability " + std::to_string(cap), r);
		if (required) { uint32_t actual = 0; if (!Get(cap, actual) || Number(type, actual) != Number(type, value)) throw std::runtime_error("TWAIN GETCURRENT bestaetigt angeforderten Wert nicht: Capability=" + std::to_string(cap)); }
	}
	int FeederLoaded() {
		uint32_t value = 0;
		try { if (Get(CAP_FEEDERLOADED, value) && value <= 1) return static_cast<int>(value); }
		catch (const std::runtime_error& e) { Log("Optional CAP_FEEDERLOADED: " + std::string(e.what())); }
		return -1;
	}
	ScanCapabilities Caps(bool enableFeeder = false) {
		ScanCapabilities c; uint32_t value = 0; c.feeder = Get(CAP_FEEDERENABLED, value);
		if (enableFeeder && c.feeder) Set(CAP_FEEDERENABLED, 6, 1);
		c.feederLoaded = FeederLoaded(); c.duplex = Get(CAP_DUPLEX, value) && value != 0; c.brightness = Get(ICAP_BRIGHTNESS, value);
		Values feeders; if (Query(CAP_FEEDERENABLED, 1, feeders) && std::find(feeders.items.begin(), feeders.items.end(), 0) != feeders.items.end()) c.flatbed = 1;
		c.uiControllable = Get(0x100e, value) && value != 0;
		if (!Get(ICAP_UNITS, value)) throw std::runtime_error("TWAIN: Masseinheit der Faehigkeiten nicht bestimmbar.");
		if (value != 0) Set(ICAP_UNITS, 4, 0);
		if (Get(ICAP_PHYSICALWIDTH, value)) c.maxWidthMm = Unfixed(value) * 25.4;
		if (Get(ICAP_PHYSICALHEIGHT, value)) c.maxHeightMm = Unfixed(value) * 25.4;
		auto list = [&](uint16_t cap, std::vector<double>& items, CapabilityRange* range) { Values v; if (Query(cap, 1, v)) { for (auto item : v.items) items.push_back(Number(v.type, item)); if (range) *range = v.range; } };
		list(ICAP_XRESOLUTION, c.resolutions, &c.xResolutionRange); list(ICAP_YRESOLUTION, c.yResolutions, &c.yResolutionRange);
		list(ICAP_XFERMECH, c.transferMechanisms, nullptr); list(ICAP_PIXELTYPE, c.pixelTypes, nullptr); return c;
	}
	void Enable(bool dialog) { UserInterface ui{static_cast<uint16_t>(dialog), 0, parent}; auto r = Call(1, 9, 0x502, &ui); if (r != 0) throw Error("TWAIN ENABLEDS", r); state = 5; }
	void Ready() { if (state != 5) throw std::runtime_error("TWAIN: XFERREADY ausserhalb Zustand 5."); state = 6; Log("XFERREADY; state=6"); }
	uint16_t Transfer(uint16_t data, void* value) { if (state != 6 && state != 7) throw std::runtime_error("TWAIN: Transfer ausserhalb Zustand 6/7."); auto r = Call(2, data, 1, value); if (r == 0 || r == 6 || r == 3) state = 7; return r; }
	uint16_t End() { if (state != 7) throw std::runtime_error("TWAIN: ENDXFER ausserhalb Zustand 7."); Pending pending{}; auto r = Call(1, 5, 0x701, &pending); if (r != 0) throw Error("TWAIN ENDXFER", r); state = pending.count ? 6 : 5; Log("Pending=" + std::to_string(pending.count) + "; state=" + std::to_string(state)); return pending.count; }
};
static Image Dib(HANDLE handle) {
	if (!handle) throw std::runtime_error("TWAIN lieferte kein Bild."); auto bytes = GlobalSize(handle); auto data = static_cast<const uint8_t*>(GlobalLock(handle));
	if (!data) throw std::runtime_error("TWAIN-Bild konnte nicht gesperrt werden.");
	struct Unlock { HANDLE h; ~Unlock() { GlobalUnlock(h); } } unlock{handle};
	if (bytes < sizeof(BITMAPINFOHEADER)) throw std::runtime_error("TWAIN-Bildkopf ist unvollstaendig."); BITMAPINFOHEADER h{}; memcpy(&h, data, sizeof(h));
	if (h.biSize < sizeof(h) || h.biSize > bytes || h.biWidth <= 0 || !h.biHeight || h.biHeight == LONG_MIN || h.biPlanes != 1 || h.biCompression != BI_RGB || (h.biBitCount != 1 && h.biBitCount != 8 && h.biBitCount != 24 && h.biBitCount != 32)) throw std::runtime_error("TWAIN lieferte ein nicht unterstuetztes natives Bildformat.");
	Image image; image.width = h.biWidth; image.height = std::abs(h.biHeight); if (uint64_t(image.width) * image.height > 100000000) throw std::runtime_error("TWAIN-Bild groesser als 100 Megapixel.");
	image.dpiX = h.biXPelsPerMeter > 0 ? h.biXPelsPerMeter * .0254 : 300; image.dpiY = h.biYPelsPerMeter > 0 ? h.biYPelsPerMeter * .0254 : 300;
	size_t colors = h.biBitCount <= 8 ? (h.biClrUsed ? h.biClrUsed : size_t(1) << h.biBitCount) : 0;
	size_t offset = h.biSize + colors * sizeof(RGBQUAD); size_t stride = ((size_t(image.width) * h.biBitCount + 31) / 32) * 4;
	if (colors > 256 || offset > bytes || stride * image.height > bytes - offset) throw std::runtime_error("TWAIN-Bilddaten sind unvollstaendig.");
	image.pixels.resize(size_t(image.width) * image.height * 4);
	for (uint32_t y = 0; y < image.height; ++y) for (uint32_t x = 0; x < image.width; ++x) {
		const auto row = data + offset + (h.biHeight > 0 ? size_t(image.height) - 1 - y : size_t(y)) * stride; auto dst = image.pixels.data() + (size_t(y) * image.width + x) * 4;
		if (h.biBitCount <= 8) { size_t index = h.biBitCount == 1 ? ((row[x / 8] >> (size_t(7) - size_t(x) % 8)) & 1) : row[x]; if (index >= colors) throw std::runtime_error("Ungueltige TWAIN-Palette."); memcpy(dst, data + size_t(h.biSize) + index * sizeof(RGBQUAD), 3); }
		else memcpy(dst, row + size_t(x) * (h.biBitCount / 8), 3); dst[3] = 255;
	}
	image.Validate(); return image;
}
static std::vector<ScanSource> Sources(HWND owner) { Session session(owner); std::vector<ScanSource> out; for (const auto& source : session.List()) out.push_back({Wide(source.name), Wide(source.manufacturer)}); return out; }
static ScanCapabilities Capabilities(HWND owner, const std::wstring& source, bool enableFeeder = false) { Session session(owner); session.Open(source); return session.Caps(enableFeeder); }
struct ScriptCall { uint32_t group; uint16_t data, message, result, value = 0; };
static std::vector<ScriptCall> script;
static size_t scriptIndex = 0;
static bool scriptMismatch = false;
static uint16_t WINAPI ScriptEntry(Identity*, Identity*, uint32_t group, uint16_t data, uint16_t message, void* value) {
	if (scriptIndex >= script.size()) { scriptMismatch = true; return 1; }
	auto expected = script[scriptIndex++];
	if (group != expected.group || data != expected.data || message != expected.message) scriptMismatch = true;
	if (data == 8) *static_cast<Status*>(value) = {expected.value, 0};
	if (data == 5) *static_cast<Pending*>(value) = {expected.value, 0};
	if (data == 1 && expected.result == 0) {
		auto c = static_cast<Capability*>(value); c->container = 5; c->handle = GlobalAlloc(GHND, sizeof(One));
		if (!c->handle) { scriptMismatch = true; return 1; }
		auto one = static_cast<One*>(GlobalLock(c->handle));
		if (!one) { GlobalFree(c->handle); c->handle = nullptr; scriptMismatch = true; return 1; }
		*one = {4, 0xdead0000u | expected.value}; GlobalUnlock(c->handle);
	}
	return expected.result;
}
static void Metadata(Image& image, const ImageInfo& info) {
	uint32_t x = 0, y = 0; memcpy(&x, &info.xResolution, 4); memcpy(&y, &info.yResolution, 4);
	image.dpiX = Unfixed(x); image.dpiY = Unfixed(y);
	image.kind = info.bitsPerPixel == 1 ? PixelKind::Mono1 : info.bitsPerPixel == 8 && info.pixelType == 1 ? PixelKind::Gray8 : info.bitsPerPixel == 24 ? PixelKind::Color24 : PixelKind::Other;
}
class MemoryImage {
	Image image; std::vector<uint8_t> coverage; uint64_t received = 0; int bits; bool vanilla, lsb;
public:
	MemoryImage(const ImageInfo& info, bool whiteZero, bool lowBitFirst) : bits(info.bitsPerPixel), vanilla(whiteZero), lsb(lowBitFirst) {
		if (info.width <= 0 || info.height <= 0 || uint64_t(info.width) * info.height > 100000000 || info.planar || info.compression || !((bits == 1 && info.pixelType == 0 && info.samples == 1 && info.bits[0] == 1) || (bits == 8 && info.pixelType == 1 && info.samples == 1 && info.bits[0] == 8) || (bits == 24 && info.pixelType == 2 && info.samples == 3 && info.bits[0] == 8 && info.bits[1] == 8 && info.bits[2] == 8))) throw std::runtime_error("TWAIN: Nicht unterstuetztes Speicherbildformat oder ungueltige Bildmasse.");
		image.width = info.width; image.height = info.height; Metadata(image, info); image.pixels.resize(size_t(image.width) * image.height * 4); coverage.resize((size_t(image.width) * image.height + 7) / 8);
	}
	void Add(const MemoryTransfer& block, const uint8_t* data, size_t capacity) {
		if (block.compression || !block.columns || !block.rows || block.xOffset > image.width || block.columns > image.width - block.xOffset || block.yOffset > image.height || block.rows > image.height - block.yOffset) throw std::runtime_error("TWAIN: Ungueltiges Speichertransfer-Rechteck.");
		uint64_t rowBytes = (uint64_t(block.columns) * bits + 7) / 8;
		if (block.bytesPerRow < rowBytes || block.bytesWritten > capacity || uint64_t(block.rows - 1) * block.bytesPerRow + rowBytes > block.bytesWritten) throw std::runtime_error("TWAIN: Speichertransfer-Puffer unvollstaendig.");
		for (uint32_t y = 0; y < block.rows; ++y) for (uint32_t x = 0; x < block.columns; ++x) {
			size_t index = size_t(block.yOffset + y) * image.width + block.xOffset + x; uint8_t mask = uint8_t(1u << (index % 8));
			if (coverage[index / 8] & mask) throw std::runtime_error("TWAIN: Ueberlappende Speichertransfer-Bloecke."); coverage[index / 8] |= mask; ++received;
			auto row = data + size_t(y) * block.bytesPerRow; auto pixel = image.pixels.data() + index * 4;
			if (bits == 24) { pixel[0] = row[x * 3 + 2]; pixel[1] = row[x * 3 + 1]; pixel[2] = row[x * 3]; }
			else { uint8_t value = bits == 1 ? uint8_t(((row[x / 8] >> (lsb ? x % 8 : 7 - x % 8)) & 1) * 255) : row[x]; if (vanilla) value = 255 - value; pixel[0] = pixel[1] = pixel[2] = value; } pixel[3] = 255;
		}
	}
	Image Finish() { if (received != uint64_t(image.width) * image.height) throw std::runtime_error("TWAIN: XFERDONE mit fehlenden Bildbereichen."); image.Validate(); return std::move(image); }
};
static ScanResult Scan(HWND owner, const ScanOptions& requestedOptions, Trace& trace, const std::function<bool()>& abort, const std::function<void(const Image&, size_t)>& checkpoint, const std::function<void(size_t)>& pageStarted = {}) {
	ScanResult result;
	ScanOptions options = requestedOptions;
	try {
		if (!std::isfinite(options.xMm) || !std::isfinite(options.yMm) || options.xMm < 0 || options.yMm < 0 || options.xMm > 2000 || options.yMm > 2000) throw std::runtime_error("Ungueltige Scanposition.");
		if (!std::isfinite(options.dpi) || options.dpi < 25 || options.dpi > 9600 || !std::isfinite(options.widthMm) || !std::isfinite(options.heightMm) || options.widthMm <= 0 || options.heightMm <= 0 || options.widthMm > 2000 || options.heightMm > 2000 || !std::isfinite(options.brightness) || options.brightness < -1000 || options.brightness > 1000 || options.mode < 0 || options.mode > 2 || (options.transfer != -1 && options.transfer != 0 && options.transfer != 2) || options.timeoutMs < 1000 || options.timeoutMs > 180000) throw std::runtime_error("Ungueltige Scaneinstellungen.");
		Session session(owner, &trace); session.Open(options.source); auto caps = session.Caps(); uint32_t current = 0;
		if (!options.driverDialog) {
			if (caps.feeder) {
				session.Set(CAP_FEEDERENABLED, 6, 1);
				int loaded = session.FeederLoaded();
				if (loaded < 0) throw std::runtime_error("TWAIN: Papierstatus des Einzugs nicht bestimmbar; automatische Papierzufuhr nicht moeglich. Bitte Scanner-Menue verwenden.");
				options.feeder = loaded == 1;
				if (!options.feeder) session.Set(CAP_FEEDERENABLED, 6, 0);
			} else options.feeder = false;
			options.duplex = UseDuplex(options.feeder, caps.duplex, requestedOptions.feeder, requestedOptions.duplex);
			session.Log("Automatic paper source: feeder=" + std::to_string(options.feeder) + " duplex=" + std::to_string(options.duplex));
		}
		int mechanism = options.transfer;
		if (mechanism == -1) mechanism = std::find(caps.transferMechanisms.begin(), caps.transferMechanisms.end(), 2) != caps.transferMechanisms.end() ? 2 : 0;
		if (std::find(caps.transferMechanisms.begin(), caps.transferMechanisms.end(), mechanism) == caps.transferMechanisms.end()) throw std::runtime_error("TWAIN: Angeforderter Transfermechanismus wird nicht angeboten.");
		if (!options.driverDialog) {
			if (!caps.uiControllable) throw std::runtime_error("TWAIN: Quelle bestaetigt keinen dialogfreien Betrieb (CAP_UICONTROLLABLE).");
			if (session.Get(0x100b, current)) session.Set(0x100b, 6, 0);
			if (session.Get(CAP_FEEDERENABLED, current)) session.Set(CAP_FEEDERENABLED, 6, options.feeder); else if (options.feeder) throw std::runtime_error("Quelle bietet keinen steuerbaren Papiereinzug.");
			if (session.Get(CAP_DUPLEXENABLED, current)) session.Set(CAP_DUPLEXENABLED, 6, options.duplex); else if (options.duplex) throw std::runtime_error("Quelle bietet keinen steuerbaren Duplexmodus.");
			session.Set(ICAP_UNITS, 4, 0); session.Set(ICAP_PIXELTYPE, 4, options.mode);
			if (session.Get(ICAP_BITDEPTH, current) && current != BitDepth(options.mode)) session.Set(ICAP_BITDEPTH, 4, BitDepth(options.mode));
			if (options.feeder && session.Get(0x1007, current)) session.Set(0x1007, 6, 1);
			session.Set(ICAP_XRESOLUTION, 7, Fixed(options.dpi)); session.Set(ICAP_YRESOLUTION, 7, Fixed(options.dpi));
			if (session.Get(ICAP_BRIGHTNESS, current)) session.Set(ICAP_BRIGHTNESS, 7, Fixed(options.brightness)); else if (options.brightness != 0) throw std::runtime_error("Quelle unterstuetzt keine Helligkeitssteuerung.");
			session.Set(1, 1, options.feeder ? uint32_t(-1) : 1);
			if (options.maximumArea) MaximumArea(options, session.Caps());
			Layout layout{}; layout.frame = AreaFrame(options);
			auto r = session.Call(2, 0x102, 6, &layout); if (r != 0) throw session.Error("TWAIN SET IMAGELAYOUT", r);
			Layout actual{}; r = session.Call(2, 0x102, 1, &actual); if (r != 0) throw session.Error("TWAIN GET IMAGELAYOUT", r);
			session.Log("RequestedLayoutInches=" + DescribeFrame(layout.frame) + " ActualLayoutInches=" + DescribeFrame(actual.frame));
			if (!SameRasterFrame(layout.frame, actual.frame, options.dpi)) throw std::runtime_error("TWAIN: Quelle hat den Scanbereich um mehr als ein Ausgabepixel geaendert.");
		}
		session.Set(ICAP_XFERMECH, 4, mechanism);
		if (mechanism == 2) { session.Set(0x0100, 4, 0); if (session.Get(0x1120, current) && current) session.Set(0x1120, 4, 0); }
		if (!options.driverDialog) {
			for (auto setting : {std::pair<uint16_t, uint32_t>{ICAP_UNITS, 0}, {ICAP_PIXELTYPE, uint32_t(options.mode)}, {ICAP_XRESOLUTION, Fixed(options.dpi)}, {ICAP_YRESOLUTION, Fixed(options.dpi)}}) {
				if (!session.Get(setting.first, current) || current != setting.second) throw std::runtime_error("TWAIN: Einstellung durch spaetere Capability geaendert: " + std::to_string(setting.first));
			}
			bool feederKnown = session.Get(CAP_FEEDERENABLED, current);
			if (feederKnown && bool(current) != options.feeder) throw std::runtime_error("TWAIN: Papiereinzug wurde durch spaetere Einstellungen geaendert.");
			if (!feederKnown && options.feeder) throw std::runtime_error("TWAIN: Papiereinzug nicht bestaetigt.");
			bool duplexKnown = session.Get(CAP_DUPLEXENABLED, current);
			if (duplexKnown && bool(current) != options.duplex) throw std::runtime_error("TWAIN: Duplex wurde durch spaetere Einstellungen geaendert.");
			if (!duplexKnown && options.duplex) throw std::runtime_error("TWAIN: Duplex nicht bestaetigt.");
			session.Get(0x100f, current);
			if (options.feeder) { if (session.FeederLoaded() != 1) throw std::runtime_error("PRECONDITION: feeder empty or unknown"); }
			else session.Get(CAP_FEEDERLOADED, current);
		}
		session.Log("Selected transfer=" + std::to_string(mechanism) + " showUI=" + std::to_string(options.driverDialog));
		const auto start = GetTickCount64();
		auto check = [&]() { if (abort()) { result.cancelled = true; throw std::runtime_error("TWAIN: Scan abgebrochen."); } if (GetTickCount64() - start >= options.timeoutMs) throw std::runtime_error("TWAIN: Scan-Zeitlimit erreicht."); };
		check(); session.Enable(options.driverDialog);
		bool ready = false; while (!ready) {
			check(); MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT); MSG msg{};
			while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
				if (msg.message == WM_QUIT) { PostQuitMessage(static_cast<int>(msg.wParam)); result.cancelled = true; throw std::runtime_error("TWAIN: Anwendung beendet."); }
				Event event{&msg, 0}; auto r = session.Call(1, 2, 0x601, &event);
				if (r == 4) { session.Log("DS event=" + std::to_string(event.message)); if (event.message == 0x101) { session.Ready(); ready = true; break; } if (event.message == 0x102 || event.message == 0x103) { result.cancelled = true; throw std::runtime_error("TWAIN: Quelle hat Scan geschlossen."); } }
				else if (r == 5) { TranslateMessage(&msg); DispatchMessageW(&msg); } else throw session.Error("TWAIN PROCESSEVENT", r);
			}
		}
		uint64_t totalBytes = 0; uint16_t pending = 0;
		if (!session.Get(ICAP_XFERMECH, current) || current != uint32_t(mechanism)) throw std::runtime_error("TWAIN: Quelle hat den Transfermechanismus nach ENABLEDS geaendert.");
		do {
			check(); if (result.pages.size() >= 1000) throw std::runtime_error("TWAIN: Sicherheitsgrenze von 1000 Seiten erreicht.");
			if (pageStarted) pageStarted(result.pages.size());
			ImageInfo info{}; auto r = session.Call(2, 0x101, 1, &info); if (r != 0) throw session.Error("TWAIN GET IMAGEINFO", r);
			session.Log("ImageInfo=" + std::to_string(info.width) + "x" + std::to_string(info.height) + " bpp=" + std::to_string(info.bitsPerPixel) + " pixelType=" + std::to_string(info.pixelType) + " compression=" + std::to_string(info.compression));
			Image image;
			if (mechanism == 0) {
				HANDLE native = nullptr; r = session.Transfer(0x104, &native);
				struct Free { HANDLE h; ~Free() { if (h) GlobalFree(h); } } free{native};
				if (r == 3) { result.cancelled = true; session.End(); throw std::runtime_error("TWAIN: Nativer Transfer abgebrochen."); }
				if (r != 6) throw session.Error("TWAIN DAT_IMAGENATIVEXFER", r);
				image = Dib(native); Metadata(image, info);
			} else {
				uint32_t flavor = 0, order = 1;
				if ((info.pixelType == 0 || info.pixelType == 1) && !session.Get(0x111f, flavor)) throw std::runtime_error("TWAIN: Pixelpolaritaet nicht bestimmbar.");
				if (info.bitsPerPixel == 1 && !session.Get(0x111c, order)) throw std::runtime_error("TWAIN: Bitreihenfolge nicht bestimmbar.");
				if (flavor > 1 || order > 1) throw std::runtime_error("TWAIN: Unbekannte Pixelpolaritaet/Bitreihenfolge.");
				MemoryImage assembled(info, flavor == 1, order == 0); SetupMemory setup{}; r = session.Call(1, 6, 1, &setup); if (r != 0) throw session.Error("TWAIN SETUPMEMXFER", r);
				session.Log("Memory buffer min=" + std::to_string(setup.minimum) + " max=" + std::to_string(setup.maximum) + " preferred=" + std::to_string(setup.preferred));
				if (!setup.minimum || setup.maximum < setup.minimum || setup.minimum > 64 * 1024 * 1024) throw std::runtime_error("TWAIN: Ungueltige Speicherpufferanforderung.");
				uint32_t length = std::clamp(setup.preferred, setup.minimum, (std::min)(setup.maximum, 64u * 1024 * 1024)); std::vector<uint8_t> buffer(length);
				uint32_t blocks = 0;
				do { check(); if (++blocks > 1000000) throw std::runtime_error("TWAIN: Zu viele Speicherbloecke."); MemoryTransfer block{}; block.memory = {0x9, length, buffer.data()}; r = session.Transfer(0x103, &block);
					if (r == 3) { result.cancelled = true; session.End(); throw std::runtime_error("TWAIN: Speichertransfer abgebrochen."); }
					if (r != 0 && r != 6) {
						if (r == 1 && session.OperationFailed() && blocks == 1 && !block.bytesWritten && result.pages.empty()) {
							session.Log(session.Error("TWAIN DAT_IMAGEMEMXFER", r).what());
							throw std::runtime_error("Der Scanner hat keine Bilddaten geliefert. Moeglicherweise wurden alle Seiten als leer uebersprungen.");
						}
						throw session.Error("TWAIN DAT_IMAGEMEMXFER", r);
					}
					session.Log("Memory block=" + std::to_string(block.columns) + "x" + std::to_string(block.rows) + " offset=" + std::to_string(block.xOffset) + "," + std::to_string(block.yOffset) + " stride=" + std::to_string(block.bytesPerRow) + " bytes=" + std::to_string(block.bytesWritten));
					if (block.bytesWritten) assembled.Add(block, buffer.data(), buffer.size()); else if (r != 6) throw std::runtime_error("TWAIN: Speichertransfer ohne Fortschritt.");
				} while (r != 6);
				image = assembled.Finish();
			}
			image.Validate(); totalBytes += image.pixels.size(); if (totalBytes > 1500000000) throw std::runtime_error("TWAIN: Speichergrenze pro Scan erreicht.");
			checkpoint(image, result.pages.size()); image.pixels.Spill(); image.pixels.ReleaseMemory(); result.pages.push_back(std::move(image));
			pending = session.End();
		} while (pending);
		result.completed = true;
	} catch (const std::exception& e) { result.error = e.what(); trace.Write("SCAN FAILED " + result.error); }
	trace.Write("Scan completed=" + std::to_string(result.completed) + " cancelled=" + std::to_string(result.cancelled) + " pages=" + std::to_string(result.pages.size())); return result;
}
}
namespace {
constexpr uint32_t RequestVersion = 4;
#pragma pack(push, 1)
struct Request { uint32_t version = RequestVersion; int32_t mode = 2; double dpi = 300, width = 210, height = 297, brightness = 0; uint32_t feeder = 0, duplex = 0, dialog = 1; int32_t transfer = -1; uint32_t timeoutMs = 90000; wchar_t source[256]{}; double x = 0, y = 0; uint32_t maximumArea = 0; };
#pragma pack(pop)
static_assert(std::endian::native == std::endian::little && sizeof(wchar_t) == 2 && sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
static_assert(sizeof(Request) == 592 && alignof(Request) == 1);
static_assert(offsetof(Request, x) == 572 && offsetof(Request, y) == 580 && offsetof(Request, maximumArea) == 588);
static_assert(offsetof(Request, version) == 0 && offsetof(Request, mode) == 4 && offsetof(Request, dpi) == 8);
static_assert(offsetof(Request, width) == 16 && offsetof(Request, height) == 24 && offsetof(Request, brightness) == 32);
static_assert(offsetof(Request, feeder) == 40 && offsetof(Request, duplex) == 44 && offsetof(Request, dialog) == 48);
static_assert(offsetof(Request, transfer) == 52 && offsetof(Request, timeoutMs) == 56 && offsetof(Request, source) == 60);
static void SetRequestSource(Request& request, const std::wstring& source) {
	if (source.size() >= 256) throw std::runtime_error("TWAIN-Quellenname zu lang.");
	std::fill_n(request.source, 256, L'\0');
	std::copy(source.begin(), source.end(), request.source);
}
static void WriteRequest(std::ostream& out, const Request& request) {
	out.write(reinterpret_cast<const char*>(&request), sizeof(request));
	if (!out) throw std::runtime_error("TWAIN-Auftrag konnte nicht geschrieben werden.");
}
static Request ReadRequest(std::istream& in) {
	Request request;
	in.read(reinterpret_cast<char*>(&request), sizeof(request));
	if (!in || in.peek() != std::char_traits<char>::eof()) throw std::runtime_error("Ungueltige TWAIN-Auftragslaenge (erwartet: 592 Bytes).");
	if (request.version != RequestVersion) throw std::runtime_error("Ungueltige TWAIN-Protokollversion (erwartet: " + std::to_string(RequestVersion) + ", empfangen: " + std::to_string(request.version) + "). Scanner.exe und TwainHost.exe muessen denselben Stand haben.");
	if (request.source[255] || request.feeder > 1 || request.duplex > 1 || request.dialog > 1 || request.maximumArea > 1 || !std::isfinite(request.x) || !std::isfinite(request.y) || request.x < 0 || request.y < 0 || request.x > 2000 || request.y > 2000) throw std::runtime_error("Ungueltige TWAIN-Auftragsdaten (Quellenname, Position oder Boolesche Werte).");
	return request;
}
static void WriteRange(std::ostream& out, const CapabilityRange& r) { out << ' ' << r.available << ' ' << r.minimum << ' ' << r.maximum << ' ' << r.step << ' ' << r.current << ' ' << r.defaultValue; }
static void ReadRange(std::istream& in, CapabilityRange& r) { in >> r.available >> r.minimum >> r.maximum >> r.step >> r.current >> r.defaultValue; }
static void WriteCaps(std::ostream& out, const ScanCapabilities& c) {
	out << std::setprecision(17) << 3 << ' ' << c.feeder << ' ' << c.duplex << ' ' << c.brightness << ' ' << c.maxWidthMm << ' ' << c.maxHeightMm << ' ' << c.flatbed << ' ' << c.uiControllable;
	WriteRange(out, c.xResolutionRange); WriteRange(out, c.yResolutionRange);
	for (auto list : {&c.resolutions, &c.yResolutions, &c.transferMechanisms, &c.pixelTypes}) { out << ' ' << list->size(); for (double v : *list) out << ' ' << v; }
	out << ' ' << c.feederLoaded;
}
static ScanCapabilities ReadCaps(std::istream& in) {
	ScanCapabilities c; int version = 0; in >> version >> c.feeder >> c.duplex >> c.brightness >> c.maxWidthMm >> c.maxHeightMm >> c.flatbed >> c.uiControllable;
	ReadRange(in, c.xResolutionRange); ReadRange(in, c.yResolutionRange);
	for (auto list : {&c.resolutions, &c.yResolutions, &c.transferMechanisms, &c.pixelTypes}) { uint32_t count = 0; in >> count; if (count > 65536) throw std::runtime_error("TWAIN: Capability-Antwort zu gross."); for (uint32_t n = 0; n < count; ++n) { double value = 0; in >> value; list->push_back(value); } }
	if (version == 3) in >> c.feederLoaded;
	if (!in || (version != 2 && version != 3) || c.feederLoaded < -1 || c.feederLoaded > 1) throw std::runtime_error("TWAIN: Ungueltige Capability-Antwort."); return c;
}
static void RecoverPages(const std::filesystem::path& directory, ScanResult& result) {
	for (size_t n = 0; n < 1000; ++n) {
		auto path = directory / L"pages" / std::to_wstring(n); if (!std::filesystem::exists(path / L"ready")) break;
		try { Document page; LoadWorkspace(page, path); if (page.pages.size() != 1) throw std::runtime_error("Ungueltige gesicherte Seite."); result.pages.push_back(std::move(page.pages.front().image)); }
		catch (const std::exception& e) { result.completed = false; result.error += " TWAIN-Seitenbergung: " + std::string(e.what()); break; }
	}
}
static std::filesystem::path PreserveRecovery(const std::filesystem::path& directory, const std::filesystem::path& target, std::string& error) {
	std::error_code ec; std::filesystem::rename(directory, target, ec);
	if (ec) { error += " TWAIN: Recovery-Archivierung fehlgeschlagen (" + ec.message() + "); Originalordner bleibt erhalten."; return directory; }
	return target;
}
static void WriteWide(std::ofstream& stream, const std::wstring& text) { uint32_t length = static_cast<uint32_t>(text.size()); stream.write(reinterpret_cast<const char*>(&length), 4); stream.write(reinterpret_cast<const char*>(text.data()), length * sizeof(wchar_t)); }
static std::wstring ReadWide(std::ifstream& stream) { uint32_t length = 0; stream.read(reinterpret_cast<char*>(&length), 4); if (!stream || length > 256) throw std::runtime_error("Ungueltige TWAIN-Quellenantwort."); std::wstring text(length, L' '); stream.read(reinterpret_cast<char*>(text.data()), length * sizeof(wchar_t)); if (!stream) throw std::runtime_error("Unvollstaendige TWAIN-Quellenantwort."); return text; }
static size_t CountReadyPages(const std::filesystem::path& directory, size_t completed = 0) {
	std::error_code ec;
	while (completed < 1000 && std::filesystem::is_regular_file(directory / L"pages" / std::to_wstring(completed) / L"ready", ec)) ++completed;
	return completed;
}
static std::wstring ScanProgressText(size_t started, bool cancelling) {
	return cancelling ? L"Abbruch wird angefordert …" : L"Seite " + std::to_wstring(std::max(size_t(1), started)) + L" wird gescannt …";
}
class BridgeProgress {
	HWND owner = nullptr, window = nullptr, label = nullptr, button = nullptr, bar = nullptr;
	bool wasEnabled = false;
	bool& cancellationRequested;
	size_t completed = 0;
	std::wstring text;
	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
		auto self = reinterpret_cast<BridgeProgress*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
		if (message == WM_NCCREATE) {
			self = static_cast<BridgeProgress*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
		}
		if (self && (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wParam) == IDCANCEL))) {
			self->cancellationRequested = true;
			EnableWindow(self->button, FALSE);
			return 0;
		}
		return DefWindowProcW(hwnd, message, wParam, lParam);
	}
public:
	BridgeProgress(HWND parent, bool& requested) : owner(parent), wasEnabled(parent && IsWindowEnabled(parent)), cancellationRequested(requested) {
		if (wasEnabled) EnableWindow(owner, FALSE);
	}
	BridgeProgress(const BridgeProgress&) = delete;
	BridgeProgress& operator=(const BridgeProgress&) = delete;
	~BridgeProgress() {
		if (window) DestroyWindow(window);
		if (wasEnabled) { EnableWindow(owner, TRUE); SetForegroundWindow(owner); }
	}
	void Create() {
		INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_PROGRESS_CLASS};
		if (!InitCommonControlsEx(&controls)) throw std::runtime_error("TWAIN-Fortschrittsanzeige konnte nicht initialisiert werden.");
		auto instance = GetModuleHandleW(nullptr);
		WNDCLASSW wc{}; wc.lpfnWndProc = WindowProc; wc.hInstance = instance; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1); wc.lpszClassName = L"ScannerTwainBridgeProgress";
		if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) throw std::runtime_error("TWAIN-Fensterklasse konnte nicht registriert werden.");
		RECT bounds{0, 0, 400, 150}; AdjustWindowRectEx(&bounds, WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
		RECT parent{}; GetWindowRect(owner, &parent);
		window = CreateWindowExW(WS_EX_DLGMODALFRAME, wc.lpszClassName, L"Lesen vom Gerät", WS_CAPTION | WS_SYSMENU,
			parent.left + ((parent.right - parent.left) - (bounds.right - bounds.left)) / 2, parent.top + ((parent.bottom - parent.top) - (bounds.bottom - bounds.top)) / 2,
			bounds.right - bounds.left, bounds.bottom - bounds.top, owner, nullptr, instance, this);
		if (!window) throw std::runtime_error("TWAIN-Fortschrittsfenster konnte nicht angelegt werden.");
		label = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 16, 16, 368, 44, window, nullptr, instance, nullptr);
		bar = CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | PBS_MARQUEE, 16, 70, 368, 20, window, nullptr, instance, nullptr);
		button = CreateWindowExW(0, L"BUTTON", L"Abbrechen", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 274, 106, 110, 28, window, reinterpret_cast<HMENU>(IDCANCEL), instance, nullptr);
		if (!label || !bar || !button) throw std::runtime_error("TWAIN-Fortschrittssteuerelemente konnten nicht angelegt werden.");
		for (auto control : {label, button}) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
		text = ScanProgressText(0, false); SetWindowTextW(label, text.c_str());
		SendMessageW(bar, PBM_SETMARQUEE, TRUE, 30);
		ShowWindow(window, SW_SHOWNORMAL); UpdateWindow(window); SetFocus(button);
	}
	bool Owns(HWND target) const { return window && (target == window || IsChild(window, target)); }
	void Update(const std::filesystem::path& directory, bool cancelling) {
		if (!window) return;
		std::error_code ec;
		while (completed < 1000 && std::filesystem::is_regular_file(directory / L"pages" / std::to_wstring(completed) / L"started", ec)) ++completed;
		auto next = ScanProgressText(completed, cancelling);
		if (next != text) { text = std::move(next); SetWindowTextW(label, text.c_str()); }
		if (cancelling) { EnableWindow(button, FALSE); SendMessageW(bar, PBM_SETMARQUEE, FALSE, 0); }
	}
};
static std::filesystem::path Bridge(HWND owner, const wchar_t* operation, const ScanOptions& options) {
	auto dir = UserDataDirectory() / L"Bridge" / NewId(); std::filesystem::create_directories(dir);
	try {
		Request request; request.mode = options.mode; request.dpi = options.dpi; request.width = options.widthMm; request.height = options.heightMm; request.brightness = options.brightness; request.feeder = options.feeder; request.duplex = options.duplex; request.dialog = options.driverDialog;
		request.transfer = options.transfer; request.timeoutMs = options.timeoutMs;
		request.x = options.xMm; request.y = options.yMm; request.maximumArea = options.maximumArea;
		SetRequestSource(request, options.source);
		std::ofstream f(dir / L"request", std::ios::binary); WriteRequest(f, request); f.close(); if (!f) throw std::runtime_error("TWAIN-Auftrag konnte nicht geschrieben werden.");
		std::vector<wchar_t> module(32768); DWORD length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
		if (!length || length >= module.size()) throw std::runtime_error("Anwendungspfad fuer den TWAIN-Hilfsprozess konnte nicht ermittelt werden.");
		auto helper = std::filesystem::path(module.data()).parent_path() / L"TwainHost.exe";
#ifndef _WIN64
		helper = module.data();
#endif
		if (!std::filesystem::exists(helper)) throw std::runtime_error("Der x86-TWAIN-Hilfsprozess TwainHost.exe fehlt neben Scanner.exe. Bitte beide Architekturen mit Scripts\\Build.ps1 erstellen.");
		std::wstring command = L"\"" + helper.wstring() + L"\" --twain-helper " + operation + L" \"" + dir.wstring() + L"\"";
		bool scan = wcscmp(operation, L"scan") == 0; bool cancellation = false, cancellationRequested = false;
		BridgeProgress progress(owner, cancellationRequested);
		if (scan && owner) progress.Create();
		STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION process{};
		if (!CreateProcessW(helper.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, helper.parent_path().c_str(), &startup, &process)) throw std::runtime_error("TWAIN-Hilfsprozess konnte nicht gestartet werden.");
		CloseHandle(process.hThread);
		struct ProcessCleanup {
			HANDLE handle;
			~ProcessCleanup() { if (WaitForSingleObject(handle, 0) == WAIT_TIMEOUT) { TerminateProcess(handle, 124); WaitForSingleObject(handle, 5000); } CloseHandle(handle); }
		} processCleanup{process.hProcess};
		DWORD limit = scan ? (std::min)(options.timeoutMs, 180000u) + 10000 : 60000;
		auto cancel = [&]() { if (!cancellation) { std::ofstream marker(dir / L"cancel"); marker << "abort"; cancellation = true; } };
		auto started = GetTickCount64(); DWORD wait = WAIT_TIMEOUT;
		while (GetTickCount64() - started < limit) {
			if (scan && (cancellationRequested || (owner && (GetAsyncKeyState(VK_ESCAPE) & 0x8000)) || GetTickCount64() - started >= options.timeoutMs)) cancel();
			progress.Update(dir, cancellation);
			wait = MsgWaitForMultipleObjects(1, &process.hProcess, FALSE, 100, QS_ALLINPUT);
			if (wait == WAIT_OBJECT_0 || wait == WAIT_FAILED) break;
			MSG message{}; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
				if (message.message == WM_QUIT) { if (scan) cancel(); PostQuitMessage(static_cast<int>(message.wParam)); break; }
				if (scan && message.message == WM_CLOSE) cancel();
				if (progress.Owns(message.hwnd) || (message.message != WM_COMMAND && message.message != WM_CLOSE)) { TranslateMessage(&message); DispatchMessageW(&message); }
				if (scan && cancellationRequested) cancel();
			}
			wait = WAIT_TIMEOUT;
		}
		DWORD code = 1; if (wait != WAIT_OBJECT_0) { TerminateProcess(process.hProcess, 124); WaitForSingleObject(process.hProcess, 5000); code = 124; } else GetExitCodeProcess(process.hProcess, &code);
		if (wait != WAIT_OBJECT_0) { std::ofstream error(dir / L"error.txt", std::ios::binary); error << "TWAIN-Hilfsprozess beendet: Zeitlimit oder Wartefehler; Exit=124. Vollstaendig gesicherte Seiten werden geborgen."; }
		if (scan) { std::ofstream exit(dir / L"exit.txt"); exit << code << ' ' << cancellation; }
		else if (wait != WAIT_OBJECT_0) throw std::runtime_error("TWAIN-Hilfsprozess hat das Zeitlimit ueberschritten.");
		else if (code != 0) { std::ifstream error(dir / L"error.txt", std::ios::binary); std::string message((std::istreambuf_iterator<char>(error)), {}); throw std::runtime_error(message.empty() ? "TWAIN-Hilfsprozess fehlgeschlagen." : message); }
		return dir;
	} catch (...) { std::error_code ec; std::filesystem::remove_all(dir, ec); throw; }
}
struct RemoveBridge { std::filesystem::path path; ~RemoveBridge() { if (!path.empty()) { std::error_code ec; std::filesystem::remove_all(path, ec); } } };
}
std::vector<ScanSource> TwainSources(HWND owner) {
#ifdef _WIN64
	auto dir = Bridge(owner, L"list", {}); RemoveBridge cleanup{dir}; std::ifstream f(dir / L"sources.bin", std::ios::binary); uint32_t version = 0, count = 0; f.read(reinterpret_cast<char*>(&version), 4); f.read(reinterpret_cast<char*>(&count), 4); if (!f || version != 1 || count > 1000) throw std::runtime_error("Ungueltige TWAIN-Quellenantwort."); std::vector<ScanSource> result; for (uint32_t i = 0; i < count; ++i) { auto name = ReadWide(f); auto manufacturer = ReadWide(f); result.push_back({name, manufacturer}); } if (f.peek() != EOF) throw std::runtime_error("Unerwartete TWAIN-Quellendaten."); return result;
#else
	return tw::Sources(owner);
#endif
}
ScanCapabilities TwainCapabilities(HWND owner, const std::wstring& source, bool enableFeeder) {
#ifdef _WIN64
	ScanOptions options; options.source = source; options.feeder = enableFeeder; auto dir = Bridge(owner, L"caps", options); RemoveBridge cleanup{dir}; std::ifstream f(dir / L"caps.txt"); return ReadCaps(f);
#else
	return tw::Capabilities(owner, source, enableFeeder);
#endif
}
ScanResult TwainScan(HWND owner, const ScanOptions& options) {
	auto dir = Bridge(owner, L"scan", options); RemoveBridge cleanup{dir}; ScanResult result;
	size_t expectedPages = 0; std::ifstream status(dir / L"status.txt"); status >> result.completed >> result.cancelled >> expectedPages;
	DWORD code = 1; bool cancellation = false; std::ifstream exit(dir / L"exit.txt"); exit >> code >> cancellation;
	std::ifstream error(dir / L"error.txt", std::ios::binary); result.error.assign(std::istreambuf_iterator<char>(error), {});
	if (!status || !exit || code != 0 || !result.error.empty()) { result.completed = false; if (result.error.empty()) result.error = "TWAIN-Hilfsprozess fehlgeschlagen; Exit=" + std::to_string(code) + "."; }
	result.cancelled = result.cancelled || cancellation;
	RecoverPages(dir, result);
	if (result.completed && result.pages.size() != expectedPages) { result.completed = false; result.error = "TWAIN: Anzahl gesicherter Seiten stimmt nicht mit Abschlussstatus ueberein."; }
	if (result.pages.empty() && result.completed) { result.completed = false; result.error = "TWAIN: Kein vollstaendig erworbenes Bild."; }
	auto diagnostics = UserDataDirectory() / L"TwainDiagnostics"; std::filesystem::create_directories(diagnostics); result.diagnostics = diagnostics / (dir.filename().wstring() + L".log");
	std::ofstream log(result.diagnostics, std::ios::binary); std::ifstream trace(dir / L"trace.log", std::ios::binary); if (trace) log << trace.rdbuf();
	log << "\nHelperExit=" << code << " Completed=" << result.completed << " Cancelled=" << result.cancelled << " RecoveredPages=" << result.pages.size() << "\n" << result.error << '\n';
	status.close(); exit.close(); error.close(); trace.close();
	if (!result.completed || result.cancelled) { cleanup.path.clear(); result.recoveryDirectory = PreserveRecovery(dir, diagnostics / dir.filename(), result.error); log << "RecoveryDirectory=" << result.recoveryDirectory.string() << '\n' << result.error << '\n'; }
	return result;
}
int TwainHelperMain(const std::filesystem::path& directory, const std::wstring& operation) {
	auto root = (UserDataDirectory() / L"Bridge").lexically_normal(); auto dir = directory.lexically_normal(); auto name = dir.filename().wstring();
	if (dir.parent_path() != root || name.size() != 38 || name.find_first_not_of(L"{}0123456789abcdefABCDEF-") != std::wstring::npos) return 2;
	try {
		std::ifstream f(dir / L"request", std::ios::binary); Request r = ReadRequest(f);
		HWND owner = CreateWindowExW(0, L"STATIC", L"Scanner TWAIN", WS_OVERLAPPED, CW_USEDEFAULT, CW_USEDEFAULT, 400, 200, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
		struct Window { HWND h; ~Window() { if (h) DestroyWindow(h); } } window{owner};
		if (operation == L"list") { auto sources = tw::Sources(owner); std::ofstream out(dir / L"sources.bin", std::ios::binary); uint32_t version = 1, count = static_cast<uint32_t>(sources.size()); out.write(reinterpret_cast<const char*>(&version), 4); out.write(reinterpret_cast<const char*>(&count), 4); for (auto& s : sources) { WriteWide(out, s.name); WriteWide(out, s.manufacturer); } if (!out) throw std::runtime_error("TWAIN-Quellen konnten nicht gespeichert werden."); }
		else if (operation == L"caps") { auto c = tw::Capabilities(owner, r.source, r.feeder != 0); std::ofstream out(dir / L"caps.txt"); WriteCaps(out, c); if (!out) throw std::runtime_error("TWAIN-Faehigkeiten konnten nicht gespeichert werden."); }
		else if (operation == L"scan") {
			ScanOptions o; o.source = r.source; o.mode = r.mode; o.dpi = r.dpi; o.widthMm = r.width; o.heightMm = r.height; o.brightness = r.brightness; o.feeder = r.feeder != 0; o.duplex = r.duplex != 0; o.driverDialog = r.dialog != 0; o.transfer = r.transfer; o.timeoutMs = r.timeoutMs;
			o.xMm = r.x; o.yMm = r.y; o.maximumArea = r.maximumArea != 0;
			tw::Trace trace(dir / L"trace.log");
			auto checkpoint = [&](const Image& image, size_t index) { Document page; page.pages.push_back({NewId(), image}); auto path = dir / L"pages" / std::to_wstring(index); SaveWorkspace(page, path); std::ofstream ready(path / L"ready"); ready << "complete"; ready.close(); if (!ready) throw std::runtime_error("TWAIN: Seiten-Checkpoint konnte nicht abgeschlossen werden."); trace.Write("Checkpoint page=" + std::to_string(index)); };
			auto pageStarted = [&](size_t index) { auto path = dir / L"pages" / std::to_wstring(index); std::filesystem::create_directories(path); std::ofstream started(path / L"started"); started << "started"; started.close(); if (!started) throw std::runtime_error("TWAIN: Seitenstart konnte nicht gemeldet werden."); };
			auto result = tw::Scan(owner, o, trace, [&]() { return std::filesystem::exists(dir / L"cancel"); }, checkpoint, pageStarted);
			std::ofstream status(dir / L"status.txt"); status << result.completed << ' ' << result.cancelled << ' ' << result.pages.size();
			if (!result.error.empty()) { std::ofstream error(dir / L"error.txt", std::ios::binary); error << result.error; }
			return result.completed && !result.cancelled && status ? 0 : 1;
		}
		else throw std::runtime_error("Unbekannter TWAIN-Auftrag.");
		return 0;
	} catch (const std::exception& e) { std::ofstream error(dir / L"error.txt", std::ios::binary); error << e.what(); return 1; }
}
int RunTwainTests(std::wstring& report) {
	int failures = 0; auto test = [&](bool ok, const wchar_t* label) { if (!ok) ++failures; report += (ok ? L"PASS " : L"FAIL ") + std::wstring(label) + L"\n"; };
	test(ScanProgressText(0, false) == L"Seite 1 wird gescannt …", L"TWAIN progress starts with scanning page only");
	test(ScanProgressText(1, false) == L"Seite 1 wird gescannt …" && ScanProgressText(2, false) == L"Seite 2 wird gescannt …" && ScanProgressText(12, false) == L"Seite 12 wird gescannt …", L"TWAIN progress shows started page without anticipating next page");
	test(ScanProgressText(2, true) == L"Abbruch wird angefordert …", L"TWAIN progress cancellation does not claim another page is scanning");
	test(tw::BitDepth(0) == 1 && tw::BitDepth(1) == 8 && tw::BitDepth(2) == 24, L"TWAIN bit depth is total bits per pixel: mono 1, gray 8, RGB 24");
	test(sizeof(tw::Identity) == 156 && sizeof(tw::One) == 6 && sizeof(tw::ImageInfo) == 42, L"TWAIN packed ABI");
	test(tw::Unfixed(tw::Fixed(-125.5)) == -125.5 && tw::Unfixed(tw::Fixed(300)) == 300, L"TWAIN FIX32 signed conversion");
	test(tw::Fixed(300) == 0x0000012c && tw::Fixed(1.5) == 0x80000001 && tw::Unfixed(0x8000ff82) == -125.5, L"TWAIN FIX32 independent ABI bit patterns");
	test(sizeof(tw::SetupMemory) == 12 && sizeof(tw::MemoryTransfer) == (sizeof(void*) == 4 ? 38 : 42), L"TWAIN memory transfer packed ABI x86/x64");
	tw::Frame requested{{0,16384},{0,32768},{3,61440},{2,19661}}, rounded = requested; rounded.right = {3,61604};
	test(tw::SameRasterFrame(requested, rounded, 100), L"TWAIN layout accepts bounded subpixel raster quantization");
	rounded.bottom = {2,22938}; test(!tw::SameRasterFrame(requested, rounded, 100), L"TWAIN layout rejects independent oversized Y change");
	auto rejects = [](const std::function<void()>& call) { try { call(); return false; } catch (const std::exception&) { return true; } };
	auto requestBytes = [](const Request& request) { std::ostringstream out(std::ios::out | std::ios::binary); WriteRequest(out, request); return out.str(); };
	auto readRequestBytes = [](const std::string& bytes) { std::istringstream in(bytes, std::ios::in | std::ios::binary); return ReadRequest(in); };
	Request request; request.mode = 1; request.dpi = 75.25; request.brightness = -125.5; request.feeder = request.duplex = 1; request.dialog = 0; request.transfer = 2; request.timeoutMs = 123456; SetRequestSource(request, L"\u00e4\u4e2d");
	Request sourceRequest; std::fill_n(sourceRequest.source, 256, wchar_t(0xfefe)); SetRequestSource(sourceRequest, L"");
	test(std::wstring(readRequestBytes(requestBytes(sourceRequest)).source).empty() && std::all_of(sourceRequest.source, sourceRequest.source + 256, [](wchar_t c) { return c == 0; }), L"TWAIN enumeration source clears debug fill bytes");
	SetRequestSource(sourceRequest, std::wstring(255, L'Z')); SetRequestSource(sourceRequest, L"Brother");
	test(std::wstring(readRequestBytes(requestBytes(sourceRequest)).source) == L"Brother" && std::all_of(sourceRequest.source + 7, sourceRequest.source + 256, [](wchar_t c) { return c == 0; }), L"TWAIN shorter source clears entire previous name");
	test(rejects([&]() { SetRequestSource(sourceRequest, std::wstring(256, L'Z')); }), L"TWAIN source assignment rejects oversized name");
	auto bytes = requestBytes(request);
	request.x = 12.5; request.y = 25.25; request.maximumArea = 1; bytes = requestBytes(request);
	std::string golden(592, '\0');
	auto wireValue = [&](size_t offset, uint64_t value, size_t count) { for (size_t n = 0; n < count; ++n) golden.at(offset + n) = static_cast<char>((value >> (8 * n)) & 255); };
	wireValue(0, 4, 4); wireValue(4, 1, 4); wireValue(8, 0x4052d00000000000ULL, 8); wireValue(16, 0x406a400000000000ULL, 8); wireValue(24, 0x4072900000000000ULL, 8); wireValue(32, 0xc05f600000000000ULL, 8);
	wireValue(40, 1, 4); wireValue(44, 1, 4); wireValue(52, 2, 4); wireValue(56, 123456, 4); wireValue(60, 0x00e4, 2); wireValue(62, 0x4e2d, 2);
	wireValue(572, 0x4029000000000000ULL, 8); wireValue(580, 0x4039400000000000ULL, 8); wireValue(588, 1, 4);
	test(bytes == golden, L"TWAIN request v4 independent little-endian 592-byte wire fixture x86/x64 including X/Y/maximum area");
	auto restoredRequest = readRequestBytes(golden);
	test(restoredRequest.version == 4 && restoredRequest.mode == 1 && restoredRequest.dpi == 75.25 && restoredRequest.width == 210 && restoredRequest.height == 297 && restoredRequest.brightness == -125.5 && restoredRequest.feeder == 1 && restoredRequest.duplex == 1 && restoredRequest.dialog == 0 && restoredRequest.transfer == 2 && restoredRequest.timeoutMs == 123456 && restoredRequest.x == 12.5 && restoredRequest.y == 25.25 && restoredRequest.maximumArea == 1 && std::wstring(restoredRequest.source) == L"\u00e4\u4e2d", L"TWAIN request independent fixture restores every field and UTF-16 source");
	Request defaults; auto restoredDefaults = readRequestBytes(requestBytes(defaults));
	test(requestBytes(restoredDefaults) == requestBytes(defaults) && restoredDefaults.transfer == -1 && restoredDefaults.dpi == 300 && restoredDefaults.source[0] == 0, L"TWAIN request default roundtrip preserves signed transfer and empty source");
	std::fill_n(request.source, 255, L'Z'); request.source[255] = 0;
	test(std::wstring(readRequestBytes(requestBytes(request)).source).size() == 255, L"TWAIN request accepts maximum terminated source");
	test(rejects([&]() { readRequestBytes(""); }) && rejects([&]() { readRequestBytes(bytes.substr(0, 4)); }) && rejects([&]() { readRequestBytes(bytes.substr(0, 571)); }), L"TWAIN request rejects empty and truncated data");
	test(rejects([&]() { readRequestBytes(bytes + '\0'); }) && rejects([&]() { readRequestBytes(bytes + std::string(4, '\0')); }), L"TWAIN request rejects trailing bytes and legacy padded size");
	for (unsigned char version : {0, 2, 3, 5}) { std::string invalid = bytes; invalid[0] = static_cast<char>(version); test(rejects([&]() { readRequestBytes(invalid); }), L"TWAIN request rejects unsupported protocol version"); }
	for (size_t offset : {40, 44, 48, 588}) { std::string invalid = bytes; invalid[offset] = 2; test(rejects([&]() { readRequestBytes(invalid); }), L"TWAIN request rejects invalid boolean field"); }
	Request invalidPosition; invalidPosition.x = -1; test(rejects([&]() { readRequestBytes(requestBytes(invalidPosition)); }), L"TWAIN bridge rejects negative X"); invalidPosition.x = 0; invalidPosition.y = std::numeric_limits<double>::quiet_NaN(); test(rejects([&]() { readRequestBytes(requestBytes(invalidPosition)); }), L"TWAIN bridge rejects nonfinite Y");
	ScanOptions positioned; positioned.xMm = 25.4; positioned.yMm = 50.8; positioned.widthMm = 76.2; positioned.heightMm = 101.6; auto frame = tw::AreaFrame(positioned);
	test(tw::Coordinate(frame.left) == 1 && tw::Coordinate(frame.top) == 2 && tw::Coordinate(frame.right) == 4 && tw::Coordinate(frame.bottom) == 6, L"TWAIN scan layout applies X/Y to all four edges");
	ScanCapabilities maximum; maximum.maxWidthMm = 297; maximum.maxHeightMm = 432; tw::MaximumArea(positioned, maximum);
	test(positioned.xMm == 0 && positioned.yMm == 0 && positioned.widthMm == 297 && positioned.heightMm == 432, L"TWAIN maximum area uses actual capabilities and clears origin");
	test(rejects([&]() { tw::MaximumArea(positioned, {}); }), L"TWAIN unknown maximum area never assumes A4");
	std::string unterminated = bytes; unterminated[570] = 1;
	test(rejects([&]() { readRequestBytes(unterminated); }), L"TWAIN request rejects source without required final terminator");
	std::ostringstream failedRequest; failedRequest.setstate(std::ios::badbit);
	test(rejects([&]() { WriteRequest(failedRequest, defaults); }), L"TWAIN request reports write failure");
	auto put = [](std::vector<uint8_t>& b, size_t offset, uint32_t v, size_t size = 4) { for (size_t n = 0; n < size; ++n) b.at(offset + n) = uint8_t(v >> (n * 8)); };
	std::vector<uint8_t> enumeration(26); put(enumeration, 0, 7, 2); put(enumeration, 2, 3); put(enumeration, 6, 1); put(enumeration, 10, 2); put(enumeration, 14, 0x4000004b); put(enumeration, 18, 0x80000096); put(enumeration, 22, 300);
	auto decoded = tw::Decode(4, enumeration.data(), enumeration.size());
	test(decoded.items.size() == 3 && tw::Number(decoded.type, decoded.items[0]) == 75.25 && tw::Number(decoded.type, decoded.current) == 150.5 && tw::Number(decoded.type, decoded.items[2]) == 300, L"TWAIN ENUMERATION independent FIX32 values/current index");
	test(rejects([&]() { tw::Decode(4, enumeration.data(), 25); }), L"TWAIN truncated capability rejected");
	put(enumeration, 6, 3); test(rejects([&]() { tw::Decode(4, enumeration.data(), enumeration.size()); }), L"TWAIN invalid current index rejected");
	std::vector<uint8_t> range(22); put(range, 0, 7, 2); put(range, 2, 0x4000004b); put(range, 6, 600); put(range, 10, 0x80000000); put(range, 14, 300); put(range, 18, 0x400000c8);
	auto ranged = tw::Decode(6, range.data(), range.size());
	test(ranged.range.available && ranged.range.minimum == 75.25 && ranged.range.maximum == 600 && ranged.range.step == .5 && ranged.range.defaultValue == 300 && ranged.range.current == 200.25, L"TWAIN RANGE independent minimum/maximum/step/default/current");
	std::vector<uint8_t> array{4, 0, 3, 0, 0, 0, 0, 0, 2, 0, 4, 0}; auto mechanisms = tw::Decode(3, array.data(), array.size());
	test(mechanisms.items == std::vector<uint32_t>{0, 2, 4} && !mechanisms.hasCurrent, L"TWAIN ARRAY uses packed UINT16 items");
	std::vector<uint8_t> negative{1, 0, 255, 255, 0, 0}; auto signedValue = tw::Decode(5, negative.data(), negative.size());
	test(tw::Number(signedValue.type, signedValue.current) == -1, L"TWAIN INT16 transfer count sign extension");
	tw::ImageInfo info{}; info.width = 3; info.height = 2; info.samples = 3; info.bits[0] = info.bits[1] = info.bits[2] = 8; info.bitsPerPixel = 24; info.pixelType = 2; info.xResolution = {75, 16384}; info.yResolution = {150, 32768};
	tw::MemoryImage rgb(info, false, false); uint8_t pixels[]{11, 33, 57, 61, 82, 103, 127, 149, 173, 0, 0, 0, 191, 17, 219, 23, 47, 71, 89, 113, 137, 0, 0, 0};
	tw::MemoryTransfer block{}; block.columns = 3; block.rows = 1; block.yOffset = 1; block.bytesPerRow = 12; block.bytesWritten = 9; rgb.Add(block, pixels + 12, 12); block.yOffset = 0; rgb.Add(block, pixels, 12); auto color = rgb.Finish();
	test(color.pixels == std::vector<uint8_t>{57,33,11,255,103,82,61,255,173,149,127,255,219,17,191,255,71,47,23,255,137,113,89,255} && color.dpiX == 75.25 && color.dpiY == 150.5, L"TWAIN RGB strips preserve channels, top-down rows and unequal DPI");
	tw::MemoryImage missing(info, false, false); missing.Add(block, pixels, 12);
	test(rejects([&]() { missing.Finish(); }), L"TWAIN XFERDONE with missing rows rejected");
	test(rejects([&]() { missing.Add(block, pixels, 12); }), L"TWAIN overlapping blocks rejected");
	block.yOffset = 2; test(rejects([&]() { tw::MemoryImage out(info, false, false); out.Add(block, pixels, 12); }), L"TWAIN out-of-bounds block rejected");
	block.yOffset = 0; block.bytesWritten = 8; test(rejects([&]() { tw::MemoryImage out(info, false, false); out.Add(block, pixels, 12); }), L"TWAIN truncated RGB row rejected");
	info.width = 3; info.height = 1; info.samples = 1; info.bits[0] = 8; info.bitsPerPixel = 8; info.pixelType = 1;
	tw::MemoryImage gray(info, true, false); uint8_t grays[]{0, 37, 255}; block.columns = 3; block.rows = 1; block.bytesPerRow = block.bytesWritten = 3; gray.Add(block, grays, 3); auto g = gray.Finish();
	test(g.pixels == std::vector<uint8_t>{255,255,255,255,218,218,218,255,0,0,0,255}, L"TWAIN VANILLA grayscale independent polarity values");
	info.width = 5; info.bits[0] = 1; info.bitsPerPixel = 1; info.pixelType = 0; uint8_t mono = 0x0d;
	tw::MemoryImage bw(info, false, true); block.columns = 5; block.bytesPerRow = block.bytesWritten = 1; bw.Add(block, &mono, 1); auto b = bw.Finish();
	test(b.pixels[0] == 255 && b.pixels[4] == 0 && b.pixels[8] == 255 && b.pixels[12] == 255 && b.pixels[16] == 0, L"TWAIN LSB-first odd-width mono independent pattern");
	auto startScript = [&](std::vector<tw::ScriptCall> calls) { tw::script = std::move(calls); tw::scriptIndex = 0; tw::scriptMismatch = false; };
	auto scriptOk = [&]() { return !tw::scriptMismatch && tw::scriptIndex == tw::script.size(); };
	startScript({{1,9,0x502,0},{2,0x103,1,0},{2,0x103,1,6},{1,5,0x701,0,2},{2,0x103,1,6},{1,5,0x701,0,0},{1,9,0x501,0}});
	{ tw::Session s(tw::ScriptEntry); s.Enable(false); s.Ready(); s.Transfer(0x103, nullptr); s.Transfer(0x103, nullptr); test(s.End() == 2, L"TWAIN ENDXFER preserves pending page count"); s.Transfer(0x103, nullptr); test(s.End() == 0, L"TWAIN final ENDXFER returns state 5"); }
	test(scriptOk(), L"TWAIN two pages: disable without invalid state-5 RESET");
	startScript({{1,9,0x502,0},{2,0x104,1,1},{1,8,1,0,5},{1,5,7,0},{1,9,0x501,0}});
	{ tw::Session s(tw::ScriptEntry); s.Enable(false); s.Ready(); auto rc = s.Transfer(0x104, nullptr); test(std::string(s.Error("native", rc).what()).find("RC=1, CC=5, state=6") != std::string::npos, L"TWAIN native failure captures immediate RC/CC before recovery"); }
	test(scriptOk(), L"TWAIN state-6 failure resets without illegal ENDXFER");
	startScript({{1,9,0x502,0},{2,0x103,1,0},{2,0x103,1,1},{1,8,1,0,5},{1,5,0x701,0,65535},{1,5,7,0},{1,9,0x501,0}});
	{ tw::Session s(tw::ScriptEntry); s.Enable(false); s.Ready(); s.Transfer(0x103, nullptr); s.Transfer(0x103, nullptr); }
	test(scriptOk(), L"TWAIN state-7 failure ENDXFER then RESET then DISABLE");
	startScript({{1,9,0x502,0},{1,9,0x501,0}});
	{ tw::Session s(tw::ScriptEntry); s.Enable(false); test(rejects([&]() { s.Transfer(0x104, nullptr); }), L"TWAIN refuses transfer before XFERREADY"); }
	test(scriptOk(), L"TWAIN abort while waiting disables without RESET");
	startScript({{1,1,2,0,0}});
	{ tw::Session s(tw::ScriptEntry); uint32_t value = 99; test(s.Get(tw::CAP_FEEDERENABLED, value) && value == 0, L"TWAIN ONEVALUE ignores unspecified UINT16 padding bits"); }
	test(scriptOk(), L"TWAIN GETCURRENT padded ONEVALUE call sequence");
	startScript({{1,9,0x502,0},{2,0x103,1,6},{1,5,0x701,0,1},{2,0x103,1,3},{1,5,0x701,0,0},{1,9,0x501,0}});
	{ tw::Session s(tw::ScriptEntry); s.Enable(false); s.Ready(); s.Transfer(0x103, nullptr); s.End(); test(s.Transfer(0x103, nullptr) == 3, L"TWAIN second-page driver cancellation remains cancellation"); s.End(); }
	test(scriptOk(), L"TWAIN cancellation ends active transfer and disables without extra RESET");
	ScanCapabilities caps; caps.feeder = true; caps.maxWidthMm = 296.75; caps.maxHeightMm = 431.125; caps.resolutions = {75.25, 300}; caps.yResolutions = {150.5, 600}; caps.transferMechanisms = {0, 2}; caps.xResolutionRange = ranged.range;
	std::stringstream serialized; WriteCaps(serialized, caps); auto restored = ReadCaps(serialized);
	test(restored.maxWidthMm == 296.75 && restored.maxHeightMm == 431.125 && restored.resolutions == caps.resolutions && restored.yResolutions == caps.yResolutions && restored.xResolutionRange.current == 200.25 && restored.transferMechanisms == caps.transferMechanisms, L"TWAIN bridge capability roundtrip keeps independent axis values");
	auto recoveryDirectory = UserDataDirectory() / L"Tests" / NewId(); RemoveBridge recoveryCleanup{recoveryDirectory};
	test(CountReadyPages(recoveryDirectory) == 0, L"TWAIN progress counts zero before any checkpoint exists");
	Document fixture; fixture.pages.push_back({NewId(), color}); SaveWorkspace(fixture, recoveryDirectory / L"pages" / L"0");
	test(CountReadyPages(recoveryDirectory) == 0, L"TWAIN progress ignores saved page without ready marker");
	{ std::ofstream ready(recoveryDirectory / L"pages" / L"0" / L"ready"); ready << "complete"; }
	std::filesystem::create_directories(recoveryDirectory / L"pages" / L"1");
	std::filesystem::create_directories(recoveryDirectory / L"pages" / L"2");
	{ std::ofstream ready(recoveryDirectory / L"pages" / L"2" / L"ready"); ready << "complete"; }
	test(CountReadyPages(recoveryDirectory) == 1 && CountReadyPages(recoveryDirectory, 1) == 1, L"TWAIN progress stops at unfinished checkpoint even if later marker exists");
	ScanResult recovered; recovered.error = "RC=1, CC=5"; RecoverPages(recoveryDirectory, recovered);
	test(!recovered.completed && recovered.error == "RC=1, CC=5" && recovered.pages.size() == 1 && recovered.pages[0].pixels == color.pixels && recovered.pages[0].dpiX == 75.25 && recovered.pages[0].dpiY == 150.5, L"TWAIN failed scan salvages completed checkpoint, ignores unfinished next page");
	{ std::ofstream ready(recoveryDirectory / L"pages" / L"1" / L"ready"); ready << "complete"; }
	test(CountReadyPages(recoveryDirectory) == 3 && CountReadyPages(recoveryDirectory, 1) == 3, L"TWAIN progress advances through consecutive ready markers without loading images");
	ScanResult corrupt; corrupt.completed = true; RecoverPages(recoveryDirectory, corrupt);
	test(!corrupt.completed && !corrupt.error.empty() && corrupt.pages.size() == 1 && corrupt.pages[0].pixels == color.pixels, L"TWAIN corrupt later checkpoint retains earlier acquired page and failure");
	auto archived = recoveryDirectory.parent_path() / NewId(); RemoveBridge archiveCleanup{archived}; std::string archiveError;
	{ std::ofstream held(recoveryDirectory / L"held.txt"); held << "open"; auto retained = PreserveRecovery(recoveryDirectory, archived, archiveError);
		test(retained == recoveryDirectory && !archiveError.empty() && std::filesystem::exists(recoveryDirectory / L"pages" / L"0" / L"ready"), L"TWAIN failed recovery rename with open Windows handle retains original page"); }
	archiveError.clear(); auto moved = PreserveRecovery(recoveryDirectory, archived, archiveError);
	test(moved == archived && archiveError.empty() && std::filesystem::exists(archived / L"pages" / L"0" / L"ready"), L"TWAIN recovery rename succeeds after stream handles close");
	test(TwainHelperMain(UserDataDirectory() / L"invalid", L"list") == 2, L"TWAIN bridge rejects arbitrary paths"); return failures;
}
}
