# Konzept: eigenständige Windows-Scanneranwendung

## 1. Ziel und Kurzempfehlung

Eine eigenständige Anwendung soll den bewährten Arbeitsablauf von Grewe Scanner-Interface nachbilden: Scanner auswählen, Unterlagen einlesen, Seiten kontrollieren und bearbeiten, anschließend als Dokument speichern. Erstes Referenzgerät ist der Brother MFC-J6935DW im Netzwerk.

**Festgelegter Zielumfang:** Alle in `Infos/Hilfe` beschriebenen Funktionen der Professional-Ausgabe umsetzen, ausschließlich Faxfunktionen entfallen. TWAIN ist nach Nutzerbestätigung die primäre Scanneranbindung. Die verbindliche Funktionsmatrix steht in [FUNKTIONSUMFANG.md](FUNKTIONSUMFANG.md).

**Empfehlung zur Reihenfolge:** Das vorhandene C++20-/Win32-Projekt weiterverwenden und zuerst einen kleinen TWAIN-Prototyp am Brother entwickeln. Anschließend den vollständigen Umfang in prüfbaren Etappen umsetzen. Eine erste Teilversion ist keine Abnahme des vollständigen Nachbaus.

Das ist ein Konzept, keine bereits implementierte Scannerlösung. Der Zielumfang und TWAIN als Hauptweg sind vorgegeben; die technische Architektur bleibt ein Umsetzungsvorschlag.

## 2. Untersuchungsgrundlage und belastbare Befunde

### Projekt

- Der Informationsordner heißt `Infos`, nicht `Info`.
- `Scanner.cpp` enthält das klassische Win32-Grundgerüst mit Fenster, Menü und Info-Dialog. Scanneranbindung, Dokumentmodell und Export fehlen noch.
- `Scanner.vcxproj` verwendet C++20, Unicode, Toolset v145 sowie Win32- und x64-Konfigurationen. SDL-Prüfungen sind aktiviert. Daraus allein folgt noch keine vollständige Sicherheitsbewertung.

### Bereitgestellte Altprogramm-Unterlagen

Die elf ursprünglichen JPG-Dateien, die drei ergänzten Programmansichten und alle 44 Hilfebilder in `Infos/Hilfe` wurden mit der lokalen deutschen Windows-Texterkennung ausgewertet. `Infos/Hilfe/AUSWERTUNG-OCR.txt` enthält nur den Auszug der ursprünglichen 26 Hilfeseiten; die späteren Ergänzungen sind in der Quellenabdeckung der Funktionsmatrix erfasst. Hinzu kommt die Nutzerklärung zur Rechteckbearbeitung samt Beispielbild. OCR kann Beschriftungen falsch lesen und ersetzt keine Bedienprüfung. Die beiden EXE-Dateien wurden nicht gestartet; untersucht wurden lediglich Dateimetadaten und lesbare Zeichenfolgen.

| Quelle | Erkennbarer Inhalt | Bedeutung für den Nachbau |
| --- | --- | --- |
| `Info.jpg`, EXE-Metadaten, `Programm schnipsel.txt` | Grewe Scanner-Interface 7 Professional; Oberfläche 7.03, Dateiversion 7.0.3.1; Copyright 2012 | Identifikation der Vorlage; kein verfügbarer Quellcode |
| `Scanner.jpg`, `Action.jpg` | Scannen, Importieren, Speichern, Drucken, Fax, E-Mail, Kopiermodus | Arbeitsablauf und spätere Zusatzfunktionen |
| `IF.jpg` | TW-Brother MFC-J6935DW LAN sowie WIA-Brother MFC-J6935DW LAN; weitere HP-Quelle | Geräteabstraktion über TWAIN/WIA liegt nahe |
| `Panel.jpg`, `PanelVorschau.jpg` | Brother-Treiberdialog: Vorschau, Auflösung, Farbe, Dokumentgröße, Helligkeit, Kontrast, Duplex und Profile | Viele Einstellungen werden bereits vom Herstellertreiber bereitgestellt |
| `PanelMenu.jpg` | Scanner-Menü sowie Schwarzweiß-/Graustufen-/Farbmodus | Einfacher Scanmodus und Treiberdialog als alternative Bedienwege |
| `Bearbeiten.jpg` | Zwischenablage, Löschen, Drehen, Invertieren, Spiegeln, Zuschneiden, Rückseiten einsortieren, Reihenfolge umkehren | Priorisierung der Seitenbearbeitung |
| `Ansicht.jpg` | Ganze Seite, optimale Breite, zwei Seiten, Symbole, Zoom, Vollbild | Seitenvorschau und Navigation |
| `Extra.jpg`, `Hilfe.jpg` | Unter anderem Scanner-/Faxauswahl, Hintergrundfarbe, Hilfe und Info | Nachrangige Einstellungen; einzelne OCR-Beschriftungen sind unklar |
| `Scanner-Interface.exe` | Dateiversion 7.0.3.1, x86-Manifest, `CTwain`-Zeichenfolgen | Zusätzlicher Hinweis auf TWAIN; kein Beweis für alle internen Abläufe |
| `Programm schnipsel.txt` | PDF-1.3-/1.4- und PDF/A-1b-Marker sowie Bild-/Metadatenstrukturen | PDF-Ausgabe ist plausibel; tatsächlich angebotene Exportoptionen und PDF/A-Konformität bleiben ungeprüft |

