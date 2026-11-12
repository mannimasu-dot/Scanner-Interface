# Copilot Instructions

## General Guidelines
- Nach Abschluss einer Aufgabe immer eine kurze Ergebniszusammenfassung geben und anschließend task_complete aufrufen; der Benutzer hat das wiederholte Auslassen ausdrücklich korrigiert. Eine Antwort ohne diesen Abschluss ist unvollständig.
- Bei längeren Arbeiten im Scanner-Projekt regelmäßig kurze Statusmeldungen geben. Abgebrochene Werkzeugaufrufe und fehlende Ergebnisse sofort mitteilen; lange Arbeitsblöcke in kurze, überprüfbare Schritte aufteilen.
- Auch eine abgeschlossene Übergabe an einen spezialisierten Agenten nach kurzer Zusammenfassung mit task_complete kennzeichnen; dabei nicht behaupten, die delegierte Fehlerbehebung sei bereits abgeschlossen.

## Projektrichtlinien
- Für den Nachbau im Repository Scanner ist TWAIN die primäre Scanneranbindung. Maßgebliche Funktionsspezifikation ist Infos/Hilfe; alle dort beschriebenen Funktionen sollen umgesetzt werden, ausgenommen Faxfunktionen. TWAIN bleibt der primäre Scannerweg ohne stillen WIA-Ersatz.
- Im Dialog „Ausschneiden und Freistellen“ markiert der Benutzer ein Rechteck. Ausschneiden löscht den Rechteckinhalt im bestehenden Bild; Freistellen übernimmt mit „Übernehmen“ nur den Rechteckbereich als Scan. Nicht mit dem Ausschneiden ganzer Seiten in die Zwischenablage verwechseln. Für Scanbereiche bearbeiten wurde keine weitere Hilfe gefunden.
- Auch reine Analyse- und Dokumentationsaufgaben nach einer kurzen Ergebniszusammenfassung ausdrücklich mit task_complete abschließen.