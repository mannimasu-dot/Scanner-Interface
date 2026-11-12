#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include "ScannerPixels.h"

namespace scanner {
enum class PixelKind : uint32_t { Color24, Gray8, Mono1, Other };
// Canonical pixels: top-down BGRA, opaque alpha; physical resolution in DPI.
struct Image {
	uint32_t width = 0, height = 0;
	double dpiX = 300, dpiY = 300;
	PixelKind kind = PixelKind::Color24;
	PixelBuffer pixels;
	void Validate() const;
};
struct Page {
	std::wstring id;
	Image image, original;
	mutable Image thumbnail;
	std::vector<Image> history;
	Page() = default;
	Page(std::wstring identity, Image value) : id(std::move(identity)), image(std::move(value)), original(image) { image.pixels.Spill(); image.pixels.ReleaseMemory(); }
	void Replace(Image value);
	void Undo();
	const Image& Thumbnail() const;
};
enum class Edit { RotateLeft, RotateRight, Rotate180, Invert, MirrorHorizontal, MirrorVertical };
std::vector<Image> ReadImages(const std::filesystem::path& path);
void Transform(Image& image, Edit operation);
void DeleteRectangle(Image& image, RECT rectangle);
Image Crop(const Image& image, RECT rectangle);
std::wstring NewId();
struct Document {
	std::vector<Page> pages;
	std::vector<Page> clipboard;
	std::vector<size_t> selection;
	void SelectAll();
	void EraseSelected();
	void CopySelected(bool cut);
	void Paste(size_t before);
	void MoveSelected(size_t before);
	void Reverse();
	void InterleaveBacks(size_t frontCount);
	void EditSelected(Edit operation);
	void UndoSelected();
	void RestoreOriginals();
	void TrimCache(size_t budget = 64 * 1024 * 1024) const;
	std::vector<Image> Images(bool selectedOnly) const;
};
void SaveWorkspace(const Document& document, const std::filesystem::path& directory);
void LoadWorkspace(Document& document, const std::filesystem::path& directory);
void ClearWorkspace(const std::filesystem::path& directory);
bool CopyWindowsClipboard(HWND owner, const Image& image);
Image ReadWindowsClipboard(HWND owner);
std::filesystem::path UserDataDirectory();
int RunCoreTests(const std::filesystem::path& directory, std::wstring& report);

enum class Format { Bmp, Jpeg, Png, Tiff, Pdf, PdfA };
enum class PdfPageMode { A4, A4LandscapeIfNeeded, Original };
enum class TiffCompression { None, G3, G4, Lzw, Zip };
enum class OcrLanguage { German, English, GermanEnglish };
struct ExportOptions {
	Format format = Format::Pdf;
	bool multipage = true;
	int quality = 100;
	int pngCompression = 6;
	PdfPageMode pdfPageMode = PdfPageMode::Original;
	bool showThumbnails = true;
	bool searchablePdf = false;
	OcrLanguage ocrLanguage = OcrLanguage::German;
	TiffCompression tiffColor = TiffCompression::Lzw;
	TiffCompression tiffMono = TiffCompression::G4;
	bool invertMono = false;
	std::wstring title, subject, keywords, author;
};
std::vector<std::filesystem::path> ExportImages(const std::vector<Image>& images,
	const std::filesystem::path& path, const ExportOptions& options);
int RunExportTests(const std::filesystem::path& directory, std::wstring& report);
}