Die Hilfebilder belegen BMP, PNG, JPEG, TIFF, PDF und PDF/A-1b, Einzel-/Mehrseitenausgabe in TIFF/PDF/PDF/A sowie konkrete Qualitäts-, Seitenformat- und Kompressionsoptionen. Speicher-/Druck-/Maildialoge, Dateinamenautomatik und Gerätetasten sind durch die Ergänzungen dokumentiert. Weitere Hilfe zu Scanbereichen und Rechteckbearbeitung ist nach Nutzerangabe nicht vorhanden und wird nicht erneut angefordert. Verbleibende kleine Umsetzungs- und Testfragen stehen in der Funktionsmatrix. Texterkennung und Verschlüsselung sind nicht als Altprogrammmerkmale belegt. `Hilfe.exe` wurde aus Sicherheitsgründen nicht als Hilfesystem geöffnet.

Die vom Nutzer beschriebene Einstellung des Herstellers wird als Ausgangslage übernommen, nicht als unabhängig bestätigte Unternehmensinformation. Die alte Adresse `https://www.grewe.de` lieferte beim Abruf HTTP 404; das belegt für sich weder Firmenstatus noch das Fehlen anderer Archive.

### Nur lesende Prüfung auf diesem PC

- Windows führt `Brother MFC-J6935DW LAN` als vorhandenes Gerät.
- `WIA.DeviceManager` meldet diese Quelle als Scanner, Typ 1.
- Ein Brother-TWAIN-Treiber liegt unter `C:\Windows\twain_32\Brimi16a\MFC-J6935DWN1\TwDsFe.ds`.
- Seine PE-Architektur ist `0x014C`, also x86; die Dateiversion lautet 5.8.109.1.
- Damit sind sowohl die WIA-Registrierung als auch eine konkrete 32-Bit-TWAIN-Quelle belegt. Ein zusätzlicher 64-Bit-TWAIN-Treiber ist durch diese Untersuchung nicht nachgewiesen.
- Es wurden keine Scans gestartet, keine Firmware aktualisiert und keine Treiber- oder Netzwerkeinstellungen geändert. Geräteerreichbarkeit, tatsächliche Treiberfunktionen und Übertragungssicherheit sind noch nicht verifiziert.

## 3. Wie die Netzwerkverbindung einzuordnen ist

Eine Anwendung kann nicht ohne Weiteres „jeden Scanner“ direkt ansprechen. Die breite Kompatibilität entsteht üblicherweise durch standardisierte Anwendungsschnittstellen und passende Gerätetreiber:

**Anwendung → TWAIN → Brother-Treiber → Netzwerk → Scanner.**

Für WIA-Geräte beschreibt `Infos/Hilfe/Hilfe23.jpg` die Windows-WIA/TWAIN-Konvertierung. Eine solche Quelle ist ebenfalls über TWAIN anzusprechen, sofern die Brücke für Architektur und Treiber verfügbar ist. Die frühere Annahme eines zwingend eigenen nativen WIA-Backends wird dadurch ersetzt.

Dass der Brother über das Netzwerk arbeitet, bedeutet deshalb nicht, dass die neue Anwendung ein eigenes Brother-Netzwerkprotokoll benötigt. Die vorliegenden Hinweise sprechen für treibervermitteltes Scannen. Das konkrete Übertragungsprotokoll des Brother-Treibers wurde nicht untersucht.

