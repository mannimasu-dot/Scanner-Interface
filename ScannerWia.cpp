#include "ScannerWia.h"
#include <sti.h>
#include <wia.h>
#include <shlwapi.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <iterator>
#include <sstream>
#include <stdexcept>
#pragma comment(lib, "shlwapi.lib")

namespace scanner {
namespace {
using Microsoft::WRL::ComPtr;

void Check(HRESULT hr, const char* operation) {
	if (FAILED(hr)) {
		std::ostringstream message;
		message << "WIA: " << operation << " (HRESULT 0x" << std::hex << static_cast<unsigned long>(hr) << ").";
		throw std::runtime_error(message.str());
	}
}
struct ComSession {
	HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	ComSession() { if (hr != RPC_E_CHANGED_MODE) Check(hr, "COM nicht verfuegbar"); }
	~ComSession() { if (SUCCEEDED(hr)) CoUninitialize(); }
	ComSession(const ComSession&) = delete;
	ComSession& operator=(const ComSession&) = delete;
};
struct String {
	BSTR value;
	explicit String(const std::wstring& text) : value(SysAllocString(text.c_str())) {
		if (!value) throw std::bad_alloc();
	}
	~String() { SysFreeString(value); }
	String(const String&) = delete;
	String& operator=(const String&) = delete;
};
struct Variant {
	PROPVARIANT value{};
	~Variant() { PropVariantClear(&value); }
};
PROPSPEC Spec(PROPID id) {
	PROPSPEC spec{};
	spec.ulKind = PRSPEC_PROPID;
	spec.propid = id;
	return spec;
}
ComPtr<IWiaPropertyStorage> Properties(IWiaItem2* item) {
	ComPtr<IWiaPropertyStorage> properties;
	Check(item->QueryInterface(IID_PPV_ARGS(properties.GetAddressOf())), "Geraeteeigenschaften nicht verfuegbar");
	return properties;
}
bool ReadLong(IWiaPropertyStorage* properties, PROPID id, LONG& result) {
	auto spec = Spec(id);
	Variant data;
	HRESULT hr = properties->ReadMultiple(1, &spec, &data.value);
	if (hr == S_FALSE || hr == E_INVALIDARG) return false;
	Check(hr, "Eigenschaft konnte nicht gelesen werden");
	if (data.value.vt == VT_EMPTY) return false;
	if (data.value.vt != VT_I4) throw std::runtime_error("WIA: Ungueltiger Eigenschaftstyp.");
	result = data.value.lVal;
	return true;
}
LONG RequiredLong(IWiaPropertyStorage* properties, PROPID id) {
	LONG value = 0;
	if (!ReadLong(properties, id, value)) throw std::runtime_error("WIA: Benoetigte Eigenschaft fehlt.");
	return value;
}
void WriteLong(IWiaPropertyStorage* properties, PROPID id, LONG value) {
	auto spec = Spec(id);
	PROPVARIANT data{};
	data.vt = VT_I4;
	data.lVal = value;
	Check(properties->WriteMultiple(1, &spec, &data, WIA_IPA_FIRST), "Einstellung wurde abgelehnt");
	if (RequiredLong(properties, id) != value) throw std::runtime_error("WIA: Treiber hat eine angeforderte Einstellung veraendert.");
}
std::wstring ReadString(IWiaPropertyStorage* properties, PROPID id) {
	auto spec = Spec(id);
	Variant data;
	Check(properties->ReadMultiple(1, &spec, &data.value), "Geraeteinformation konnte nicht gelesen werden");
	if (data.value.vt != VT_BSTR || !data.value.bstrVal || !*data.value.bstrVal)
		throw std::runtime_error("WIA: Geraetename oder Geraete-ID fehlt.");
	return std::wstring(data.value.bstrVal, SysStringLen(data.value.bstrVal));
}
ComPtr<IWiaDevMgr2> Manager() {
	ComPtr<IWiaDevMgr2> manager;
	Check(CoCreateInstance(CLSID_WiaDevMgr2, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(manager.GetAddressOf())),
		"WIA-2-Geraeteverwaltung nicht verfuegbar");
	return manager;
}
struct TemporaryDirectory {
	std::filesystem::path path;
	TemporaryDirectory() {
		path = std::filesystem::temp_directory_path() / (L"Scanner-WIA-" + NewId());
		if (!std::filesystem::create_directory(path)) throw std::runtime_error("WIA: Temporaeres Verzeichnis konnte nicht neu erstellt werden.");
	}
	~TemporaryDirectory() { std::error_code error; std::filesystem::remove_all(path, error); }
};
struct DialogFiles {
	LONG count = 0;
	BSTR* paths = nullptr;
	~DialogFiles() {
		if (paths) { for (LONG i = 0; i < count; ++i) SysFreeString(paths[i]); CoTaskMemFree(paths); }
	}
};
bool Cancelled(HRESULT hr) { return hr == S_FALSE || hr == E_ABORT || hr == HRESULT_FROM_WIN32(ERROR_CANCELLED); }
void Decode(const std::filesystem::path& path, ScanResult& result) {
	auto images = ReadImages(path);
	if (images.empty()) throw std::runtime_error("WIA: Uebertragene Datei enthaelt keine lesbaren Bilder.");
	result.pages.insert(result.pages.end(), std::make_move_iterator(images.begin()), std::make_move_iterator(images.end()));
}
ComPtr<IWiaItem2> ScanItem(IWiaItem2* root, bool feeder) {
	const GUID& category = feeder ? WIA_CATEGORY_FEEDER : WIA_CATEGORY_FLATBED;
	ComPtr<IEnumWiaItem2> items;
	Check(root->EnumChildItems(nullptr, items.GetAddressOf()), "Scanbereiche konnten nicht aufgelistet werden");
	if (!items) throw std::runtime_error("WIA: Keine Scanbereiche vorhanden.");
	for (;;) {
		ComPtr<IWiaItem2> item;
		ULONG fetched = 0;
		Check(items->Next(1, item.GetAddressOf(), &fetched), "Scanbereich-Auflistung fehlgeschlagen");
		if (!fetched) break;
		if (!item) throw std::runtime_error("WIA: Ungueltiger Scanbereich.");
		GUID actual{};
		LONG type = 0;
		Check(item->GetItemCategory(&actual), "Scanbereich-Kategorie fehlt");
		Check(item->GetItemType(&type), "Scanbereich-Typ fehlt");
		if (IsEqualGUID(actual, category) && (type & WiaItemTypeImage) && (type & WiaItemTypeTransfer)) return item;
	}
	throw std::runtime_error(feeder ? "WIA: Kein uebertragbarer Dokumenteneinzug vorhanden." : "WIA: Kein uebertragbares Flachbett vorhanden.");
}
bool SupportsFlags(IWiaPropertyStorage* properties, PROPID id, LONG requested) {
	auto spec = Spec(id);
	Variant data;
	ULONG attributes = 0;
	HRESULT hr = properties->GetPropertyAttributes(1, &spec, &attributes, &data.value);
	if (hr == S_FALSE || hr == E_INVALIDARG) return false;
	Check(hr, "Faehigkeiten konnten nicht gelesen werden");
	return (attributes & WIA_PROP_FLAG) && data.value.vt == (VT_VECTOR | VT_I4) &&
		data.value.cal.cElems >= WIA_FLAG_NUM_ELEMS && data.value.cal.pElems &&
		(requested & ~data.value.cal.pElems[WIA_FLAG_VALUES]) == 0;
}
void ConfigureHandling(IWiaPropertyStorage* root, IWiaPropertyStorage* child, const ScanOptions& options) {
	LONG capabilities = 0, current = 0;
	bool rootConfigured = false;
	if (ReadLong(root, WIA_DPS_DOCUMENT_HANDLING_CAPABILITIES, capabilities)) {
		LONG needed = options.feeder ? FEED : FLAT;
		if (options.duplex) needed |= DUP;
		if ((capabilities & needed) != needed) throw std::runtime_error("WIA: Angeforderte Papierzufuhr oder Duplex nicht unterstuetzt.");
		if (ReadLong(root, WIA_DPS_DOCUMENT_HANDLING_SELECT, current)) {
			WriteLong(root, WIA_DPS_DOCUMENT_HANDLING_SELECT, (options.feeder ? FEEDER : FLATBED) | (options.duplex ? DUPLEX : 0));
			rootConfigured = true;
		}
	}
	if (options.feeder) {
		// WIA 2 uses the feeder child's selection, not the legacy root FEEDER bit.
		LONG selection = options.duplex ? DUPLEX : FRONT_ONLY;
		if (ReadLong(child, WIA_IPS_DOCUMENT_HANDLING_SELECT, current)) {
			if (!SupportsFlags(child, WIA_IPS_DOCUMENT_HANDLING_SELECT, selection))
				throw std::runtime_error("WIA: Einzug unterstuetzt den angeforderten Seitenmodus nicht.");
			WriteLong(child, WIA_IPS_DOCUMENT_HANDLING_SELECT, selection);
		} else if (options.duplex && !rootConfigured) {
			throw std::runtime_error("WIA: Duplex kann nicht explizit aktiviert werden.");
		}
		WriteLong(child, WIA_IPS_PAGES, ALL_PAGES);
	}
}
template<class Range, class Write> void ConfigureArea(const ScanOptions& options, LONG dpi, Range range, Write write) {
	write(WIA_IPS_XPOS, 0); write(WIA_IPS_YPOS, 0);
	auto xRange = range(WIA_IPS_XEXTENT), yRange = range(WIA_IPS_YEXTENT);
	LONG width = options.maximumArea ? xRange.second : static_cast<LONG>(std::lround(options.widthMm * dpi / 25.4));
	LONG height = options.maximumArea ? yRange.second : static_cast<LONG>(std::lround(options.heightMm * dpi / 25.4));
	write(WIA_IPS_XEXTENT, xRange.first); write(WIA_IPS_YEXTENT, yRange.first);
	write(WIA_IPS_XPOS, options.maximumArea ? 0 : static_cast<LONG>(std::lround(options.xMm * dpi / 25.4)));
	write(WIA_IPS_YPOS, options.maximumArea ? 0 : static_cast<LONG>(std::lround(options.yMm * dpi / 25.4)));
	write(WIA_IPS_XEXTENT, width); write(WIA_IPS_YEXTENT, height);
}
void ConfigureImage(IWiaPropertyStorage* properties, const ScanOptions& options) {
	const LONG intent[] = { WIA_INTENT_IMAGE_TYPE_TEXT, WIA_INTENT_IMAGE_TYPE_GRAYSCALE, WIA_INTENT_IMAGE_TYPE_COLOR };
	WriteLong(properties, WIA_IPS_CUR_INTENT, intent[options.mode]);
	LONG dpi = static_cast<LONG>(std::lround(options.dpi));
	WriteLong(properties, WIA_IPS_XRES, dpi);
	WriteLong(properties, WIA_IPS_YRES, dpi);
	auto extentRange = [&](PROPID id) {
		auto spec = Spec(id); Variant range; ULONG attributes = 0;
		Check(properties->GetPropertyAttributes(1, &spec, &attributes, &range.value), "Maximaler Scanbereich nicht verfuegbar");
		if (!(attributes & WIA_PROP_RANGE) || range.value.vt != (VT_VECTOR | VT_I4) || range.value.cal.cElems < WIA_RANGE_NUM_ELEMS || !range.value.cal.pElems || range.value.cal.pElems[WIA_RANGE_MAX] <= 0) throw std::runtime_error("WIA: Maximaler Scanbereich nicht bekannt.");
		return std::pair<LONG, LONG>{range.value.cal.pElems[WIA_RANGE_MIN], range.value.cal.pElems[WIA_RANGE_MAX]};
	};
	ConfigureArea(options, dpi, extentRange, [&](PROPID id, LONG value) { WriteLong(properties, id, value); });
	LONG brightness = 0;
	if (ReadLong(properties, WIA_IPS_BRIGHTNESS, brightness)) {
		auto spec = Spec(WIA_IPS_BRIGHTNESS);
		Variant range;
		ULONG attributes = 0;
		Check(properties->GetPropertyAttributes(1, &spec, &attributes, &range.value), "Helligkeitsbereich nicht verfuegbar");
		if (!(attributes & WIA_PROP_RANGE) || range.value.vt != (VT_VECTOR | VT_I4) ||
			range.value.cal.cElems < WIA_RANGE_NUM_ELEMS || !range.value.cal.pElems)
			throw std::runtime_error("WIA: Helligkeitssteuerung nicht unterstuetzt.");
		const LONG* values = range.value.cal.pElems;
		double minimum = values[WIA_RANGE_MIN], maximum = values[WIA_RANGE_MAX], step = values[WIA_RANGE_STEP];
		if (maximum < minimum || step <= 0) throw std::runtime_error("WIA: Ungueltiger Helligkeitsbereich.");
		double value = minimum + (options.brightness + 1000) * (maximum - minimum) / 2000;
		value = minimum + std::round((value - minimum) / step) * step;
		WriteLong(properties, WIA_IPS_BRIGHTNESS, static_cast<LONG>(std::clamp(value, minimum, maximum)));
	} else if (options.brightness != 0) {
		throw std::runtime_error("WIA: Helligkeitssteuerung nicht unterstuetzt.");
	}
	auto spec = Spec(WIA_IPA_FORMAT);
	PROPVARIANT format{};
	format.vt = VT_CLSID;
	GUID bmp = WiaImgFmt_BMP;
	format.puuid = &bmp;
	Check(properties->WriteMultiple(1, &spec, &format, WIA_IPA_FIRST), "BMP-Uebertragungsformat wurde abgelehnt");
}
class DownloadCallback final : public IWiaTransferCallback {
	std::atomic<ULONG> references{1};
	std::filesystem::path directory;
	ULONGLONG started = GetTickCount64();
	uint32_t timeout;
	std::vector<ComPtr<IStream>> streams;
public:
	std::vector<std::filesystem::path> paths;
	HRESULT failure = S_OK;
	explicit DownloadCallback(const std::filesystem::path& path, uint32_t timeoutMs) : directory(path), timeout(timeoutMs) {}
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
		if (!object) return E_POINTER;
		*object = nullptr;
		if (iid != IID_IUnknown && iid != IID_IWiaTransferCallback) return E_NOINTERFACE;
		*object = static_cast<IWiaTransferCallback*>(this);
		AddRef();
		return S_OK;
	}
	ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
	ULONG STDMETHODCALLTYPE Release() override { ULONG remaining = --references; if (!remaining) delete this; return remaining; }
	HRESULT TimeLimit() {
		if (SUCCEEDED(failure) && GetTickCount64() - started >= timeout) failure = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
		return failure;
	}
	HRESULT STDMETHODCALLTYPE TransferCallback(LONG, WiaTransferParams* parameters) override {
		if (!parameters) return E_POINTER;
		if (FAILED(parameters->hrErrorStatus)) failure = parameters->hrErrorStatus;
		return TimeLimit();
	}
	HRESULT STDMETHODCALLTYPE GetNextStream(LONG, BSTR, BSTR, IStream** stream) override {
		if (!stream) return E_POINTER;
		*stream = nullptr;
		if (FAILED(TimeLimit())) return failure;
		try {
			// Driver-provided BSTR item names are not trusted as filesystem paths.
			auto path = directory / (std::to_wstring(paths.size() + 1) + L".bmp");
			ComPtr<IStream> file;
			Check(SHCreateStreamOnFileEx(path.c_str(), STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE,
				FILE_ATTRIBUTE_NORMAL, TRUE, nullptr, file.GetAddressOf()), "Uebertragungsdatei konnte nicht erstellt werden");
			paths.push_back(path);
			streams.push_back(file);
			*stream = file.Detach();
			return S_OK;
		} catch (const std::bad_alloc&) { failure = E_OUTOFMEMORY; }
		catch (...) { failure = E_FAIL; }
		return failure;
	}
	void CloseStreams() {
		for (auto& stream : streams) Check(stream->Commit(STGC_DEFAULT), "Uebertragungsdatei konnte nicht abgeschlossen werden");
		streams.clear();
	}
};
void Validate(const ScanOptions& options) {
	if (!std::isfinite(options.xMm) || !std::isfinite(options.yMm) || options.xMm < 0 || options.yMm < 0 || options.xMm > 2000 || options.yMm > 2000) throw std::runtime_error("WIA: Ungueltige Scanposition.");
	if (!std::isfinite(options.dpi) || options.dpi < 25 || options.dpi > 9600 ||
		!std::isfinite(options.widthMm) || !std::isfinite(options.heightMm) ||
		options.widthMm <= 0 || options.heightMm <= 0 || options.widthMm > 2000 || options.heightMm > 2000 ||
		options.widthMm * options.dpi / 25.4 < 0.5 || options.heightMm * options.dpi / 25.4 < 0.5 ||
		!std::isfinite(options.brightness) || options.brightness < -1000 || options.brightness > 1000 ||
		options.mode < 0 || options.mode > 2 || options.timeoutMs < 1000 || (options.duplex && !options.feeder))
		throw std::runtime_error("WIA: Ungueltige Scanoptionen; Duplex erfordert den Dokumenteneinzug.");
}
}

