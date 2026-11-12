# Verbindlicher Funktionsumfang – Scanner

## 1. Vorgabe und Leseregeln

- Maßgeblich sind die 44 Hilfebilder in `Infos/Hilfe`: die ursprünglichen 26 Seiten, 13 Buchstabenergänzungen sowie `Speichern11d.jpg` bis `Speichern11h.jpg`. Ergänzend gelten die Programmansichten und die Nutzerklärung zur Rechteckbearbeitung samt Beispielbild.
- Alle dort beschriebenen Anwendungsfunktionen gehören zum Zielumfang der Professional-Ausgabe, ohne Werbehinweis auf den Seiten. Es wird keine künstlich eingeschränkte Standardausgabe nachgebaut.
- **Einzige vom Nutzer ausgeschlossene Funktionsgruppe ist Fax:** kein Faxversand, keine Faxprogrammwahl, keine Fax-Gerätetastenaktion und kein Fax-Toolbarziel.
- TIFF-Kompression „Fax G3/G4“ ist Bildkodierung, kein Faxversand, und bleibt Bestandteil des Exports.
- **TWAIN ist die primäre Scanneranbindung**, insbesondere `TW-Brother MFC-J6935DW LAN`. Laut `Hilfe23.jpg` verwendet das Vorbild für WIA-Geräte eine WIA/TWAIN-Konvertierung. Ein direkter eigener WIA-Backend ist keine daraus ableitbare Pflicht; verfügbare Brückenquellen sind zu prüfen.
- Etappen ändern die Reihenfolge der Implementierung, nicht den vereinbarten Gesamtumfang. Eine erste Teilversion ist keine Abnahme des vollständigen Nachbaus.
- Diese Datei ist eine Spezifikation, kein Implementierungs- oder Testnachweis. Alle F-Anforderungen sind noch umzusetzen beziehungsweise nachzuweisen.
- Die Abnahmekriterien sind vorgeschlagene, überprüfbare Konkretisierungen der Hilfe. Wo diese keine Details liefert, werden keine historischen Dialogoptionen oder Algorithmen erfunden.
- Der lokale Rohtext `Infos/Hilfe/AUSWERTUNG-OCR.txt` deckt die ursprünglichen 26 Seiten ab, nicht die späteren Ergänzungen. Maßgeblich bleiben die Bilder; Erkennungsfehler insbesondere bei Tastenkürzeln und Kompressionsnamen sind möglich.

## 2. Funktions- und Abnahmematrix

Alle Quellen in dieser Tabelle sind relativ zu `Infos/Hilfe/`, sofern nicht anders angegeben.

### Scanner und Erfassung

| ID | Verbindliches Verhalten | Quelle | Abnahme / Bedingung |
| --- | --- | --- | --- |
| F01 | TWAIN-Scanner auswählen und zwischen mehreren Quellen schnell wechseln; Gerätekonfiguration aus der Auswahl aufrufen | Hilfe1, Hilfe5, Hilfe22; zusätzlich `Infos/IF.jpg` | Brother-TWAIN-Quelle wird angeboten und verwendet; eine zweite verfügbare Quelle ist unabhängig auswählbar. |
| F02 | Verfügbare WIA/TWAIN-Brückenquellen wie beim Vorbild berücksichtigen | Hilfe.jpg, Hilfe23 | Vorhandene kompatible Brückenquelle wird erkannt und getestet; fehlende Brücke nicht als universelle Geräteunterstützung darstellen. |
| F03 | Scanmodus, Auflösung, Scanbereich, temporäre Helligkeit und geräteabhängige Papierzufuhr; anschließend Scan in die Seitenübersicht | Hilfe9, Hilfe9a; zusätzlich `Infos/Panel.jpg`, `Infos/PanelMenu.jpg` | Brother-Treiberdialog ist als Alternative auswählbar; eigene Einstellungen berücksichtigen Gerätefähigkeiten. Temporäre Helligkeit gilt bis Programmende oder Scannerwechsel; dauerhafte Vorwahl gemäß F07. |
| F04 | Automatischen Einzug und Duplex unterstützen | Hilfe1 | Mehrere Blätter und beidseitige Erfassung mit einem geeigneten Gerät liefern die erwarteten Seiten und Reihenfolge; nicht vorhandene Hardwarefähigkeiten bleiben klar erkennbar. |
| F05 | Großformate, insbesondere A3/A2 bei geeigneten Scannern | Hilfe1 | Modell, Vorschau und Export besitzen keine feste A4-Grenze; Hardwaretest nur mit tatsächlich passendem Gerät. Keine A2-Zusage für den Brother. |
| F06 | Scanbereiche dauerhaft definieren, neu anlegen, bearbeiten, löschen und auf Standardliste zurücksetzen; optional nur zum Scanner passende Bereiche anzeigen | Hilfe1, Hilfe9a, Hilfe9b | „Liste bearbeiten“ öffnet die Verwaltung; Filter blendet Bereiche außerhalb der maximalen Scanfläche aus. Persistenz und physikalische Maße bleiben korrekt. Weitere Hilfe zu Eingabefeldern wurde nicht gefunden, siehe O01. |
| F07 | Gerätespezifische Helligkeitsvorwahl dauerhaft speichern und bei Gerätewahl anwenden | Hilfe1, Hilfe6 | Zwei Quellen behalten getrennte Vorwahlen; Auswahl und Neustart laden den korrekten Wert, soweit vom Treiber unterstützt. |
| F08 | Die als „automatisches Zuschneiden“ erwähnte Funktion berücksichtigen | Hilfe.jpg; Nutzerklärung zur Rechteckbearbeitung | Der bestätigte Rechteckablauf steht in F22. Die allgemeine Hilfebezeichnung belegt noch keine eigenständige Motiverkennung; ein darüber hinausgehendes Automatikverhalten bleibt eine Testfrage gemäß O05. |