| Weg | Vorteil | Grenze / Empfehlung |
| --- | --- | --- |
| TWAIN | Breite Scannerunterstützung, vorhandener Brother-Dialog, gerätespezifische Funktionen | Architekturabhängige Datenquellen, zustandsbasierter Ablauf und Ereignisverarbeitung. Für den gewohnten Brother-Ablauf zuerst erproben. |
| WIA über TWAIN-Brücke | Laut Hilfe der vom Vorbild verwendete Weg für WIA-Geräte | Verfügbarkeit und Funktionen der Brücke prüfen. Ein direkter WIA-2.0-Backend ist eine mögliche spätere technische Alternative, keine Voraussetzung für den vorgegebenen TWAIN-Hauptweg. |
| Direkte Netzwerkprotokolle, z. B. eSCL/AirScan oder WSD Scan | Perspektivisch weniger Abhängigkeit von Herstellertreibern | Unterstützung des konkreten Geräts nicht bestätigt; eigener Aufwand für Erkennung, Fähigkeiten, Authentisierung, Transport und Fehlerfälle. Nicht Teil der ersten Version. |
| TWAIN Direct | Eigenständiger standardisierter Ansatz für Netzwerkscannen | Nicht mit einer normalen TWAIN-LAN-Datenquelle gleichzusetzen. Unterstützung dieses Brothers ist nicht belegt. |

Die Brother-Downloadseite für das konkrete Modell bietet eine Windows-11-Auswahl an. Das ist keine Zusicherung, dass jede TWAIN-/WIA-Funktion unter der lokalen Windows-Version unterstützt wird. Treiberpaket, Architektur und tatsächliche Fähigkeiten müssen separat geprüft werden.

## 4. Verbindlicher Gesamtumfang

Die 45 Anforderungen mit Einzelquellen und Abnahmekriterien stehen in [FUNKTIONSUMFANG.md](FUNKTIONSUMFANG.md). Die Hauptgruppen sind:

1. TWAIN-Quellenwahl, schneller Gerätewechsel, Treiberdialog und Scanmodi, Einzug/Duplex/Großformate nach Gerätefähigkeit, dauerhafte Scanbereiche und Helligkeitsvorwahl.
2. Seitenübersicht, Einzel-/Zweiseitenansicht, Vollbild, Zoom und Verschieben, Mehrfachauswahl sowie die beschriebenen Maus-/Tastaturwege und Toolbar-Ablageziele.
3. Bildimport einschließlich Drag-and-drop, Seitenreihenfolge, Drehen, Spiegeln, Invertieren, Rechteck-Löschen/Freistellen sowie interne/externe Zwischenablage. Rückseiten einsortieren entfällt auf Nutzerwunsch; siehe F19. Die allgemein erwähnte Zuschneideautomatik nicht ohne Nachweis als separate Motiverkennung auslegen; siehe F08/O05.
4. BMP, PNG, JPEG, TIFF, PDF und PDF/A-1b; Einzel-/Mehrseiten-TIFF/PDF/PDF/A, sechs Dateinamen-Automatikmodi, Qualitäts-/Kompressionswahl, verlustfreies PDF und drei PDF-Seitenmodi.
5. Optionales Öffnen der ersten gespeicherten Seite und drei wählbare Beendigungsmodi einschließlich dauerhafter Wiederherstellung der Seitenübersicht beim nächsten Start.
6. Drucken, Kopiermodus mit Exemplaren und Skalierung auf die Druckfläche, E-Mail-Übergabe über einen kompatiblen MAPI-Client, jeweils unter Beachtung der Seitenauswahl.
7. Scanner-Gerätetasten mit Anlegen/Bearbeiten/Löschen der Belegungen, soweit der Treiber passende Windows-Geräteereignisse unterstützt; aktuelle eigene Hilfe und Produktinfo.

**Ausgeschlossen:** sämtliche Faxfunktionen einschließlich Auswahl, Versand, Toolbarziel und Gerätetastenaktion. TIFF-„Fax G3/G4“ bleibt als Schwarzweiß-Bildkompression erhalten und ist kein Faxversand. Historische Grewe-Kontakt-/Vertriebsangebote und Windows-XP-/Vista-/7-Voraussetzungen sind keine nachzubauenden Produktfunktionen.

Nicht in der Hilfe belegte Erweiterungen wie Offline-OCR, Entzerrung, Leerseitenerkennung, PDF-Import oder direkte Netzwerkprotokolle sind kein Bestandteil dieses Auftrags. Sie dürfen die Pflichtfunktionen weder ersetzen noch verzögern.

