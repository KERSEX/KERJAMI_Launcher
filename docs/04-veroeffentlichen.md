# Ein Release veröffentlichen

Diese Anleitung führt von einem fertigen Stand zu einem Release des Launchers, das sich per Selbstupdate bei
den Nutzern installiert. Die Entscheidungen dahinter stehen in
[03-entscheidungen.md](03-entscheidungen.md), D-39 bis D-46.

## Was ein Release ist

Ein GitHub-Release im Repository `KERSEX/KERJAMI_Launcher` mit dem Tag `v<Version>` (zum Beispiel `v0.1.0`, kein
Pre-Release) und zwei Dateien:

| Datei | Inhalt |
|---|---|
| `KERJAMI-<Version>-Windows-x64-Portable.zip` | `KERJAMI_Launcher.exe` und `update-manifest.json` |
| `KERJAMI-<Version>-Windows-x64-Portable.zip.sha256` | eine Zeile `<SHA-256>  <Dateiname>` |

Das ZIP ist zugleich der Download für neue Nutzer (entpacken, `KERJAMI_Launcher.exe` starten) und das Paket, das
sich ein vorhandener Launcher selbst installiert. Die Prüfsummendatei braucht der Launcher, wenn GitHub für das ZIP
keinen eigenen Hashwert ausweist; mit beiden Dateien ist ein Release immer prüfbar.

## Schritt für Schritt (von Hand)

1. **Version erhöhen.** In `CMakeLists.txt` die Zahl in `project(… VERSION x.y.z …)` ändern. Das Paketskript liest
   die Version aus dem gebauten Programm, das Paket kann also nichts anderes behaupten als das Programm.

2. **Paket bauen und prüfen:**

   ```powershell
   .\scripts\package-portable.ps1 -Test
   ```

   Das Skript baut, lässt alle Tests laufen, stellt die Dateien zusammen, startet den Selbsttest des Programms,
   schreibt das Manifest, packt das ZIP, schreibt die Prüfsumme und prüft das Ergebnis mit
   `verify-package.ps1` (zwölf Prüfungen, darunter: keine Spur des Erstellers im Paket, und der Launcher nimmt das
   ZIP selbst als Update-Paket an). Es bricht bei jedem Fehler ab. Das Ergebnis liegt in `dist\`.

   Ein vorhandenes ZIP prüft man allein mit:

   ```powershell
   .\scripts\verify-package.ps1 -Zip dist\KERJAMI-0.1.0-Windows-x64-Portable.zip
   ```

3. **Auf einem sauberen Rechner ansehen**, am besten in der Windows-Sandbox oder auf einem Rechner ohne
   Entwicklungsumgebung: ZIP entpacken, `KERJAMI_Launcher.exe` starten, den Store öffnen. Die Skripte zeigen,
   dass das Programm startfähig und portabel ist; ein Blick auf die Oberfläche ersetzen sie nicht.

4. **Release anlegen** auf GitHub: Tag `v<Version>` (muss genau die Version des Programms nennen), die beiden
   Dateien aus `dist\` als Assets hochladen, Beschreibung als Changelog schreiben (sie erscheint den Nutzern in der
   Rückfrage vor dem Update). Es darf nur **eine** Datei zu `KERJAMI-*-Windows-x64-Portable.zip` passen (D-23).

5. **Prüfen, dass das Selbstupdate es findet:** Ein installierter Launcher mit der Vorversion sucht drei Sekunden
   nach dem Start und dann im Prüfintervall nach Updates, zeigt das Update auf dem Dashboard und in den
   Einstellungen und installiert es nach „Aktualisieren“. Das ist der erste echte Test gegen GitHub, denn bisher
   lief das Selbstupdate nur gegen einen lokalen Server (D-41).

## Ein neues Projekt freigeben

Die Projektliste steckt im Launcher (D-43). Einen Eintrag ergänzt man in `resources/registry/projects.json`
(Felder: [registry/README.md](registry/README.md)), baut und testet (`RegistryManagerTests` verlangt, dass kein
Eintrag verworfen wird) und veröffentlicht ein neues Release des Launchers. Wer das Update installiert, sieht das
Projekt im Store.

Voraussetzungen im Repository des Projekts: ein Release (kein Pre-Release), dessen Tag eine Version enthält
(`v1.2.0`, oder mit `tag_prefix` etwa `KERS_SubsystemsV0.3.1`), und eine Datei, die zum `asset_pattern` passt:
ein ZIP, oder die `.exe` selbst. GitHub berechnet für neu hochgeladene Dateien einen SHA-256-Wert (`digest`); ohne
ihn oder eine Prüfsummendatei im Release wird nichts installiert (D-28).

## Mit GitHub Actions

`.github/workflows/release.yml` macht die Schritte 1 (Prüfung), 2 und einen Entwurf von 4 selbst, wenn man einen Tag
`v<Version>` pusht. **Der Workflow ist noch nie gelaufen** (D-46): Er baut mit MSVC und einem geteilten Qt, die
Entwicklung läuft mit llvm-mingw und einem statischen Qt. Beim ersten Lauf ist mit Anpassungen zu rechnen. Er legt
nur einen Entwurf an, den man ansehen und selbst veröffentlichen muss.

## Vor dem ersten öffentlichen Release

- **E-01** entscheiden: Compiler und Qt-Variante, und damit die Lizenzfrage (LGPL, statisches Linken). Das Paket
  enthält keine Lizenztexte; das muss geklärt sein, bevor jemand es herunterlädt.
- Eine **Signatur** der EXE erwägen: Ohne sie warnt Windows SmartScreen vor neuen Downloads, und Virenscanner
  sind misstrauischer (ein früherer Fehlalarm, D-40).
- Die Workflows einmal laufen lassen, oder von Hand veröffentlichen.
- Die Projektliste vervollständigen (E-07): KERS Overlay und Tankstellenmeal sind eingetragen, die anderen vier
  Projekte haben noch kein Release.