### Darstellung und Bedienung

| ID | Verbindliches Verhalten | Quelle | Abnahme / Bedingung |
| --- | --- | --- | --- |
| F09 | Kombinierte Seitenübersicht und Seitendarstellung, Zweiseitendarstellung und Vollbild | Hilfe1; zusätzlich `Infos/Ansicht.jpg` | Modi sind umschaltbar, gewählte Seite und Dokumentreihenfolge bleiben erhalten. |
| F10 | Zoom per Strg+linkem Doppelklick, Strg+mittlerem Doppelklick, Strg+Mausrad sowie Plus/Minus | Hilfe16 | Die beschriebenen Wege vergrößern/verkleinern die Ansicht; genaue Strg-Zuordnung für Plus/Minus am Bild vor UI-Festlegung prüfen. |
| F11 | Vergrößerten Bildausschnitt mit linker Alt-Taste und Ziehen bei gedrückter linker Maustaste verschieben | Hilfe16 | Ansicht lässt sich verschieben, ohne Seiten zu verschieben oder Bilddaten zu ändern. |
| F12 | Einzel-/Mehrfachauswahl durch Anklicken, Strg+Anklicken und Auswahlrahmen | Hilfe1, Hilfe16–18 | Nicht zusammenhängende Auswahl ist möglich; Aktionen betreffen genau die ausgewählten Seiten. |
| F13 | Gewählte Seiten auf Toolbarziele Speichern, Drucken und Mailen ziehen | Hilfe16 | Nur die gezogenen Seiten werden an die entsprechende Aktion übergeben, in definierter Dokumentreihenfolge; Faxziel entfällt. |
| F14 | Linken Doppelklick zwischen Vergrößern und Öffnen des Zuschnittdialogs konfigurieren | Hilfe4 | Beide Modi funktionieren; mittlerer Doppelklick verkleinert im Zoommodus, Strg+linker/mittlerer Doppelklick ermöglicht Zoom auch im Zuschnittmodus. |
| F15 | Hilfe mit Inhaltsnavigation und Suchmöglichkeit bereitstellen; Produktinfo und verständliche Problemlösung | Hilfe0, Hilfe24 und Hilfekopfzeilen; zusätzlich `Infos/Hilfe.jpg` | Eigene aktuelle Hilfetexte sind erreichbar und durchsuchbar; veraltete Grewe-Kontakt-/Downloadangebote werden nicht als eigener Support ausgegeben. |

### Import, Reihenfolge und Bearbeitung