Fortschritt, Abbrechen, verständliche Fehler, lokale Verarbeitung und Sicherheitsanforderungen bleiben technische Qualitätsziele. O01–O08 in der Funktionsmatrix dokumentieren den aktuellen Klärungsstand, erledigte Dokumentationslücken und verbleibende Test-/Umsetzungsdetails; sie sind keine pauschale Liste noch zu beschaffender Hilfe.

**Bestätigte Rechteckbearbeitung:** Im Zuschneidefenster ein Rechteck zeichnen. Ausschneiden löscht dessen Inhalt im bestehenden Bild, ohne Außenbereich oder Bildgröße zu ändern. Freistellen übernimmt mit „Übernehmen“ nur den Rechteckbereich als Scan. Dies ist von der Seiten-Zwischenablage zu trennen. Löschfüllfarbe und genaue Übernahmeposition sind noch nicht festgelegt.

## 5. Architekturvorschlag

### Technologie und Oberfläche

C++20 und Win32 passen zur vorhandenen Basis und zu den nativen Scanner-APIs. Für die erste Version ist weder ein Sprachwechsel noch ein neues UI-Framework erforderlich. Zeitgemäße Bedienung erfordert dennoch explizite Arbeit an DPI-Skalierung, Tastaturbedienung, Kontrast, Zugänglichkeit und reaktionsfähiger Oberfläche; das ergibt sich nicht allein aus C++ oder 64 Bit.

Empfohlene Fensteraufteilung: oben Scannerwahl und Scanaktionen, links Seitenminiaturen, mittig Dokumentvorschau, daneben beziehungsweise in einem Dialog Scaneinstellungen, unten Fortschritt und Status. Die Anordnung der Vorlage kann als Arbeitsablauf dienen, ohne Grafiken oder Oberfläche identisch zu kopieren.

### Verantwortlichkeiten

Die folgenden Bezeichnungen sind geplante Komponenten, noch keine vorhandenen Dateien:

| Komponente | Aufgabe |
| --- | --- |
| `ScannerApp` | Fenster, Benutzeraktionen, Einstellungen, Dokumentstatus |
| `IScannerBackend` | Quellen auflisten, Fähigkeiten abfragen, Sitzung starten, Abbruch anfordern; Seiten, Fortschritt und Fehler melden |
| `TwainBackend` | TWAIN-Zustandsautomat, Datenquelle, Treiberfenster und Bildtransfer |
| `WiaBackend` (nur bei späterem technischem Bedarf) | Möglicher nativer WIA-Zugriff; nicht zusätzlich zur TWAIN-Brücke vorausgesetzt |
| `ScanSession` | Ablaufzustände, Abbruch, Zeitüberwachung und Übergabe fertiger Seiten |
| `Document` / `Page` | Stabile Seiten-IDs, Reihenfolge, Bildreferenzen, DPI, Seitengröße und Bearbeitungsschritte |
| `ImagePipeline` | Dekodieren, Miniaturen, Drehung/Zuschnitt und begrenzte Bildverarbeitung |
| `PdfExporter` | Geordnete Seitenübernahme, physikalische Größen, Metadaten und zuverlässiges Speichern |
| `ImageExporter` | BMP/PNG/JPEG sowie Mehrseiten-TIFF mit benötigter Kompressionswahl |
| `WorkspaceStore` | Dauerhafte Seiten, Reihenfolge und Bearbeitungszustand; drei Beendigungsmodi |
| `PrintService` / `CopyController` | Druckausgabe, Exemplare, Druckflächen-Skalierung und Scan-Druck-Ablauf |
| `MailService` | MAPI-Entwurf mit Anhängen, Clientprüfung und gegebenenfalls Architekturadapter |
| `DeviceEventService` | Unterstützte Scanner-Gerätetasten und zugehörige Windows-Ereignisregistrierung |
| `Diagnostics` | Technische Fehler ohne Dokumentinhalte oder unnötige personenbezogene Daten |

Ein simuliertes Backend soll die Oberfläche und Dokumentverarbeitung ohne Scanner testbar machen. Unterschiede zwischen Quellen müssen sichtbar bleiben: Die gemeinsame Schnittstelle darf nicht behaupten, dass alle Geräte dieselben Funktionen besitzen.

### 32-/64-Bit und Treiberisolation

Der lokal nachgewiesene TWAIN-Treiber ist 32 Bit. Ein 64-Bit-Prozess kann diese DLL nicht direkt laden. Daher:

