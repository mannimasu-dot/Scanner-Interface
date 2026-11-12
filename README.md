# Scanner

Eigenständiger Windows-Dokumentarbeitsplatz in C++20 mit TWAIN als primärer Scanneranbindung.

## Fertige Windows-Version herunterladen

Der Workflow **Windows build** baut bei Pushes auf `master` und bei Pull Requests eine geprüfte x64-Version. Das ZIP enthält `Scanner.exe`, den erforderlichen x86-TWAIN-Hilfsprozess `TwainHost.exe`, `Hilfe.html` und die erforderlichen Lizenzhinweise. Die Release-Binaries enthalten die C++-Runtime statisch.

1. Im Repository **Actions → Windows build** öffnen und den neuesten erfolgreichen Lauf auswählen.
2. Unter **Artifacts** das ZIP **Scanner-Windows-x64** herunterladen und entpacken.
3. `Scanner.exe` starten. Für den Scanbetrieb muss ein passender Windows-TWAIN-Scannertreiber installiert sein.

Bei einem auf `v` beginnenden Versions-Tag veröffentlicht derselbe Workflow das ZIP zusätzlich als dauerhaften Download unter **Releases**. OCR benötigt eine separat installierte Tesseract-Laufzeit; die übrigen Funktionen benötigen sie nicht.

## Aus dem Quellcode bauen

Visual Studio mit C++-Desktopentwicklung, Windows SDK und MSVC-Toolset v145 werden benötigt. Vom Repository-Stamm aus:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\Build.ps1 -Configuration Release
```

Das vollständige Build- und Testverfahren sowie die Programmhinweise stehen in [Docs/README.md](Docs/README.md).