| ID | Verbindliches Verhalten | Quelle | Abnahme / Bedingung |
| --- | --- | --- | --- |
| F16 | Eine oder mehrere Bilddateien über Dateiauswahl importieren; Bildimport per Ziehen ins Fenster | Hilfe1, Hilfe10 | BMP/JPEG/TIFF und verfügbare weitere unterstützte Bildcodecs werden verarbeitet; Fehler werden dateibezogen gemeldet. PDF-Import wird von der Hilfe ausdrücklich nicht verlangt. Mehrseiten-TIFF-Import siehe O06. |
| F17 | Seiten per Maus ziehen und umordnen, auch Mehrfachauswahl | Hilfe1, Hilfe16, Hilfe17 | Auswahl bleibt beim Verschieben als geordnete Gruppe erhalten; keine Seite wird dupliziert oder verloren. |
| F18 | Seiten mit Tastatur markieren, Strg+X ausschneiden und Strg+V an der Zielseite einfügen; ohne Auswahl am Ende einfügen | Hilfe17 | Reihenfolge entspricht dem Ziel; mit Pfeil-ab kann nach der letzten Seite die Auswahl aufgehoben und am Ende eingefügt werden. |
| F19 | Rückseiten einsortieren entfällt im Menü auf Nutzerwunsch. | Nutzeränderung 12.11.2026; Hilfe17 | Kein Menüpunkt oder Bedienablauf zum Einsortieren von Rückseiten. |
| F20 | Gesamte Seitensortierung umkehren | Hilfe1, Hilfe17 | A,B,C wird C,B,A; zweimaliges Anwenden stellt die ursprüngliche Reihenfolge wieder her. |
| F21 | Eine oder mehrere Seiten in einem Durchgang drehen, invertieren und spiegeln | Hilfe1, Hilfe18; zusätzlich `Infos/Bearbeiten.jpg` | Links-/Rechts-/180°-Drehung und angebotene Spiegel-/Invertieraktionen wirken auf genau die Auswahl; Bildmaße/DPI bleiben korrekt. Spiegelachsen siehe O05. |
| F22 | Dialog „Ausschneiden und Freistellen“: Rechteck zeichnen, markierten Inhalt löschen oder auf den Rechteckbereich freistellen und mit „Übernehmen“ als Scan übernehmen | Hilfe18, `Infos/Bearbeiten2.jpg`, Nutzerklärung und Beispielbild | Ausschneiden entfernt nur den Rechteckinhalt, nicht die ganze Seite; Außenbereich und Bildabmessungen bleiben erhalten. Freistellen übernimmt nur den Rechteckbereich mit passenden Maßen/DPI. „Abbrechen“ übernimmt keine Änderung. Löschfüllfarbe ist noch nicht festgelegt. Kein Gleichsetzen mit F18/F23. |
| F23 | Eine/mehrere Seiten kopieren oder ausschneiden und über interne Zwischenablage einfügen | Hilfe19 | Alle gewählten Seiten lassen sich innerhalb der Anwendung übertragen; interne Mehrseitenablage endet mit dem Prozess. |
| F24 | Bildaustausch mit anderen Windows-Programmen über die allgemeine Zwischenablage | Hilfe19 | Erste gewählte Seite ist als übliches Bildformat extern verfügbar; ein extern kopiertes Bild kann eingefügt werden. Interne Mehrseitendaten werden extern nicht vorausgesetzt; nach Neustart gibt es keine alte interne Mehrseitenablage. |

### Speichern und Aufbewahrung

