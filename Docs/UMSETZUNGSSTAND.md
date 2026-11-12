# Tatsächlicher Implementierungs- und Prüfstand

## Aktuelle Anpassung: Sitzungsoptionen und zentrale Mehrseitenansichten

Auf Nutzerwunsch bietet Extras → Optionen wieder drei Beendigungsarten. Seiten können für den nächsten Start behalten, nach Rückfrage gelöscht oder ohne Rückfrage gelöscht werden (Voreinstellung). Die interne Zwischenablage wird nicht wiederhergestellt. Temporäre Pixelablage und die getrennte TWAIN-Seitenbergung bleiben davon unabhängig.

Die linke Seitenliste ist nicht mehr sichtbar. Im zentralen Bereich werden alle Seiten angeordnet: volle Breite untereinander, ganze Seiten untereinander, zwei Seiten pro Reihe sowie große und kleine Symbolraster. Die Symbolgrößen sind anhand `Infos/Darstellung` angenähert. Auswahl und Sortierung erfolgen im zentralen Bereich. Der Symbole-Hauptbutton wählt das große Raster, sein Pfeil öffnet die Ansichtsauswahl. Plus/Minus wechseln benachbarte Ansichtsmodi; Vollbild erweitert das Anwendungsfenster.

IDE-Build und Release Win32/x64 einschließlich TWAIN-Helfer erfolgreich. Hardwarefreier UI-Lauf `TestResults/SessionViews-2562263de1e6459982f36d82395e1c1d`: alle neuen Sitzungs-, Auswahl-, Layout- und Scrollprüfungen bestanden; weiterhin ein bekannter Fehler „Empty workspace entirely white without hints“. Keine Hardwaretests oder vollständige visuelle Abnahme. Die Sitzungsprüfungen dieses historischen Laufs beschreiben den damaligen Stand; maßgeblich sind die Beendigungsoptionen der Aktualisierung oben.

## Aktuelle Anpassung: Scannerverbindung und Scanmodus

- Beim normalen Start werden TWAIN-Quellen und native WIA-Scanner im Hintergrund ermittelt. Der Pfeil am Button „Scannen“ zeigt die Verbindungen mit markierter Auswahl; Backend und Gerätekennung werden gespeichert. Kein stiller Wechsel zwischen TWAIN und WIA.
- Die Seitenleiste bietet „Schwarz/Weiss“, „Graustufen“, „Farbe“ und „Scanner-Menü“. Die ersten drei Modi zeigen Auflösung, Scanbereich, Papierzufuhr und Helligkeit. „Scanner-Menü“ blendet diese Felder aus und verwendet beim anschließenden Scannen die Oberfläche der ausgewählten Verbindung; bei TWAIN-Brother also den Brother-Treiberdialog.
- `ScannerWia.h/.cpp` ergänzt einen ausdrücklich wählbaren WIA-2-Scanweg mit direkter Übertragung beziehungsweise Gerätedialog. Das tatsächliche Treiberverhalten ist noch nicht abgenommen.
- Das TWAIN-Anforderungsformat ist jetzt explizit auf 572 Bytes/Version 3 festgelegt und statisch geprüft. Unterschiedliche Struktur-Offsets zwischen x86/x64 konnten als Ursache des gemeldeten Fehlers ausgeschlossen werden; dessen konkrete Ursache bleibt ohne den fehlerhaften Auftrag offen. Längen-, Versions- und Datenfehler werden nun getrennt gemeldet. `Scanner.exe` und `TwainHost.exe` müssen gemeinsam aktualisiert werden.
- Die aktuelle Hardware- und Bedienabnahme übernimmt der Benutzer. Für diese Anpassung werden keine automatischen Hardware-Scans oder Druckaufträge ausgeführt. Historische Hardwareergebnisse unten sind keine Freigabe dieser Änderung.
- Technische Kontrolle: IDE-Build sowie Release Win32/x64 einschließlich gemeinsam erzeugtem TWAIN-Helfer erfolgreich. Einmaliger hardwarefreier x64-UI-Lauf unter `TestResults/Interface-UI-7307dcfb9417406f8ffacddacbe0729f`: 82 bestandene Prüfungen, ein bereits bekannter Fehler „Empty workspace entirely white without hints“. Alle neuen Quellen-/Modusprüfungen bestehen; keine vollständige visuelle Freigabe. Die 14 neuen TWAIN-Protokollprüfungen wurden isoliert unter Win32/x64 erfolgreich ausgeführt.

## Ergebnis und Abgrenzung

Das frühere Win32-Grundgerüst wurde durch eine ausführbare Dokumentanwendung ersetzt. **Dateibasierte Pixelablage, erhaltene Originale, persistente Bearbeitungshistorie, drei Ansichtsmodi, Windows-Ereignisverwaltung und Unicode-MAPI mit MIME-Dateientwurf sind jetzt ergänzt.** Dialogfreie Brother-Flachbettscans sind nativ und per Speichertransfer nachgewiesen; inzwischen auch **vier tatsächlich erworbene ADF-/Duplexseiten sowie physischer Testdruck und Scan-Kopie über den Brother-Druckpfad**. x64→x86, automatischer Abbruch und Wiederanlauf sind ebenfalls zuvor real geprüft. **PDF/A-1b und echte PNG-Kompressionsstufen 0–9 sind implementiert und unabhängig geprüft.** Vollständige physische Gesamtfreigabe bleibt offen: Ausdruckqualität, Seitenreihenfolge/-orientierung, separater Simplex-ADF-Lauf, Gerätetastenbetätigung und realer Mailclient sind nicht abgenommen.

