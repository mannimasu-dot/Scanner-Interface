#pragma once
#include "ScannerCore.h"
#include "ScannerDeviceEvents.h"
namespace scanner {
struct PrintOptions { int copies = 1; bool collate = true, fit = true, rotate = true; std::wstring printer; std::filesystem::path output; int* jobId = nullptr; std::vector<BYTE> deviceMode; };
std::vector<std::wstring> PrintPrinters();
std::wstring DefaultPrinter();
bool ConfigurePrinter(HWND owner, const std::wstring& printer, std::vector<BYTE>& deviceMode, bool prompt);
bool PrintImages(HWND owner, const std::vector<Image>& images, const PrintOptions& options);
// Speichert einen MIME-Entwurf am exakten Zielpfad; kein Ueberschreiben, kein Mailclient-Aufruf.
void SaveMailDraft(const std::vector<std::filesystem::path>& attachments, const std::filesystem::path& path);
void PrepareMail(HWND owner, const std::vector<std::filesystem::path>& attachments);
// Nur Datei-/Encodingtests; Rueckgabe: Fehleranzahl. Oeffnet keine UI und keinen Mailclient.
int RunServiceTests(const std::filesystem::path& directory, std::wstring& report);
void RegisterDeviceApplication();
}