1. Den ersten reinen TWAIN-Anbindungsprototyp als Win32/x86 bauen, um den vorhandenen Treiber ohne Architekturbrücke zu erproben.
2. Für die alltagstaugliche Zielarchitektur ein x64-Hauptprogramm und einen separaten x86-TWAIN-Hilfsprozess vorsehen. So bleibt der vorhandene Treiber nutzbar und ein Treiberabsturz wird vom Hauptprogramm getrennt.
3. Einen x64-TWAIN-Hilfsprozess erst ergänzen, wenn andere Geräte ihn benötigen. Ein reines x86-Hauptprogramm wäre einfacher, ist wegen Speichergrenzen bei großen Farbscans aber nicht das bevorzugte Dauerziel.

Der TWAIN-Hilfsprozess braucht eine passende Nachrichtenschleife, geregelte Zustandsübergänge und eine abgestimmte Elternfenster-/Dialogbehandlung. Ein gegebenenfalls später eingeführter nativer WIA-Zugriff braucht korrekt initialisierte COM-Apartments und geregelte Objektlebenszeiten. Diese Aufgaben gehören nicht in einen beliebigen Hintergrundthread ohne Ereignisverarbeitung. MAPI ist unabhängig auf die vorhandene Clientarchitektur zu prüfen; der x86-TWAIN-Prozess löst nicht automatisch alle Mail-Kompatibilitätsfragen.

Die Prozesskommunikation soll versioniert und lokal sein, etwa über Named Pipes mit benutzerbezogenen Zugriffsrechten. Längen, Zustände, Pfade und Bildgrößen validieren; keine frei wählbaren DLL-Pfade oder auszuführenden Befehle akzeptieren. Große Bilddaten nicht unbegrenzt in Nachrichten kopieren. Der Hilfsprozess ist eine Stabilitäts- und Architekturgrenze, nicht automatisch eine Sicherheitssandbox.

### Speicher und Dokumentlebensdauer

Schon A4 mit 300 dpi benötigt als unkomprimiertes RGB-Bild ungefähr 26 MB; A3 ungefähr das Doppelte. Bei 600 dpi vervierfacht sich der Bedarf. Deshalb nur benötigte Vorschauen im RAM halten, Seiten intern dateibasiert verwalten und Grenzen für Pixelzahl, Speicher und temporären Plattenbedarf setzen.

Originalscans erhalten und Drehungen/Zuschnitt als Bearbeitungsschritte modellieren. Ein persistenter benutzergebundener Arbeitsbereich muss die drei beschriebenen Beendigungsmodi unterstützen: Seiten behalten, nach Rückfrage leeren oder ohne Rückfrage leeren. Behalten stellt Seiten beim nächsten Start wieder her, unabhängig davon, ob bereits ein Export erfolgte. Konsistente Metadaten und Bilddateien sowie klare Lösch-/Aufbewahrungsregeln sind deshalb von Anfang an erforderlich. Interne Mehrseiten-Zwischenablage dagegen nicht über Programmende hinweg erhalten.

## 6. PDF, Bilder und optionale Texterkennung

- Windows Imaging Component (WIC) für geeignete Bildformate und Konvertierungen prüfen; kein eigener JPEG-/PNG-Decoder.
- Die Auswahl muss BMP/PNG/JPEG/TIFF einschließlich Mehrseiten-TIFF und der geforderten Kompressionsoptionen abdecken. Import nur für ausdrücklich unterstützte Bildformate anbieten.
- BMP wird unkomprimiert ohne Formatoptionen ausgegeben, JPEG verlustbehaftet mit Qualitätswahl, PNG verlustfrei mit Kompressionsfaktor. TIFF benötigt getrennte Optionen für Schwarzweiß und Farbe/Graustufen, G3/G4-Invertierung und den dokumentierten ZIP-Kompatibilitätshinweis. Ein zusätzlicher kurzer TIFF-Codecname ist im vorhandenen Bild noch zu bestätigen; nicht aus OCR raten.
- Für PDF einen gepflegten C++-Writer auswählen. Beispielsweise PoDoFo auf benötigte Funktionen, Windows-Build, Lizenz und Sicherheitswartung prüfen; noch keine Bibliothek festgelegt oder eingebunden. PDF/A-Konformität, verlustfreie Bildspeicherung, Qualitätswahl und variable Seitenmaße müssen vor Bibliotheksfestlegung erprobt werden.
- PDF-Seitenmodi laut Hilfe: A4 mit Verkleinerung bei Bedarf, A4 mit bedarfsweisem Querformat oder Vorlagengröße aus Pixeln/DPI. Seitenverhältnis erhalten; nicht jeden Scan auf A4 verzerren.
- PDF-Qualität 100 bedeutet bei 24-Bit-Farbe/8-Bit-Graustufen verlustfreie Kompression; andere Bildformate werden immer verlustfrei gespeichert. JPEG bleibt davon unabhängig verlustbehaftet. PDF-Metadaten und automatisch öffnende Seitenübersicht als Viewer-Präferenz unterstützen; deren Anzeige kann der Betrachter ignorieren.
- Große Dokumente seitenweise exportieren. Zieldatei zunächst vollständig in einer temporären Datei im Zielverzeichnis erzeugen und erst nach Erfolg ersetzen. Bestehende Dateien nicht bei einem abgebrochenen Export zerstören.
- Keine eigene PDF-Implementierung aus den Zeichenfolgen des Altprogramms ableiten.
- PDF/A-1b ist durch Hilfe11c ausdrücklich vorgegeben. Nicht durch bloßes Setzen eines Metadatenfelds versprechen: passende Farbräume/Profile und alle Konformitätsregeln berücksichtigen und beispielsweise mit veraPDF validieren.
- Texterkennung ist kein belegtes Altprogrammmerkmal und nicht Teil des vereinbarten Nachbaus; mögliche spätere Erweiterung getrennt beauftragen.
- Vor Übernahme externer Bibliotheken Lizenz, transitive Abhängigkeiten, Wartungszustand und Updateweg dokumentieren.

