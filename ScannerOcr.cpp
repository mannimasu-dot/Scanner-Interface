#include "ScannerOcr.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace scanner {
static std::wstring OcrEnvironment(const wchar_t* name) {
	DWORD size = GetEnvironmentVariableW(name, nullptr, 0); if (!size) return {};
	std::wstring value(size, L'\0'); DWORD read = GetEnvironmentVariableW(name, value.data(), size);
	if (!read || read >= size) throw std::runtime_error("OCR-Umgebung konnte nicht gelesen werden.");
	value.resize(read); return value;
}
static std::filesystem::path TesseractPath() {
	const auto explicitPath = OcrEnvironment(L"SCANNER_TESSERACT");
	if (!explicitPath.empty()) {
		std::filesystem::path path(explicitPath);
		if (!path.is_absolute() || !std::filesystem::is_regular_file(path)) throw std::runtime_error("SCANNER_TESSERACT muss auf eine vorhandene absolute tesseract.exe zeigen.");
		return path;
	}
	std::wstring module(32768, L'\0'); DWORD length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
	if (!length || length >= module.size()) throw std::runtime_error("OCR-Programmpfad konnte nicht bestimmt werden.");
	module.resize(length);
	std::vector<std::filesystem::path> candidates{std::filesystem::path(module).parent_path() / L"tesseract" / L"tesseract.exe"};
	for (const auto* key : {L"ProgramW6432", L"ProgramFiles", L"ProgramFiles(x86)"}) { auto base = OcrEnvironment(key); if (!base.empty()) candidates.push_back(std::filesystem::path(base) / L"Tesseract-OCR" / L"tesseract.exe"); }
	auto local = OcrEnvironment(L"LOCALAPPDATA"); if (!local.empty()) candidates.push_back(std::filesystem::path(local) / L"Programs" / L"Tesseract-OCR" / L"tesseract.exe");
	for (const auto& candidate : candidates) if (std::filesystem::is_regular_file(candidate)) return candidate;
	const auto search = OcrEnvironment(L"PATH"); std::wstring found(32768, L'\0');
	DWORD size = search.empty() ? 0 : SearchPathW(search.c_str(), L"tesseract.exe", nullptr, static_cast<DWORD>(found.size()), found.data(), nullptr);
	if (size && size < found.size()) { found.resize(size); return std::filesystem::absolute(found); }
	throw std::runtime_error("OCR benoetigt Tesseract 4/5 mit Sprachdateien. Tesseract installieren, neben Scanner.exe unter tesseract bereitstellen oder SCANNER_TESSERACT setzen. Es wurde kein durchsuchbares PDF erstellt.");
}
static std::wstring QuoteOcrArgument(const std::wstring& value) {
	std::wstring result = L"\""; size_t slashes = 0;
	for (wchar_t c : value) {
		if (c == L'\\') { ++slashes; continue; }
		if (c == L'\"') result.append(slashes * 2 + 1, L'\\'); else result.append(slashes, L'\\');
		slashes = 0; result += c;
	}
	result.append(slashes * 2, L'\\'); return result + L'\"';
}
struct OcrHandle {
	HANDLE value = nullptr;
	~OcrHandle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
static std::string ReadOcrFile(const std::filesystem::path& path, size_t limit) {
	std::ifstream input(path, std::ios::binary);
	if (!input || std::filesystem::file_size(path) > limit) throw std::runtime_error("OCR-Ausgabe fehlt oder ueberschreitet das Groessenlimit.");
	std::string value((std::istreambuf_iterator<char>(input)), {});
	if (input.bad()) throw std::runtime_error("OCR-Ausgabe konnte nicht vollstaendig gelesen werden.");
	return value;
}
static void RunTesseract(const std::filesystem::path& executable, std::wstring arguments, const std::filesystem::path& work) {
	SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
	OcrHandle log{CreateFileW((work / L"engine.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
	OcrHandle input{CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr)};
	OcrHandle job{CreateJobObjectW(nullptr, nullptr)};
	if (log.value == INVALID_HANDLE_VALUE || input.value == INVALID_HANDLE_VALUE || !job.value) throw std::runtime_error("OCR-Prozessressourcen konnten nicht angelegt werden.");
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	if (!SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) throw std::runtime_error("OCR-Prozessbegrenzung fehlgeschlagen.");
	SIZE_T bytes = 0;
	if (InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes) || GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0) throw std::runtime_error("Groesse der OCR-Prozessattribute konnte nicht bestimmt werden.");
	std::vector<unsigned char> storage(bytes); auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
	if (!attributes || !InitializeProcThreadAttributeList(attributes, 1, 0, &bytes)) throw std::runtime_error("OCR-Prozessattribute fehlgeschlagen.");
	struct Attributes { LPPROC_THREAD_ATTRIBUTE_LIST value; ~Attributes() { DeleteProcThreadAttributeList(value); } } cleanup{attributes};
	HANDLE handles[]{input.value, log.value};
	if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, sizeof(handles), nullptr, nullptr)) throw std::runtime_error("OCR-Prozesshandles fehlgeschlagen.");
	STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup); startup.lpAttributeList = attributes;
	startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES; startup.StartupInfo.hStdInput = input.value; startup.StartupInfo.hStdOutput = startup.StartupInfo.hStdError = log.value;
	PROCESS_INFORMATION process{}; auto command = QuoteOcrArgument(executable.wstring()) + L" " + arguments;
	if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr, work.c_str(), &startup.StartupInfo, &process)) throw std::runtime_error("Tesseract konnte nicht gestartet werden (Windows-Fehler " + std::to_string(GetLastError()) + "). Installation und abhaengige DLLs pruefen.");
	OcrHandle child{process.hProcess}, thread{process.hThread};
	if (!AssignProcessToJobObject(job.value, child.value) || ResumeThread(thread.value) == static_cast<DWORD>(-1)) { TerminateProcess(child.value, 1); WaitForSingleObject(child.value, INFINITE); throw std::runtime_error("OCR-Prozess konnte nicht sicher gestartet werden."); }
	DWORD wait = WaitForSingleObject(child.value, 120000);
	if (wait != WAIT_OBJECT_0) { TerminateJobObject(job.value, 1); WaitForSingleObject(child.value, INFINITE); throw std::runtime_error("OCR abgebrochen: Zeitlimit von 120 Sekunden pro Seite oder Prozess-Wartefehler. Keine Ausgabe uebernommen."); }
	DWORD code = 1; if (!GetExitCodeProcess(child.value, &code)) throw std::runtime_error("OCR-Prozessstatus fehlt.");
	if (code) {
		CloseHandle(log.value); log.value = nullptr;
		auto detail = ReadOcrFile(work / L"engine.log", 1024 * 1024); if (detail.size() > 2000) detail.resize(2000);
		throw std::runtime_error("Tesseract-OCR fehlgeschlagen (Exit " + std::to_string(code) + "). Sprachdateien/Installation pruefen. " + detail);
	}
}
std::vector<OcrWord> ParseOcrTsv(const std::string& data, uint32_t width, uint32_t height) {
	std::istringstream input(data); std::string line;
	if (!std::getline(input, line)) throw std::runtime_error("OCR-TSV-Ausgabe ist leer.");
	if (!line.empty() && line.back() == '\r') line.pop_back();
	if (line != "level\tpage_num\tblock_num\tpar_num\tline_num\tword_num\tleft\ttop\twidth\theight\tconf\ttext") throw std::runtime_error("Ungueltige OCR-TSV-Kopfzeile.");
	std::vector<OcrWord> words;
	while (std::getline(input, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back(); if (line.empty()) continue;
		std::array<std::string, 12> fields; size_t start = 0;
		for (size_t i = 0; i < 11; ++i) { auto tab = line.find('\t', start); if (tab == std::string::npos) throw std::runtime_error("Unvollstaendige OCR-TSV-Zeile."); fields[i] = line.substr(start, tab - start); start = tab + 1; } fields[11] = line.substr(start);
		auto integer = [&](size_t i) { int value = 0; const auto& s = fields[i]; auto result = std::from_chars(s.data(), s.data() + s.size(), value); if (result.ec != std::errc{} || result.ptr != s.data() + s.size()) throw std::runtime_error("Ungueltige OCR-Koordinate."); return value; };
		int level = integer(0); if (level < 1 || level > 5) throw std::runtime_error("Ungueltige OCR-TSV-Ebene.");
		if (level != 5 || fields[11].find_first_not_of(" \t\r\n") == std::string::npos) continue;
		OcrWord word{integer(6), integer(7), integer(8), integer(9), {}};
		if (word.left < 0 || word.top < 0 || word.width <= 0 || word.height <= 0 || uint64_t(word.left) + word.width > width || uint64_t(word.top) + word.height > height) throw std::runtime_error("OCR-Wort liegt ausserhalb der Bildseite.");
		const auto& text = fields[11]; int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
		if (!length) throw std::runtime_error("OCR liefert ungueltiges UTF-8.");
		word.text.resize(length); MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), word.text.data(), length);
		for (wchar_t c : word.text) if (c < 32 || c == 0xfffe || c == 0xffff) throw std::runtime_error("OCR liefert ungueltige Textzeichen.");
		words.push_back(std::move(word)); if (words.size() > 1000000) throw std::runtime_error("Zu viele OCR-Woerter auf einer Seite.");
	}
	return words;
}
std::vector<OcrWord> RecognizeText(const Image& image, OcrLanguage language, const std::filesystem::path& work) {
	if (language < OcrLanguage::German || language > OcrLanguage::GermanEnglish) throw std::runtime_error("Ungueltige OCR-Sprache.");
	const auto executable = TesseractPath();
	auto configured = OcrEnvironment(L"SCANNER_TESSDATA"); if (configured.empty()) configured = OcrEnvironment(L"TESSDATA_PREFIX");
	const auto data = configured.empty() ? executable.parent_path() / L"tessdata" : std::filesystem::path(configured);
	if (!data.is_absolute()) throw std::runtime_error("OCR-Sprachdatenpfad muss absolut sein.");
	for (auto name : {L"deu", L"eng"}) {
		if ((language == OcrLanguage::German && std::wstring(name) == L"eng") || (language == OcrLanguage::English && std::wstring(name) == L"deu")) continue;
		if (!std::filesystem::is_regular_file(data / (std::wstring(name) + L".traineddata"))) throw std::runtime_error(std::wstring(name) == L"deu" ? "OCR-Sprachdatei deu.traineddata fehlt. Deutsch installieren oder explizit Englisch waehlen." : "OCR-Sprachdatei eng.traineddata fehlt. Englisch installieren oder explizit Deutsch waehlen.");
	}
	if (!std::filesystem::create_directory(work)) throw std::runtime_error("Privates OCR-Arbeitsverzeichnis konnte nicht angelegt werden.");
	struct Work { std::filesystem::path path; ~Work() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{work};
	ExportOptions png; png.format = Format::Png; ExportImages({image}, work / L"input.png", png);
	const wchar_t* languages[]{L"deu", L"eng", L"deu+eng"};
	RunTesseract(executable, L"input.png result --tessdata-dir " + QuoteOcrArgument(data.wstring()) + L" -l " + languages[static_cast<int>(language)] + L" --dpi " + std::to_wstring(static_cast<int>(std::clamp(image.dpiY, 70., 2400.))) + L" --psm 3 -c tessedit_create_tsv=1", work);
	return ParseOcrTsv(ReadOcrFile(work / L"result.tsv", 64 * 1024 * 1024), image.width, image.height);
}
}