KONZEPT.md und FUNKTIONSUMFANG.md bleiben unverändert die Zielspezifikation. Ihre Anforderungen werden durch diesen Bericht nicht reduziert. Die 44 Hilfebilder wurden lokal erneut mit Windows-OCR ausgewertet; der vollständige neue Rohtext liegt unter `TestResults/Hilfe-OCR.txt`. OCR bleibt fehlerbehaftet. Die Altprogramme wurden weder ausgeführt noch als Laufzeitkomponenten verwendet.

## Neue Komponenten

| Datei | Tatsächliche Aufgabe |
| --- | --- |
| Scanner.cpp | COM-Initialisierung, UI-Start, explizite Diagnose-/Testmodi |
| ScannerCore.h/.cpp | Bild-/Dokumentmodell, Mehrseiten-WIC-Import, Transformationen, Rechtecke, Seitenzwischenablage, Windows-DIB, persistenter Arbeitsbereich |
| ScannerExport.cpp | BMP/JPEG/TIFF über Windows-WIC; PNG mit tatsächlich gewählter zlib-Stufe; Raster-PDF/PDF-A-1b mit ICC/XMP/Flate; temporäre Ausgaben und kollisionssicherer Commit |
| ScannerTwain.h/.cpp | TWAIN-ABI, Capability-Container/-Listen/-Bereiche, geprüfte Einstellungen, native und unkomprimierte Speichertransfers, Zustandsführung, Abbruch, Seiten-Checkpoints und Prozessbrücke |
| ScannerServices.h/.cpp | GDI-Druck/PRN, Simple-MAPI-Entwurf, explizite Still-Image-Anwendungsregistrierung |
| ScannerUi.h/.cpp | Native Oberfläche, Miniaturen, Auswahl/Sortierung, Vorschau/Zoom, Dialoge, Optionen und generierte durchsuchbare HTML-Hilfe |
| TwainHost.targets | Automatischer x86-Hilfsprozessbau und Bereitstellung beim x64-Projektbuild |
| Scripts\Build.ps1 | Reproduzierbare Win32-/x64-Builds sowie automatische Core-/UI-/Formatprüfungen |
| Scripts\Validate-Pdf.ps1 | Unabhängiger Windows.Data.Pdf-Parser/Renderer; **keine PDF/A-Validierung** |
| Scripts\Validate-Images.ps1 | Unabhängige System.Drawing-Pixelprüfung, TIFF-Tags und PNG-Stufenprüfung mit Pixeln/DPI/zlib-Kennzeichnung/Kompressionswirkung |
| Scripts\Validate-PdfA.ps1 | Unabhängige veraPDF-1b-Konformitätsprüfung einschließlich absichtlich ungültiger Negativkontrolle |
| Scripts\Read-Help.ps1 | Lokale OCR aller 44 JPG-Hilfeseiten |

Der benötigte Kompressionsteil von zlib 1.4 ist statisch eingebunden; das unveränderte ICC-sRGB2014-Profil ist als RCDATA eingebettet. Lizenz-/Herkunftsangaben stehen in `ThirdParty/NOTICE.md` und `ThirdParty/zlib/LICENSE`. Der Profilhash wurde byteidentisch mit dem offiziellen ICC-Download bestätigt. Der PDF-Writer ist eigener Code, keine aus Altprogrammen extrahierte Implementierung. Die nachgewiesene PDF/A-Konformität gilt für die getesteten Raster-Ausgabepfade, nicht für beliebige zukünftige PDF-Erweiterungen oder eine unbelegte absolute Farbtreue jedes Scanner-/Importprofils. Wartung und Regressionstests bleiben erforderlich. Java/veraPDF dienen ausschließlich als getrennte lokale Testwerkzeuge, nicht als Anwendungslaufzeit.

## Abdeckung F01–F41

„Implementiert“ bezeichnet vorhandenen Code, nicht automatisch vollständige Geräte-/Bedienabnahme. Konkrete Belege und Einschränkungen stehen in der letzten Spalte.