## 7. Sicherheits- und Qualitätsanforderungen

- Standardbenutzerrechte; kein Administratorzwang für Scannen oder Speichern. Treiberinstallation bleibt ein getrennter Vorgang.
- Keine Komponenten der alten EXE-Dateien übernehmen oder zur Laufzeit benötigen.
- Nur vertrauenswürdig installierte Scannerquellen und benötigte Bibliotheken laden; DLL-Suchpfade absichern.
- RAII für Handles/COM/Dateien, geprüfte Größenarithmetik und kontrollierte Fehlerpfade verwenden.
- Compiler-/Linker-Härtung wie ASLR, DEP, Stackschutz und Control Flow Guard für die eigenen geeigneten Komponenten prüfen und aktivieren. Vorhandenes `SDLCheck` ersetzt diese Prüfung nicht.
- Keine Dokumentinhalte in Logs; Diagnoseberichte und eventuelle Speicherabbilder können sensible Daten enthalten und dürfen nicht automatisch versendet werden.
- Exportpfade und Überschreiben prüfen; temporäre Exportreste aufräumen. Den persistenten Arbeitsbereich nicht nach jedem Export löschen, sondern gemäß eingestelltem Beendigungsmodus behandeln. Sichere physische Löschung auf SSDs nicht versprechen.
- Keine eingehenden Firewallfreigaben oder dauerhaften Hintergrunddienste pauschal anlegen.
- Netzwerksicherheit des Herstellertreibers separat bewerten: Die Anwendung kann eine unverschlüsselte Treiberverbindung nicht nachträglich durch ihre Oberfläche verschlüsseln. Bei Bedarf vertrauenswürdiges LAN und Gerätesegmentierung vorsehen; keine Zertifikatsprüfung umgehen.
- Für direkt implementierte HTTPS-Protokolle später Zertifikate korrekt prüfen und Anmeldedaten über Windows-Schutzmechanismen speichern.
- Signierte Releases und ein nachvollziehbarer Updateprozess sind Ziel der Verteilung. Auch Gerätetreiber und Scannerfirmware bleiben separate Wartungsabhängigkeiten.

## 8. Umsetzung in überprüfbaren Etappen

### Etappe A – Kompatibilitätsnachweis, bevor die Oberfläche wächst

- Installierte Treiberversionen, Windows-Version, WIA- und TWAIN-Quellen erfassen.
- Kleiner x86-TWAIN-Prototyp: Quelle wählen, vorhandenen Brother-Dialog öffnen und eine Seite übernehmen.
- Vorhandene WIA/TWAIN-Brückenquellen zusätzlich auflisten und bei verfügbarer Quelle prüfen; nativer WIA-Vergleich ist keine Voraussetzung für den TWAIN-Prototyp.
- Flachbett, Einzug, tatsächliche Duplexunterstützung, Seitenreihenfolge, Auflösungen und benötigte Papierformate am Gerät prüfen. Größere Formate nicht allein aus einer Produktbezeichnung ableiten.
- Netzwerkunterbrechung, Offlinegerät, Abbruch und belegtes Gerät kontrolliert testen; keine Änderung an laufenden Scanaufträgen ohne Benutzeraktion.
- Ergebnis als Kompatibilitätsmatrix festhalten. TWAIN bleibt der festgelegte Hauptweg; Abweichungen werden als Probleme untersucht, nicht stillschweigend durch einen anderen Hauptweg ersetzt.