| ID | Verbindliches Verhalten | Quelle | Abnahme / Bedingung |
| --- | --- | --- | --- |
| F25 | BMP, PNG, JPEG, TIFF, PDF und PDF/A speichern; Zielordner, Dateiname und formatabhängige Optionen wählen | Hilfe11a, Speichern11d–h | Ausgabe ist unabhängig lesbar und ohne Werbehinweis; alle/markierte Seiten sind wählbar. Formatdetails stehen unten. |
| F26 | PDF/PDF/A/TIFF wahlweise als Mehrseitendatei oder einzelne Dateien ausgeben | Hilfe11a, Speichern11f, Speichern11h | Mehrseitenausgabe erhält Reihenfolge und Maße; Einzeldateinamen werden um Seitennummern ergänzt. BMP/JPEG/PNG erzeugen je Seite eine Datei. |
| F27 | Sechs Varianten der Dateinamenautomatik anbieten | Hilfe11b | Keine Vorgabe, Dateiauswahl öffnen, Ordnerauswahl öffnen, letzten Namen einsetzen, letzten Namen mit Index versehen oder Datum/Zeit verwenden. Präzise Muster/Kollisionen siehe O02. |
| F28 | Drei PDF-Seitenmodi: A4 einpassen, A4 einpassen mit bedarfsweisem Querformat oder Vorlagengröße | Speichern11f | Zu große Vorlagen werden im A4-Modus verkleinert; zweiter Modus wählt bei beschriebenem Bedarf Querformat. Vorlagengröße entspricht dem Scan-/Freistellausschnitt. |
| F29 | PDF/PDF/A bei Qualitätswert 100 für 24-Bit-Farbe und 8-Bit-Graustufen verlustfrei speichern; andere Bildformate immer verlustfrei | Speichern11f | Roundtrip bestätigt unveränderte zum Export vorgesehene Bilddaten. Ein JPEG-Qualitätswert 100 darf nicht als Ersatz für den verlustfreien PDF-Pfad dienen. |
| F30 | JPEG-/PDF-Qualität sowie PNG-Kompressionsfaktor einstellen; Formatoptionen zurücksetzen | Speichern11e–g | JPEG bleibt verlustbehaftet; PNG bleibt unabhängig vom Faktor verlustfrei. PDF-Qualität folgt F29. „Zurücksetzen“ stellt die vereinbarten Standardwerte wieder her; unbelegte Werte siehe O02. |
| F31 | TIFF-Verfahren getrennt für Schwarzweiß und Farb-/Graustufen wählen; optionale Schwarzweiß-Invertierung für G3/G4 und Zurücksetzen | Speichern11h | Unkomprimiert, G3/G4 für Schwarzweiß und ZIP sind belegt; ein weiterer verlustfreier Name bleibt OCR-unsicher, siehe O02. Ausgaben und Invertierung unabhängig prüfen. Codecbezeichnungen mit „Fax“ bedeuten keinen Faxversand. |
| F32 | PDF/A-1b als konforme Archivierungsoption nach ISO 19005-1 | Hilfe11a, Hilfe11c, Speichern11f | Export und Mailanhang bieten die Option; Profil PDF/A-1b ist ausdrücklich belegt und mit unabhängigem Validator nachzuweisen. |
| F33 | Nach Speichern optional die erste Seite mit dem zugeordneten Programm öffnen | Hilfe4 | Option lässt sich schalten; geöffnet wird die erste zugehörige Ausgabedatei, nicht eine beliebige temporäre Datei. |
| F34 | Beim Beenden drei Modi: Seiten behalten, nach Rückfrage leeren, ohne Rückfrage leeren | Hilfe4 | Behalten stellt die Seiten beim nächsten Start wieder her; die beiden Leer-Modi respektieren Benutzerentscheidung beziehungsweise Einstellung. Dokumentdaten und Bearbeitungszustand konsistent verwalten. |

### Drucken, Kopieren und E-Mail

| ID | Verbindliches Verhalten | Quelle | Abnahme / Bedingung |
| --- | --- | --- | --- |
| F35 | Alle/markierte Seiten drucken; Drucker-/Treiberkonfiguration, Ausgabe als PRN-Datei soweit unterstützt, Druckflächen-Skalierung, bedarfsweise 90°-Drehung, Exemplare und Sortieren | Hilfe12a, Hilfe16 | Auswahl und Reihenfolge stimmen; Optionen werden gemäß Hilfebeschreibung angewendet. PRN ist keine PDF-Ausgabe. Druckfehler löschen keine Dokumentseiten. |
| F36 | „Kopieren“ startet sofort einen Scan mit aktuellen Einstellungen (Flachbett/Einzug/Duplex) und druckt ohne Scananzeige oder Druckdialog auf dem Windows-Standarddrucker | Nutzeränderung 12.11.2026 ersetzt Moduswechsel aus Hilfe9, Hilfe10, Hilfe15 | Ein Klick, kein bleibender Kopiermodus, keine Dokumentänderung; nur vollständige erfolgreiche Scans drucken. Fehler und Abbruch sichtbar melden; keine automatische E-Mail- oder Faxaktion. |
| F37 | Im Kopiermodus Anzahl der Exemplare einstellen | Hilfe15 | Gewählte positive Anzahl wird ohne doppelte Anwendung durch Treiber und Anwendung korrekt ausgegeben. |
| F38 | Im Kopiermodus optional auf die bedruckbare Fläche skalieren | Hilfe15 | Bei aktivierter Option wird randnaher Inhalt vollständig innerhalb der druckbaren Fläche ausgegeben; ohne Option keine verdeckte Anpassung. |
| F39 | Alle/markierte Seiten über MAPI als Nachricht vorbereiten; Dateiname, Formatoptionen, Einzel-/Mehrdateiausgabe, PDF/A und PDF-Metadaten wählen | Hilfe14a, Hilfe14b, Hilfe16 | „Weiter“ öffnet den Client-Versanddialog mit Anhängen; der Nutzer stellt die Nachricht fertig und sendet selbst. Fehlende MAPI-Unterstützung verständlich melden; Clientarchitektur testen. |