| ID | Stand | Beleg / verbleibende Grenze |
| --- | --- | --- |
| F01 | Implementiert, teilweise real geprüft | x64→x86 listet drei echte Quellen, darunter `TW-Brother MFC-J6935DW LAN`. Quellenauswahl und Konfigurationsaufruf vorhanden. Wechsel zwischen mehreren realen Geräten noch nicht durchgescannt. |
| F02 | Historische Brückenerkennung real geprüft; natives WIA neu implementiert | Brother- und HP-WIA/TWAIN-Brücken wurden über TWAIN gefunden. Zusätzlich ist jetzt ein ausdrücklich wählbares WIA-2-Backend vorhanden; dessen aktuelle Geräte- und Bedienabnahme steht aus. Kein universeller Geräteanspruch. |
| F03 | Implementiert, Geräteabnahme begrenzt | Farbe/Grau/SW, DPI, Rechteckmaße, Helligkeit, Einzug und Treiberdialog. Einstellungen werden per GETCURRENT und nach abhängigen Änderungen geprüft; Scanbereichs-Rasterrundung höchstens ein Ausgabepixel mit Protokollierung. Vollständige X-/Y-Auflösungslisten beziehungsweise RANGE-Daten, Pixeltypen und Transfermechanismen implementiert; Brother meldet je elf DPI-Werte. Real geprüft: Graustufen-Flachbett, 100 DPI. |
| F04 | Implementiert, realer Duplex-ADF geprüft | Brother meldet aktiven Einzug und Duplex; dialogfreier A4-Lauf liefert vier vollständige Seiten, unabhängig dekodiert. Optische Vor-/Rückseitenreihenfolge und Orientierung nicht bestätigt; separater Simplexlauf zuvor an äußerem Timeout gescheitert. |
| F05 | Implementiert mit Grenzen | Keine feste A4-Modellgrenze; A3/A2-Bereichsvorgaben und variable PDF-Seiten. 100-Megapixel-/Speichergrenzen gelten. Keine A2-Zusage für Brother. |
| F06 | Implementiert | Eigene Millimeterbereiche neu/bearbeiten/löschen/zurücksetzen/filtern und persistieren. Verwaltung über Scanner-Menü sowie „Liste bearbeiten…“ im Bereichskombinationsfeld. |
| F07 | Implementiert, synthetisch geprüft | Getrennte dauerhafte Geräteprofile und temporärer Wert. UI-Test schreibt/liest zwei getrennte synthetische Profile. Keine Zwei-Geräte-Helligkeitsabnahme. |
| F08 | Bestätigter Rechteckablauf umgesetzt | Keine unbelegte automatische Motiverkennung hinzugefügt. Siehe F22. |
| F09 | Implementiert, UI-geprüft | Kombinierte Übersicht/Vorschau, reine Symbol-/Nur-Seitenansicht, zwei aufeinanderfolgende Seiten und Vollbild. Ansichtswechsel ohne Änderung der Bilddaten automatisch geprüft. |
| F10 | Implementiert | Strg-Doppelklick links/mittig, Strg-Mausrad, Plus/Minus einschließlich expliziter Strg-Tastenbelegung. Nicht jede physische Eingabevariante automatisch end-to-end geprüft. |
| F11 | Implementiert | Linke Alt-Taste + linkes Ziehen verschiebt die Vorschau, ohne Bilddaten zu verändern. Physische Eingabe nicht getestet. |
| F12 | Implementiert, UI-geprüft | Standard-Mehrfachauswahl mit Strg/Umschalt; Auswahlrahmen vom freien Miniaturrand. Versteckter UI-Test betätigt den Rahmen über Fensternachrichten. |
| F13 | Implementiert | Gruppenziehen auf Speichern/Drucken/Mailen; strikt nur gezogene Auswahl. Kein echter Druck-/Mailtest über Drag-and-drop. |
| F14 | Implementiert | Persistente Doppelklickkonfiguration; Strg erzwingt Zoom. Zuschnitt-Dialogübernahme/-abbruch automatisch getestet. |
| F15 | Implementiert | Eigene deutsche HTML-Hilfe mit Navigation, lokaler Suche, Grenzen und Problemlösung; F1/Produktinfo. Keine historischen Kontaktangebote als eigener Support. |
| F16 | Implementiert, synthetisch geprüft | WIC-Import einschließlich Mehrseiten-TIFF, Mehrdateiauswahl und Dateidrop; dateibezogene Fehler. BMP/JPEG/PNG/TIFF durch Roundtrips geprüft. GIF ist codecabhängig und nicht separat getestet. Kein PDF-Import. |
| F17 | Implementiert, Kern geprüft | Stabile geordnete Mehrseiten-Gruppenverschiebung; UI-Mausweg vorhanden. Kein automatischer vollständiger physischer Drag-Durchlauf. |
| F18 | Implementiert, Kern geprüft | Tastaturauswahl, Strg+X/V, Ziel vor markierter Seite bzw. am Ende; Pfeil-ab nach letzter Seite hebt Auswahl auf. |
| F19 | Auf Nutzerwunsch aus dem Menü entfernt | Die bisherige interne Sortierfunktion und ihre Kernprüfungen bleiben erhalten, sind aber nicht mehr über das Menü erreichbar. |
| F20 | Implementiert, synthetisch geprüft | Gesamtreihenfolge umkehren; zweimalige Umkehr geprüft. |
| F21 | Implementiert, synthetisch geprüft | 90° links/rechts, 180°, invertieren, horizontal/vertikal spiegeln; Auswahl, Pixel und DPI geprüft. |
| F22 | Implementiert, Kern/UI-geprüft | Eigener Rechteckdialog; weißes Inhaltslöschen erhält Außenbereich und Maße; Freistellen ersetzt die bestehende Seite erst auf Übernehmen. Abbrechen verändert nichts. |
| F23 | Implementiert, synthetisch geprüft | Interne Mehrseitenkopie/-ausschnitt/-einfügung; neue IDs beim Einfügen; kein Persistieren der internen Ablage. |
| F24 | Teilweise implementiert, ungetestet | Erste gewählte Seite wird als CF_DIB veröffentlicht, externes unkomprimiertes 24-/32-Bit-CF_DIB eingelesen. Weitere DIB-/Clipboardformate fehlen. Benutzerzwischenablage wurde nicht für Tests überschrieben. |
| F25 | Implementiert, Export geprüft | BMP, JPEG, PNG, TIFF, PDF und PDF/A-1b nutzbar. PDF/A unabhängig mit veraPDF geprüft. Kanonische interne Bilddaten sind 8 Bit/Kanal, BGRA mit deckender Alpha. Kein unveränderter Erhalt beliebiger höherer Eingangsbittiefen. |
| F26 | Implementiert, synthetisch/unabhängig geprüft | Mehrseiten-PDF/PDF-A/TIFF und nummerierte Einzeldateien; Reihenfolge, TIFF-Framezahl und PDF/A-Konformität auch der Einzeldateien geprüft. |
| F27 | Implementiert | Sechs Automatikmodi vorhanden. Neue explizite Namens-/Kollisionsregeln, kein Anspruch historischer identischer Muster. Vollständige Dialogautomatik nicht end-to-end getestet. |
| F28 | Implementiert, unabhängig geprüft | A4 nur verkleinern, bedarfsweises Querformat, Originalmaße. Alle drei MediaBox-Größen zusätzlich mit Windows.Data.Pdf überprüft. |
| F29 | Implementiert, automatisch geprüft | Qualität 100 nutzt tatsächlich verlustfreie Flate-komprimierte RGB-Daten, **kein JPEG**. Exakte Daten/Kompressionspfad geprüft; andere Quellpixeltypen erzwingen ebenfalls Verlustfreiheit. PDF/A bei Qualität 80 und 100 sowie Grau/Mono/anderen Quelltypen unabhängig validiert. |
| F30 | Implementiert, automatisch/unabhängig geprüft | JPEG-/PDF-Qualität, persistente Optionen und Reset vorhanden. PNG-Stufen 0–9 werden direkt an zlib übergeben. WIC- und unabhängige System.Drawing-Pixel-/DPI-Prüfung aller zehn Stufen; zlib-Kennzeichnung und tatsächliche Größenwirkung geprüft. Optionen/Metadaten werden im UI-Formular und bei Persistenz-Roundtrips geprüft. |
| F31 | Implementiert, unabhängig geprüft | Getrennte Mono-/Farbwahl, None/G3/G4/LZW/ZIP, G3/G4-Invertierung. Pixelroundtrips sowie tatsächliche TIFF-Tags 1/3/4/5/8 geprüft. ZIP bleibt enthalten. LZW ist eine gewählte verlustfreie Implementierungsoption; der schwer lesbare historische Kurzname bleibt durch OCR allein nicht bestätigt. |
| F32 | Implementiert, unabhängig validiert | ICC-v2-OutputIntent, ICCBased-RGB-Bilder, PDF-ID und konsistente XMP-/Info-Metadaten. Je Konfiguration zwölf PDF/A-1b-Dateien mit veraPDF 1.30.4 validiert: alle drei Seitenmodi, Qualität 80/100, Unicode/XML-Zeichen, leere Metadaten, Einzel-/Mehrseitenausgabe und Quelltypen. Absichtlich ungültige Konformitätsangabe Z wird mit nicht konformem XML-Bericht und Exit 1 abgewiesen. |
| F33 | Implementiert, ungetestet | Optionale Shell-Öffnung der ersten tatsächlichen Ausgabedatei. Im Testbetrieb keine Fremdanwendungen automatisch geöffnet. |
| F34 | Implementiert, automatisch geprüft | Generationenwechsel, Reihenfolge/Pixels/DPI/IDs, Wiederherstellung und transiente Zwischenablage. Behalten, Leeren ohne Rückfrage sowie alle drei Entscheidungen des Rückfragemodus sind ohne Benutzereingabe testbar. Keine vollständige Stromausfallgarantie/automatische Reparatur. |
| F35 | Implementiert, physischer GDI-Druckpfad geprüft | Microsoft Print to PDF und direkter Brother-Auftrag10 mit einer synthetischen Testseite erfolgreich. EndDoc und Queue-Zustand Printing, danach entfernt. Optische Ausdruckqualität und Brother-PRN bleiben nicht bestätigt. |
| F36 | Implementiert, Scan-zu-Druck-Pfad real geprüft | Erste neu erworbene Duplexseite über dieselbe PrintImages-Pipeline als Brother-Auftrag12 ausgegeben; EndDoc und Queue-Verlauf erfolgreich. UI-Kopierdialog und optische Kopie nicht end-to-end abgenommen. |
| F37 | Implementiert, ungetestet | Positive Exemplare, Treiber auf 1, anwendungsseitige Ausgabe verhindert beabsichtigte doppelte Multiplikation. Tatsächliches Treiberverhalten noch nachzuweisen. |
| F38 | Implementiert, ungetestet | Optionale bedruckbare Fläche statt stiller Größenanpassung. Kein Druckflächen-Hardwaretest. |
| F39 | Implementiert, 34 Serviceprüfungen, Client ungetestet | Unicode-MAPI bevorzugt; ANSI nur verlustfrei. Provider-/Architekturfehler führen zu standardkonformem `.eml`-Dateientwurf mit UTF-8-Dateinamen und Base64-Anhängen. Kein automatischer Versand und kein mailto-Ersatz. Positiv-/Negativtests für Encoding, Kollisionen und Pfadinjektion; tatsächlicher Client bleibt unabgenommen. |
| F40 | Implementiert, Ereignisauflistung real geprüft | Explizite Still-Image-Registrierung, Windows-Ereignisabfrage und gezielte Register-/Entfernen-API; Verarbeitung von /StiEvent:. Brother meldet Datei-/E-Mail-/OCR-/Bildtasten. WIA ausschließlich zur Windows-Ereignisverwaltung, nie zum Bilderwerb. Physische Tastenbetätigung nicht geprüft. |
| F41 | Implementiert, Geräteauslösung offen | Liste vorhandener Belegungen und gemeldeter Windows-Ereignisse; Neu/Bearbeiten/Umbenennen/Löschen und gezielte Windows-Zuordnung. Starten/Scannen/Kopieren/Ignorieren mit Scanparametern, kein Fax. Registrierung/Entfernung und Erstzuordnung über physische Taste bleiben gesondert abzunehmen. |