**Abnahme:** Der Brother-TWAIN-Weg liefert reproduzierbar korrekte Seiten; wichtige Einstellungen und Einschränkungen sind bekannt. Wenn er nicht zuverlässig funktioniert, zuerst Treiber-/Geräteproblem klären statt weitere UI zu bauen.

### Etappe B – Technisches Grundgerüst

- Dokument-/Seitenmodell, persistenten Arbeitsbereich mit drei Beendigungsmodi und simuliertes Backend implementieren.
- x64-Anwendung und x86-TWAIN-Hilfsprozess mit begrenzter, validierter Kommunikation aufsetzen.
- Fehler, Abbruch, Treiberfenster und Dokumentlebensdauer von Anfang an sauber behandeln.

**Abnahme:** Ein abstürzender oder nicht antwortender Hilfsprozess beendet die Oberfläche nicht; bereits empfangene Seiten bleiben erhalten. Hängende Treiber lassen sich möglicherweise nicht sofort kooperativ abbrechen: Zeitüberschreitung und Neustart anbieten, ohne erfolgreiche Freigabe des Geräts zu behaupten.

### Etappe C – Nutzbare Teilversion

- Scanaktionen, Miniaturen, Vorschau, Drehen, Löschen und Umordnen ergänzen.
- Mehrseitiges PDF und Bildexport mit sauberer Fehlerbehandlung umsetzen.
- Ungespeicherte Änderungen und Speicherkonflikte behandeln.

Diese Teilversion erfüllt ausdrücklich noch nicht den vollständigen Auftrag.

**Abnahme:** Ein typischer mehrseitiger Brother-Scan lässt sich kontrollieren, ordnen und verlustfrei hinsichtlich der vorgesehenen Verarbeitung speichern; Seitenzahl, Reihenfolge, Bildqualität und physikalisches Format stimmen.

### Etappe D – Vollständiger Funktionsausbau nach Hilfe

- Sämtliche noch fehlenden Anforderungen F01–F45 aus der Funktionsmatrix umsetzen, insbesondere dauerhafte Scanbereiche/Helligkeit, bestätigte Rechteckbearbeitung, Zwischenablage und vollständige Bedienwege. F19 entfällt auf Nutzerwunsch. Allgemeine Automatikbezeichnungen nur gemäß bestätigtem Verhalten konkretisieren.
- Alle sechs Speicherformate, Mehrseiten-TIFF, PDF/A, Qualitäts-/Kompressionswahl und automatische Dateinamen ergänzen.
- Drucken, Kopiermodus, MAPI-Mailübergabe und unterstützte Scanner-Gerätetasten einschließlich Konfiguration implementieren.
- Verbleibende Detailpunkte aus O01–O08 vor Abnahme der jeweiligen Funktion anhand der vorhandenen Unterlagen, gezielter Rückfragen oder Tests klären. Nicht vorhandene weitere Hilfeseiten nicht voraussetzen. Faxversand bleibt vollständig ausgeschlossen; TIFF-G3/G4-Kompression bleibt enthalten.

### Etappe E – Freigabe und Wartbarkeit

- Automatisierte Tests für Reihenfolge, Bearbeitung, Größenberechnung, Export, Speicherfehler, IPC-Validierung und ungewöhnlich große Bilder erstellen.
- Hardwaretests für Flachbett, leeren Einzug, mehrere Seiten, Duplex, Abbruch, Offlinegerät und Netzwerkunterbrechung dokumentieren; weitere Geräte getrennt testen.
- DPI-/Mehrmonitorverhalten, Tastaturbedienung, Zugänglichkeit, Installation ohne Adminlaufzeit und Bibliothekslizenzen prüfen.
- Alle sechs Ausgabeformate unabhängig lesen und prüfen; PDF/A zwingend zusätzlich mit Validator testen. Persistente Seiten, Auswahlaktionen, Mehrseiten-TIFF, MAPI und unterstützte Hardwareereignisse mitprüfen.
- Vollständige Abnahme anhand F01–F45 durchführen. Verbleibende geräteabhängige Grenzen ausdrücklich dokumentieren; fehlende Hardwareunterstützung nicht als bestandenen Test verbuchen.