### Scanner-Gerätetasten

| ID | Verbindliches Verhalten | Quelle | Abnahme / Bedingung |
| --- | --- | --- | --- |
| F40 | Unterstützte Scannerfunktionstasten über Still-Image-/passende Windows-Geräteereignisse zur Steuerung verwenden | Hilfe7, Hilfe21 | Funktion wird mit geeignetem Treiber nachgewiesen; fehlende Ereignisunterstützung ist eine sichtbare Gerätebeschränkung und wird nicht simuliert. Brother-Unterstützung noch ungetestet. |
| F41 | Gerätetastenbelegungen neu anlegen, bearbeiten und löschen; Windows-Ereigniszuordnung erläutern; Starten, Scannen, Kopieren oder Ignorieren zuordnen | Hilfe7a–e | Erstes Hardwareereignis öffnet die Zuordnung; „Neu“ erfordert laut Vorlage Beenden und Betätigen der Taste. Scanmodus, Auflösung, Scanbereich und Papierzufuhr der Belegung beachten. Windows-Anleitung modernisieren, Faxaktionen ausschließen. |

### Konkretisierte Export- und Bearbeitungsdetails

- **BMP (`Speichern11d.jpg`):** unkomprimiert, pro Seite eine Datei, keine Formatoptionen.
- **JPEG (`Speichern11e.jpg`):** 24-Bit-Farbe und 8-Bit-Graustufen direkt; andere Bildformate vor Export umwandeln. Verlustbehaftete Qualitätswahl und Zurücksetzen, pro Seite eine Datei.
- **PDF/PDF/A (`Hilfe11a.jpg`, `Hilfe11c.jpg`, `Speichern11f.jpg`):** Titel, Thema, Stichworte und Verfasser speichern; mehrere Stichworte laut Hilfe durch Leerzeichen trennen. Drei Seitenmodi gemäß F28; automatisch öffnende Seitenübersicht als Viewer-Präferenz setzen, deren Beachtung vom Betrachter abhängt. Qualität 100 ist bei Farbe/Graustufen verlustfrei, andere Bildformate sind immer verlustfrei. Zurücksetzen anbieten. PDF/A-Profil ist ausdrücklich PDF/A-1b.
- **PNG (`Speichern11g.jpg`):** verlustfrei, einstellbarer Kompressionsfaktor ohne Qualitätsänderung; höhere Kompression kann langsamer sein. Zurücksetzen, pro Seite eine Datei.
- **TIFF (`Speichern11h.jpg`):** Einzel-/Mehrseitendateien; getrennte Kompressionswahl für Schwarzweiß und Farbe/Graustufen. G3/G4 nur Schwarzweiß; optionale invertierte Speicherung bei diesen beiden Verfahren als ausdrücklich gewählter Kompatibilitätsmodus. ZIP bleibt wegen des vollständigen Funktionsumfangs enthalten, der in der Hilfe genannte Kompatibilitätshinweis wird dokumentiert. Eine historische Ankündigung möglicher späterer Entfernung ist keine bereits erfolgte Streichung. Ein weiterer verlustfreier Codecname ist noch am vorhandenen Bild zu bestätigen.
- **Speichern/Mailen:** Alle Seiten oder nur markierte wählen; PDF/PDF/A/TIFF wahlweise zusammenfassen, sonst pro Seite eine nummerierte Datei. Datei-/Ordnerwahl und die sechs Automatikmodi sind in Hilfe11a/b beschrieben; Maildialog übernimmt die passenden Formatoptionen und Metadaten gemäß Hilfe14a.
- **Rechteckbearbeitung (Nutzerklärung, `Infos/Bearbeiten2.jpg`, Beispielbild):** „Zuschneiden“ öffnet die Bearbeitung einer Rechteckauswahl. „Ausschneiden“ löscht den Inhalt dieses Bereichs innerhalb des Bilds; „Freistellen“ übernimmt über „Übernehmen“ nur den Bereich als Scan. Nicht mit dem Ausschneiden ganzer Seiten in die interne/Windows-Zwischenablage verwechseln. Löschfüllfarbe und die genaue Ersetzungs-/Einfügeposition des übernommenen Scans sind nicht aus der Erklärung abzuleiten und bleiben kleine Umsetzungsrückfragen, keine Forderung nach weiterer Hilfe.

