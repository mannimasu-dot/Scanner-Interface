# Eingebundene Fremdkomponenten

## zlib 1.4

- Quelle: https://zlib.net/zlib14.zip
- SHA-256 des lokal verwendeten Archivs: `38BC7ED1C8AC3C870A159623DEACF74A46C393AE1AD23F1DCA1ED8E272DEB058`.
- Die übernommenen Dateien unter `zlib/` sind unveränderte Auszüge aus dem Archiv. Der Binärvergleich gegen das entpackte Archiv ergab keine geänderte Kopie.
- Vollständige Lizenz: [zlib/LICENSE](zlib/LICENSE), zusätzliche Autoren-/Herkunftsangaben stehen in den jeweiligen Quelldateien und `zlib/README`.
- Es wird nur der benötigte Kompressions-/CRC-Teil statisch eingebunden. Keine zusätzliche zlib-DLL ist für den Programmlauf erforderlich.

## ICC-sRGB2014-Profil

- Unveränderte Datei: `icc/sRGB2014.icc`, 3024 Bytes, ICC v2 RGB.
- Herausgeber: International Color Consortium (ICC).
- Offizielle Downloadadresse: https://registry.color.org/rgb-registry/profiles/sRGB2014.icc
- SHA-256: `384B832DE3412066743B52A75EE906B6FB9FB8D9E09E936FC2C43223815C6E0A`.
- Der erneute Download von dieser Adresse war byteidentisch mit der eingebundenen Datei. Identifikation und Copyright-Tags bleiben unverändert erhalten.
- Das Profil wird als RCDATA in das Programm eingebettet und für den PDF-Farbraum und PDF/A-OutputIntent verwendet. Es wird nicht nachträglich aus dem Netzwerk geladen.

ICC-Lizenzbedingungen laut https://registry.color.org/profile-library/#license:

> This profile is made available by the International Color Consortium, and may be copied, distributed, embedded, made, used, and sold without restriction. Altered versions of this profile shall have the original identification and copyright information removed and shall not be misrepresented as the original profile.

## Tesseract OCR und PDF-Schrift

- Die unveränderte unsichtbare PDF-Schrift `tesseract/pdf.ttf` stammt aus https://github.com/tesseract-ocr/tesseract/blob/5.5.0/tessdata/pdf.ttf und wird als RCDATA eingebettet. Lizenz: Apache-2.0, siehe [tesseract/LICENSE](tesseract/LICENSE).
- OCR wird durch einen separaten lokalen Tesseract-4/5-Prozess ausgeführt. Tesseract steht unter Apache-2.0: https://github.com/tesseract-ocr/tesseract .
- Lokal vorbereitet ist der Windows-Build aus https://github.com/tesseract-ocr/tesseract/releases/download/5.5.3/tesseract-ocr-w64-setup-5.5.3.20260724.exe unter `artifacts\tools\tesseract`; er ist nicht als Quellbestandteil eingecheckt. Bei portabler Bereitstellung werden die DLLs, Sprachdaten und mitgelieferten `doc`-Lizenz-/Herkunftsdokumente unverändert mitkopiert. Für die abhängigen Bibliotheken gelten deren jeweilige Lizenzen.
- Deutsche und englische Sprachdaten: https://github.com/tesseract-ocr/tessdata_fast/tree/4.1.0 , Dateien `deu.traineddata` und `eng.traineddata`, Apache-2.0. Das Programm lädt zur Laufzeit keine Modelle aus dem Netz.

## Nur lokale Testwerkzeuge, nicht Bestandteil der Anwendung

veraPDF CLI und eine Java-Laufzeit liegen lokal unter `artifacts/tools` und werden nicht als Anwendungsbestandteil eingebunden oder durch dieses Repository weiterverteilt. Java wird nicht zum Scannen oder Exportieren benötigt.

Der verwendete veraPDF-CLI-Build ist 1.30.4. Die vorhandene Signatur des Installationsarchivs wurde mit dem Schlüssel-Fingerabdruck `13DD102B4DD69354D12DE5A83184863278B17FE7` mathematisch erfolgreich geprüft. Das ist keine zusätzliche Web-of-Trust-Bestätigung der Schlüsselidentität. Die lokale Java-Downloadmetadatei liegt unter `artifacts/tools/jre-provenance.json`.

Auf anderen Rechnern sind diese Prüfwerkzeuge getrennt aus ihren offiziellen Quellen bereitzustellen. `Scripts\Validate-PdfA.ps1` akzeptiert explizite Java-/veraPDF-Pfade und schlägt bei fehlenden Werkzeugen fehl, statt Konformität ungeprüft zu behaupten.
