#include "ScannerCore.h"
#include "ScannerOcr.h"
#include <map>
#include <wincodec.h>
#include <wrl/client.h>
#include <shlwapi.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <array>
#include "ThirdParty/zlib/zlib.h"
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "shlwapi.lib")

namespace scanner {
using Microsoft::WRL::ComPtr;
static void CheckExport(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("Bildencoder fehlgeschlagen (" + std::to_string(static_cast<unsigned long>(hr)) + "). Die angeforderte Kompression wird vom installierten Codec moeglicherweise nicht unterstuetzt."); }
static bool Monochrome(const Image& image) {
	for (size_t i = 0; i < image.pixels.size(); i += 4) if (image.pixels[i] != image.pixels[i + 1] || image.pixels[i] != image.pixels[i + 2] || (image.pixels[i] != 0 && image.pixels[i] != 255)) return false;
	return true;
}
static void Property(IPropertyBag2* bag, const wchar_t* name, VARTYPE type, ULONG value) {
	PROPBAG2 p{}; p.pstrName = const_cast<wchar_t*>(name); VARIANT v{}; VariantInit(&v); v.vt = type;
	if (type == VT_R4) v.fltVal = value / 100.f; else if (type == VT_UI1) v.bVal = static_cast<BYTE>(value); else v.ulVal = value;
	CheckExport(bag->Write(1, &p, &v));
}
static void Encode(IStream* stream, const std::vector<Image>& images, const ExportOptions& options) {
	ComPtr<IWICImagingFactory> factory; CheckExport(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
	GUID container = GUID_ContainerFormatPng;
	switch (options.format) { case Format::Bmp: container = GUID_ContainerFormatBmp; break; case Format::Jpeg: container = GUID_ContainerFormatJpeg; break; case Format::Tiff: container = GUID_ContainerFormatTiff; break; default: break; }
	ComPtr<IWICBitmapEncoder> encoder; CheckExport(factory->CreateEncoder(container, nullptr, &encoder)); CheckExport(encoder->Initialize(stream, WICBitmapEncoderNoCache));
	for (const auto& source : images) {
		PixelReadScope release{source.pixels};
		source.Validate(); bool mono = Monochrome(source); Image image = source;
		ComPtr<IWICBitmapFrameEncode> frame; ComPtr<IPropertyBag2> bag; CheckExport(encoder->CreateNewFrame(&frame, &bag));
		if (options.format == Format::Jpeg) Property(bag.Get(), L"ImageQuality", VT_R4, options.quality);
		if (options.format == Format::Tiff) {
			auto compression = mono ? options.tiffMono : options.tiffColor; BYTE c = WICTiffCompressionNone;
			switch (compression) {
			case TiffCompression::G3: c = WICTiffCompressionCCITT3; break;
			case TiffCompression::G4: c = WICTiffCompressionCCITT4; break;
			case TiffCompression::Lzw: c = WICTiffCompressionLZW; break;
			case TiffCompression::Zip: c = WICTiffCompressionZIP; break;
			default: break;
			}
			if (!mono && (compression == TiffCompression::G3 || compression == TiffCompression::G4)) throw std::runtime_error("TIFF G3/G4 ist nur fuer Schwarzweissbilder ohne Graustufen zulaessig.");
			if (mono && options.invertMono && (compression == TiffCompression::G3 || compression == TiffCompression::G4)) Transform(image, Edit::Invert);
			Property(bag.Get(), L"TiffCompressionMethod", VT_UI1, c);
		}
		CheckExport(frame->Initialize(bag.Get())); CheckExport(frame->SetSize(image.width, image.height)); CheckExport(frame->SetResolution(image.dpiX, image.dpiY));
		WICPixelFormatGUID pixelFormat = mono && options.format == Format::Tiff ? GUID_WICPixelFormatBlackWhite : GUID_WICPixelFormat24bppBGR;
		CheckExport(frame->SetPixelFormat(&pixelFormat));
		ComPtr<IWICBitmap> bitmap; CheckExport(factory->CreateBitmapFromMemory(image.width, image.height, GUID_WICPixelFormat32bppBGRA, image.width * 4, static_cast<UINT>(image.pixels.size()), const_cast<BYTE*>(static_cast<const Image&>(image).pixels.data()), &bitmap));
		CheckExport(bitmap->SetResolution(image.dpiX, image.dpiY));
		ComPtr<IWICFormatConverter> converter; CheckExport(factory->CreateFormatConverter(&converter));
		CheckExport(converter->Initialize(bitmap.Get(), pixelFormat, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
		CheckExport(frame->WriteSource(converter.Get(), nullptr)); CheckExport(frame->Commit());
	}
	CheckExport(encoder->Commit()); CheckExport(stream->Commit(STGC_DEFAULT));
}
static std::string JpegBytes(const Image& image, int quality) {
	ComPtr<IStream> stream; CheckExport(CreateStreamOnHGlobal(nullptr, TRUE, &stream)); ExportOptions o; o.format = Format::Jpeg; o.quality = quality; Encode(stream.Get(), {image}, o);
	STATSTG stat{}; CheckExport(stream->Stat(&stat, STATFLAG_NONAME)); if (stat.cbSize.HighPart || stat.cbSize.LowPart > 400000000) throw std::runtime_error("JPEG-Daten zu gross.");
	LARGE_INTEGER zero{}; CheckExport(stream->Seek(zero, STREAM_SEEK_SET, nullptr)); std::string bytes(stat.cbSize.LowPart, '\0'); ULONG read = 0; CheckExport(stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read)); if (read != bytes.size()) throw std::runtime_error("JPEG-Daten unvollstaendig."); return bytes;
}
static std::string Number(double v) { std::ostringstream s; s.imbue(std::locale::classic()); s << std::fixed << std::setprecision(5) << v; return s.str(); }
static std::string Deflate(const std::string& data, int level) {
	uLongf size = compressBound(static_cast<uLong>(data.size())); std::string result(size, '\0');
	if (compress2(reinterpret_cast<Bytef*>(result.data()), &size, reinterpret_cast<const Bytef*>(data.data()), static_cast<uLong>(data.size()), level) != Z_OK) throw std::runtime_error("Deflate-Kompression fehlgeschlagen.");
	result.resize(size); return result;
}
static void BigEndian(std::string& bytes, uint32_t value) { for (int shift = 24; shift >= 0; shift -= 8) bytes += static_cast<char>(value >> shift); }
static void PngChunk(std::ofstream& out, const char* type, const std::string& data) {
	std::string header; BigEndian(header, static_cast<uint32_t>(data.size())); header.append(type, 4); out.write(header.data(), header.size()); out.write(data.data(), data.size());
	auto crc = crc32(0, reinterpret_cast<const Bytef*>(type), 4); crc = crc32(crc, reinterpret_cast<const Bytef*>(data.data()), static_cast<uInt>(data.size()));
	std::string trailer; BigEndian(trailer, static_cast<uint32_t>(crc)); out.write(trailer.data(), trailer.size());
}
static void WritePng(const Image& image, const std::filesystem::path& path, int level) {
	PixelReadScope release{image.pixels};
	image.Validate(); std::ofstream out(path, std::ios::binary); if (!out) throw std::runtime_error("PNG-Zieldatei kann nicht angelegt werden.");
	out.write("\x89PNG\r\n\x1a\n", 8); std::string header; BigEndian(header, image.width); BigEndian(header, image.height); header.append("\x08\x02\0\0\0", 5); PngChunk(out, "IHDR", header);
	std::string resolution; BigEndian(resolution, static_cast<uint32_t>(std::lround(image.dpiX / .0254))); BigEndian(resolution, static_cast<uint32_t>(std::lround(image.dpiY / .0254))); resolution += '\1'; PngChunk(out, "pHYs", resolution);
	PngChunk(out, "sRGB", std::string(1, '\0'));
	std::string rows; rows.reserve((size_t(image.width) * 3 + 1) * image.height);
	for (uint32_t y = 0; y < image.height; ++y) {
		rows += '\0';
		for (uint32_t x = 0; x < image.width; ++x) { const auto p = (size_t(y) * image.width + x) * 4; rows += static_cast<char>(image.pixels[p + 2]); rows += static_cast<char>(image.pixels[p + 1]); rows += static_cast<char>(image.pixels[p]); }
	}
	PngChunk(out, "IDAT", Deflate(rows, level)); PngChunk(out, "IEND", {}); out.close(); if (!out) throw std::runtime_error("PNG konnte nicht vollstaendig geschrieben werden.");
}
static std::string XmlText(const std::wstring& text) {
	std::string result;
	for (size_t i = 0; i < text.size(); ++i) {
		uint32_t c = text[i];
		if (c >= 0xd800 && c <= 0xdbff) { if (++i == text.size() || text[i] < 0xdc00 || text[i] > 0xdfff) throw std::runtime_error("Ungueltiges UTF-16 in PDF-Metadaten."); c = 0x10000 + ((c - 0xd800) << 10) + text[i] - 0xdc00; }
		else if ((c >= 0xdc00 && c <= 0xdfff) || (c < 32 && c != 9 && c != 10 && c != 13) || c == 0xfffe || c == 0xffff) throw std::runtime_error("Ungueltiges XML-Zeichen in PDF-Metadaten.");
		if (c == '&') result += "&amp;"; else if (c == '<') result += "&lt;"; else if (c == '>') result += "&gt;"; else if (c == 13) result += "&#xD;";
		else if (c < 0x80) result += static_cast<char>(c);
		else if (c < 0x800) { result += static_cast<char>(0xc0 | (c >> 6)); result += static_cast<char>(0x80 | (c & 63)); }
		else if (c < 0x10000) { result += static_cast<char>(0xe0 | (c >> 12)); result += static_cast<char>(0x80 | ((c >> 6) & 63)); result += static_cast<char>(0x80 | (c & 63)); }
		else { result += static_cast<char>(0xf0 | (c >> 18)); result += static_cast<char>(0x80 | ((c >> 12) & 63)); result += static_cast<char>(0x80 | ((c >> 6) & 63)); result += static_cast<char>(0x80 | (c & 63)); }
	}
	return result;
}
static std::string SrgbProfile() {
	auto module = GetModuleHandleW(nullptr); auto resource = FindResourceW(module, L"SCANNER_SRGB", RT_RCDATA); if (!resource) throw std::runtime_error("Eingebettetes sRGB-Profil fehlt.");
	auto loaded = LoadResource(module, resource); auto data = static_cast<const char*>(LockResource(loaded)); auto size = SizeofResource(module, resource);
	if (!data || size < 128 || std::string(data + 36, 4) != "acsp" || std::string(data + 16, 4) != "RGB " || static_cast<unsigned char>(data[8]) != 2) throw std::runtime_error("Ungueltiges sRGB-ICC-v2-Profil.");
	return std::string(data, size);
}
static std::string PdfText(const std::wstring& text) {
	std::ostringstream s; s << "<FEFF" << std::hex << std::uppercase << std::setfill('0');
	for (wchar_t c : text) s << std::setw(4) << static_cast<unsigned int>(static_cast<uint16_t>(c)); s << ">"; return s.str();
}
static std::string OcrHex(uint32_t value, int digits = 4) {
	std::ostringstream s; s << std::hex << std::uppercase << std::setfill('0') << std::setw(digits) << value; return s.str();
}
struct PdfOcrFont {
	std::map<uint32_t, uint16_t> codes;
	std::string Encode(const std::wstring& text, size_t& length) {
		std::string result; length = 0;
		for (size_t i = 0; i < text.size(); ++i) {
			uint32_t c = text[i];
			if (c >= 0xd800 && c <= 0xdbff) { if (++i == text.size() || text[i] < 0xdc00 || text[i] > 0xdfff) throw std::runtime_error("Ungueltiges UTF-16 im OCR-Text."); c = 0x10000 + ((c - 0xd800) << 10) + text[i] - 0xdc00; }
			else if (c >= 0xdc00 && c <= 0xdfff) throw std::runtime_error("Ungueltiges UTF-16 im OCR-Text.");
			auto found = codes.find(c);
			if (found == codes.end()) { if (codes.size() >= 65534) throw std::runtime_error("Zu viele verschiedene Unicode-Zeichen im OCR-Dokument."); found = codes.emplace(c, static_cast<uint16_t>(codes.size() + 1)).first; }
			result += OcrHex(found->second); ++length;
		}
		return result;
	}
	std::string UnicodeMap() const {
		std::string map = "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n/CMapName /ScannerOCR def\n/CMapType 2 def\n1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n";
		auto it = codes.begin();
		while (it != codes.end()) {
			size_t count = std::min<size_t>(100, std::distance(it, codes.end())); map += std::to_string(count) + " beginbfchar\n";
			for (size_t i = 0; i < count; ++i, ++it) { auto c = it->first; const auto unicode = c < 0x10000 ? OcrHex(c) : OcrHex(0xd800 + ((c - 0x10000) >> 10)) + OcrHex(0xdc00 + ((c - 0x10000) & 1023)); map += "<" + OcrHex(it->second) + "> <" + unicode + ">\n"; }
			map += "endbfchar\n";
		}
		return map + "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n";
	}
};
static std::string OcrCommands(const std::vector<OcrWord>& words, const Image& image, double scale, double x, double y, PdfOcrFont& font) {
	std::string commands;
	for (const auto& word : words) {
		size_t length = 0, ignored = 0; auto text = font.Encode(word.text, length); if (!length) continue;
		auto space = font.Encode(L" ", ignored); const auto height = word.height * 72. / image.dpiY * scale;
		const auto width = word.width * 72. / image.dpiX * scale;
		commands += "BT\n3 Tr\n/OCR " + Number(height) + " Tf\n" + Number(100 * width / (length * height * .5)) + " Tz\n1 0 0 1 " + Number(x + word.left * 72. / image.dpiX * scale) + " " + Number(y + (image.height - word.top - word.height) * 72. / image.dpiY * scale) + " Tm\n<" + text + space + "> Tj\nET\n";
	}
	return commands;
}
static void WritePdf(const std::vector<Image>& images, const std::filesystem::path& path, const ExportOptions& options) {
	const auto title = XmlText(options.title), subject = XmlText(options.subject), keywords = XmlText(options.keywords), author = XmlText(options.author);
	const auto profile = SrgbProfile(); SYSTEMTIME time{}; GetSystemTime(&time); char iso[32]{}, date[32]{};
	sprintf_s(iso, "%04u-%02u-%02uT%02u:%02u:%02uZ", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
	sprintf_s(date, "D:%04u%02u%02u%02u%02u%02uZ", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
	std::string xmp = "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\"><rdf:Description rdf:about=\"\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:pdf=\"http://ns.adobe.com/pdf/1.3/\" xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\" xmlns:pdfaid=\"http://www.aiim.org/pdfa/ns/id/\">";
	if (options.format == Format::PdfA) xmp += "<pdfaid:part>1</pdfaid:part><pdfaid:conformance>B</pdfaid:conformance>";
	xmp += "<dc:format>application/pdf</dc:format><dc:title><rdf:Alt><rdf:li xml:lang=\"x-default\">" + title + "</rdf:li></rdf:Alt></dc:title><dc:description><rdf:Alt><rdf:li xml:lang=\"x-default\">" + subject + "</rdf:li></rdf:Alt></dc:description><dc:creator><rdf:Seq><rdf:li>" + author + "</rdf:li></rdf:Seq></dc:creator><pdf:Keywords>" + keywords + "</pdf:Keywords><pdf:Producer>Scanner</pdf:Producer><xmp:CreatorTool>Scanner</xmp:CreatorTool><xmp:CreateDate>" + iso + "</xmp:CreateDate><xmp:ModifyDate>" + iso + "</xmp:ModifyDate><xmp:MetadataDate>" + iso + "</xmp:MetadataDate></rdf:Description></rdf:RDF></x:xmpmeta>\n<?xpacket end=\"w\"?>";
	std::ofstream out(path, std::ios::binary); if (!out) throw std::runtime_error("PDF-Zieldatei kann nicht angelegt werden.");
	const size_t fontObject = 6 + images.size() * 3;
	const size_t count = 5 + images.size() * 3 + (options.searchablePdf ? 6 : 0); std::vector<std::streamoff> offsets(count + 1); PdfOcrFont font;
	out << "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
	auto object = [&](size_t n, const std::string& value) { offsets[n] = out.tellp(); out << n << " 0 obj\n" << value << "\nendobj\n"; };
	object(1, "<< /Type /Catalog /Pages 2 0 R /Metadata 4 0 R /OutputIntents [<< /Type /OutputIntent /S /GTS_PDFA1 /OutputConditionIdentifier (sRGB2014) /Info (sRGB IEC61966-2.1) /DestOutputProfile 5 0 R >>]" + std::string(options.showThumbnails ? " /PageMode /UseThumbs" : " /PageMode /UseNone") + " >>");
	std::string kids; for (size_t i = 0; i < images.size(); ++i) kids += std::to_string(6 + i * 3) + " 0 R ";
	object(2, "<< /Type /Pages /Count " + std::to_string(images.size()) + " /Kids [ " + kids + "] >>");
	object(3, "<< /Title " + PdfText(options.title) + " /Subject " + PdfText(options.subject) + " /Keywords " + PdfText(options.keywords) + " /Author " + PdfText(options.author) + " /Creator " + PdfText(L"Scanner") + " /Producer (Scanner) /CreationDate (" + date + ") /ModDate (" + date + ") >>");
	object(4, "<< /Type /Metadata /Subtype /XML /Length " + std::to_string(xmp.size()) + " >>\nstream\n" + xmp + "\nendstream");
	object(5, "<< /N 3 /Alternate /DeviceRGB /Length " + std::to_string(profile.size()) + " >>\nstream\n" + profile + "\nendstream");
	for (size_t i = 0; i < images.size(); ++i) {
		const auto& image = images[i]; image.Validate(); size_t n = 6 + i * 3;
		PixelReadScope release{image.pixels};
		double w = image.width * 72. / image.dpiX, h = image.height * 72. / image.dpiY, pw = w, ph = h, scale = 1;
		if (options.pdfPageMode != PdfPageMode::Original) {
			pw = 595.27559; ph = 841.88976;
			if (options.pdfPageMode == PdfPageMode::A4LandscapeIfNeeded && w > h && (w > pw || h > ph)) std::swap(pw, ph);
			scale = std::min({1., pw / w, ph / h});
		}
		if (pw > 14400 || ph > 14400) throw std::runtime_error("PDF-Seite ueberschreitet die PDF-1.4-Grenze von 200 Zoll.");
		object(n, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + Number(pw) + " " + Number(ph) + "] /Resources << /XObject << /Im0 " + std::to_string(n + 1) + " 0 R >>" + (options.searchablePdf ? " /Font << /OCR " + std::to_string(fontObject) + " 0 R >>" : "") + " >> /Contents " + std::to_string(n + 2) + " 0 R >>");
		bool jpeg = options.quality < 100 && !Monochrome(image) && (image.kind == PixelKind::Color24 || image.kind == PixelKind::Gray8); std::string pixels;
		if (jpeg) pixels = JpegBytes(image, options.quality);
		else { pixels.resize(size_t(image.width) * image.height * 3); for (size_t p = 0, d = 0; p < image.pixels.size(); p += 4, d += 3) { pixels[d] = image.pixels[p + 2]; pixels[d + 1] = image.pixels[p + 1]; pixels[d + 2] = image.pixels[p]; } pixels = Deflate(pixels, 6); }
		offsets[n + 1] = out.tellp(); out << n + 1 << " 0 obj\n<< /Type /XObject /Subtype /Image /Width " << image.width << " /Height " << image.height << " /ColorSpace [/ICCBased 5 0 R] /BitsPerComponent 8 " << (jpeg ? "/Filter /DCTDecode " : "/Filter /FlateDecode ") << "/Length " << pixels.size() << " >>\nstream\n";
		out.write(pixels.data(), pixels.size()); out << "\nendstream\nendobj\n";
		auto commands = "q\n" + Number(w * scale) + " 0 0 " + Number(h * scale) + " " + Number((pw - w * scale) / 2) + " " + Number((ph - h * scale) / 2) + " cm\n/Im0 Do\nQ\n";
		if (options.searchablePdf) commands += OcrCommands(RecognizeText(image, options.ocrLanguage, std::filesystem::absolute(path.parent_path()) / (L".scanner-ocr-" + NewId())), image, scale, (pw - w * scale) / 2, (ph - h * scale) / 2, font);
		object(n + 2, "<< /Length " + std::to_string(commands.size()) + " >>\nstream\n" + commands + "\nendstream");
	}
	if (options.searchablePdf) {
		auto stream = [&](size_t n, const std::string& data, const std::string& entries = "") { object(n, "<< " + entries + " /Length " + std::to_string(data.size()) + " >>\nstream\n" + data + "\nendstream"); };
		auto module = GetModuleHandleW(nullptr); auto resource = FindResourceW(module, L"SCANNER_OCR_FONT", RT_RCDATA); auto loaded = resource ? LoadResource(module, resource) : nullptr;
		auto data = loaded ? static_cast<const char*>(LockResource(loaded)) : nullptr; auto size = resource ? SizeofResource(module, resource) : 0;
		if (!data || size != 572) throw std::runtime_error("Eingebettete OCR-Schrift fehlt oder ist ungueltig.");
		object(fontObject, "<< /Type /Font /Subtype /Type0 /BaseFont /GlyphLessFont /Encoding /Identity-H /DescendantFonts [" + std::to_string(fontObject + 1) + " 0 R] /ToUnicode " + std::to_string(fontObject + 5) + " 0 R >>");
		object(fontObject + 1, "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /GlyphLessFont /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /FontDescriptor " + std::to_string(fontObject + 2) + " 0 R /DW 500 /CIDToGIDMap " + std::to_string(fontObject + 4) + " 0 R >>");
		object(fontObject + 2, "<< /Type /FontDescriptor /FontName /GlyphLessFont /Flags 5 /FontBBox [0 0 500 1000] /ItalicAngle 0 /Ascent 1000 /Descent 0 /CapHeight 1000 /StemV 80 /FontFile2 " + std::to_string(fontObject + 3) + " 0 R >>");
		stream(fontObject + 3, std::string(data, size), "/Length1 " + std::to_string(size));
		std::string gids((font.codes.size() + 1) * 2, '\0'); for (size_t i = 1; i <= font.codes.size(); ++i) gids[i * 2 + 1] = '\1';
		stream(fontObject + 4, gids); stream(fontObject + 5, font.UnicodeMap());
	}
	auto xref = out.tellp(); out << "xref\n0 " << count + 1 << "\n0000000000 65535 f \n";
	for (size_t n = 1; n <= count; ++n) out << std::setw(10) << std::setfill('0') << offsets[n] << " 00000 n \n";
	std::string id; for (wchar_t c : NewId()) if (iswxdigit(c)) id += static_cast<char>(c);
	out << "trailer\n<< /Size " << count + 1 << " /Root 1 0 R /Info 3 0 R /ID [<" << id << "><" << id << ">] >>\nstartxref\n" << xref << "\n%%EOF\n";
	out.close(); if (!out) throw std::runtime_error("PDF konnte nicht vollstaendig geschrieben werden.");
}
std::vector<std::filesystem::path> ExportImages(const std::vector<Image>& images, const std::filesystem::path& path, const ExportOptions& options) {
	if (images.empty()) throw std::runtime_error("Keine Seiten zum Speichern vorhanden.");
	if (options.quality < 0 || options.quality > 100 || options.pngCompression < 0 || options.pngCompression > 9) throw std::runtime_error("Ungueltige Qualitaets- oder Kompressionseinstellung.");
	if (options.format < Format::Bmp || options.format > Format::PdfA || options.pdfPageMode < PdfPageMode::A4 || options.pdfPageMode > PdfPageMode::Original || options.tiffColor < TiffCompression::None || options.tiffColor > TiffCompression::Zip || options.tiffMono < TiffCompression::None || options.tiffMono > TiffCompression::Zip) throw std::runtime_error("Ungueltige Exportoption.");
	if (options.ocrLanguage < OcrLanguage::German || options.ocrLanguage > OcrLanguage::GermanEnglish) throw std::runtime_error("Ungueltige OCR-Sprache.");
	for (const auto& image : images) image.Validate();
	bool together = options.multipage && (options.format == Format::Pdf || options.format == Format::PdfA || options.format == Format::Tiff);
	size_t count = together ? 1 : images.size(); std::vector<std::filesystem::path> targets, staged;
	for (size_t n = 0; n < count; ++n) {
		std::filesystem::path target = path; if (count > 1) { std::wostringstream suffix; suffix << L"_" << std::setw(4) << std::setfill(L'0') << n + 1; target = path.parent_path() / (path.stem().wstring() + suffix.str() + path.extension().wstring()); }
		if (std::filesystem::exists(target)) throw std::runtime_error("Eine Zieldatei existiert bereits. Bitte einen anderen Namen waehlen; vorhandene Dateien werden nicht ueberschrieben."); targets.push_back(target);
	}
	try {
		for (size_t n = 0; n < count; ++n) {
			std::filesystem::path temp = targets[n]; temp += NewId() + L".tmp"; staged.push_back(temp);
			const std::vector<Image> singlePage = together ? std::vector<Image>{} : std::vector<Image>{images[n]};
			const std::vector<Image>& pages = together ? images : singlePage;
			if (options.format == Format::Pdf || options.format == Format::PdfA) WritePdf(pages, temp, options);
			else if (options.format == Format::Png) WritePng(pages[0], temp, options.pngCompression);
			else { ComPtr<IStream> stream; CheckExport(SHCreateStreamOnFileEx(temp.c_str(), STGM_WRITE | STGM_CREATE | STGM_SHARE_EXCLUSIVE, FILE_ATTRIBUTE_NORMAL, TRUE, nullptr, &stream)); Encode(stream.Get(), pages, options); }
		}
		size_t committed = 0;
		try { for (; committed < targets.size(); ++committed) if (!MoveFileExW(staged[committed].c_str(), targets[committed].c_str(), MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Exportdatei konnte nicht an den Zielort uebernommen werden."); }
		catch (...) { for (size_t i = 0; i < committed; ++i) { std::error_code ec; std::filesystem::remove(targets[i], ec); } throw; }
	} catch (...) { for (const auto& temp : staged) { std::error_code ec; std::filesystem::remove(temp, ec); } throw; }
	return targets;
}
int RunExportTests(const std::filesystem::path& directory, std::wstring& report) {
	int failures = 0, count = 0; auto test = [&](bool ok, const wchar_t* name) { ++count; if (!ok) ++failures; report += (ok ? L"PASS " : L"FAIL ") + std::wstring(name) + L"\n"; };
	Image image; image.width = 7; image.height = 5; image.pixels.resize(140, 255); for (size_t p = 0; p < image.pixels.size(); p += 4) { image.pixels[p] = static_cast<uint8_t>(p); image.pixels[p + 1] = 90; image.pixels[p + 2] = 170; }
	for (auto format : {Format::Bmp, Format::Png, Format::Tiff, Format::Jpeg}) {
		ExportOptions o; o.format = format; auto path = directory / (L"roundtrip-" + std::to_wstring(static_cast<int>(format)) + L".img");
		auto files = ExportImages({image}, path, o); auto decoded = ReadImages(files[0]);
		test(decoded.size() == 1 && decoded[0].width == image.width && decoded[0].height == image.height && (format == Format::Jpeg || decoded[0].pixels == image.pixels), (L"WIC roundtrip format " + std::to_wstring(static_cast<int>(format))).c_str());
	}
	ExportOptions o; o.format = Format::Tiff; auto files = ExportImages({image, image}, directory / L"multi.tif", o); test(ReadImages(files[0]).size() == 2, L"Multipage TIFF");
	Image mono = image; for (size_t p = 0; p < mono.pixels.size(); p += 4) { auto v = (p / 4) % 2 ? 255 : 0; mono.pixels[p] = mono.pixels[p + 1] = mono.pixels[p + 2] = static_cast<uint8_t>(v); }
	for (auto compression : {TiffCompression::None, TiffCompression::G3, TiffCompression::G4, TiffCompression::Lzw, TiffCompression::Zip}) {
		o.tiffMono = compression; auto f = ExportImages({mono}, directory / (L"mono-" + std::to_wstring(static_cast<int>(compression)) + L".tif"), o); test(ReadImages(f[0])[0].pixels == mono.pixels, (L"TIFF mono codec " + std::to_wstring(static_cast<int>(compression))).c_str());
	}
	o = {}; o.format = Format::Pdf; o.title = L"Pruefung Umlaut \u00e4"; files = ExportImages({image, image}, directory / L"lossless.pdf", o);
	std::ifstream pdf(files[0], std::ios::binary); std::string bytes((std::istreambuf_iterator<char>(pdf)), {}); std::string rgb; for (size_t p = 0; p < image.pixels.size(); p += 4) { rgb += static_cast<char>(image.pixels[p + 2]); rgb += static_cast<char>(image.pixels[p + 1]); rgb += static_cast<char>(image.pixels[p]); }
	test(bytes.find(Deflate(rgb, 6)) != std::string::npos && bytes.find("/FlateDecode") != std::string::npos && bytes.find("/DCTDecode") == std::string::npos && bytes.find("/Count 2") != std::string::npos, L"PDF quality100 exact compressed RGB and multipage");
	o.quality = 80; files = ExportImages({image}, directory / L"jpeg.pdf", o); std::ifstream jp(files[0], std::ios::binary); bytes.assign((std::istreambuf_iterator<char>(jp)), {}); test(bytes.find("/DCTDecode") != std::string::npos, L"PDF lower quality JPEG path");
	Image other = image; other.kind = PixelKind::Other; auto otherFiles = ExportImages({other}, directory / L"other-lossless.pdf", o); std::ifstream otherPdf(otherFiles[0], std::ios::binary); bytes.assign((std::istreambuf_iterator<char>(otherPdf)), {}); test(bytes.find(Deflate(rgb, 6)) != std::string::npos && bytes.find("/DCTDecode") == std::string::npos, L"PDF other source formats always lossless");
	Image landscape; landscape.width = 320; landscape.height = 200; landscape.dpiX = landscape.dpiY = 25; landscape.pixels.resize(320 * 200 * 4, 255);
	for (int mode = 0; mode < 3; ++mode) { o.quality = 100; o.pdfPageMode = static_cast<PdfPageMode>(mode); auto pageFiles = ExportImages({landscape}, directory / (L"page-mode-" + std::to_wstring(mode) + L".pdf"), o); std::ifstream f(pageFiles[0], std::ios::binary); bytes.assign((std::istreambuf_iterator<char>(f)), {}); std::string expected = mode == 0 ? "/MediaBox [0 0 595.27559 841.88976]" : mode == 1 ? "/MediaBox [0 0 841.88976 595.27559]" : "/MediaBox [0 0 921.60000 576.00000]"; test(bytes.find(expected) != std::string::npos, (L"PDF physical page mode " + std::to_wstring(mode)).c_str()); }
	ExportOptions originalPages; auto originalFiles = ExportImages({image, landscape}, directory / L"original-pages.pdf", originalPages); std::ifstream originalPdf(originalFiles[0], std::ios::binary); bytes.assign((std::istreambuf_iterator<char>(originalPdf)), {}); test(bytes.find("/MediaBox [0 0 1.68000 1.20000]") != std::string::npos && bytes.find("/MediaBox [0 0 921.60000 576.00000]") != std::string::npos && bytes.find("/Count 2") != std::string::npos, L"PDF original size uses each page image dimensions and DPI");
	ExportOptions split; split.format = Format::Png; auto splitFiles = ExportImages({image, image}, directory / L"split.png", split); test(splitFiles.size() == 2 && splitFiles[0].filename() == L"split_0001.png" && splitFiles[1].filename() == L"split_0002.png", L"Per-page numbering");
	for (auto compression : {TiffCompression::None, TiffCompression::Lzw, TiffCompression::Zip}) { ExportOptions t; t.format = Format::Tiff; t.tiffColor = compression; auto f = ExportImages({image}, directory / (L"color-" + std::to_wstring(static_cast<int>(compression)) + L".tif"), t); auto decoded = ReadImages(f[0]); test(decoded[0].pixels == image.pixels && std::abs(decoded[0].dpiX - image.dpiX) < .1, (L"TIFF color codec and DPI " + std::to_wstring(static_cast<int>(compression))).c_str()); }
	ExportOptions inverted; inverted.format = Format::Tiff; inverted.invertMono = true; auto invFiles = ExportImages({mono}, directory / L"inverted-g4.tif", inverted); Image expectedMono = mono; Transform(expectedMono, Edit::Invert); test(ReadImages(invFiles[0])[0].pixels == expectedMono.pixels, L"G4 explicit inversion");
	bool rejected = false; try { ExportImages({image}, files[0], o); } catch (...) { rejected = true; } test(rejected, L"Export refuses collision");
	Image pattern; pattern.width = 257; pattern.height = 129; pattern.dpiX = 150; pattern.dpiY = 96; pattern.pixels.resize(size_t(pattern.width) * pattern.height * 4, 255);
	std::array<uintmax_t, 10> sizes{};
	for (int level = 0; level <= 9; ++level) {
		ExportOptions png; png.format = Format::Png; png.pngCompression = level;
		auto f = ExportImages({pattern}, directory / (L"png-level-" + std::to_wstring(level) + L".png"), png); auto decoded = ReadImages(f[0]); sizes[level] = std::filesystem::file_size(f[0]);
		test(decoded.size() == 1 && decoded[0].pixels == pattern.pixels && std::abs(decoded[0].dpiX - 150) < .02 && std::abs(decoded[0].dpiY - 96) < .02, (L"PNG level " + std::to_wstring(level) + L" exact pixels and asymmetric DPI").c_str());
	}
	test(sizes[0] > sizes[1] && sizes[1] > sizes[9], L"PNG levels actually change Deflate compression");
	for (int mode = 0; mode < 3; ++mode) for (int quality : {80, 100}) {
		ExportOptions archive; archive.format = Format::PdfA; archive.quality = quality; archive.pdfPageMode = static_cast<PdfPageMode>(mode); archive.showThumbnails = quality == 100;
		archive.title = L"Archiv \u00e4 <&> \U0001F4C4"; archive.subject = L"Zeile 1\r\nZeile 2"; archive.author = L"Autor \u00df"; archive.keywords = L"Scanner Test & Archiv";
		auto f = ExportImages({image, landscape}, directory / (L"archive-mode-" + std::to_wstring(mode) + L"-q" + std::to_wstring(quality) + L".pdf"), archive); std::ifstream input(f[0], std::ios::binary); std::string data((std::istreambuf_iterator<char>(input)), {});
		test(data.find("<pdfaid:part>1</pdfaid:part>") != std::string::npos && data.find("/DestOutputProfile 5 0 R") != std::string::npos && data.find("/Count 2") != std::string::npos && (data.find("/DCTDecode") != std::string::npos) == (quality < 100), (L"PDF/A core mode " + std::to_wstring(mode) + L" quality " + std::to_wstring(quality)).c_str());
	}
	ExportOptions archive; archive.format = Format::PdfA;
	auto emptyMetadata = ExportImages({image}, directory / L"archive-empty-metadata.pdf", archive); test(emptyMetadata.size() == 1, L"PDF/A permits empty metadata");
	archive.multipage = false; auto singles = ExportImages({image, image}, directory / L"archive-split.pdf", archive); test(singles.size() == 2, L"PDF/A per-page option");
	for (auto kind : {PixelKind::Gray8, PixelKind::Mono1, PixelKind::Other}) {
		Image source = mono; source.kind = kind; if (kind == PixelKind::Gray8) for (size_t p = 0; p < source.pixels.size(); p += 4) source.pixels[p] = source.pixels[p + 1] = source.pixels[p + 2] = static_cast<uint8_t>(p);
		archive.quality = 65; auto f = ExportImages({source}, directory / (L"archive-kind-" + std::to_wstring(static_cast<int>(kind)) + L".pdf"), archive); std::ifstream input(f[0], std::ios::binary); std::string data((std::istreambuf_iterator<char>(input)), {});
		test((data.find("/DCTDecode") != std::string::npos) == (kind == PixelKind::Gray8), L"PDF/A source-kind quality semantics");
	}
	auto mustReject = [&](const std::vector<Image>& pages, ExportOptions bad, const wchar_t* name) { const auto target = directory / L"invalid-export.img"; bool caught = false; try { ExportImages(pages, target, bad); } catch (...) { caught = true; } test(caught && !std::filesystem::exists(target), name); };
	for (int level : {-1, 10}) { ExportOptions bad; bad.format = Format::Png; bad.pngCompression = level; mustReject({image}, bad, L"PNG invalid level rejected without output"); }
	for (int quality : {-1, 101}) { ExportOptions bad; bad.quality = quality; mustReject({image}, bad, L"Invalid quality rejected without output"); }
	for (Format format : {Format::Jpeg, Format::Pdf}) { ExportOptions minimum; minimum.format = format; minimum.quality = 0; auto zero = ExportImages({image}, directory / (format == Format::Jpeg ? L"quality-zero.jpg" : L"quality-zero.pdf"), minimum); test(zero.size() == 1 && std::filesystem::file_size(zero[0]) > 0, L"Quality zero exports successfully"); }
	for (int option = 0; option < 4; ++option) { ExportOptions bad; if (option == 0) bad.format = static_cast<Format>(99); if (option == 1) bad.pdfPageMode = static_cast<PdfPageMode>(99); if (option == 2) bad.tiffColor = static_cast<TiffCompression>(99); if (option == 3) bad.tiffMono = static_cast<TiffCompression>(99); mustReject({image}, bad, L"Invalid option enum rejected"); }
	for (const auto& text : {std::wstring(1, wchar_t(1)), std::wstring(1, wchar_t(0xd800)), std::wstring(1, wchar_t(0xdc00)), std::wstring(1, wchar_t(0xffff))}) { ExportOptions bad; bad.format = Format::PdfA; bad.title = text; mustReject({image}, bad, L"Invalid XML/UTF16 metadata rejected"); }
	ExportOptions bad; mustReject({}, bad, L"Empty export rejected"); Image huge = image; huge.dpiX = huge.dpiY = 1; huge.width = 201; huge.height = 1; huge.pixels.resize(804, 255); mustReject({huge}, bad, L"PDF physical size limit rejected");
	bad.format = Format::Tiff; bad.tiffColor = TiffCompression::G4; mustReject({image}, bad, L"TIFF color fax compression rejected");
	Image invalid = image; invalid.pixels.pop_back(); mustReject({image, invalid}, {}, L"Invalid later page rejected atomically");
	bool noTemps = true; for (const auto& entry : std::filesystem::directory_iterator(directory)) if (entry.path().extension() == L".tmp") noTemps = false; test(noTemps, L"Rejected exports leave no temporary files");
	const std::string tsvHeader = "level\tpage_num\tblock_num\tpar_num\tline_num\tword_num\tleft\ttop\twidth\theight\tconf\ttext\n";
	const auto words = ParseOcrTsv(tsvHeader + "5\t1\t6\t1\t1\t1\t0\t2005\t0\t230\t95.000000\t \n5\t1\t7\t1\t1\t1\t10\t20\t30\t40\t95\tScanner\n", 2456, 3484);
	test(words.size() == 1 && words[0].text == L"Scanner" && words[0].left == 10 && words[0].top == 20 && words[0].width == 30 && words[0].height == 40, L"OCR real Brother whitespace placeholder ignored while actual word preserved");
	for (const auto& coordinates : {std::string("0\t2005\t0\t230"), std::string("2450\t20\t30\t40"), std::string("10\t3480\t30\t40"), std::string("-1\t20\t30\t40")}) {
		bool rejected = false; try { ParseOcrTsv(tsvHeader + "5\t1\t1\t1\t1\t1\t" + coordinates + "\t95\tScanner\n", 2456, 3484); } catch (const std::runtime_error&) { rejected = true; }
		test(rejected, L"OCR nonblank words with invalid rectangles remain rejected");
	}
	report += L"Export: " + std::to_wstring(count) + L" tests, " + std::to_wstring(failures) + L" failures\n"; return failures;
}
}