## 3. Ergänzungen aus den ursprünglichen Oberflächenbildern

Diese Funktionen sind zusätzlich sichtbar und werden mitgeführt, auch wenn sie in den Hilfebildern nicht einzeln erläutert werden:

| ID | Funktion | Quelle | Abnahme |
| --- | --- | --- | --- |
| F42 | Neu / Seitenübersicht leeren; Schließen | `Infos/Action.jpg` | Übersicht wird kontrolliert geleert; Beenden respektiert F34. |
| F43 | Seiten löschen und alle auswählen | `Infos/Bearbeiten.jpg` | Nur gewählte Seiten werden gelöscht; „Alle auswählen“ umfasst die ganze Übersicht. |
| F44 | Optimale Breite, ganze Seite, Symbolansichten, Zoom-Befehle | `Infos/Ansicht.jpg` | Alle sichtbaren Ansichtsarten sind erreichbar, ohne Bilddaten zu verändern. |
| F45 | Hintergrundfarbe ändern | `Infos/Extra.jpg` | Farbe ist einstellbar; lesbare Auswahlmarkierungen und Systemkontrast bleiben berücksichtigt. |

Scannerprofile im Brother-Treiberdialog sind zunächst Treiberfunktionen. Dauerhafte Scanbereiche und Helligkeitsvorwahl der Anwendung sind dagegen ausdrücklich eigene Pflichtfunktionen F06/F07. Zusätzliche frei gestaltbare Anwendungsprofile werden nicht ohne Quelle als Altprogrammmerkmal behauptet.

## 4. Klärungsstand und verbleibende Test-/Umsetzungsdetails

Die angeforderten Ergänzungen wurden ausgewertet. Für „Scanbereiche bearbeiten“ und „Ausschneiden und Freistellen“ gibt es nach Nutzerangabe keine weitere Hilfe. Diese Seiten werden nicht erneut angefordert; Nutzerklärung und vorhandene Quellen sind Grundlage. Kleine verbleibende Details sind beim Umsetzen oder am Vorbild zu klären, ohne Funktionen zu streichen.