Zusatzfunktionen: persistente Hintergrundfarbe, reine Symbol-/Seitenansicht, Originalwiederherstellung und Rückgängig vorhanden. Auswahltext/-hintergrund berücksichtigt Windows-Hochkontrast. Umfassende Hoch-DPI-/Barrierefreiheitsabnahme und exakte historische Oberflächenanordnung sind nicht zugesichert.

## Bewusst festgelegte neue Vorgaben

Diese Werte sind **Implementierungsentscheidungen**, nicht als historische Beobachtungen aus den Hilfebildern auszugeben:

- Farbe, 300 dpi, A4 und Herstellertreiberdialog als anfängliche Scanwerte; Helligkeit 0.
- Standardbereiche A4/A3/A2/Letter/Foto 100 × 150 mm/Visitenkarte 85 × 55 mm. Verfügbarkeit am Scanner ist gesondert zu prüfen.
- Weiß als Rechteck-Löschfarbe und Hintergrund für importierte Transparenz. Freistellen ersetzt die bearbeitete Seite an ihrer bisherigen Position.
- Spiegeln horizontal = links/rechts, vertikal = oben/unten.
- Behalten als anfänglicher Beendigungsmodus; Export öffnet nicht automatisch eine Fremdanwendung.
- PDF/JPEG-Qualität 100, PDF-Vorlagengröße, Mehrseitenausgabe und Viewer-Seitenübersicht. JPEG bleibt auch mit 100 verlustbehaftet.
- TIFF LZW für Farbe/Grau, G4 für rein binäre Bilder, keine automatische G3/G4-Invertierung.
- PNG-Standardwert 6 ist eine tatsächliche zlib-Stufe; 0–9 sind wählbar. Unterschiedliche Stufen müssen bei jedem beliebigen Motiv nicht zwangsläufig unterschiedliche Dateigrößen erzeugen. Das Testmotiv ergibt IDAT-Größen 99.604/31.561/30.618 Bytes für Stufen 0/1/9 bei identischen Pixeln und DPI.
- Einzeldateisuffix `_0001`, Zeitname `Scan_YYYYMMDD_HHMMSS`, Index-/Zeitkollisionen durch zusätzlichen Index. Bestehende Exportdateien werden nicht überschrieben; kein Transaktions-Overwrite für Dateisätze.
- Standardmäßig unterstützter unkomprimierter TWAIN-Speichertransfer, sonst nativ; kein stiller WIA-Ersatz und kein zweiter Erwerb nach Transferfehlern. Expliziter nativer Kontrollpfad bleibt verfügbar.
- 90 Sekunden Scanzeitlimit, 10 Sekunden Parent-Nachfrist, 60 Sekunden für x64-Quellendiagnosen. Die Parent-Wartezeit beginnt nach Rückkehr von CreateProcess; anfängliche Betriebssystem-Prozesserzeugungsverzögerungen sind davon nicht begrenzt. Scans beider Architekturen sind prozessisoliert. Abbruchmarker wird zwischen Transfers geprüft; ENDXFER/RESET/DISABLEDS folgen dem tatsächlich erreichten Zustand, blockierende Treiber werden nötigenfalls beendet. Vollständige Checkpoints werden geborgen; eine unfertige Seite wird nicht übernommen.

