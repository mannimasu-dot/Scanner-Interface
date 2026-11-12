#include "ScannerCore.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <shlobj.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>
#include <set>
#include <stdexcept>
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

namespace scanner {
using Microsoft::WRL::ComPtr;
static void Check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("Windows-Bildverarbeitung fehlgeschlagen (" + std::to_string(static_cast<unsigned long>(hr)) + ")."); }
static std::vector<size_t> Selected(const Document& d) {
	std::vector<size_t> s = d.selection; std::sort(s.begin(), s.end());
	s.erase(std::unique(s.begin(), s.end()), s.end());
	s.erase(std::lower_bound(s.begin(), s.end(), d.pages.size()), s.end()); return s;
}
void Image::Validate() const {
	if (!width || !height || width > 100000 || height > 100000 ||
		uint64_t(width) * height > 100000000 || pixels.size() != uint64_t(width) * height * 4 ||
		!std::isfinite(dpiX) || !std::isfinite(dpiY) || dpiX < 1 || dpiY < 1 || dpiX > 100000 || dpiY > 100000)
		throw std::runtime_error("Ungueltige Bildabmessungen, Aufloesung oder Bildgroesse (maximal 100 Megapixel pro Seite).");
}
std::wstring NewId() {
	GUID id{}; Check(CoCreateGuid(&id)); wchar_t text[40]{};
	if (!StringFromGUID2(id, text, 40)) throw std::runtime_error("GUID konnte nicht in Text umgewandelt werden.");
	return text;
}
std::vector<Image> ReadImages(const std::filesystem::path& path) {
	ComPtr<IWICImagingFactory> factory; Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
	ComPtr<IWICBitmapDecoder> decoder; Check(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder));
	UINT count = 0; Check(decoder->GetFrameCount(&count));
	if (!count || count > 10000) throw std::runtime_error("Ungueltige Anzahl von Bildseiten.");
	std::vector<Image> images;
	for (UINT n = 0; n < count; ++n) {
		ComPtr<IWICBitmapFrameDecode> frame; Check(decoder->GetFrame(n, &frame)); Image image;
		WICPixelFormatGUID nativeFormat{}; Check(frame->GetPixelFormat(&nativeFormat));
		image.kind = nativeFormat == GUID_WICPixelFormat24bppBGR || nativeFormat == GUID_WICPixelFormat24bppRGB ? PixelKind::Color24 : nativeFormat == GUID_WICPixelFormat8bppGray ? PixelKind::Gray8 : nativeFormat == GUID_WICPixelFormatBlackWhite ? PixelKind::Mono1 : PixelKind::Other;
		Check(frame->GetSize(&image.width, &image.height)); frame->GetResolution(&image.dpiX, &image.dpiY);
		if (!std::isfinite(image.dpiX) || image.dpiX < 1) image.dpiX = 300;
		if (!std::isfinite(image.dpiY) || image.dpiY < 1) image.dpiY = 300;
		if (uint64_t(image.width) * image.height > 100000000) throw std::runtime_error("Bild ueberschreitet die Grenze von 100 Megapixeln.");
		image.pixels.resize(size_t(image.width) * image.height * 4); image.Validate();
		ComPtr<IWICFormatConverter> converter; Check(factory->CreateFormatConverter(&converter));
		Check(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
		Check(converter->CopyPixels(nullptr, image.width * 4, static_cast<UINT>(image.pixels.size()), image.pixels.data()));
		for (size_t p = 0; p < image.pixels.size(); p += 4) { unsigned alpha = image.pixels[p + 3]; for (size_t c = 0; c < 3; ++c) image.pixels[p + c] = static_cast<uint8_t>((image.pixels[p + c] * alpha + 255 * (255 - alpha) + 127) / 255); image.pixels[p + 3] = 255; }
		image.pixels.Spill(); image.pixels.ReleaseMemory(); images.push_back(std::move(image));
	}
	return images;
}
void Transform(Image& image, Edit operation) {
	image.Validate(); Image out = image;
	if (operation == Edit::RotateLeft || operation == Edit::RotateRight) { std::swap(out.width, out.height); std::swap(out.dpiX, out.dpiY); }
	const auto* input = static_cast<const Image&>(image).pixels.data(); auto* output = out.pixels.data();
	for (uint32_t y = 0; y < image.height; ++y) for (uint32_t x = 0; x < image.width; ++x) {
		uint32_t dx = x, dy = y;
		switch (operation) {
		case Edit::RotateRight: dx = image.height - 1 - y; dy = x; break;
		case Edit::RotateLeft: dx = y; dy = image.width - 1 - x; break;
		case Edit::Rotate180: dx = image.width - 1 - x; dy = image.height - 1 - y; break;
		case Edit::MirrorHorizontal: dx = image.width - 1 - x; break;
		case Edit::MirrorVertical: dy = image.height - 1 - y; break;
		default: break;
		}
		auto src = input + (size_t(y) * image.width + x) * 4;
		auto dst = output + (size_t(dy) * out.width + dx) * 4;
		for (int c = 0; c < 3; ++c) dst[c] = operation == Edit::Invert ? static_cast<uint8_t>(255 - src[c]) : src[c];
		dst[3] = 255;
	}
	image = std::move(out);
}
static RECT Clip(const Image& image, RECT r) {
	if (r.left > r.right) std::swap(r.left, r.right); if (r.top > r.bottom) std::swap(r.top, r.bottom);
	r.left = std::clamp<LONG>(r.left, 0, image.width); r.right = std::clamp<LONG>(r.right, 0, image.width);
	r.top = std::clamp<LONG>(r.top, 0, image.height); r.bottom = std::clamp<LONG>(r.bottom, 0, image.height);
	if (r.left == r.right || r.top == r.bottom) throw std::runtime_error("Kein gueltiges Rechteck ausgewaehlt."); return r;
}
void DeleteRectangle(Image& image, RECT rectangle) {
	image.Validate(); auto r = Clip(image, rectangle);
	for (LONG y = r.top; y < r.bottom; ++y) std::fill(image.pixels.begin() + (size_t(y) * image.width + r.left) * 4, image.pixels.begin() + (size_t(y) * image.width + r.right) * 4, 255);
}
Image Crop(const Image& image, RECT rectangle) {
	image.Validate(); auto r = Clip(image, rectangle); Image out;
	out.width = r.right - r.left; out.height = r.bottom - r.top; out.dpiX = image.dpiX; out.dpiY = image.dpiY;
	out.kind = image.kind;
	out.pixels.resize(size_t(out.width) * out.height * 4);
	for (uint32_t y = 0; y < out.height; ++y) std::copy_n(image.pixels.data() + ((size_t(y) + r.top) * image.width + r.left) * 4, size_t(out.width) * 4, out.pixels.data() + size_t(y) * out.width * 4);
	return out;
}
void Document::SelectAll() { selection.resize(pages.size()); std::iota(selection.begin(), selection.end(), size_t(0)); }
void Document::EraseSelected() { auto s = Selected(*this); for (auto it = s.rbegin(); it != s.rend(); ++it) pages.erase(pages.begin() + *it); selection.clear(); }
void Document::CopySelected(bool cut) { clipboard.clear(); for (auto i : Selected(*this)) clipboard.push_back(pages[i]); if (cut) EraseSelected(); }
void Document::Paste(size_t before) {
	before = std::min(before, pages.size()); std::vector<Page> copy = clipboard; for (auto& p : copy) p.id = NewId();
	pages.insert(pages.begin() + before, copy.begin(), copy.end()); selection.resize(copy.size()); std::iota(selection.begin(), selection.end(), before);
}
void Document::MoveSelected(size_t before) {
	auto s = Selected(*this); before = std::min(before, pages.size()); std::vector<Page> moved;
	for (auto i : s) moved.push_back(pages[i]);
	size_t adjusted = before - std::count_if(s.begin(), s.end(), [before](size_t i) { return i < before; });
	EraseSelected(); pages.insert(pages.begin() + adjusted, moved.begin(), moved.end()); selection.resize(moved.size()); std::iota(selection.begin(), selection.end(), adjusted);
}
void Document::Reverse() {
	auto s = Selected(*this); for (auto& i : s) i = pages.size() - 1 - i;
	std::reverse(pages.begin(), pages.end()); std::sort(s.begin(), s.end()); selection = std::move(s);
}
void Document::InterleaveBacks(size_t frontCount) {
	if (!frontCount || frontCount > pages.size() || pages.size() - frontCount != frontCount) throw std::runtime_error("Zum Einsortieren muessen gleich viele Vorder- und Rueckseiten vorliegen. Vorderseiten zuerst, danach Rueckseiten in umgekehrter Stapelreihenfolge scannen.");
	std::vector<Page> out; out.reserve(pages.size());
	for (size_t i = 0; i < frontCount; ++i) { out.push_back(pages[i]); out.push_back(pages[pages.size() - 1 - i]); }
	pages = std::move(out); selection.clear();
}
void Document::EditSelected(Edit operation) {
	auto s = Selected(*this); std::vector<Image> edited; for (auto i : s) { Image image = pages[i].image; Transform(image, operation); image.pixels.Spill(); image.pixels.ReleaseMemory(); pages[i].image.pixels.ReleaseMemory(); edited.push_back(std::move(image)); }
	for (size_t j = 0; j < s.size(); ++j) pages[s[j]].Replace(std::move(edited[j]));
}
void Page::Replace(Image value) {
	value.Validate(); value.pixels.Spill();
	if (original.pixels.empty()) original = image;
	history.push_back(image); if (history.size() > 20) history.erase(history.begin());
	image = std::move(value); thumbnail = {}; image.pixels.ReleaseMemory();
}
void Page::Undo() { if (!history.empty()) { image = history.back(); history.pop_back(); thumbnail = {}; } }
const Image& Page::Thumbnail() const {
	if (thumbnail.width) return thumbnail;
	image.Validate(); PixelReadScope release{image.pixels}; Image preview; double scale = std::min(1., 180. / std::max(image.width, image.height));
	preview.width = std::max(1u, static_cast<uint32_t>(image.width * scale)); preview.height = std::max(1u, static_cast<uint32_t>(image.height * scale)); preview.dpiX = image.dpiX * preview.width / image.width; preview.dpiY = image.dpiY * preview.height / image.height;
	preview.dpiX = std::max(1., preview.dpiX); preview.dpiY = std::max(1., preview.dpiY); preview.pixels.resize(size_t(preview.width) * preview.height * 4);
	const auto* input = image.pixels.data(); auto* output = preview.pixels.data();
	for (uint32_t y = 0; y < preview.height; ++y) for (uint32_t x = 0; x < preview.width; ++x) std::copy_n(input + (size_t(uint64_t(y) * image.height / preview.height) * image.width + uint64_t(x) * image.width / preview.width) * 4, 4, output + (size_t(y) * preview.width + x) * 4);
	preview.pixels.Spill(); preview.pixels.ReleaseMemory(); thumbnail = std::move(preview); return thumbnail;
}
void Document::UndoSelected() { for (auto i : Selected(*this)) pages[i].Undo(); }
void Document::RestoreOriginals() { for (auto i : Selected(*this)) pages[i].Replace(pages[i].original); }
void Document::TrimCache(size_t budget) const {
	size_t resident = 0;
	for (const auto& page : pages) { page.image.pixels.Spill(); page.original.pixels.Spill(); page.original.pixels.ReleaseMemory(); for (const auto& old : page.history) { old.pixels.Spill(); old.pixels.ReleaseMemory(); } resident += page.image.pixels.ResidentBytes(); }
	for (const auto& page : clipboard) { page.image.pixels.Spill(); page.image.pixels.ReleaseMemory(); }
	for (const auto& page : pages) if (resident > budget) { const auto size = page.image.pixels.ResidentBytes(); page.image.pixels.ReleaseMemory(); resident -= std::min(resident, size); }
}
std::vector<Image> Document::Images(bool selectedOnly) const {
	std::vector<Image> images;
	if (selectedOnly) { for (auto i : Selected(*this)) images.push_back(pages[i].image); }
	else for (const auto& p : pages) images.push_back(p.image);
	return images;
}
std::filesystem::path UserDataDirectory() {
	PWSTR path = nullptr; Check(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &path));
	auto dir = std::filesystem::path(path) / L"Scanner"; CoTaskMemFree(path); std::filesystem::create_directories(dir); return dir;
}
template<class T> static void Write(std::ostream& f, const T& v) { f.write(reinterpret_cast<const char*>(&v), sizeof(v)); if (!f) throw std::runtime_error("Arbeitsbereich konnte nicht geschrieben werden."); }
template<class T> static T Read(std::istream& f) { T v{}; f.read(reinterpret_cast<char*>(&v), sizeof(v)); if (!f) throw std::runtime_error("Beschaedigter Arbeitsbereich."); return v; }
static bool SafeGeneration(const std::wstring& s) { return s.size() == 38 && s.front() == L'{' && s.back() == L'}' && s.find_first_not_of(L"{}0123456789abcdefABCDEF-") == std::wstring::npos; }
static void WriteImage(std::ostream& output, const Image& image) {
	image.Validate(); PixelReadScope release{image.pixels};
	Write(output, image.width); Write(output, image.height); Write(output, image.dpiX); Write(output, image.dpiY); Write(output, static_cast<uint32_t>(image.kind));
	output.write(reinterpret_cast<const char*>(image.pixels.data()), image.pixels.size());
	if (!output) throw std::runtime_error("Seitendaten konnten nicht gespeichert werden.");
}
static Image ReadImage(std::istream& input, bool withKind, uint64_t& total) {
	Image image; image.width = Read<uint32_t>(input); image.height = Read<uint32_t>(input); image.dpiX = Read<double>(input); image.dpiY = Read<double>(input);
	if (withKind) { auto kind = Read<uint32_t>(input); if (kind > 3) throw std::runtime_error("Ungueltiger Bildtyp."); image.kind = static_cast<PixelKind>(kind); }
	auto size = uint64_t(image.width) * image.height * 4; total += size;
	if (size > 400000000 || total > 20000000000ULL) throw std::runtime_error("Arbeitsbereich ueberschreitet die Datengrenze.");
	image.pixels.resize(static_cast<size_t>(size)); image.Validate(); input.read(reinterpret_cast<char*>(image.pixels.data()), image.pixels.size());
	if (!input) throw std::runtime_error("Beschaedigte Seitendaten."); image.pixels.Spill(); image.pixels.ReleaseMemory(); return image;
}
void SaveWorkspace(const Document& document, const std::filesystem::path& directory) {
	if (document.pages.size() > 10000) throw std::runtime_error("Arbeitsbereich auf 10000 Seiten begrenzt.");
	document.TrimCache(); uint64_t total = 0; for (const auto& page : document.pages) { page.image.Validate(); total += page.image.pixels.size() + page.original.pixels.size(); for (const auto& old : page.history) total += old.pixels.size(); if (total > 20000000000ULL) throw std::runtime_error("Arbeitsbereich einschliesslich Originalen und Historie ueberschreitet 20 GB."); }
	std::filesystem::create_directories(directory); auto id = NewId(); auto generation = directory / id; std::filesystem::create_directory(generation);
	bool committed = false;
	try {
		std::ofstream manifest(generation / L"pages.bin", std::ios::binary); Write(manifest, uint32_t(0x53434E34)); Write(manifest, uint32_t(document.pages.size()));
		for (size_t n = 0; n < document.pages.size(); ++n) {
			const auto& image = document.pages[n].image; image.Validate();
			std::ofstream f(generation / (std::to_wstring(n) + L".page"), std::ios::binary);
			WriteImage(f, image); f.close(); if (!f) throw std::runtime_error("Datentraeger beim Speichern voll oder nicht verfuegbar.");
			const auto& page = document.pages[n]; std::ofstream original(generation / (std::to_wstring(n) + L".original"), std::ios::binary);
			WriteImage(original, page.original.pixels.empty() ? image : page.original); Write(original, uint32_t(page.history.size()));
			for (const auto& old : page.history) WriteImage(original, old); original.close(); if (!original) throw std::runtime_error("Original und Historie konnten nicht gespeichert werden.");
			std::wstring pageId = document.pages[n].id; if (!SafeGeneration(pageId)) pageId = NewId();
			manifest.write(reinterpret_cast<const char*>(pageId.data()), 38 * sizeof(wchar_t));
		}
		manifest.close(); if (!manifest) throw std::runtime_error("Arbeitsbereich konnte nicht gespeichert werden.");
		auto temp = directory / L"current.tmp"; std::ofstream pointer(temp, std::ios::binary);
		pointer.write(reinterpret_cast<const char*>(id.data()), id.size() * sizeof(wchar_t)); pointer.close(); if (!pointer) throw std::runtime_error("Arbeitsbereich konnte nicht gespeichert werden.");
		if (!MoveFileExW(temp.c_str(), (directory / L"current").c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Arbeitsbereich konnte nicht aktiviert werden.");
		committed = true;
		std::error_code enumerateError;
		for (const auto& entry : std::filesystem::directory_iterator(directory, enumerateError)) if (entry.is_directory() && entry.path() != generation && SafeGeneration(entry.path().filename().wstring())) { std::error_code ec; std::filesystem::remove_all(entry.path(), ec); }
	} catch (...) { if (!committed) { std::error_code ec; std::filesystem::remove_all(generation, ec); throw; } }
}
void LoadWorkspace(Document& document, const std::filesystem::path& directory) {
	auto pointerPath = directory / L"current"; if (!std::filesystem::exists(pointerPath)) return;
	if (std::filesystem::file_size(pointerPath) != 38 * sizeof(wchar_t)) throw std::runtime_error("Beschaedigter Arbeitsbereichsverweis.");
	std::ifstream pointer(pointerPath, std::ios::binary); std::wstring id(38, L' '); pointer.read(reinterpret_cast<char*>(id.data()), 76);
	if (!pointer || !SafeGeneration(id)) throw std::runtime_error("Ungueltiger Arbeitsbereichsverweis.");
	auto generation = directory / id; std::ifstream f(generation / L"pages.bin", std::ios::binary);
	auto version = Read<uint32_t>(f);
	if (version != 0x53434E32 && version != 0x53434E33 && version != 0x53434E34) throw std::runtime_error("Unbekannte Arbeitsbereichsversion.");
	auto count = Read<uint32_t>(f); if (count > 10000) throw std::runtime_error("Zu viele Arbeitsbereichsseiten.");
	Document restored; uint64_t bytes = 0;
	for (uint32_t n = 0; n < count; ++n) {
		Page page; page.id.resize(38); f.read(reinterpret_cast<char*>(page.id.data()), 76); if (!f || !SafeGeneration(page.id)) throw std::runtime_error("Ungueltige Seitenkennung.");
		std::ifstream imageFile(generation / (std::to_wstring(n) + L".page"), std::ios::binary); page.image = ReadImage(imageFile, version != 0x53434E32, bytes);
		if (imageFile.peek() != EOF) throw std::runtime_error("Beschaedigte Seitendaten."); page.original = page.image;
		auto originalPath = generation / (std::to_wstring(n) + L".original");
		if (version == 0x53434E34 && !std::filesystem::exists(originalPath)) throw std::runtime_error("Originaldatei des Arbeitsbereichs fehlt.");
		if (std::filesystem::exists(originalPath)) { std::ifstream original(originalPath, std::ios::binary); page.original = ReadImage(original, true, bytes); auto historyCount = Read<uint32_t>(original); if (historyCount > 20) throw std::runtime_error("Ungueltige Bearbeitungshistorie."); for (uint32_t i = 0; i < historyCount; ++i) page.history.push_back(ReadImage(original, true, bytes)); if (original.peek() != EOF) throw std::runtime_error("Beschaedigte Originaldaten."); }
		restored.pages.push_back(std::move(page));
	}
	if (f.peek() != EOF) throw std::runtime_error("Unerwartete Daten im Arbeitsbereich."); document = std::move(restored);
}
void ClearWorkspace(const std::filesystem::path& directory) { if (std::filesystem::exists(directory)) std::filesystem::remove_all(directory); }
bool CopyWindowsClipboard(HWND owner, const Image& image) {
	image.Validate(); const size_t size = sizeof(BITMAPINFOHEADER) + image.pixels.size(); HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size); if (!memory) return false;
	auto data = static_cast<uint8_t*>(GlobalLock(memory)); if (!data) { GlobalFree(memory); return false; }
	BITMAPINFOHEADER h{}; h.biSize = sizeof(h); h.biWidth = image.width; h.biHeight = -static_cast<LONG>(image.height); h.biPlanes = 1; h.biBitCount = 32; h.biCompression = BI_RGB; h.biSizeImage = static_cast<DWORD>(image.pixels.size()); h.biXPelsPerMeter = static_cast<LONG>(image.dpiX / .0254); h.biYPelsPerMeter = static_cast<LONG>(image.dpiY / .0254);
	memcpy(data, &h, sizeof(h)); memcpy(data + sizeof(h), image.pixels.data(), image.pixels.size()); GlobalUnlock(memory);
	if (!OpenClipboard(owner)) { GlobalFree(memory); return false; } EmptyClipboard(); bool ok = SetClipboardData(CF_DIB, memory) != nullptr; CloseClipboard(); if (!ok) GlobalFree(memory); return ok;
}
Image ReadWindowsClipboard(HWND owner) {
	if (!OpenClipboard(owner)) throw std::runtime_error("Zwischenablage ist belegt.");
	struct Close { ~Close() { CloseClipboard(); } } close;
	auto memory = GetClipboardData(CF_DIB); if (!memory) throw std::runtime_error("Die Zwischenablage enthaelt kein unterstuetztes DIB-Bild.");
	auto size = GlobalSize(memory); auto data = static_cast<const uint8_t*>(GlobalLock(memory));
	if (!data) throw std::runtime_error("Zwischenablage konnte nicht gelesen werden.");
	struct Unlock { HANDLE h; ~Unlock() { GlobalUnlock(h); } } unlock{memory};
	if (size < sizeof(BITMAPINFOHEADER)) throw std::runtime_error("Ungueltiges Zwischenablagebild."); BITMAPINFOHEADER h{}; memcpy(&h, data, sizeof(h));
	if (h.biSize < sizeof(h) || h.biSize > size || h.biWidth <= 0 || !h.biHeight || h.biHeight == LONG_MIN || h.biPlanes != 1 || h.biCompression != BI_RGB || (h.biBitCount != 24 && h.biBitCount != 32)) throw std::runtime_error("Zwischenablage: nur unkomprimierte 24-/32-Bit-DIB-Bilder werden unterstuetzt.");
	Image image; image.width = h.biWidth; image.height = std::abs(h.biHeight);
	if (uint64_t(image.width) * image.height > 100000000) throw std::runtime_error("Zwischenablagebild zu gross.");
	image.dpiX = h.biXPelsPerMeter > 0 ? h.biXPelsPerMeter * .0254 : 300; image.dpiY = h.biYPelsPerMeter > 0 ? h.biYPelsPerMeter * .0254 : 300;
	size_t stride = ((size_t(image.width) * h.biBitCount + 31) / 32) * 4;
	if (stride * image.height > size - h.biSize) throw std::runtime_error("Unvollstaendige Zwischenablagebilddaten.");
	image.pixels.resize(size_t(image.width) * image.height * 4);
	for (uint32_t y = 0; y < image.height; ++y) for (uint32_t x = 0; x < image.width; ++x) { const auto src = data + size_t(h.biSize) + size_t(h.biHeight > 0 ? image.height - y - 1 : y) * stride + size_t(x) * (h.biBitCount / 8); auto dst = image.pixels.data() + (size_t(y) * image.width + x) * 4; memcpy(dst, src, 3); dst[3] = 255; }
	image.Validate(); return image;
}
int RunCoreTests(const std::filesystem::path& directory, std::wstring& report) {
	int failures = 0, tests = 0; auto test = [&](bool ok, const wchar_t* name) { ++tests; report += (ok ? L"PASS " : L"FAIL ") + std::wstring(name) + L"\n"; if (!ok) ++failures; };
	Image image; image.width = 3; image.height = 2; image.dpiX = 200; image.dpiY = 300; image.pixels.resize(24, 255); for (size_t i = 0; i < 6; ++i) image.pixels[i * 4] = static_cast<uint8_t>(i);
	Image rotated = image; Transform(rotated, Edit::RotateRight); test(rotated.width == 2 && rotated.height == 3 && rotated.dpiX == 300 && rotated.pixels[0] == 3, L"Rotation pixel and DPI"); Transform(rotated, Edit::RotateLeft); test(rotated.pixels == image.pixels && rotated.dpiX == image.dpiX, L"Rotation roundtrip");
	Image erased = image; DeleteRectangle(erased, RECT{1, 0, 2, 1}); test(erased.width == 3 && erased.pixels[4] == 255 && erased.pixels[0] == 0 && erased.pixels[8] == 2, L"Rectangle delete preserves outside");
	auto cropped = Crop(image, RECT{1, 0, 3, 2}); test(cropped.width == 2 && cropped.height == 2 && cropped.pixels[0] == 1 && cropped.pixels[8] == 4 && cropped.dpiY == 300, L"Crop pixels dimensions DPI");
	bool rejected = false; try { Crop(image, RECT{0, 0, 0, 0}); } catch (...) { rejected = true; } test(rejected, L"Reject empty crop");
	Document d; for (int i = 0; i < 4; ++i) d.pages.push_back(Page{NewId(), image}); const std::vector<Page> ids = d.pages;
	d.selection = {2, 0, 0, 900}; d.MoveSelected(4); test(d.pages[0].id == ids[1].id && d.pages[2].id == ids[0].id && d.pages[3].id == ids[2].id, L"Stable multi-selection move");
	d.CopySelected(true); test(d.pages.size() == 2 && d.clipboard.size() == 2, L"Cut internal pages"); d.Paste(2); test(d.pages.size() == 4 && d.pages[2].id != ids[0].id, L"Paste unique identities");
	d.pages = ids; d.InterleaveBacks(2); test(d.pages[0].id == ids[0].id && d.pages[1].id == ids[3].id && d.pages[2].id == ids[1].id && d.pages[3].id == ids[2].id, L"Reverse-stack back interleave");
	rejected = false; try { d.InterleaveBacks(1); } catch (...) { rejected = true; } test(rejected, L"Reject mismatched backs");
	d.Reverse(); d.Reverse(); test(d.pages[0].id == ids[0].id, L"Reverse twice");
	auto work = directory / L"workspace-test"; SaveWorkspace(d, work); Document restored; LoadWorkspace(restored, work); test(restored.pages.size() == 4 && restored.pages[0].id == d.pages[0].id && restored.pages[0].image.pixels == image.pixels && restored.clipboard.empty(), L"Persistent workspace and transient clipboard");
	d.pages.pop_back(); SaveWorkspace(d, work); LoadWorkspace(restored, work); test(restored.pages.size() == 3, L"Atomic workspace replacement"); ClearWorkspace(work);
	rejected = false; Image bad = image; bad.pixels.pop_back(); try { bad.Validate(); } catch (...) { rejected = true; } test(rejected, L"Reject truncated image");
	for (auto edit : {Edit::Invert, Edit::MirrorHorizontal, Edit::MirrorVertical, Edit::Rotate180}) { Image roundtrip = image; Transform(roundtrip, edit); Transform(roundtrip, edit); test(roundtrip.pixels == image.pixels && roundtrip.dpiX == image.dpiX, (L"Edit involution " + std::to_wstring(static_cast<int>(edit))).c_str()); }
	auto clipped = Crop(image, RECT{-50, -10, 2, 1}); test(clipped.width == 2 && clipped.height == 1 && clipped.pixels[4] == 1, L"Crop clips outside coordinates");
	std::filesystem::create_directories(work); { std::ofstream corrupt(work / L"current", std::ios::binary); corrupt << "../outside"; } auto oldCount = restored.pages.size(); rejected = false; try { LoadWorkspace(restored, work); } catch (...) { rejected = true; } test(rejected && restored.pages.size() == oldCount, L"Reject corrupt workspace without replacing current document"); ClearWorkspace(work);
	Image copy = image; const auto firstPixel = static_cast<const Image&>(image).pixels[0]; copy.pixels[0] ^= 255;
	test(static_cast<const Image&>(image).pixels[0] == firstPixel && copy.pixels != image.pixels, L"Pixel copy-on-write preserves source");
	Document history; history.pages.push_back({NewId(), image}); history.selection = {0}; history.EditSelected(Edit::Invert);
	const auto& thumb = history.pages[0].Thumbnail(); test(thumb.width <= 180 && thumb.height <= 180 && thumb.pixels == history.pages[0].image.pixels, L"Lazy thumbnail retains small-image pixels");
	test(history.pages[0].original.pixels == image.pixels && history.pages[0].image.pixels != image.pixels, L"Original preserved after edit");
	history.TrimCache(0); test(history.pages[0].image.pixels.ResidentBytes() == 0 && history.pages[0].original.pixels.ResidentBytes() == 0, L"Disk-backed cache releases all resident pixels");
	auto historyPath = directory / L"history-work"; SaveWorkspace(history, historyPath); Document loaded; LoadWorkspace(loaded, historyPath);
	test(loaded.pages[0].image.pixels.ResidentBytes() == 0 && loaded.pages[0].original.pixels.ResidentBytes() == 0 && loaded.pages[0].history.size() == 1, L"Workspace restores lazy originals and history");
	loaded.selection = {0}; loaded.UndoSelected(); test(loaded.pages[0].image.pixels == image.pixels, L"Undo survives restart");
	test(loaded.pages[0].Thumbnail().pixels == image.pixels, L"Undo invalidates thumbnail");
	loaded.pages[0].Replace(Crop(image, RECT{0,0,1,1})); loaded.RestoreOriginals(); test(loaded.pages[0].image.width == image.width && loaded.pages[0].image.pixels == image.pixels, L"Restore original after crop");
	for (int i = 0; i < 25; ++i) loaded.EditSelected(Edit::Invert); test(loaded.pages[0].history.size() == 20 && loaded.pages[0].original.pixels == image.pixels, L"Bounded history never discards original");
	SaveWorkspace(loaded, historyPath);
	for (const auto& entry : std::filesystem::recursive_directory_iterator(historyPath)) if (entry.path().extension() == L".original") std::filesystem::remove(entry.path());
	Document legacy; rejected = false; try { LoadWorkspace(legacy, historyPath); } catch (...) { rejected = true; } test(rejected, L"Missing original in new workspace rejected");
	for (const auto& entry : std::filesystem::recursive_directory_iterator(historyPath)) if (entry.path().filename() == L"pages.bin") { std::fstream manifest(entry.path(), std::ios::in | std::ios::out | std::ios::binary); Write(manifest, uint32_t(0x53434E33)); }
	LoadWorkspace(legacy, historyPath); test(legacy.pages[0].original.pixels == legacy.pages[0].image.pixels, L"Legacy workspace without original sidecar migrates"); ClearWorkspace(historyPath);
	report += L"Core: " + std::to_wstring(tests) + L" tests, " + std::to_wstring(failures) + L" failures\n"; return failures;
}
}