| ID | Stand | Verbleibende Frage / Maßnahme |
| --- | --- | --- |
| O01 | Scaneinstellungen und Bereichsverwaltung durch Hilfe9a/b geklärt; keine weitere Bereichshilfe gefunden. | Konkrete Eingabefelder/Einheiten und Standardwerte bei Bedarf anhand des Programms beziehungsweise mit gezielter Rückfrage festlegen. Anlegen/Bearbeiten/Löschen/Zurücksetzen/Filter sind verbindlich. |
| O02 | Speicherablauf, Automatik, PDF/A-1b und Formatoptionen durch Hilfe11a–c sowie Speichern11d–h geklärt. | Kurzen zusätzlichen TIFF-Codecnamen am vorhandenen Bild bestätigen; exakte Qualitäts-/Kompressionsbereiche, Resetwerte, Index-/Zeitmuster und Kollisionsregeln nur soweit belegt übernehmen, sonst vor Implementierung entscheiden. PDF-Qualität 100 und alle drei Seitenmodi sind bereits eindeutig. |
| O03 | Druck-/Maildialoge durch Hilfe12a und Hilfe14a/b dokumentiert. | Keine weitere allgemeine Dialoghilfe nötig; installierten MAPI-Client, Bitness und Druckverhalten testen. `mailto:` ersetzt keine Anhangübergabe. |
| O04 | Entfallen: Rückseiten einsortieren ist auf Nutzerwunsch kein Bedienablauf mehr. | Keine weitere Abnahme erforderlich. |
| O05 | Rechteck-Löschen und Freistellen durch Nutzerklärung und Beispielbild geklärt; keine weitere Dialoghilfe vorhanden. | Löschfüllfarbe, Übernahmeposition und Spiegelachsen bei Bedarf gezielt klären. Ob die allgemeine Aussage „automatisches Zuschneiden“ einen weiteren Ablauf meint, am Vorbild prüfen; keine eigenständige Motiverkennung als bereits belegtes Verhalten behaupten. |
| O06 | Bildimport bleibt laut Hilfe10 betriebssystem-/codecabhängig. | Unterstützte Formatmatrix und Mehrseiten-TIFF-Import testen; keine weitere allgemeine Hilfeseite benötigt. |
| O07 | Einrichtung, Erstzuordnung und Aktionen durch Hilfe7a–e geklärt. | Windows-Ereignisregistrierung für aktuelles Windows und tatsächliche Brother-Tastenunterstützung testen; historische XP/Vista/7-Schritte nicht wörtlich nachbauen. |
| O08 | Sichtbare Bedienwege liegen vor; einzelne Standardwerte und Kürzel sind nicht vollständig aufgelistet. | Vorhandene Menüs und Verhalten prüfen; nur tatsächlich relevante Restfragen vor Umsetzung entscheiden. |

## 5. Abdeckung aller Hilfebilder

| Hilfebild | Abdeckung / Einordnung |
| --- | --- |
| Hilfe.jpg | F01/F02, F08, F16, F25/F26/F32, F35/F36/F39; Professional ohne Werbehinweis. Fax ausgeschlossen. |
| Hilfe0.jpg | Inhaltsverzeichnis: Navigation und Vollständigkeitskontrolle; F15. |
| Hilfe1.jpg | F04–F07, F09, F12, F16/F17/F20/F21/F23, F27–F32. |
| Hilfe2.jpg | Historische Systemanforderungen, keine Pflicht zu Windows XP/Vista/7; aktuelle Windows-Zielplattform. Druck-/Mail-Voraussetzungen in F35/F39. Fax entfällt. |
| Hilfe3.jpg | Historische Kontaktdaten, keine nachzubauende Firmenidentität oder Bestellfunktion; aktuelle eigene Produktinfo gemäß F15. |
| Hilfe4.jpg | F14, F33/F34. |
| Hilfe5.jpg | F01. |
| Hilfe6.jpg | F07, F40/F41. |
| Hilfe7.jpg | F40/F41, O07. |
| Hilfe8.jpg | Ausschließlich Faxprogrammwahl: vollständig ausgeschlossen. |
| Hilfe9.jpg | F03/F36, O01. |
| Hilfe10.jpg | F16/F36, O06. |
| Hilfe11.jpg | F25–F32, O02. |
| Hilfe12.jpg | F35, O03. |
| Hilfe13.jpg | Ausschließlich Faxen: vollständig ausgeschlossen. |
| Hilfe14.jpg | F39, O03. |
| Hilfe15.jpg | F36–F38. |
| Hilfe16.jpg | F10–F13/F17; Fax-Toolbarziel entfällt. |
| Hilfe17.jpg | F17–F20, O04. |
| Hilfe18.jpg | F12/F21/F22, O05. |
| Hilfe19.jpg | F18/F23/F24. |
| Hilfe20.jpg | F39; historische Windows-Live-Mail-Empfehlung wird nicht übernommen. |
| Hilfe21.jpg | F40/F41 mit Treiberbedingung. |
| Hilfe22.jpg | F01 und TWAIN als Schnittstellengrundlage. |
| Hilfe23.jpg | F02, WIA/TWAIN-Brücke statt unbelegter nativer WIA-Implementierung. |
| Hilfe24.jpg | F15; veraltete Grewe-Supportadresse nicht als aktuelles eigenes Angebot übernehmen. |
| Hilfe7a.jpg | F40/F41: Gerätetastenverwaltung. |
| Hilfe7b.jpg | F40: Still-Image-Voraussetzung und Treibergrenzen. |
| Hilfe7c.jpg | F40/F41: Windows-Ereigniszuordnung und erster Tastendruck. |
| Hilfe7d.jpg | F41: neue Taste durch Programmende und Hardwaretastendruck zuordnen. |
| Hilfe7e.jpg | F41: Starten/Scannen/Kopieren/Ignorieren und Scaneinstellungen. Faxoptimierte Auflösung entfällt als Faxvoreinstellung. |
| Hilfe9a.jpg | F03/F07: Scanmodus, Auflösung, Bereich, temporäre Helligkeit, Papierzufuhr. |
| Hilfe9b.jpg | F06: Bereichsliste und Verwaltungsaktionen einschließlich Filter. |
| Hilfe11a.jpg | F25/F26/F32: Speicherablauf, Dateiformate, Metadaten, Zusammenfassen und PDF/A. |
| Hilfe11b.jpg | F27: sechs Varianten der Dateinamenautomatik. |
| Hilfe11c.jpg | F32: PDF/A-1b ausdrücklich bestätigt. |
| Hilfe12a.jpg | F35: vollständige Druckoptionen einschließlich PRN, Rotation und Sortieren. |
| Hilfe14a.jpg | F39: Anhänge, Formate, Zusammenfassen, PDF/A und Metadaten. |
| Hilfe14b.jpg | F39: MAPI-Voraussetzung, keine zusätzliche Pflicht zu historischem Mailprogramm. |
| Speichern11d.jpg | F25/F26: BMP unkomprimiert, ohne Formatoptionen. |
| Speichern11e.jpg | F25/F30: JPEG-Bildtypen, Qualitätswahl und Zurücksetzen. |
| Speichern11f.jpg | F28–F30/F32: Seitenmodi, PDF-Metadaten, Seitenübersicht und verlustfreie Qualität 100. |
| Speichern11g.jpg | F25/F30: PNG-Kompressionsfaktor ohne Qualitätsverlust. |
| Speichern11h.jpg | F26/F31: TIFF-Verfahren und optionale G3/G4-Invertierung. |