std::vector<ScanSource> WiaSources() {
	ComSession session;
	auto manager = Manager();
	ComPtr<IEnumWIA_DEV_INFO> devices;
	Check(manager->EnumDeviceInfo(WIA_DEVINFO_ENUM_LOCAL, devices.GetAddressOf()), "Geraete konnten nicht aufgelistet werden");
	if (!devices) throw std::runtime_error("WIA: Keine Geraeteauflistung verfuegbar.");
	std::vector<ScanSource> sources;
	for (;;) {
		ComPtr<IWiaPropertyStorage> properties;
		ULONG fetched = 0;
		Check(devices->Next(1, properties.GetAddressOf(), &fetched), "Geraeteauflistung fehlgeschlagen");
		if (!fetched) break;
		if (!properties) throw std::runtime_error("WIA: Geraeteeigenschaften fehlen.");
		if (GET_STIDEVICE_TYPE(RequiredLong(properties.Get(), WIA_DIP_DEV_TYPE)) != StiDeviceTypeScanner) continue;
		sources.push_back({ ReadString(properties.Get(), WIA_DIP_DEV_NAME), ReadString(properties.Get(), WIA_DIP_DEV_ID) });
	}
	return sources;
}

ScanResult WiaScan(HWND owner, const std::wstring& deviceId, const ScanOptions& options) {
	ScanResult result;
	try {
		Validate(options);
		if (deviceId.empty() || deviceId.find(L'\0') != std::wstring::npos) throw std::runtime_error("WIA: Ungueltige Geraete-ID.");
		ComSession session;
		auto manager = Manager();
		String id(deviceId);
		ComPtr<IWiaItem2> root;
		Check(manager->CreateDevice(0, id.value, root.GetAddressOf()), "Scanner konnte nicht geoeffnet werden");
		if (!root) throw std::runtime_error("WIA: Treiber liefert kein Scannergeraet.");
		TemporaryDirectory temporary;
		if (options.driverDialog) {
			String folder(temporary.path.wstring()), filename(L"Scan");
			DialogFiles files;
			ComPtr<IWiaItem2> selected;
			HRESULT hr = root->DeviceDlg(0, owner, folder.value, filename.value, &files.count, &files.paths, selected.GetAddressOf());
			if (Cancelled(hr)) { result.cancelled = true; return result; }
			Check(hr, "Treiberdialog oder Uebertragung fehlgeschlagen");
			if (files.count <= 0 || !files.paths) throw std::runtime_error("WIA: Treiberdialog hat keine Dateien geliefert.");
			for (LONG i = 0; i < files.count; ++i) {
				if (!files.paths[i] || !*files.paths[i]) throw std::runtime_error("WIA: Treiber liefert einen leeren Dateipfad.");
				std::filesystem::path path(std::wstring(files.paths[i], SysStringLen(files.paths[i])));
				if (path.is_relative()) path = temporary.path / path;
				Decode(path, result);
			}
		} else {
			auto item = ScanItem(root.Get(), options.feeder);
			auto rootProperties = Properties(root.Get()), properties = Properties(item.Get());
			ConfigureHandling(rootProperties.Get(), properties.Get(), options);
			ConfigureImage(properties.Get(), options);
			ComPtr<IWiaTransfer> transfer;
			Check(item.As(&transfer), "Direkte WIA-Uebertragung nicht verfuegbar");
			ComPtr<DownloadCallback> callback;
			callback.Attach(new DownloadCallback(temporary.path, options.timeoutMs));
			HRESULT hr = transfer->Download(0, callback.Get());
			Check(callback->failure, "Direkte Uebertragung fehlgeschlagen");
			if (Cancelled(hr)) { result.cancelled = true; return result; }
			Check(hr, "Download fehlgeschlagen");
			transfer.Reset();
			callback->CloseStreams();
			for (const auto& path : callback->paths) Decode(path, result);
		}
		if (result.pages.empty()) throw std::runtime_error("WIA: Scan hat keine lesbaren Seiten geliefert.");
		result.completed = true;
	} catch (const std::exception& error) { result.error = error.what(); }
	return result;
}
int RunWiaTests(std::wstring& report) {
	int failures = 0; auto test = [&](bool ok, const wchar_t* name) { if (!ok) ++failures; report += (ok ? L"PASS " : L"FAIL ") + std::wstring(name) + L"\n"; };
	ScanOptions options; options.xMm = 25.4; options.yMm = 50.8; options.widthMm = 76.2; options.heightMm = 101.6;
	std::vector<std::pair<PROPID, LONG>> writes;
	auto range = [](PROPID id) { return std::pair<LONG, LONG>{2, id == WIA_IPS_XEXTENT ? 1200 : 2400}; };
	auto write = [&](PROPID id, LONG value) { writes.emplace_back(id, value); };
	ConfigureArea(options, 100, range, write);
	test(writes == std::vector<std::pair<PROPID, LONG>>{{WIA_IPS_XPOS, 0}, {WIA_IPS_YPOS, 0}, {WIA_IPS_XEXTENT, 2}, {WIA_IPS_YEXTENT, 2}, {WIA_IPS_XPOS, 100}, {WIA_IPS_YPOS, 200}, {WIA_IPS_XEXTENT, 300}, {WIA_IPS_YEXTENT, 400}}, L"WIA writes actual X/Y positions and width/height in resolution pixels using supported minimum extents");
	writes.clear(); options.maximumArea = true; ConfigureArea(options, 100, range, write);
	test(writes[4].second == 0 && writes[5].second == 0 && writes[6].second == 1200 && writes[7].second == 2400, L"WIA maximum area uses device extent maxima without assumed paper size");
	options.xMm = -1; bool rejected = false; try { Validate(options); } catch (const std::exception&) { rejected = true; } test(rejected, L"WIA rejects negative area position before accessing hardware");
	options.xMm = 0; options.yMm = std::nan(""); rejected = false; try { Validate(options); } catch (const std::exception&) { rejected = true; } test(rejected, L"WIA rejects nonfinite area position before accessing hardware");
	return failures;
}
}