## 9. Noch zu entscheidende Punkte

1. Verbleibende Eingabedetails zu Scanbereichen und Löschfüllfarbe/Übernahmeposition bei Bedarf gezielt abstimmen. Weitere Hilfe zu Scanbereichen und Rechteckbearbeitung ist laut Nutzer nicht vorhanden; der Funktionsablauf ist beschrieben.
2. Zusätzlichen TIFF-Codecnamen am vorhandenen Bild prüfen; nicht dokumentierte Wertebereiche, Resetwerte, Index-/Zeitmuster und Kollisionsregeln am Vorbild testen oder gezielt festlegen. PDF/A-1b, PDF-Qualität 100, Seitenmodi und Automatikvarianten sind bereits geklärt.
3. Vorhandenen MAPI-Mailclient und Windows-Geräteereignisse des Brothers einschließlich Architektur-/Treiberabhängigkeiten prüfen.
4. Verteilungsziel der ersten Freigabe bestimmen: zunächst vorhandener PC oder bereits Installation auf weiteren Windows-11-PCs.
5. Standardwert für die drei Beendigungsmodi sowie transparente Speicher-/Lösch- und Wiederherstellungsregeln festlegen; die Verfügbarkeit aller drei Modi steht bereits fest.

Zielumfang, Faxausschluss und TWAIN-Hauptweg stehen fest und werden nicht erneut zur Auswahl gestellt. Die vorhandenen Unterlagen reichen für den Anbindungsprototyp und die weitere Planung aus; es wird kein weiteres allgemeines Hilfepaket vorausgesetzt. Kleine Restentscheidungen werden vor der jeweiligen Umsetzung geklärt. Aufwandsschätzungen werden nach Etappe A belastbarer.

## 10. Quellen und Grenzen der Recherche

Primärquellen sind die lokalen Dateien unter `Infos`, insbesondere alle 44 Hilfebilder in `Infos/Hilfe`, die Nutzerfestlegungen zu TWAIN, Fax und Rechteckbearbeitung samt Beispielbild sowie die oben beschriebenen ausschließlich lesenden Geräte-/Treiberabfragen. Die vollständige Quellenabdeckung steht in `FUNKTIONSUMFANG.md`; der maschinelle Text der ursprünglichen 26 Hilfeseiten in `Infos/Hilfe/AUSWERTUNG-OCR.txt`.

Öffentliche Dokumentation, beim Erstellen des Konzepts abgerufen:

- Microsoft: [Windows Image Acquisition (WIA)](https://learn.microsoft.com/en-us/windows/win32/wia/-wia-startpage) – Windows-Schnittstelle und Treiberabstraktion.
- Microsoft: [Reading Device Properties](https://learn.microsoft.com/en-us/windows/win32/wia/-wia-reading-device-properties) – Eigenschaftsabfragen statt angenommener Gerätefähigkeiten.
- Microsoft: [Transferring Image Data in WIA 2.0](https://learn.microsoft.com/en-us/windows/win32/wia/-wia-transferring-image-data-in-wia2) – Transfermodell.
- [TWAIN Working Group](https://twain.org/specification/) – Standardisierungsorganisation; der Abruf lieferte nur eine allgemeine Beschreibung, keine ausgewertete vollständige Spezifikation. Vor Implementierung SDK, Spezifikation und passende DSM-Version konkret prüfen.
- Brother: [Downloads MFC-J6935DW, Deutschland](https://support.brother.com/g/b/downloadtop.aspx?c=de&lang=de&prod=mfcj6935dw_us_eu) – Modellzuordnung und Windows-11-Auswahl bestätigt; keine Treiberpakete heruntergeladen oder installiert.

Nicht nachgewiesen sind die vollständigen internen Abläufe des Altprogramms, dessen tatsächliche Sicherheitsprobleme, alle Gerätefähigkeiten, direkte eSCL-/WSD-/TWAIN-Direct-Unterstützung und die Verschlüsselung der Brother-Treiberverbindung. Breite Scannerunterstützung ist ein Architekturziel, keine Zusage für jedes Gerät.

Der Nachbau soll unabhängig aus öffentlichen Schnittstellen und eigenen Implementierungen entstehen. Der Kauf des alten Programms oder ein möglicherweise nicht mehr existierender Hersteller macht dessen Code, Grafiken und sonstige geschützte Inhalte nicht frei verfügbar. Eine juristische Lizenzprüfung ist nicht Bestandteil dieses technischen Konzepts.