Zusätzlich wurden `Infos/Programm.jpg`, `Infos/Bearbeiten1.jpg` und `Infos/Bearbeiten2.jpg` berücksichtigt. Die aktuelle Nutzerklärung samt bereitgestelltem Beispielbild ist die Quelle für das genaue Rechteck-Löschen/Freistellen; dafür wird kein nicht vorhandener lokaler Bilddateiname vorausgesetzt.

## 6. Technische Konsequenzen und Gesamtabnahme

- Dokumentmodell und persistenter Arbeitsbereich müssen von Anfang an F34 tragen; reine flüchtige temporäre Dateien reichen nicht.
- Exportbibliotheken erst nach Nachweis für sechs Formate, Mehrseiten-TIFF, verlustfreie PDF-Bilder und das vereinbarte PDF/A-Profil auswählen. Ein PDF-Writer allein deckt den Umfang nicht ab.
- Für Druck/Kopie, MAPI und Gerätetasten eigene Adapter vorsehen. Lokale MAPI-Clientarchitektur und gegebenenfalls geeignete Prozessgrenze testen; x64-Hauptprogramm garantiert keine Kompatibilität mit jedem Mailclient.
- Die Konfiguration von Windows-Geräteereignissen kann getrennte Installations-/Registrierungsschritte benötigen; daraus folgt kein pauschaler Administratorzwang für den normalen Betrieb.
- Behalten/Löschen und Wiederherstellung sensibler Dokumente transparent gestalten, keine stillen dauerhaften Kopien nach aktivem Leeren.
- **Gesamtabnahme:** Alle F01–F45 sind implementiert und nachgewiesen oder als treiber-/gerätebedingt mit geeigneter Referenzhardware verifiziert. Offene O-Punkte sind vor Abnahme der betroffenen Funktion geklärt; fehlende Hardwareunterstützung darf nicht als erfolgreiche Funktionsprüfung gelten. Keine Faxfunktion bleibt in Oberfläche, Konfiguration oder Ereigniszuordnung.
- Offline-Texterkennung, Entzerrung, Leerseitenerkennung, PDF-Import und direkte Netzwerkprotokolle sind in der vorliegenden Hilfe nicht belegt. Sie sind keine stillschweigend vereinbarten Pflichtfunktionen und benötigen einen eigenen Erweiterungsauftrag.