## Testverfahren und Nachweise

### Automatisch ausführbar

`Scripts\Build.ps1 -Test` baut Win32/x64 und führt je Konfiguration aus:

- C++-Kernprüfungen zu Transformationen, Pixel/DPI, Rechtecken, Gruppensortierung, Zwischenablage, Rückseiten, Arbeitsbereich und beschädigten Eingaben.
- Exportprüfungen für BMP/JPEG/PNG/TIFF/PDF, alle TIFF-Verfahren, Mono-Invertierung, Qualität 100 vs. JPEG-Pfad, unveränderliche andere Quelltypen, Seitengrößen und Kollisionen.
- 37 TWAIN-Prüfungen (33 zusätzliche Regressionen): unabhängige FIX32-Bitmuster, gepackte ABI, ONEVALUE/ARRAY/ENUMERATION/RANGE, UINT16-Padding, unterschiedliche X-/Y-DPI, RGB-Kanäle/Reihenfolge/Padding, Graupolarität, LSB-Mono, unvollständige/überlappende/ungültige Blöcke, Zustands-Triplets mit sofortigem RC1/CC5, Abbruch, Teilseiten und Windows-Recovery-Dateihandles. Der frühere gemeinsame Hin-/Rückkonvertierungsfehler wird nicht mehr nur symmetrisch getestet.
- Versteckte echte UI-Fenster, Auswahlrahmen, Mehrseitenbefehle, Vorschaumalerei, Formulare, Zuschnitt-Übernehmen/-Abbrechen, Profilpersistenz und alle Beendigungsentscheidungen. Die Rückfrageantwort wird injiziert, nicht vom Nutzer angefordert.
- Sechs unabhängige Windows.Data.Pdf-Lade-/Renderprüfungen einschließlich aller drei physikalischen Seitenmodi.
- 23 unabhängige Bild-/TIFF-/PNG-Prüfungen: System.Drawing-Pixelvergleich für verlustfreie Formate, JPEG-Dimensionen, tatsächliche TIFF-Compression-Tags und elf zusätzliche PNG-Stufen-/Kompressionsprüfungen.
- 13 unabhängige veraPDF-Prüfungen je Konfiguration: zwölf tatsächlich konforme PDF/A-1b-Ausgaben und eine zwingend abzuweisende ungültige Identifikation. Java-Prozess ohne UI, Zeitlimit, asynchrones Lesen beider Ausgabekanäle. Fehlende Werkzeuge oder positive Nichtkonformität führen zu Testfehlern; keine still übersprungene Validierung.

