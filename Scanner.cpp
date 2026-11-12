// Scanner.cpp : Definiert den Einstiegspunkt für die Anwendung.
//

#include "framework.h"
#include "ScannerUi.h"
#include "ScannerTwain.h"
#include "ScannerWia.h"
#include "ScannerServices.h"
#include <shellapi.h>
#include <objbase.h>
#include <fstream>
#include <stdexcept>

int APIENTRY wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int show)
{
    HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) return 1;
    struct Com { ~Com() { CoUninitialize(); } } com;
    int count = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::wstring> arguments;
    for (int i = 1; argv && i < count; ++i) arguments.emplace_back(argv[i]);
    if (argv) LocalFree(argv);
    if (arguments.size() == 3 && arguments[0] == L"--twain-helper")
        return scanner::TwainHelperMain(arguments[2], arguments[1]);
    if (!arguments.empty() && arguments[0] == L"--self-test")
    {
        std::filesystem::path directory = arguments.size() > 1 ? std::filesystem::path(arguments[1]) : scanner::UserDataDirectory() / L"Tests" / scanner::NewId();
        std::wstring report; int failures = 0;
        try
        {
            if (std::filesystem::exists(directory) && !std::filesystem::is_empty(directory)) return 2;
            std::filesystem::create_directories(directory);
            try { failures += scanner::RunCoreTests(directory, report); } catch (const std::exception& e) { ++failures; report += L"FAIL Core exception: " + std::wstring(e.what(), e.what() + strlen(e.what())) + L"\n"; }
            try { failures += scanner::RunExportTests(directory, report); } catch (const std::exception& e) { ++failures; report += L"FAIL Export exception: " + std::wstring(e.what(), e.what() + strlen(e.what())) + L"\n"; }
            failures += scanner::RunTwainTests(report);
            failures += scanner::RunWiaTests(report);
            try { failures += scanner::RunServiceTests(directory, report); } catch (const std::exception& e) { ++failures; report += L"FAIL Services exception: " + std::wstring(e.what(), e.what() + strlen(e.what())) + L"\n"; }
            report += L"TOTAL FAILURES: " + std::to_wstring(failures) + L"\n";
            std::wofstream output(directory / L"results.txt"); output << report;
        }
        catch (...) { return 3; }
        return failures ? 1 : 0;
    }
    if (arguments.size() == 2 && arguments[0] == L"--device-events")
    {
        std::wofstream output{std::filesystem::path(arguments[1])};
        try { auto events = scanner::EnumerateDeviceEvents(); for (const auto& event : events) output << event.deviceName << L" | " << event.deviceId << L" | " << event.eventId << L" | " << event.eventName << L"\n"; return output ? 0 : 2; }
        catch (const std::exception& e) { output << L"ERROR: " << std::wstring(e.what(), e.what() + strlen(e.what())); return 1; }
    }
    if ((arguments.size() == 3 && arguments[0] == L"--physical-print-test") || (arguments.size() == 4 && arguments[0] == L"--print-image-test"))
    {
        const bool copy = arguments[0] == L"--print-image-test";
        std::ofstream output{std::filesystem::path(arguments.back()), std::ios::binary};
        if (!output) return 2;
        try {
            scanner::Image image;
            if (copy) { auto pages = scanner::ReadImages(arguments[2]); if (pages.size() != 1) throw std::runtime_error("Kopietest verlangt genau eine Bildseite."); image = std::move(pages[0]); }
            else { image.width = image.height = 300; image.dpiX = image.dpiY = 100; image.pixels.resize(300 * 300 * 4, 255); for (size_t y = 0; y < 300; ++y) for (size_t x = 0; x < 300; ++x) if (x == 10 || x == 289 || y == 10 || y == 289 || (y > 130 && y < 170 && x > 40 && x < 260)) { const auto p = (y * 300 + x) * 4; image.pixels[p] = image.pixels[p + 1] = image.pixels[p + 2] = 0; } }
            int job = 0; scanner::PrintOptions options; options.printer = arguments[1]; options.fit = false; options.jobId = &job;
            output << "RequestedPages=1 Copies=1 Mode=" << (copy ? "acquired-image-copy" : "synthetic-test") << "\n"; output.flush();
            if (!scanner::PrintImages(nullptr, {image}, options)) throw std::runtime_error("Druckauftrag abgebrochen.");
            output << "GdiEndDocSucceeded=1 JobId=" << job << " Pages=1\nPhysicalOutputVisuallyConfirmed=0\n";
            return output ? 0 : 2;
        } catch (const std::exception& e) { output << "ERROR: " << e.what() << "\n"; return 1; }
    }
    if (arguments.size() == 3 && arguments[0] == L"--print-file-test")
    {
        try { scanner::Image image; image.width = image.height = 100; image.pixels.resize(40000, 255); for (size_t y = 10; y < 90; ++y) for (size_t x = 10; x < 90; ++x) { const auto p = (y * 100 + x) * 4; image.pixels[p] = image.pixels[p + 1] = image.pixels[p + 2] = 0; } scanner::PrintOptions options; options.printer = arguments[1]; options.output = arguments[2]; return scanner::PrintImages(nullptr, {image}, options) ? 0 : 1; } catch (...) { return 1; }
    }
    if (arguments.size() == 2 && arguments[0] == L"--twain-probe")
    {
        std::ofstream output(std::filesystem::path(arguments[1]), std::ios::binary);
        try
        {
            auto sources = scanner::TwainSources(nullptr);
            output << "TWAIN sources: " << sources.size() << "\n";
            for (const auto& source : sources)
            {
                int size = WideCharToMultiByte(CP_UTF8, 0, source.name.data(), static_cast<int>(source.name.size()), nullptr, 0, nullptr, nullptr);
                std::string name(size, ' '); WideCharToMultiByte(CP_UTF8, 0, source.name.data(), static_cast<int>(source.name.size()), name.data(), size, nullptr, nullptr);
                output << name << "\n";
            }
            return output ? 0 : 2;
        }
        catch (const std::exception& e) { output << "ERROR: " << e.what() << "\n"; return 1; }
    }
    if ((arguments.size() == 3 && arguments[0] == L"--twain-capabilities") || ((arguments.size() == 3 || arguments.size() == 4) && arguments[0] == L"--hardware-scan-test"))
    {
        std::filesystem::path target(arguments[2]);
        if (arguments[0] == L"--hardware-scan-test") { if (std::filesystem::exists(target) && !std::filesystem::is_empty(target)) return 2; std::filesystem::create_directories(target); }
        std::ofstream output(arguments[0] == L"--hardware-scan-test" ? target / L"hardware-results.txt" : target, std::ios::binary);
        try
        {
            if (arguments[0] == L"--twain-capabilities")
            {
            auto capabilities = scanner::TwainCapabilities(nullptr, arguments[1]);
            output << "Feeder=" << capabilities.feeder << " Duplex=" << capabilities.duplex << " Brightness=" << capabilities.brightness << " MaxMm=" << capabilities.maxWidthMm << "x" << capabilities.maxHeightMm << "\n";
            output << "FeederLoaded=" << capabilities.feederLoaded << " (-1=unknown)\n";
            output << "FlatbedAdvertised=" << capabilities.flatbed << " (-1=unknown) UIControllable=" << capabilities.uiControllable << "\n";
            for (double dpi : capabilities.resolutions) output << "SupportedXDpi=" << dpi << "\n";
            for (double dpi : capabilities.yResolutions) output << "SupportedYDpi=" << dpi << "\n";
            for (double value : capabilities.transferMechanisms) output << "TransferMechanism=" << value << "\n";
            for (double value : capabilities.pixelTypes) output << "PixelType=" << value << "\n";
            auto range = [&](const char* axis, const scanner::CapabilityRange& r) { if (r.available) output << axis << "DpiRange=" << r.minimum << ":" << r.maximum << " Step=" << r.step << " Current=" << r.current << " Default=" << r.defaultValue << "\n"; };
            range("X", capabilities.xResolutionRange); range("Y", capabilities.yResolutionRange);
            }
            if (arguments[0] == L"--hardware-scan-test")
            {
                scanner::ScanOptions options; options.source = arguments[1]; options.mode = 1; options.dpi = 100; options.widthMm = options.heightMm = 100; options.driverDialog = false;
                if (arguments.size() == 4) { if (arguments[3] == L"native") options.transfer = 0; else if (arguments[3] == L"memory") options.transfer = 2; else if (arguments[3] == L"adf" || arguments[3] == L"duplex") { options.feeder = true; options.duplex = arguments[3] == L"duplex"; } else throw std::runtime_error("Hardwaretest-Modus muss native, memory, adf oder duplex sein."); }
                if (options.feeder)
                {
                    options.widthMm = 210; options.heightMm = 297;
                    options.transfer = -1; options.timeoutMs = 90000;
                }
                output << "Requested: gray, 100 dpi, " << options.widthMm << "x" << options.heightMm << " mm, " << (options.feeder ? "adf" : "flatbed") << ", " << (options.duplex ? "duplex" : "simplex") << ", no driver UI\n"; output.flush();
                auto result = scanner::TwainScan(nullptr, options);
                output << "Completed=" << result.completed << " Cancelled=" << result.cancelled << " AcquiredPages=" << result.pages.size() << "\nDiagnostics=" << result.diagnostics.string() << "\n";
                if (!result.recoveryDirectory.empty()) output << "RecoveryDirectory=" << result.recoveryDirectory.string() << "\n";
                if (std::filesystem::exists(result.diagnostics)) std::filesystem::copy_file(result.diagnostics, target / L"twain-trace.log");
                auto& images = result.pages;
                scanner::ExportOptions exportOptions; exportOptions.format = scanner::Format::Png;
                if (!images.empty()) { auto files = scanner::ExportImages(images, target / L"hardware.png", exportOptions); output << "ScannedPages=" << images.size() << " FirstPixels=" << images[0].width << "x" << images[0].height << " Dpi=" << images[0].dpiX << "x" << images[0].dpiY << " ExportedFiles=" << files.size() << "\n"; }
                if (!result.completed || result.cancelled || images.empty()) throw std::runtime_error(result.error.empty() ? "Hardwaretest lieferte keinen vollstaendigen Scan." : result.error);
            }
            return output ? 0 : 2;
        }
        catch (const std::exception& e) { output << "ERROR: " << e.what() << "\n"; return 1; }
    }
    return scanner::RunApplication(instance, show, arguments);
}