Die nativen Tests sind nicht im Visual-Studio-Test-Explorer registriert. CLI-Exitcodes, XML-Berichte und gespeicherte Ergebnisdateien sind maßgeblich. Die Negativkontrolle ersetzt keine positiven PDF/A-Nachweise; beides wird getrennt geprüft. Tests verwenden synthetische Dokumente ohne Druck-/Mailversand. Die früheren Ablehnungstests für nicht implementiertes PDF/A/PNG sind durch tatsächliche positive Funktionsprüfungen ersetzt.

### Reale Quellen und Hardware

Die x64-Anwendung hat über den x86-Hilfsprozess diese Quellen tatsächlich enumeriert:

1. TW-Brother MFC-J6935DW LAN
2. WIA-Brother MFC-J6935DW LAN
3. WIA-HPB034DB (HP ENVY 7640 series

Es gibt zwei während der Umsetzung korrigierte reale Befunde: Der Legacy-DSM liegt in `C:\Windows\twain_32.dll`, nicht in System32; TW_FIX32 ist Whole-/Fraction-Struktur und kein gewöhnlicher Integer im 16.16-Speicherlayout. Weder Problem wurde durch WIA-Substitution versteckt.

### Abschließender Build-/Testlauf

| Konfiguration | Native Core/Export/TWAIN | Versteckte UI | Unabhängig PDF | Unabhängig Bilder/TIFF/PNG | veraPDF | Summe | Ergebnis |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Debug Win32 | 118 | 48 | 6 | 23 | 13 | 208 | Alle bestanden |
| Debug x64 | 118 | 48 | 6 | 23 | 13 | 208 | Alle bestanden |
| Release Win32 | 118 | 48 | 6 | 23 | 13 | 208 | Alle bestanden |
| Release x64 | 118 | 48 | 6 | 23 | 13 | 208 | Alle bestanden |

Damit sind **832 erfolgreiche Prüfausführungen** belegt, nicht 832 unterschiedliche Testfälle. Die Konfigurationen wiederholen dieselben 208 Prüfungen. Darunter sind 48 konforme Archivdateien und vier korrekt abgewiesene Negativkontrollen. Die abschließenden Buildprotokolle enthalten keine Compilerwarnungen/-fehler; der abschließende Workspace-Build war ebenfalls erfolgreich. Frühere Fehler-/Abbruchläufe zählen nicht als bestandene Prüfungen. Der anfängliche veraPDF-Streamlängenbefund und die Prozessstatusprobleme im neuen Validierungsskript sind im aktuellen Stand behoben.

Aus früheren Läufen liegen zusätzlich eine reale Quellenprobe mit Exit 0/drei Quellen und eine isolierte fehlender-Hilfsprozess-Negativprobe mit erwartetem Exit 1 vor; diese Negativprobe wurde im Exportlauf nicht erneut ausgeführt. Neu geprüft: SHA-256 von Release-Win32-Scanner.exe und bereitgestelltem Release-x64-TwainHost.exe stimmt überein (`78EDD7D42FABFF19CB4BAA9357A029DA3DA0C96AB7B3F17D892FD7518E6DBF5B`); der Helfer ist tatsächlich das separat gebaute x86-Programm.

Aktuelle Ergebnisordner und Prüfzahlen stehen maschinenlesbar in `TestResults/Export-Final-Summary.json`. Einzelprotokolle jeweils: `results.txt`, `UI/ui-results.txt`, `independent-pdf-results.txt`, `independent-image-results.txt`, `independent-pdfa-results.txt` und `PdfA-validation/positive.xml` sowie `negative.xml`. Vollständige neue Buildausgaben: `Logs\build-export-release.log` / `Logs\build-export-debug.log`. Frühere TWAIN- und Buildprotokolle bleiben historische Nachweise; `TestResults/Final-Summary.json` gehört zum früheren Stand mit 336 Prüfungen. Alle lokalen Testartefakte können Dokumentdaten enthalten und sollen von Versionskontrolle ausgeschlossen bleiben.

### Brother-Hardware: erfolgreiche Transfers, Abbruch und Wiederanlauf

Der frühere native Fehlschlag **RC=1, CC=5 (TWCC_OPERATIONERROR)** bleibt als historischer Beleg unter `TestResults/Brother-final-3ffc7745d0014798a11a1677e86511e7/hardware-results.txt` erhalten. Seine ursprüngliche Einzelursache wurde nicht isoliert bewiesen; kein WIA-Wechsel und keine Umdeutung des Fehlschlags zu Erfolg.

Jetzt tatsächlich ausgeführte, begrenzte Hardwareprüfungen mit `TW-Brother MFC-J6935DW LAN`, ohne Benutzereingaben und ohne Treiberoberfläche:

| Prüfung | Lokaler Ordner unter TestResults | Tatsächliches Ergebnis |
| --- | --- | --- |
| x86, expliziter Speichertransfer | Brother-memory-final-9935bbb85f7945f0b33340c1caaa32dd | Exit 0; DAT_IMAGEMEMXFER RC=6; ENDXFER RC=0, Pending=0; eine PNG |
| x86, nativer Kontrolltransfer | Brother-native-control-2334cd98199b4524bdf0d4d8194351fa | Exit 0; DAT_IMAGENATIVEXFER RC=6; ENDXFER RC=0, Pending=0; eine PNG |
| Automatischer Abbruch nach XFERREADY | Brother-abort-final-c11cf3742d5c486cb9d26f39cbe51985 | Exit 1 ausdrücklich erwartet; Completed=0, Cancelled=1, 0 Seiten; RESET RC=0 und DISABLEDS RC=0; Recovery-Ordner erhalten |
| x64→x86, Standardwahl nach Abbruch | Brother-x64-recovery-cb96ef12e5e549f0b93347f1be5fe692 | Exit 0; Quelle bietet Speichertransfer, automatisch vor Erwerb gewählt; RC=6, ENDXFER RC=0/Pending=0; eine PNG |

Alle drei erfolgreichen PNGs wurden unabhängig mit System.Drawing geöffnet;

Die neue strenge Scanbereichsprüfung deckte eine reale Brother-Rundung um ein FIX32-LSB auf: angefordert 3.93701171875 Zoll, zurückgelesen 3.93699645996 Zoll. Der binäre Vergleich wurde durch eine protokollierte, auf ein Ausgabepixel begrenzte Rastertoleranz ersetzt. Größere Änderungen bleiben Fehler. Ein erster realer Abbruch deckte außerdem einen Windows-Dateihandlefehler bei der Recovery-Archivierung auf: Parent-Streams müssen vor dem Verschieben geschlossen sein. Der korrigierte Pfad erhält bei Archivierungsfehlern den Originalordner; beide Fälle sind regressionsgeprüft. Der frühere fehlgeschlagene Abbruch liegt unter `TestResults/Brother-abort-5f1f75462ad44446bda72c5f6f757f69` und zählt nicht als bestanden.

Die x64-Capability-Abfrage endete mit Exit 0; Bericht: `TestResults/Brother-capabilities-final.txt`. Treiberantworten: Einzug/Duplex/Helligkeit vorhanden, maximal 296,977 × 431,8 mm, UICONTROLLABLE=1; X und Y jeweils **100, 150, 200, 300, 400, 600, 1200, 2400, 4800, 9600, 19200 DPI**; Transfermechanismen native=0, memory=2, file=1; Pixeltypen BW=0, gray=1, RGB=2. Das sind keine Abnahmen aller Modi/Auflösungen. Der Brother liefert ENUMERATION; RANGE wird synthetisch geprüft. `FlatbedAdvertised=-1` bleibt ehrlich unbekannt, da GET nur den aktuell eingeschalteten Einzug als ONEVALUE meldet; Flachbett ist durch den tatsächlich bestätigten FEEDERENABLED=0-Scan separat belegt. Datei- und Memory-File-Erwerb sind nicht implementiert.

Jeder der vier aktuellen Hardwareordner enthält `hardware-results.txt` und `twain-trace.log`;

### Ausdrücklich nicht nachgewiesen

- PDF/A-1b ist für die aufgeführten Rastertestdateien implementiert und unabhängig validiert; keine pauschale Zusage für zukünftige Erweiterungen.
- ADF-Mehrblattfolge, Duplex, A3-Hardwarescan, A2-Hardware, Scan über jede WIA/TWAIN-Brücke.
- Physischer Druck, PRN, sortierte Mehrfachkopien, reale Kopierausgabe.
- MAPI-Entwurf in einem tatsächlich installierten Client, Bitness-/Unicodekompatibilität.
- Scanner-Gerätetasten, Still-Image-Registrierung und Windows-Ereigniszuordnung am Brother.
- Externer Clipboard-Roundtrip, vollständige physische Maus-/Tastaturabnahme, Hoch-DPI-/Barrierefreiheit, Absturz-/Stromausfalltests.

## Verbleibende physische Abnahmen und technische Grenzen

1. Reale ADF-/Duplex-Mehrblattfolge, Reihenfolge und Teilseitenbergung nach einem späteren Gerätefehler abnehmen. Dialogfreier Brother-Flachbetttransfer ist inzwischen belegt; die historische Einzelursache von RC=1/CC=5 bleibt ungeklärt, aber in den dokumentierten nativen und Speicherläufen nicht reproduziert.
2. Dateibasierte Copy-on-Write-Pixel, Originale, bis zu 20 persistente Bildbearbeitungen und ausgelagerte Miniaturen sind implementiert. Cacheziel 64 MiB, aktive Bilder und Verarbeitungspuffer können darüber liegen. Arbeitsbereich maximal 20 GB inklusive Originalen/Historie; aktuelle Importbilder maximal 9 GB/10000 Seiten, 100 MP/Seite. Vollständige Generationen werden neu geschrieben, keine inkrementelle Datenbank. Nach Prozessabsturz können verwaiste temporäre Cachedateien verbleiben; keine Stromausfallgarantie.
3. ADF/Duplex/Brückenquellen am Gerät prüfen und verbleibende Einstellungsunterschiede beheben; Fähigkeitenlisten und Rücklesung sind bereits weitgehend implementiert.
4. Physischer Druck-/Scan-Kopierpfad über Brother mit zwei Einseitenaufträgen geprüft. Ausdruckqualität, UI-Kopierablauf, Brother-PRN und tatsächlicher MAPI-Client bleiben zu prüfen. Unicode-/MIME-Dateientwurf ersetzt keine MAPI-Clientabnahme.
5. Brother-Ereignisse wurden real aufgelistet; tatsächliche Tastenbetätigung und Windows-Registrierungs-/Entfernungswirkung sind nicht physisch bestätigt.
6. Ansichtsmodi und Bereichslisten-Verwaltungseintrag sind ergänzt; umfassende physische Bedien-/Hoch-DPI-/Barrierefreiheits-/Robustheitsabnahme bleibt offen.

## Neue Gesamtprojekt-Prüfrunde

Hardwarebelege: `TestResults/Hardware-541f0c33973a482a9c2e3c4d3dc72cad`. Capabilities, Ereignisauflistung, Speichertransfer, nativer Transfer und Microsoft-Print-to-PDF-Dateiausgabe jeweils Exit 0. Beide erworbenen PNGs unabhängig mit System.Drawing als 386 × 386 Pixel bei ca. 100 DPI dekodiert. ADF und Duplex jeweils Exit 1 mit `FeederLoaded=0` und `PRECONDITION: feeder empty or unknown`; kein stiller Ersatzscan und kein bestandener Mehrblattnachweis. Ein Hardware-Skriptgesamterfolg bedeutet hier Erfolg der ausführbaren Pflichtprüfungen, nicht Erfolg der ausdrücklich blockierten ADF-/Duplex-Abnahme.

Die neuen Core-Tests prüfen Copy-on-Write, Originalerhalt, Auslagerung, persistente Historie/Rückgängig, Originalwiederherstellung nach Zuschnitt, begrenzte Historie, beschädigte neue Arbeitsbereiche und Legacy-Migration sowie Miniaturen. UI-Tests prüfen drei Ansichtsmodi und Original/Rückgängig. 34 Serviceprüfungen betreffen MIME/UTF-8/Base64, Dateinamen, Kollisionen und gefährliche Pfade, ohne Mailversand oder Mailclientstart. Der aktuelle maschinenlesbare Abschlussnachweis wird unter `TestResults/Project-Final-Summary.json` abgelegt; historische Zahlen vorheriger Abschnitte bleiben historische Läufe.

**Abgeschlossen:** Debug/Release × Win32/x64 je 258 erfolgreiche Prüfungen = **1.032 wiederholte Prüfausführungen**, keine verschiedenen 1.032 Testfälle. Je Konfiguration 31 Core, 61 Export, 37 TWAIN, 34 Services, 53 versteckte UI, 6 unabhängige PDF, 23 unabhängige Bilder und 13 veraPDF einschließlich ungültiger Negativkontrolle. Alle vier Skriptläufe und abschließender Visual-Studio-Build erfolgreich. Protokolle `Logs\build-final-release.log` und `Logs\build-final-debug.log`. Der neue Hardwarelauf ist separat ausgewiesen und nicht in diese synthetischen/Formatprüfzahlen eingerechnet.

## Nachprüfung mit eingelegten Vorlagen: ADF/Duplex, Druck und Kopie

Belege: `TestResults/AdfPrintCopy-ba851cbefcb849a8b9b518a96a87fab4`, `Logs\validate-adf-print-copy.log`, `Logs\build-adf-print.log`.

- Windows-Standarddrucker war FinePrint (virtuell, FPR12:). Wegen expliziter physischer Druckfreigabe wurde direkt `Brother MFC-J6935DW Printer` verwendet; der Standard blieb unverändert. Rückfrage war nicht beantwortbar.
- Erste Duplex-/Simplex-Versuche endeten mit äußerem Timeout124. Haupt- und Hilfsprozesse starteten jeweils merklich verzögert; gefüllter Einzug wurde bestätigt, aber keine Seite vollständig erworben. Diese Fehlversuche bleiben erhalten.
- Doppelte Capability-Voraböffnung im Hardwaretest entfernt; die erforderliche FeederLoaded-Prüfung bleibt im Scanhelfer unmittelbar vor ENABLEDS. Äußere Wiederholungsnachfrist360s, Scanlimit weiterhin90s. Korrigierter Duplexlauf `duplex-retry`: Exit0, Completed=1, Cancelled=0, vier Seiten. TWAIN-Readbacks bestätigen Einzug=1, Duplex=1, Speichertransfer2 und vier Checkpoints; eigentlicher Geräteablauf ca.17s.
- Vier PNGs unabhängig mit System.Drawing dekodiert: jeweils819×1161 Pixel, ca.100 DPI; individuelle SHA256 in `independent-duplex-images.json`. Angefordert A4/100DPI; tatsächliche Treiberpixel werden nicht künstlich umetikettiert. Visuelle Vollständigkeit, Ausrichtung und Reihenfolge nicht automatisch bestätigt.
- Synthetischer Einseiten-Testdruck Job10 und erste frisch gescannte Seite als Einseiten-Kopie Job12: jeweils GDI-EndDoc erfolgreich, Queue meldet Printing und danach NoLongerInQueue. Insgesamt zwei Seiten beauftragt. Das belegt physische Treiber-/Spoolerübergabe, nicht die optische Kontrolle der ausgegebenen Blätter.
- Separater Simplex-ADF-Test ist nicht erfolgreich abgeschlossen; Duplex hat den bereitgestellten Stapel verarbeitet. Erneutes Einlegen wäre für einen gesonderten erfolgreichen Simplexnachweis erforderlich. Keine Ersatz-Flachbettbilder als ADF-Erfolg gewertet.
- Release-Neubau und Visual-Studio-Gesamtbuild erfolgreich. Zusätzliche163 native Release-x64-Prüfungen nach der ersten CLI-Erweiterung fehlerfrei (`AdfPrint-Regression-c8149b34e83d4ba585260ac185217e6e`); nach Entfernung der redundanten Vorabfrage erneut gebaut und realer Duplexlauf erfolgreich. Frühere1032-Konfigurationsprüfungen bleiben vorheriger Stand, nicht erneut vollständig ausgeführt.
