# Entscheidungen

Jeder Eintrag nennt die Entscheidung, den Grund und den Status. Offene Einträge brauchen eine
Antwort des Teams, bevor die genannte Phase beginnt.

## Offen

### E-01 Compiler und Qt-Variante

- **Frage:** Bei llvm-mingw mit statischem Qt bleiben, oder MSVC 2022 mit dynamischem Qt
  einrichten, wie im Auftrag festgelegt?
- **Stand:** Phase 1 ist mit llvm-mingw (clang 23) und statischem Qt 6.11.2 gebaut, weil nur
  das installiert ist. Dieses Qt stammt laut Team aus dem Projekt KERS Overlay. Womit Amil und
  Hakenzahn bauen und ob ihre Umgebung mit diesem Setup funktioniert, wird noch geklärt.
  Zur Qt-Lizenz (siehe E-06) weiß das Team es noch nicht (2026-11-10). Bis dahin wird statisch
  weitergebaut.
- **Für llvm-mingw statisch:** Das Paket besteht aus einer EXE-Datei ohne DLLs und
  Plugin-Ordner. `KERJAMI_Launcher.exe` importiert nur Windows-Systembibliotheken. Das Selbstupdate
  muss wenige Dateien tauschen. Keine Visual-C++-Laufzeit nötig.
- **Für MSVC:** entspricht dem Auftrag, Debugging in Visual Studio, vorgefertigte Qt-Binaries
  in GitHub Actions. Dafür rund 40 DLLs und Plugins im Paket, `windeployqt` und `qt.conf`.
- **Zu klären bei statischem Qt:** Qt steht unter der LGPL. Statisches Linken verlangt, dass
  Empfänger die Anwendung gegen ein geändertes Qt neu linken können (Quellcode oder
  Objektdateien bereitstellen). Bei einem öffentlichen Launcher-Repository ist das erfüllbar,
  sollte aber bewusst entschieden werden.
- **Betrifft:** Phase 8 (Paketierung, GitHub Actions). Die Phasen 2 bis 7 sind davon unabhängig.

### E-02 Testprojekt für den Ende-zu-Ende-Test

- **Stand:** `KERSEX/KERS_Overlay` ist laut Team nur ein Beispiel für ein Projekt, das später über
  den Launcher geladen wird (Einzel-EXE-Release, Tags wie `KERS_SubsystemsV0.3.1`). Es ist nicht
  das Testprojekt aus dem Auftrag.
- **Gebraucht:** Ein öffentliches Repository mit mindestens zwei ZIP-Releases (zum Beispiel 0.1.0
  und 0.2.0), damit Download, SHA-256, Installation, Update und Deinstallation echt getestet
  werden können. Vorschlag: ein kleines Testprojekt, etwa `KERSEX/KERJAMI_TestProject`, dessen
  ZIP eine winzige EXE enthält.
- **Bis dahin:** Alle Tests laufen gegen einen lokalen Server im Testprozess.
- **Stand 2026-11-11:** KERS Overlay ist seit D-44 der erste Eintrag der Liste und dient als echtes
  Testprojekt, allerdings mit einer **einzelnen EXE**, nicht mit einem ZIP. Mit Erlaubnis des Teams wurde die
  ganze Installation einmal echt ausgeführt (`LAUNCHER_LIVE_DOWNLOAD_TESTS=1`, siehe die Bekannten Grenzen):
  bestanden. **Nicht echt getestet** sind weiter: ein ZIP-Release (Archivprüfung und Entpacken gegen eine echte
  Datei), ein Update von einer Version auf die nächste und das Selbstupdate des Launchers. Dafür bräuchte es
  die Testprojekte von oben.
- **Betrifft:** Phase 4 und 5 (echter Test). Phase 2 und 3 sind nicht blockiert.

### E-03 Registry-Repository (erledigt: es gibt keines)

- **Entschieden (2026-11-11):** Die Projektliste ist Teil des Launchers und kommt mit seinen Updates
  (D-43). Es gibt kein Registry-Repository, keine Registry-Adresse und keinen Abruf der Liste.
- **Offen bleibt der Inhalt der Liste.** Jeder Eintrag braucht die Felder aus `docs/registry/README.md`,
  darunter den Pfad der Startdatei im ZIP, und das Projekt ein ZIP-Release (E-07).

### E-05 Helles Theme

- Die Einstellungen sehen einen Punkt „Theme“ vor, das Designsystem beschreibt nur ein dunkles
  Theme. Die Einstellung ist im Schema vorhanden (`appearance.theme`), die Seite zeigt sie
  noch nicht, weil eine Auswahl mit einem einzigen Eintrag keinen Nutzen hat.
- **Betrifft:** Phase 7.

### E-06 Was wird auf GitHub veröffentlicht?

- **Stand (2026-11-10):** Das Team will vorerst keine Dateien hochladen, nur Releases. Gemeint ist
  offenbar: kein Quelltext und keine Projektdateien in den Repositories, nur Release-Assets. Ob das
  für das Launcher-Repository, das Registry-Repository oder beide gilt, ist noch zu bestätigen
  (das Registry-Repository entfällt seit D-43).
  Bis dahin wird nichts hochgeladen.
- **Nachtrag (2026-11-10):** Das Team hat nochmals darüber gesprochen und wünscht einen Commit des
  gesamten Stands, besonders von Plan und Stand der Doku. Das ist ein lokaler Commit. Ob und wohin
  gepusht wird, ist nicht entschieden; bis dahin wird nichts zu GitHub übertragen. Offen bleiben die
  Frage zur Qt-Lizenz (die Registry ist erledigt, E-03).
- **Folgen, wenn der Quelltext nicht veröffentlicht wird:**
  - Das Selbstupdate (Phase 6) braucht nur Releases mit ZIP-Asset und ist nicht betroffen.
  - Statisches Linken gegen Qt (E-01) wird zum Lizenzproblem: Die LGPL verlangt, dass Empfänger die
    Anwendung gegen ein geändertes Qt neu linken können. Ohne veröffentlichten Quelltext oder
    Objektdateien bleiben dynamisches Linken mit mitgelieferten Qt-DLLs oder eine kommerzielle
    Qt-Lizenz. Das muss vor dem ersten Release geklärt werden (keine Rechtsberatung).
  - GitHub Actions (Phase 8) brauchen den Quelltext im Repository. Ohne ihn wird das Release lokal
    mit `package-portable.ps1` gebaut und von Hand hochgeladen.
- **Folgen für die Registry:** keine mehr, die Liste steckt im Launcher (D-43).
- **Betrifft:** Phase 6, Phase 8.

### E-07 Die echten Projekte des Teams und ihre Releases

- **Stand (2026-11-11).** Gelesen wurden nur die Release-Listen über die GitHub-API, nichts wurde geladen.

  | Repository | Releases | Dateien im Release |
  |---|---|---|
  | `Amil300/Tankstellenmeal` (Spiel, UE 5.8) | 4 | je ein ZIP mit SHA-256-Digest, z. B. `Tankstellenmeal_0.12.3.zip`, rund 395 MB |
  | `KERSEX/KERS_Overlay` | mindestens 5 | je eine einzelne `KERS_Subsystems.exe` mit Digest (56 MB, früher 232 MB) |
  | `KERSEX/KERIA-AI_HUB`, `KERSEX/KERS_Mods`, `KERSEX/SellAndRun`, `KERSEX/Karriereleiter` | keines | – |
  | `KERSEX/KERJAMI_Launcher` | keines | – |

- **Was der Launcher heute daraus macht:**
  - **Tags:** Eine Version ist `1.2.3` oder `v1.2.3` (D-21). `TankstellenmealV0.12.3` und
    `KERS_SubsystemsV0.3.1` gelten deshalb nicht als Version: Beide Projekte würden als „ohne Release“
    erscheinen. Bei Tankstellenmeal sind außerdem `TankstellenmealV0.12` (nur zwei Zahlen) und
    `TankstellenmealV012.2` (führende Null, gehört zur Datei 0.12.1) auch ohne Präfix keine gültige Version.
  - **Dateiform:** Tankstellenmeal passt (ZIP, Digest, 395 MB liegen unter dem Limit von 2 GiB je Datei). KERS
    Overlay war eine einzelne EXE (D-14) und geht seit D-44. Ohne Release lässt sich nichts installieren.
- **Tags: gelöst** durch das optionale Registry-Feld `tag_prefix` (D-42). Mit `"tag_prefix":
  "TankstellenmealV"` und `"KERS_SubsystemsV"` erkennt der Launcher die Releases der beiden Projekte. Bei
  Tankstellenmeal bleiben `TankstellenmealV0.12` und `TankstellenmealV012.2` auch damit ungültig; die neueste
  Version `0.12.3` wird gefunden.
- **Einzel-EXE-Releases: gelöst** durch D-44. **KERS Overlay ist der erste Eintrag der Projektliste**
  (`tag_prefix` `KERS_SubsystemsV`, Datei `KERS_Subsystems.exe`). Ein Live-Test gegen GitHub
  (`LAUNCHER_LIVE_TESTS=1`, nur Metadaten) findet Version 0.3.1, 56,2 MiB, Prüfsumme vorhanden.
- **Fehlt noch für einen echten Test von Tankstellenmeal:** der Pfad der Startdatei im ZIP (`executable`). Ihn
  kennt Amil oder das ZIP selbst; es wurde nicht geladen. Für die anderen vier Projekte braucht es zuerst ein
  Release mit ZIP.
- **Betrifft:** Inhalt der Projektliste (E-03, D-43), echte Tests (E-02), Phase 7 (Projektseiten).

## Getroffen

### D-01 Produktname nur in `cmake/Branding.cmake`

Produktname, Anzeigename, EXE-Name und GitHub-Koordinaten stehen in einer Datei.
Daraus entsteht `BrandingConfig.h`, die nur `Branding.cpp` einbindet. Bezeichner im Code sind
neutral (`launcher::…`, Ziel `launcher`, Log-Kategorie `launcher.*`). Eine Umbenennung ändert
eine Datei und kompiliert eine Übersetzungseinheit neu.

### D-02 Drei Build-Ziele statt eines

`launcher_core` (ohne UI), `launcher_ui` (Widgets) und die EXE. So bleibt der Kern frei von Widgets,
und Tests prüfen Logik ohne Fenster. Die Schichtentrennung aus dem Auftrag wird damit
vom Build-System erzwungen: `launcher_core` kann Widgets gar nicht einbinden.

Seit Phase 6 gibt es darunter noch `launcher_base` (nur Qt Core): Pfade, Logger, JSON, SemVer, Hash, Archivregeln,
Registry-Parser sowie Manifest, Transaktion und Auftrag des Updaters. `launcher_core` baut darauf auf
und fügt Netzwerk, Manager und Dienste hinzu. Der Austausch der Programmdateien beim Selbstupdate
(`LauncherUpdater`, eine Betriebsart von `KERJAMI_Launcher.exe`, D-40) liegt in `launcher_base`: Er braucht weder
Netzwerk noch Fenster.

### D-03 Kein `QSettings`, kein `QStandardPaths`

Beide führen aus dem Programmordner heraus (Registry, `%LOCALAPPDATA%`). Alle Orte kommen aus
dem `PathManager`, der nur das Verzeichnis der EXE kennt.

### D-04 Nicht beschreibbarer Ordner beendet den Start

`PathManager::prepare()` legt in jedem Datenordner und im Programmordner selbst eine Probedatei
an. Attribute und ACL-Abfragen sind unter Windows unzuverlässig, ein echter Schreibversuch
nicht. Schlägt er fehl, zeigt der Launcher eine Meldung mit dem betroffenen Ordner und beendet
sich mit Exitcode 2. Es gibt keinen Ausweichort.

### D-05 Eine Launcher-Instanz pro Ordner

`launcher/launcher.lock` wird für die Laufzeit gehalten (`QLockFile`). Eine zweite Instanz
meldet das und beendet sich. Eine übrig gebliebene Sperrdatei blockiert nicht:

- Gleicher Rechner: Qt erkennt, dass der Prozess nicht mehr läuft.
- Anderer Rechner (USB-Laufwerk nach Absturz): Die Prozess-ID sagt nichts aus. Windows hält die
  Datei einer laufenden Instanz geöffnet, deshalb gelingt das Löschen nur bei einem Überbleibsel.

### D-06 Logdatei hat genau einen schreibenden Prozess

Beim ersten Testlauf schrieb eine zweite Instanz in dieselbe `launcher.log`, und die erste
überschrieb deren Eintrag. Der Logger wird deshalb erst installiert, wenn die Instanz-Sperre
gehalten wird. Der Austausch beim Selbstupdate (D-40) bekommt eine eigene Datei (`updater.log`).

### D-07 Log-Rotation und Datenschutz

- `launcher.log` wird bei 1 MiB rotiert, vier ältere Dateien bleiben. Obergrenze rund 5 MiB.
- Jede Zeile wird sofort auf die Platte geschrieben, damit sie einen Absturz übersteht.
- Der Pfad des Programmordners wird in allen Meldungen durch `<root>` ersetzt. Der Ordner liegt
  oft unter dem Benutzerprofil, und der Benutzername gehört nicht in ein Log, das für Support
  weitergegeben wird.
- Debug-Ausgaben sind aus und lassen sich mit `QT_LOGGING_RULES="launcher.*.debug=true"`
  einschalten.

### D-08 Einstellungen: sofort, atomar, fehlertolerant

- Jeder Setter schreibt sofort über `QSaveFile` (temporäre Datei, dann Austausch).
- Beschädigte Datei: wird als `settings.corrupt-<Zeit>.json` aufbewahrt, Standardwerte gelten,
  der Nutzer wird informiert.
- Falscher Typ oder Wert außerhalb des Bereichs bei einem einzelnen Eintrag: nur dieser Eintrag
  fällt auf den Standard zurück.
- Ältere Schemaversion: Kopie als `settings.v<N>-<Zeit>.json`, dann Migration.
- Neuere Schemaversion (nach Downgrade): Datei wird als `settings.v<N>-<Zeit>.json` beiseite
  gelegt, eine saubere Datei entsteht. Ein unbekanntes Layout zu interpretieren könnte
  Einstellungen unbemerkt verändern.
- Unbekannte Schlüssel bleiben beim Speichern erhalten. Die Pro-Projekt-Einstellungen aus
  Phase 5 liegen in derselben Datei und dürfen nie durch eine Launcher-Einstellung verloren gehen.

Schema Version 1:

```json
{
    "schema_version": 1,
    "appearance": { "theme": "dark" },
    "startup": { "start_page": "dashboard" },
    "updates": { "check_interval_minutes": 60, "launcher_auto_check": true }
}
```

Das Prüfintervall ist auf 15 bis 1440 Minuten begrenzt. Die Untergrenze schützt das
GitHub-Limit von 60 Anfragen pro Stunde ohne Anmeldung.

### D-09 „Startverhalten“ bedeutet Startseite

Autostart mit Windows würde einen Registry-Eintrag oder eine Verknüpfung außerhalb des
Programmordners brauchen und widerspricht dem portablen Prinzip. Die Einstellung legt deshalb
fest, welche Seite nach dem Start erscheint.

### D-10 Icons aus der Windows-Symbolschrift

Dem Qt-Build fehlt QtSvg. Die Navigation zeichnet Symbole aus „Segoe Fluent Icons“
(Windows 11) oder „Segoe MDL2 Assets“ (Windows 10 und 11). Das skaliert verlustfrei mit jeder
DPI-Einstellung und braucht keine Bilddateien. Fehlt die Schrift, bleibt nur der Text stehen.

Das Logo der Marke und alle daraus abgeleiteten Icons beschreibt D-15.

### D-11 Fusion-Stil mit Style Sheet und Farb-Tokens

Fusion sieht unter Windows 10 und 11 gleich aus und verträgt sich mit Style Sheets. Die Farben
des Designsystems stehen einmal in `Theme.cpp`. `app.qss` enthält nur Tokens (`@accent`), die
beim Laden ersetzt werden. Selbst gezeichnete Controls (Navigation, Schalter) lesen dieselbe
Palette.

### D-12 UI-Texte auf Deutsch, Code und Logs auf Englisch

Die Oberfläche ist deutsch, alle Texte laufen durch `tr()` und sind damit später übersetzbar.
Quellcode, Kommentare und Logmeldungen sind englisch.

### D-13 Mausrad ändert keine Auswahllisten

Qt ändert den Wert einer Auswahlliste, wenn das Mausrad darüber gedreht wird. Beim Scrollen der
Einstellungsseite würden so unbemerkt Einstellungen verstellt. `ui::ComboBox` ignoriert das
Mausrad, die Seite scrollt weiter.

### D-14 Projekte werden als ZIP veröffentlicht (ergänzt durch D-44)

Version 1 installiert Projekte aus ZIP-Assets, wie im Auftrag beschrieben. Einzelne EXE-Assets,
wie sie KERS Overlay heute veröffentlicht, werden nicht unterstützt. Der Grund ist die
Installationskette (Archiv prüfen, in ein Staging-Verzeichnis entpacken, erwartete Dateistruktur
kontrollieren), die für eine einzelne Datei nichts zu prüfen hätte. Die Entscheidung lässt sich
später ändern: Das Registry-Schema (Phase 2) bekommt dafür ein erweiterbares Feld für den
Asset-Typ.

**Nachtrag 2026-11-11:** Das Team will, dass auch eine einzelne EXE als Release genügt (KERS Overlay). Das gilt
seit D-44; ein Asset-Feld war dafür nicht nötig, die Dateiendung des Musters sagt es.

### D-15 Logo und Icons

- Das Original (`resources/images/logo-source.png`, Logo auf schwarzem Grund) liegt im Repository.
- `scripts/make-icons.ps1` erzeugt daraus alles Weitere, die Ergebnisse sind eingecheckt:
  - `logo.png` und `emblem.png`: Helligkeit wird zu Deckkraft. Der schwarze Grund verschwindet,
    der Glanz des Logos bleibt auf jeder dunklen Fläche erhalten.
  - `app-16.png` bis `app-256.png`: schwarze, abgerundete Kachel mit dem Emblem. Die Kachel hält
    das weiße „K“ auf hellen Taskleisten lesbar. Jede Größe wird einzeln gerechnet.
  - `app.ico`: dieselben Größen für die EXE (klassische Bitmaps bis 128 px, PNG bei 256 px).
- Das Fenster- und Taskleisten-Icon, das Emblem in der Seitenleiste und das Logo im Dashboard
  kommen aus Qt-Ressourcen. Nur diese Dateien sind in der EXE eingebettet, Quelle und `.ico`
  nicht.
- `launcher.rc.in` bettet das Icon und die Versionsinformationen (Produktname, Version) in die EXE
  ein. Beides sieht man im Explorer und in der Taskleiste. Die Texte stammen aus
  `cmake/Branding.cmake`.
- **Umbenennung:** Das Logo enthält die Schrift „KERJAMI LAUNCHER“. Bei einem neuen Namen wird
  `logo-source.png` ersetzt und das Skript neu ausgeführt. Der Name im Quelltext ändert sich
  davon unabhängig über `Branding.cmake`.

### D-16 Die Registry ist unbeglaubigte Eingabe

Alles in `projects.json` wird wie Benutzereingabe behandelt. `RegistryParser` ist eine reine
Funktion ohne Netzwerk und Dateizugriff, damit sich jede Regel direkt testen lässt. (Seit D-43 steckt die
Datei im Programm. Die Prüfung bleibt: Ein Fehler in einem Eintrag kostet nur diesen Eintrag, und der Test
der eingebauten Liste findet ihn vor dem Release.)

- Ein Dokument mit kaputter Struktur wird als Ganzes abgelehnt: kein JSON, kein Objekt,
  `schema_version` fehlt oder ist kein positiver Ganzzahlwert, `projects` ist keine Liste, mehr als
  500 Einträge. Eine neuere `schema_version` gilt als „Launcher zu alt“ und wird ebenfalls abgelehnt.
- In einem gültigen Dokument steht jeder Eintrag für sich: ein ungültiger wird verworfen und
  protokolliert, die übrigen bleiben.
- **Doppelte `id` oder doppeltes `install_directory`** (Groß- und Kleinschreibung zählt nicht, auch
  gegen deaktivierte Einträge) verwerfen **alle** beteiligten Einträge. Welcher der echte ist, lässt
  sich nicht sagen, und zwei Projekte im selben Ordner würden sich gegenseitig überschreiben.
- Bezeichner und Dateinamen werden nie getrimmt oder korrigiert, sondern streng geprüft
  (`\A…\z`, nicht `^…$`, das einen Zeilenumbruch am Ende durchlassen würde).
- Anzeigetexte werden getrimmt; Steuer- und unsichtbare Formatzeichen (etwa die Umkehrung der
  Schreibrichtung) sind verboten.
- `install_directory` ist ein einzelner Ordnername ohne Pfad, ohne führenden oder abschließenden
  Punkt und kein reservierter Windows-Name (`CON`, `NUL`, `COM1`, auch mit Endung wie `con.txt`).
- `executable` ist ein relativer Pfad mit `/`, der auf `.exe` endet. Absolute Pfade, Laufwerke,
  `:`, `\`, `..`, leere Segmente und reservierte Namen sind verboten.
- Unbekannte Felder werden ignoriert, damit das Schema wachsen kann.
- Die Prüfregeln stehen auch einzeln zur Verfügung (`isValidInstallDirectory` und andere). Der
  Installer prüft ein Projekt in Phase 4 damit noch einmal, bevor er das Dateisystem anfasst.

Die Feldreferenz für die Pflegenden steht in `docs/registry/README.md`.

### D-17 Netzwerkzugriff nur über den GitHubClient

- **Policy statt Vertrauen:** Jede Anfrage und jeder Redirect wird gegen eine feste Liste geprüft:
  nur HTTPS, nur vier GitHub-Hosts, nur der Standardport, keine Zugangsdaten in der Adresse. Der
  Host muss exakt übereinstimmen (kein Präfix und kein Suffix).
- Redirects folgt der Client selbst (`ManualRedirectPolicy`), damit jede Station geprüft werden
  kann. Höchstens fünf. Der `Location`-Header wird als Rohtext gelesen, weil Qt für relative
  Adressen nichts liefert.
- **Wiederholungen:** höchstens drei Versuche, Pause 0,5 s und 1 s. Wiederholt wird bei
  Verbindungsproblemen, Timeouts, 408 und 5xx, nicht bei anderen 4xx und nicht bei TLS-Fehlern.
- **Rate Limit:** 429 sowie 403 mit `X-RateLimit-Remaining: 0` oder `Retry-After` gelten als
  „Limit erreicht“ und werden nicht wiederholt, denn jede Wiederholung verbraucht Budget. Das
  Budget aus den Headern ist über `GitHubClient::rateLimit()` abrufbar.
- **Grenzen:** Timeout bei 15 s ohne Daten, Antworten höchstens 4 MiB.
  Zu große Antworten werden schon während des Empfangs abgebrochen.
- Jede Anfrage trägt `User-Agent: <Produkt>/<Version>` (GitHub verlangt einen), keine Cookies,
  keine Anmeldung. Der Header `X-GitHub-Api-Version` geht nur an `api.github.com`.
- Das Ergebnis kommt immer asynchron, nie aus dem Aufruf von `get()` heraus, damit der Aufrufer
  vorher verbinden kann. Protokolliert werden Host und Pfad, nie die Parameter.
- Die Policy ist nur im Code austauschbar (Konstruktorparameter). Tests setzen damit einen Server
  auf dem eigenen Rechner ein. Keine Einstellung und keine Datei kann sie ändern.

### D-18 Cache und Offline-Verhalten der Registry (entfallen)

Entfallen mit D-43. Die Liste kommt nicht mehr aus dem Netz: Es gibt keinen Cache
(`cache/registry.json`), keine Prüfung gegen eine Adresse und keinen Zustand „offline“ für die Liste.
Die Bibliothek zeigt immer die Liste, die im Programm steckt.

### D-19 Zeitplan der Registry-Abfrage (entfallen)

Entfallen mit D-43: kein Abruf beim Start, kein Prüfintervall für die Liste, kein Knopf „Aktualisieren“
in der Bibliothek, keine Einstellung „Registry automatisch prüfen“. Das „Prüfintervall“ der Einstellungen
gilt weiter für die Updates der Projekte und des Launchers.

### D-20 Bibliothek

- Suche (Name, Beschreibung, Kategorie, Tags, ohne Beachtung der Groß- und Kleinschreibung),
  Kategorie, Tag und „Nur Empfohlene“ wirken zusammen. Empfohlene Projekte stehen zuerst, danach
  alphabetisch.
- Die Filterlisten enthalten, was die Projektliste hergibt, ohne Doppelte und sortiert.
- Tags erscheinen als Chips in einem Fluss-Layout, das bei wenig Platz umbricht.
- Zustände: Liste mit Anzahl („4 Projekte“), leere Liste („Noch keine Projekte freigegeben“), nicht lesbare
  Liste (nur bei einem kaputten Build), keine Treffer mit „Filter zurücksetzen“. Seit D-43 gibt es kein
  Laden, kein Offline und keinen Knopf „Aktualisieren“.
- Installationsstatus, Versionen und Aktionen auf den Karten kommen mit den Phasen 3 bis 5, die
  Detailseite mit Phase 7.

### D-21 Versionen und Tags

- Versionen folgen Semantic Versioning 2.0.0 streng, mit einem optionalen `v` oder `V` davor (so
  heißen Git-Tags). Andere Präfixe wie `KERS_SubsystemsV0.3.1` gelten nur, wenn der Registry-Eintrag sie als
  `tag_prefix` nennt (D-42).
- Führende Nullen (`01.2.3`), Zahlen mit mehr als neun Stellen, Leerzeichen und Zeilenumbrüche
  machen eine Version ungültig.
- Reihenfolge nach der Spezifikation: erst Haupt-, Neben- und Patch-Zahl als Zahlen
  (`1.9.0 < 1.10.0`), eine Vorabversion steht unter der Version, zu der sie führt
  (`1.0.0-rc.1 < 1.0.0`), Build-Metadaten (`+…`) zählen nicht. Die Beispielreihe der Spezifikation
  ist als Test hinterlegt.
- Ein Release mit Vorabversion im Tag (`v1.0.0-beta.1`) ist nicht stabil, auch wenn GitHub es nicht
  als Pre-Release markiert hat.

### D-22 Abfrage der Releases

- Eine Anfrage `GET /repos/<Owner>/<Repo>/releases?per_page=30` je Projekt. `/releases/latest` ist
  ungeeignet: Es wählt nach Datum und nicht nach Version, ein später veröffentlichter Hotfix einer
  alten Version würde als neueste Version gelten.
- Das neueste stabile Release ist das mit der **höchsten Version** in der Antwort, nicht das erste.
  Bei gleicher Version gewinnt der erste Eintrag.
- Ausgewertet werden die ersten 30 Releases. Ist darunter kein stabiles, gilt das Projekt als „ohne
  Release“.
- Das Budget von 60 Anfragen pro Stunde ohne Anmeldung reicht nicht für die ganze Registry. Es wird
  deshalb nur bei Bedarf abgefragt: für ein geöffnetes Projekt und für installierte Projekte bei der
  Update-Prüfung, nie für alle 500 auf einmal.
- Owner, Repository, Projekt-ID und Asset-Muster werden vor dem Bau der Adresse erneut geprüft,
  gleichgültig, woher das Projekt kommt. Ungültige Projekte lösen keine Anfrage aus.
- Release-Notes werden als Klartext (Markdown-Quelltext) übernommen, höchstens 20.000 Zeichen. Es
  wird kein HTML erzeugt.
- Beobachtung: Die Liste von 30 Releases eines Projekts mit ausführlichen Notes ist rund 400 KB groß.
  Das Limit für diese Antwort liegt bei 4 MiB.

### D-23 Auswahl der Datei und Prüfsumme

- Im neuesten stabilen Release muss **genau eine** Datei zum `asset_pattern` passen. Keine Datei:
  „keine passende Datei“. Mehrere: „nicht eindeutig“. Es wird nicht geraten.
- Fehlt die Datei im neuesten Release, wird **nicht** auf ein älteres zurückgegriffen. Ein Release,
  dessen Dateien noch hochgeladen werden, würde sonst eine alte Version als Update anbieten.
- Eine Datei zählt nur, wenn sie fertig hochgeladen ist (`state: uploaded`), einen sicheren Dateinamen
  hat (kein Pfad, kein reservierter Windows-Name, höchstens 128 Zeichen) und zwischen 1 Byte und
  2 GiB groß ist. Unbrauchbare Dateien fallen einzeln heraus, das Release bleibt.
- Das Muster kennt nur `*`. Alles andere, auch `?` und `[`, gilt wörtlich. Groß- und Kleinschreibung
  wird nicht unterschieden.
- **Die Download-Adresse wird gebaut**, nicht übernommen: `https://github.com/<Owner>/<Repo>/
  releases/download/<Tag>/<Datei>` aus geprüften Teilen. Die Adresse, die die API-Antwort nennt, wird
  ignoriert (ein Test lässt sie auf einen fremden Host zeigen).
- **Prüfsumme:** Der Digest `sha256:<64 Hexzeichen>` der API wird als Kleinbuchstaben übernommen. Jede
  andere Form zählt als „kein Digest“. Beobachtung: Dateien, die vor der Einführung der Digests
  hochgeladen wurden, haben keinen (so auch das aktuelle Release von `nlohmann/json`). Ein solches
  Asset lässt sich nicht prüfen. Phase 4 entscheidet, was daraus folgt (Vorgabe: nicht installieren).
  Als zweite Quelle ist eine Prüfsummendatei `<Datei>.sha256` im selben Release vorgesehen.

### D-24 Ablauf und Regeln der Downloads

- Ein Download wird in `temp/download-<id>.part` geschrieben, auf genau die angekündigte Größe
  geprüft und dann nach `downloads/<Projekt-ID>/<Datei>` verschoben. Eine ältere Datei gleichen
  Namens wird ersetzt.
- Bei Fehler oder Abbruch bleibt keine Datei zurück. Beim Start werden übrig gebliebene
  `download-*.part` aus `temp/` gelöscht, andere Dateien dort nicht.
- Anfrage prüfen: Projekt-ID, Dateiname, Größe (1 Byte bis 2 GiB) und Adresse (URL-Policy). Eine
  ungültige Anfrage wird sofort zu einem fehlgeschlagenen Eintrag, ohne Netzwerkzugriff.
- Das Limit der Antwort ist die angekündigte Größe. Sendet der Server mehr, wird abgebrochen
  („größer als angekündigt“), sendet er weniger, scheitert die Größenprüfung.
- Vor dem Start muss auf dem Laufwerk Platz für die Datei plus 16 MiB sein, sonst scheitert der
  Download sofort mit „nicht genug Speicherplatz“. Kann der freie Platz nicht ermittelt werden, wird
  nicht geprüft.
- Standardmäßig laufen zwei Downloads gleichzeitig, weitere warten in der Reihenfolge des Eintreffens.
  Dieselbe Datei desselben Projekts wird nicht doppelt gestartet: Der zweite Aufruf liefert den
  laufenden Eintrag zurück.
- **Abbrechen ist kein Fehler.** Ein abgebrochener Download meldet weder `finished` noch `failed`.
- Eine unterbrochene Übertragung beginnt beim nächsten Versuch von vorn. Fortsetzen (HTTP Range)
  gibt es nicht.
- Die Datei liegt nach dem Download ungeprüft in `downloads/`. Sie wird weder gestartet noch
  entpackt, bevor Phase 4 die SHA-256-Prüfung bestanden hat.
- Die Liste der Downloads hält höchstens 100 beendete Einträge und gilt nur für die laufende Sitzung.

### D-25 Fortschritt und Geschwindigkeit

- **Beobachtung:** Qt meldet `downloadProgress` bei einer stockenden Übertragung erst Sekunden nach
  dem Eintreffen der Daten, im Test erst beim Timeout nach zwei Sekunden, obwohl `readyRead` die Daten
  nach 67 ms lieferte. Darauf ist keine Anzeige zu bauen.
- Der Fortschritt zählt deshalb die Bytes, die der Client selbst in die Datei schreibt, und kennt die
  Gesamtgröße aus dem angekündigten Content-Length.
- Das Tempo ist ein geglätteter Mittelwert (30 % neue Messung, 70 % bisheriger Wert), gemessen in
  Abständen von mindestens 250 ms. Ein Zurückspringen der Bytezahl (neuer Versuch) startet die
  Messung neu. Die Restzeit ergibt sich aus Tempo und Rest und wird aufgerundet.
- Die Oberfläche bekommt Fortschritt höchstens etwa zehnmal pro Sekunde und Download.

### D-26 Downloads-Seite

Zwei Bereiche: „Aktiv“ (Warteschlange und laufende Downloads, mit Fortschrittsbalken, Tempo,
Restzeit und „Abbrechen“) und „Verlauf“ (neueste zuerst, mit „Verlauf leeren“). Fehlertexte nennen
den Grund in Klartext („Keine Verbindung“, „Nicht genug freier Speicherplatz“, „Die Datei wurde nicht
gefunden (HTTP 404)“ und so weiter). Ohne Einträge zeigt die Seite einen Leerzustand. Downloads
startet der Knopf „Installieren“ der Projektkarte (D-34); die Seite selbst startet keine.

### D-27 ZIP-Verarbeitung ohne fremde Bibliothek (vorher E-04)

- **Entscheidung:** Ein eigener, strenger ZIP-Leser (`security/ZipArchive`) auf dem zlib, das in Qt
  steckt (`Qt6::ZlibPrivate`). Weder libarchive noch QuaZip kommen ins Projekt.
- **Grund:** Keine Fremdquellen herunterladen und bauen, keine zusätzliche Lizenz und Abhängigkeit,
  und jede Prüfung aus dem Auftrag (Pfade, Größen, Überlappung, Widersprüche) sitzt an einer Stelle,
  die wir kennen. Ein Archivformat mit 20 Jahren Ausnahmen wird nicht nachgebaut: der Leser versteht
  das gewöhnliche ZIP mit „gespeichert“ und „deflate“ und lehnt alles andere ab.
- **Nicht unterstützt (wird mit klarer Meldung abgelehnt):** ZIP64, Verschlüsselung, andere
  Kompressionsverfahren, mehrteilige Archive, Namen mit Nicht-ASCII-Zeichen ohne UTF-8-Kennzeichen
  (die Kodierung wäre geraten, und zwei Programme sähen zwei Namen), Archive mit mehr als 65.534
  Einträgen, ein Verzeichnis über 16 MiB. Gewöhnliche Programm-ZIPs unter 2 GiB sind davon nicht
  betroffen. Wer ein Projekt veröffentlicht, packt es ohne ZIP64 und mit UTF-8-Namen.
- **Strenge des Lesers:** Das Ende-Verzeichnis muss genau am Dateiende stehen und das Verzeichnis
  genau davor. Jeder lokale Header muss Name, Verfahren und (ohne Datenbeschreibung) Größen und
  CRC des Verzeichniseintrags wiederholen. Einträge dürfen sich nicht überlappen, nicht ins
  Verzeichnis hineinragen und nicht außerhalb der Datei liegen. Namen und Typ-Kennzeichnung müssen
  zusammenpassen. Beim Entpacken werden nie mehr Bytes geschrieben, als der Eintrag angibt (kein
  Platzfüller durch eine Bombe); Größe, überzählige komprimierte Bytes und CRC-32 werden geprüft.
- **Risiko:** `ZlibPrivate` ist ein privates Qt-Modul. Das gebündelte zlib ist Teil der statischen
  Qt-Bibliothek und ändert sich nur mit der Qt-Version, die fest verwendet wird.

### D-28 Prüfsumme: Quelle, Pflicht und Grenzen

- Quellen in dieser Reihenfolge: der Digest, den GitHub für das Asset meldet; sonst eine Datei
  `<Asset>.sha256` oder `<Asset>.sha256sum` desselben Releases; sonst `SHA256SUMS` oder
  `SHA256SUMS.txt`. Die Namen werden ohne Beachtung der Groß-/Kleinschreibung verglichen. Gibt es in
  einer Gruppe mehr als einen Kandidaten, wird keiner gewählt und die nächste Gruppe nicht
  versucht: Wer rät, welche Prüfsumme gemeint ist, prüft nichts.
- Eine Datei nur für dieses Asset darf nur den Hash enthalten. Eine Sammeldatei muss das Asset
  mit Namen nennen. Zeilen im Format von `sha256sum` (`<hash>  <name>`, `<hash> *<name>`) und `#`-Kommentare
  sind erlaubt. Eine Zeile, die keine Prüfsummenzeile ist, entwertet die ganze Datei.
- **Ohne Prüfsumme wird nichts installiert, und nichts heruntergeladen.** Der Dienst bricht vor dem
  Download ab. Es gibt keinen Schalter „trotzdem installieren“: Ein Launcher, der Programme aus dem
  Internet holt, soll das nicht anbieten. (Der Auftrag lässt das als einstellbare Ausnahme zu; sie
  wird in Version 1 nicht gebaut und kann später nachgerüstet werden.)
- Der **Installer** prüft den Hash der Datei, die er entpackt, selbst noch einmal und verlässt sich
  nicht auf den Download. So hilft die Prüfung auch, wenn die Datei zwischen Download und
  Installation verändert wurde.
- **Was die Prüfsumme leistet:** Digest und Prüfsummendatei kommen vom selben GitHub-Release wie die
  Datei. Sie schützen vor beschädigten und abgeschnittenen Dateien und vor Fehlern auf dem
  Übertragungsweg. Gegen ein übernommenes Konto oder Repository helfen sie nicht; dagegen
  hülfen nur Signaturen. Das steht hier, damit niemand mehr in die Prüfsumme hineinliest.

### D-29 Archivregeln

Ein Archiv wird als Ganzes angenommen oder abgelehnt. Der Launcher entpackt nie „den guten Teil“.
Der `ArchiveValidator` prüft alle Einträge, bevor die erste Datei geschrieben wird:

- **Pfade:** Rückwärts-Schrägstriche werden zu `/` und danach beurteilt (ein Archiv aus PowerShell
  5.1 ist so nutzbar, `..\..\x` aber trotzdem verboten). Abgelehnt werden: leere Namen, absolute Pfade,
  Laufwerksangaben, UNC- und Geräte-Pfade, `.` und `..`, leere Segmente, Segmente mit Punkt oder Leerzeichen
  am Ende, Doppelpunkt (Laufwerk und alternative Datenströme), die Zeichen `< > " | ? *`, Steuer-,
  Format- und unsichtbare Zeichen (etwa die Umkehr der Schreibrichtung, mit der ein Name „exe.txt“
  vortäuscht), nicht gepaarte Surrogate, reservierte Windows-Namen (`CON`, `NUL`, `COM1`, auch mit
  Endung), Segmente über 128 Zeichen und Ordner tiefer als 24 Ebenen.
- **Pfadlänge:** Das Budget ergibt sich aus Windows' 259 Zeichen minus dem längeren der beiden Ziele
  (Staging und `current/`) und höchstens 200. Liegt der Programmordner so tief, dass fast nichts
  bleibt, meldet der Installer das, bevor er etwas schreibt.
- **Kollisionen:** Zwei Namen, die sich nur durch Groß-/Kleinschreibung unterscheiden, sind verboten
  (NTFS unterscheidet nicht), ebenso ein Name, der zugleich Datei und Ordner ist.
- **Typen:** Symbolische Links und Sonderdateien (Gerät, Pipe) werden abgelehnt. Die Kennzeichnung
  steht im Unix-Modus des Eintrags; ein Archiv kann einen Link nur so ausdrücken.
- **Grenzen:** 20.000 Einträge, 2 GiB je Datei, 4 GiB gesamt, Kompression höchstens 500:1 für Dateien
  ab 4 MiB. Deflate kommt kaum über 1030:1; der Schutz vor einer Bombe sind die Größengrenzen und die
  Platzprüfung, das Verhältnis fängt nur Extremfälle.
- **Startdatei:** Die Datei aus `executable` der Registry muss im Archiv als Datei liegen, genau an
  diesem Pfad. Ein Archiv mit einem Oberordner (`Tool-1.2/Tool.exe`) wird nicht „zurechtgestutzt“;
  das Projekt trägt dann den vollen Pfad in die Registry ein oder packt anders.
- **Extraktor:** Er schreibt nur, was die Prüfung freigegeben hat, und prüft jedes Ziel noch einmal
  gegen den Zielordner. Dateien werden mit „darf nicht existieren“ angelegt, überschreiben also
  nichts. Liegt ein Link im Weg, bricht er ab.

### D-30 Installation als Transaktion

Ablauf im Installer, in einem Hintergrund-Thread: Anfrage prüfen → läuft das Projekt? → SHA-256 →
Archiv öffnen und prüfen → Platz prüfen → nach `temp/staging-<Projekt>/` entpacken → Startdatei
prüfen → **erst jetzt** die installierte Version nach `backups/<Version>/` verschieben → Staging nach
`current/` verschieben → Eintrag in `launcher/launcher.json`.

- Ein Fehler bis zur Startdatei berührt die Bibliothek nicht; das Staging wird gelöscht.
- Ein Fehler beim Verschieben oder Eintragen stellt die bisherige Version wieder her
  (`current/` ← Backup). Scheitert auch das, meldet der Installer `RollbackFailed` mit dem Ort des
  Backups, statt zu raten.
- Verschieben ist ein Umbenennen auf demselben Laufwerk: ganz oder gar nicht. Dateien und Ordner im
  Programmordner dürfen nicht über Laufwerke verteilt sein; das ist durch die portable Struktur
  ausgeschlossen.
- **Laufendes Projekt:** Windows lässt einen Ordner mit einer geöffneten Datei nicht umbenennen. Das
  ist der Schutz „laufende Programme niemals überschreiben“: Die Installation meldet
  `ProjectRunning` und verändert nichts. Zusätzlich nimmt der Installer eine Abfrage „läuft das
  Projekt?“ entgegen, die Phase 5 mit dem Prozess-Manager füllt. Der Test hält eine Datei so offen
  wie ein laufendes Programm (ohne Löschfreigabe).
- Umbenennen wird fünfmal im Abstand von 200 ms versucht, weil ein Virenscanner oder die
  Windows-Suche eine frisch geschriebene Datei kurz festhalten können.
- **Backups:** Die zwei neuesten bleiben, ältere werden gelöscht. Dieselbe Version ein zweites Mal
  (Neuinstallation) und ein Ordner ohne Eintrag (`unknown`) bekommen einen Zeitstempel, damit nichts
  überschrieben wird. Benutzerdaten, die ein Projekt in `current/` anlegt, wandern bei einer
  Aktualisierung mit der alten Version ins Backup, **es sei denn, der Eintrag des Projekts nennt sie in `keep`
  (D-47)**: Dann werden sie in die neue Version übernommen. Alles andere in `current/` bleibt bei der alten
  Version. Projekte, die ihre Daten außerhalb ablegen (etwa in `%APPDATA%`), brauchen `keep` nicht.
- **Platz:** Entpackte Größe plus 16 MiB müssen auf dem Laufwerk von `temp/` frei sein.
- **Abbrechen:** bis zum Beginn des Austauschs möglich (Prüfsumme, Entpacken); danach nicht mehr.
- **Heruntergeladene Datei:** Nach jedem Versuch, gelungen oder nicht, löscht der Dienst das ZIP aus
  `downloads/`. Ein neuer Versuch lädt es neu. So wächst der portable Ordner nicht (USB-Stick), und
  eine geprüfte Datei bleibt nirgends liegen, der später ein anderer Hash zugeordnet werden könnte.
  Ausnahme: Ein vorgemerktes Update (D-37) behält seine geprüfte Datei, solange es auf das Ende des
  Projekts wartet. Was beim Beenden des Launchers noch in `downloads/<Projekt>/` liegt, löscht der
  nächste Start.
- Beim Start entfernt der Launcher übrig gebliebene `staging-*` und `uninstall-*` aus `temp/`
  (Absturz, Stromausfall). Die Instanz-Sperre stellt sicher, dass kein anderer Installer läuft.

### D-31 Liste der installierten Projekte (`launcher/launcher.json`)

- Schema 1: `{"schema_version":1, "projects": {"<id>": {version, tag, install_directory, executable,
  installed_at, last_started_at}}}`. Nur Namen und relative Pfade, nie absolute Pfade.
- Jeder Eintrag wird beim Laden wie fremde Eingabe geprüft (Projekt-ID, Ordnername, Startpfad,
  Version). Ein ungültiger Eintrag wird verworfen und protokolliert, die anderen bleiben. Eine
  handbearbeitete Datei kann den Launcher also nicht zum Löschen oder Starten außerhalb der
  Bibliothek bringen.
- Eine nicht lesbare Datei wird beiseite gelegt (`launcher.corrupt-<Zeit>.json`), die Liste beginnt
  leer, der Nutzer erfährt es. Die Projektdateien bleiben liegen; sie gelten als nicht installiert
  und werden bei einer Neuinstallation als Backup `unknown` gesichert, nicht gelöscht. Eine Datei
  einer neueren Version wird genauso behandelt (`launcher.newer-…`).
- Fremde Schlüssel in der Datei bleiben beim Speichern erhalten (Phase 5 und 6 hängen eigene an).
- Jede Änderung wird sofort atomar gespeichert. Schlägt das fehl, bleibt die Liste im Speicher wie
  zuvor. Alle Zugriffe sind thread-sicher, weil der Installer im Hintergrund einträgt.
- Die Dateien auf der Platte sind die Wahrheit, die Liste das Gedächtnis des Launchers. Wurde ein
  Projektordner von Hand gelöscht, merkt der Start „Datei fehlt“, die Bibliothek gleicht nicht ab.

### D-32 Deinstallation

- Nur der Ordner aus dem Eintrag der Liste wird entfernt (nicht der aus der Registry, die sich
  geändert haben kann), nach erneuter Prüfung des Namens, und nur wenn er direkt in der Bibliothek
  liegt und keine Verknüpfung ist (Link oder Junction).
- Der Ordner wird zuerst nach `temp/uninstall-<Projekt>/` verschoben. Das ist atomar und scheitert
  sofort, wenn das Projekt läuft. Danach entfällt der Eintrag in der Liste, und das Löschen läuft im
  Hintergrund. Was dabei liegen bleibt, räumt der nächste Start auf.
- Links im Projektordner werden entfernt, nie verfolgt (ein Junction auf `C:\Users` löscht
  `C:\Users` nicht). Schreibgeschützte Dateien werden freigegeben und gelöscht.
- Gelöscht werden `current/` **und** alle Backups des Projekts. Der Bestätigungsdialog sagt das und
  dass es nicht rückgängig zu machen ist. Downloads, Einstellungen und andere Projekte bleiben.
- Fehlt der Ordner schon (von Hand gelöscht), wird nur der Eintrag entfernt.

### D-33 Start eines Projekts

Gestartet wird nur die Startdatei aus dem Eintrag, aus `current/` des Projekts, ohne Verknüpfung im
Weg, mit `current/` als Arbeitsordner. Das Projekt läuft weiter, wenn der Launcher geschlossen
wird. Die Prüfung steht in `resolveStartTarget`. Seit Phase 5 startet der `ProcessManager` (D-35)
und kennt den Prozess danach; `startProject` (`QProcess::startDetached`, ohne Überwachung) bleibt
als einfacher Weg für Tests und Werkzeuge.

### D-34 Ablauf für den Nutzer: `InstallService` und Projektkarte

- Ein Knopf „Installieren“ führt die ganze Kette aus: neuestes Release, Prüfsumme (Digest oder
  Datei), Download, Prüfung und Installation. Pro Projekt läuft höchstens eine Operation.
- Die Karte zeigt „Nicht installiert“ und „Installieren“, oder „Installiert: Version x“ mit
  „Starten“ und „Deinstallieren“, oder während der Arbeit die Stufe (Release suchen, Prüfsumme
  lesen, Download mit Fortschritt, prüfen, entpacken, installieren) mit Balken und „Abbrechen“.
- Fehler stehen in Klartext auf der Karte, bis die nächste Operation beginnt. Der Text nennt, was
  passiert ist, und wo der Nutzer handeln kann („Beende das Projekt und versuche es erneut“).
  Technische Einzelheiten stehen im Log, nicht im Dialog.
- Abbrechen ist kein Fehler (keine rote Meldung). Während der Austausch läuft, ist der Knopf
  gesperrt.
- Deinstallieren fragt immer nach. Installieren nicht: Der Klick ist die Bestätigung. Die
  Bestätigung vor einer Aktualisierung steht in D-36.
- Der Dienst nutzt „Installieren“ auch zum Ersetzen der installierten Version; die Updates (D-36)
  rufen dieselbe Kette auf, im Modus „nur vorbereiten“ (D-37).

### D-35 Prozessüberwachung (`ProcessManager`)

- **Ein Projekt läuft,** wenn (a) ein vom Launcher gestartetes Programm dieses Projekts lebt, samt
  allem, was es selbst gestartet hat, oder (b) irgendein Prozess auf dem Rechner seine Programmdatei
  im `current/`-Ordner des Projekts hat. (a) trifft auch Hilfsprogramme, die woanders liegen. (b) trifft
  ein Projekt, das aus dem Explorer, über eine Verknüpfung oder von einem früheren Launcher-Lauf
  gestartet wurde. Deshalb gilt ein Projekt nie als geschlossen, nur weil das Launcher-Fenster zu ist.
- **Technik:** Der Start erzeugt den Prozess angehalten, steckt ihn in ein Job-Objekt (Windows führt
  darin die ganze Nachkommenschaft) und setzt ihn dann fort. `BREAKAWAY_OK` ist gesetzt, damit
  Programme, die sich absichtlich aus einem Job lösen (manche Updater), nicht scheitern; diese
  Nachkommen findet dann (b). Das Job-Objekt beendet nichts, wenn der Launcher schließt. (b) liest über
  `QueryFullProcessImageName` die Programmdatei aller Prozesse, die der Benutzer lesen darf.
  Verglichen wird auf den Ordner genau (`Tool/current/` trifft `Tool/current-old/` nicht), ohne
  Groß-/Kleinschreibung und mit aufgelösten Kurznamen (8.3).
- **Zwei Fragen:** `isRunning` antwortet aus dem Stand der letzten Abfrage (alle 2 Sekunden) für die
  Oberfläche. `isRunningNow` fragt sofort und ist aus jedem Thread erlaubt. Jede Entscheidung, die
  etwas verändert (Installation, Deinstallation, Update, zweiter Start), nutzt `isRunningNow`.
- **Mehrfachstart** wird abgelehnt (`AlreadyRunning`), auch wenn das erste Programm von außen
  gestartet wurde. Die Karte zeigt „Läuft“ und sperrt „Starten“ und „Deinstallieren“.
- **Unerwartetes Ende:** Beim vom Launcher gestarteten Programm kennt der Manager den Exitcode. Als
  unerwartet gilt ein Absturzcode (Windows setzt dafür die oberen Bits, ab `0xC0000000`) oder ein
  Fehlercode innerhalb von zehn Sekunden nach dem Start. Ein Fehlercode nach langer Laufzeit ist
  üblich und kein Grund zur Meldung. Die Karte zeigt die Meldung bis zum nächsten Start.
- **Grenzen, bewusst:**
  - Ein Hilfsprogramm, das außerhalb von `current/` liegt und von einem Programm gestartet wurde,
    das nicht der Launcher gestartet hat, bleibt unsichtbar.
  - Programme, die ein Starter über den Explorer („start“, ShellExecute) hochzieht, hängen nicht
    am Job; (b) sieht sie nur, wenn ihre Datei in `current/` liegt.
  - Prozesse, deren Programmdatei dieser Benutzer nicht lesen darf, sind unsichtbar.
  - Das letzte Netz bleibt die Dateisperre des Installers: Ein Ordner mit laufendem Programm lässt
    sich nicht ersetzen (D-30), die Aktualisierung scheitert dann mit klarer Meldung.
  - Die Abfrage listet alle Prozesse. Das kostet auf einem normalen Rechner wenige Millisekunden.

### D-36 Aktualisierung der Projekte (`ProjectUpdateManager`)

- **Was geprüft wird:** Nur installierte Projekte, die noch in der Registry stehen. Eine Anfrage an
  GitHub je Projekt, nacheinander. Nie wird ein Projekt installiert, weil es in der Registry auftaucht,
  auch dann nicht, wenn für seine ID „automatisch“ eingetragen ist: Ohne Eintrag in der Liste der
  Installationen passiert nichts.
- **Wann:** Drei Sekunden nach dem Start und dann im eingestellten Intervall (mindestens 15 Minuten,
  Voreinstellung 60). Ein Projekt wird höchstens einmal je Minute gefragt. Die Runde bricht ab, wenn GitHub
  nicht erreichbar ist oder das Limit erschöpft meldet. Sie wird auch übersprungen, wenn weniger als 10
  Anfragen plus eine je Projekt übrig sind (Limit: 60 pro Stunde ohne Anmeldung): Damit bleibt Platz,
  um ein Projekt noch zu installieren.
- **Version:** Ein Update ist ein echt größerer SemVer-Wert als die installierte Version. Gleich oder
  kleiner ist „aktuell“, nie ein Downgrade.
- **Netzwerkfehler:** Eine fehlgeschlagene Prüfung ändert den Stand nicht. Ein angekündigtes Update bleibt
  angekündigt, ein unbekannter Stand bleibt unbekannt (nicht „aktuell“). `lastCheckFailed` markiert es.
- **Zustände:** Unbekannt, Aktuell, Verfügbar, Wartet in der Warteschlange, Lädt, Prüft, Vorgemerkt
  („Update Pending“), Installiert, Fehlgeschlagen.
- **Manuell** (Voreinstellung für jedes Projekt): Die Karte zeigt „Update 2.0.0“ und „Aktualisieren“. Der
  Klick öffnet eine Rückfrage mit Version und Changelog (den Release-Notes als Text). Erst nach „Aktualisieren“
  wird geladen und installiert. Die Rückfrage deckt beides ab, was der Auftrag trennt („Download bewusst
  starten“, „Installation nach Bestätigung“): Sie sagt, dass geladen, geprüft und installiert wird und
  dass ein laufendes Projekt zuerst beendet sein muss.
- **Automatisch:** Das Update wird ohne Rückfrage geladen, geprüft und installiert, sobald das Projekt
  nicht läuft (D-37). Ein laufendes Projekt wird nie angefasst.
- **Fehler:** Das Update scheitert an Hash, Archiv, Platz oder Austausch, die installierte Version bleibt
  wie sie war (Rollback, D-30). Der Modus „automatisch“ versucht dieselbe Version nicht von allein erneut,
  damit ein kaputtes Release nicht stündlich einen Download auslöst. Die Karte zeigt den Grund und „Erneut
  versuchen“; eine neuere Version beginnt von vorn. Ausnahme: „Projekt in Benutzung“ ist nicht Schuld des
  Releases, die nächste Runde versucht es wieder.
- **Abbrechen** (Warteschlange, Download, Vormerkung) gilt als Entscheidung des Nutzers: Die Version bleibt
  angekündigt, der Modus „automatisch“ startet sie nicht noch einmal von selbst. „Aktualisieren“ geht
  jederzeit.
- **Moduswechsel:** „Automatisch“ einschalten startet ein bekanntes Update sofort. „Manuell“
  einschalten nimmt ein automatisch gestartetes Update zurück, das noch nicht mit dem Installieren
  begonnen hat.
- **Deinstallation** entfernt Zustand und Einstellung des Projekts (D-38).

### D-37 Vorgemerkte Updates und Warteschlange

- Ein Update läuft in zwei Schritten: Der Dienst lädt und prüft die Datei im Modus „nur vorbereiten“ (SHA-256
  in einem Hintergrund-Thread, ein schlechter Fund erscheint also sofort und nicht Stunden später). Dann
  entscheidet der Manager: Läuft das Projekt nicht, geht es sofort in den Installer. Läuft es, wird das
  Update vorgemerkt (Pending), die geprüfte Datei bleibt liegen.
- Sobald der ProcessManager „läuft nicht mehr“ meldet, fragt der Manager noch einmal frisch
  (`isRunningNow`) und installiert dann. Der Installer prüft die Hash-Summe der Datei beim Entpacken
  ein weiteres Mal (D-28).
- **Warteschlange:** Höchstens zwei Updates laden und installieren gleichzeitig; weitere warten in der
  Reihenfolge ihrer Erkennung („Wartet in der Warteschlange“). Das passt zu den zwei parallelen Downloads
  der Download-Verwaltung (D-24).
- **Nicht gespeichert:** Eine Vormerkung überlebt das Beenden des Launchers nicht. Sie wäre mit Datei,
  Hash und Zustand auf der Platte nur mit viel Aufwand sicher, und das Ergebnis ist ohne sie dasselbe:
  Im Modus „automatisch“ findet die nächste Prüfung das Update wieder, lädt es neu und installiert es,
  sobald das Projekt nicht läuft. Im Modus „manuell“ erscheint die Ankündigung erneut. Übrig gebliebene
  Dateien löscht der Start (D-30).
- Das Projekt wird nach einem Update nicht neu gestartet.

### D-38 Einstellungen pro Projekt

- In `launcher/settings.json`: `"projects": {"<id>": {"update_mode": "manual" | "automatic"}}`.
  Schema bleibt 1 (der Abschnitt ist neu und optional).
- Jedes Projekt ist „manual“, bis der Nutzer etwas anderes wählt. Ein unbekannter oder beschädigter
  Wert zählt als „manual“: Eine kaputte Datei schaltet nie Updates ein.
- Der Eintrag gehört zur Projekt-ID und hängt weder von der installierten Version noch vom Inhalt der
  Registry ab. Ein Projekt, das aus der Registry verschwindet und wiederkommt, behält seine Wahl.
  Jede Änderung wird sofort gespeichert (D-08).
- Ungültige IDs werden abgelehnt, höchstens 500 Einträge. Unbekannte Schlüssel in einem Eintrag und Einträge
  mit IDs, die dieses Programm nicht annimmt, bleiben beim Speichern erhalten.
- Beim Deinstallieren wird der Eintrag entfernt, ein leerer Abschnitt nicht mitgeschrieben.
- Die Wahl steht auf der Projektkarte der Bibliothek („Updates: Manuell / Automatisch“), nur bei
  installierten Projekten. Die Projekt-Detailseite folgt in Phase 7.

### D-39 Launcher-Release: ZIP, Manifest und Prüfung (`UpdatePackage`, `UpdateManifest`)

- **Asset:** `KERJAMI-<Version>-Windows-x64-Portable.zip`. Das Muster steht in `cmake/Branding.cmake`
  (`BRAND_UPDATE_ASSET_PATTERN`) und kommt über `branding::updateAssetPattern()` in den Code. Gewählt
  wird wie bei Projekten (D-23): genau eine passende Datei.
- **Inhalt:** Die Programmdateien liegen an der Wurzel des ZIPs, dazu `update-manifest.json`. Das Manifest
  (Schema 1) nennt `product`, `version`, `launcher`, `files` (`path`, `size`, `sha256`) und
  `remove` (Dateien früherer Versionen, die nicht mehr dazugehören). Nur Dateien aus `files` werden je
  geschrieben, nur Dateien aus `remove` je gelöscht. Was sonst im ZIP liegt, wird nicht installiert.
  Das Programm ist **eine einzige Datei**: das ZIP enthält `KERJAMI_Launcher.exe` und das Manifest. Unbekannte
  Schlüssel (auch ein `updater` aus einem frühen Entwurf) werden ignoriert.
- **Das Manifest ist unbeglaubigte Eingabe.** Der Parser verlangt: gleiches Produkt, die Version des
  Releases, die Programmdatei des Launchers in der Liste, 1 bis 200 Dateien, höchstens 200
  Einträge in `remove`, jede Datei höchstens 2 GiB, Pfade nach den Windows-Regeln der Archivprüfung
  (D-29) und keine doppelten Pfade, auch nicht, wenn sich nur die Groß-/Kleinschreibung unterscheidet.
  **Gesperrt** sind die Ordner mit Nutzerdaten (`launcher`, `library`, `downloads`, `temp`, `cache`,
  `logs`, `updater`) in der ganzen Tiefe und das Manifest selbst. Ein Manifest, das dort hineinzielt,
  wird abgelehnt, und zwar für `files` genauso wie für `remove`.
- **Prüfkette** (`UpdatePackage::prepare`, im Hintergrund-Thread, abbrechbar): SHA-256 des ZIPs → Archivregeln
  wie bei Projekten (D-29), das Manifest gilt als Pflichtdatei → Platz (Inhalt plus 16 MiB) und
  Pfadlänge (ein Pfad im ZIP darf zusammen mit dem längeren der Ordner von Staging und Programmordner
  höchstens 259 Zeichen haben; mindestens 16 Zeichen müssen für Pfade bleiben) → Entpacken nach
  `temp/update-<Version>/` → Manifest lesen → Version des Manifests gleich Version des Releases → jede Datei
  der Liste: vorhanden, kein Link, Größe und SHA-256 stimmen. Bei einem Fehler wird das ZIP aus `downloads/`
  und der Staging-Ordner entfernt.
- **Ohne Prüfsumme kein Download** (wie D-28): Quelle ist der Digest von GitHub oder genau eine
  Prüfsummendatei im Release. Fehlt beides, bleibt es bei der Meldung „keine Prüfsumme“.
- **Reservierte ID:** Die Abfrage des Launcher-Releases läuft über die Release-Verwaltung der Projekte
  (ein künstliches Projekt mit der ID `launcher`). Damit kein Registry-Eintrag mit dieser ID dazwischen
  kommen kann, lehnt der Registry-Parser sie ab (`RegistryParser::isReservedProjectId`).
- **Version:** Ein Update ist ein echt größerer SemVer-Wert als `branding::version()`. Vorabversionen
  werden wie bei Projekten ignoriert (D-21), ein Downgrade gibt es nicht.
- **Grund für ein Manifest statt „alles im ZIP ersetzen“:** Der Austausch darf nie raten, was zum Programm
  gehört. Die Liste mit Hashes macht den Austausch prüfbar (vorher und nachher) und schützt Nutzerdaten
  auch dann, wenn ein Release-ZIP einmal falsch gepackt wird.

### D-40 Der Austausch der Programmdateien (`--apply-update`) und die Transaktion

- **Kein zweites Programm.** Der Launcher ist die einzige EXE (Wunsch des Teams: eine Datei). Für den
  Austausch kopiert er sich selbst, die laufende `KERJAMI_Launcher.exe`, nach `temp/updater-<Version>/`, schreibt
  dort `job.json` und startet die Kopie getrennt mit `--apply-update --job job.json`. `main()` erkennt das
  Flag, bevor ein Fenster oder die Einzelinstanz-Sperre entsteht, und führt nur den Austausch aus
  (`LauncherUpdater::runProgram`: ein `QCoreApplication`, kein Fenster, kein Netzwerk). Eine laufende Datei
  lässt sich nicht ersetzen; die Kopie liegt woanders und kann es. Es ist die Kopie des *laufenden*
  Launchers, nicht die des neuen Releases: Dieser Code ist nachweislich lauffähig, auch wenn das neue
  Release defekt wäre. Der Code liegt in `launcher_base` (nur Qt Core, D-02). Eine frühere Fassung hatte
  eine eigene `LauncherUpdater.exe`; Windows Defender hat sie fälschlich als Schadsoftware gemeldet
  (Bekannte Grenzen), und das Team wollte ohnehin nur eine Datei.
- **Reihenfolge im Austausch:** Sperre `updater/updater.lock` → Log `logs/updater.log` → der Auftrag muss
  stimmen (Ordner vorhanden, Staging im Programmordner) → **warten, bis der Launcher beendet ist**
  (Handle auf die PID; läuft unter dieser PID ein anderes Programm als der Launcher, wird nicht gewartet,
  weil die PID wiederverwendet wurde; höchstens 60 Sekunden, einstellbar bis 10 Minuten; danach 300 ms
  Pause für Dateisperren und Virenscanner) → Manifest prüfen → Transaktion → Bericht → Launcher neu
  starten → Aufräumen. Das Warten steht vor jedem Urteil, damit der Launcher nie neu startet, solange der
  alte noch schließt.
- **Transaktion** (`UpdateTransaction::apply`): (1) alles prüfen, bevor etwas geändert wird: jede neue Datei
  gegen ihren Hash, jeder Zielpfad gegen die Pfadregeln, keine Links. (2) Das Journal
  `updater/journal.json` schreiben. (3) Jede zu ersetzende Datei nach `updater/backups/<alte Version>-<Zeit>/`
  verschieben und die neue an ihren Platz verschieben; Dateien mit gleichem Hash bleiben unangetastet,
  Dateien aus `remove` wandern ebenfalls ins Backup. **Die Programmdatei des Launchers kommt zuletzt:**
  Solange sie die alte ist, lässt sich die alte Version noch starten. (4) Alle platzierten Dateien noch einmal gegen die Hashes prüfen. (5) Das Journal löschen.
- **Fehler:** Scheitert ein Schritt, wird der alte Zustand wiederhergestellt (`RolledBack`, Exitcode 1).
  Gelingt das nicht vollständig (`RollbackFailed`, Exitcode 3), startet der Austausch den Launcher nicht
  und nennt den Backup-Ordner. Der Launcher beendet die Wiederherstellung beim nächsten Start selbst.
- **Absturz mittendrin:** Das Journal bleibt liegen. `UpdateTransaction::recover` entscheidet beim nächsten
  Start des Launchers anhand der Dateien selbst: sind alle neu, gilt das Update als abgeschlossen, sonst
  wird zurückgerollt. Getestet wird der Abbruch nach jedem einzelnen Schritt (`simulateCrashAfter`).
- **Bericht:** `updater/last-update.json` (Erfolg, zurückgerollt, nicht angewendet, fehlgeschlagen; Versionen,
  Detail, Backup-Ordner, ob der Launcher gestartet wurde). Der Launcher zeigt ihn beim nächsten Start einmal
  und löscht ihn.
- **Exitcodes:** 0 Erfolg, 1 zurückgerollt, 2 nichts geändert, 3 fehlgeschlagen, 4 ungültiger Auftrag.
  `--quiet` unterdrückt die Meldungsfenster (für Tests); ohne `--quiet` zeigt der Austausch bei Problemen
  ein Windows-Meldungsfenster.
- **Beim Start des Launchers** (`UpdateStartup::run`, vor allem anderen): Läuft noch ein Austausch (Sperre),
  wartet der Launcher bis zu 60 Sekunden; ist er dann nicht fertig, meldet er es und beendet sich, statt
  ein zweites Programm in einen halb ausgetauschten Ordner zu starten. Dann `recover`, dann der Bericht.
  `LibraryLayout::cleanupTemp` löscht übrig gebliebene `update-*` und `updater-*` aus `temp/`.
- **Aufräumen:** Nach Erfolg löscht der Austausch den Staging-Ordner, er behält die zwei neuesten Backups in
  `updater/backups/`. Die laufende Kopie in `temp/updater-<Version>/` löscht der Launcher beim nächsten Start.

### D-41 Ablauf und Oberfläche des Launcher-Updates (`LauncherUpdateManager`)

- **Prüfung:** Drei Sekunden nach dem Start und dann im eingestellten Intervall (`updates.check_interval_minutes`,
  wie bei Projekten), aber nur, wenn „Nach Launcher-Updates suchen“ eingeschaltet ist
  (`launcherUpdateAutoCheck`). Der Knopf „Nach Updates suchen“ geht immer. Die Prüfung ist eine einzige
  API-Anfrage und wird übersprungen, wenn weniger als 10 Anfragen übrig sind (die Projekte brauchen den Rest, D-36).
- **Nie ohne Zustimmung.** Der Launcher meldet das Update, mehr nicht. „Aktualisieren“ öffnet eine Rückfrage
  mit Version und Changelog; sie sagt, dass der Launcher schließt und neu startet, dass laufende Projekte
  geöffnet bleiben und dass Einstellungen, Projekte und Downloads unverändert bleiben. Mit „Aktualisieren“
  stimmt der Nutzer dem ganzen Ablauf zu: laden, prüfen (D-39), neu starten. Ist das Paket bereit und läuft
  keine Installation, übergibt der Launcher von selbst an den Austausch. Eine zweite Rückfrage gäbe es
  nur, damit der Nutzer ein Fenster mehr wegklickt. Es gibt keinen Modus „automatisch“ für den Launcher.
- **Zustände:** Idle, Prüfung, Aktuell, Verfügbar, Lädt, Prüft („Preparing“), Bereit, Wird angewendet,
  Fehlgeschlagen. Fehlerarten: Release nicht abrufbar, keine Prüfsumme, Prüfsummendatei, Download, Paket,
  Installationen laufen noch („Busy“), Übergabe an den Austausch. Nach einem Fehler gilt „Erneut versuchen“
  für dieselbe Version ohne neue Rückfrage; eine neuere Version beginnt von vorn.
- **Busy:** Laufende Installationen oder Updates von Projekten (`InstallService::busyCount() > 0`) halten den
  Neustart auf, denn er würde sie beenden. Der Zustand „Bereit“ bleibt dann stehen, der Hinweis erklärt den
  Grund, und „Jetzt neu starten“ geht, sobald die Installationen fertig sind. Laufende *Projekte* stören
  nicht: Der Austausch fasst nur Dateien des Launchers an.
- **Abbrechen** geht beim Laden, beim Prüfen und im Zustand „Bereit“; der Staging-Ordner wird entfernt. Ab der
  Übergabe ist es zu spät.
- **Übergabe:** `apply()` kopiert den laufenden Launcher nach `temp/updater-<Version>/`, schreibt den Auftrag
  und startet die Kopie im Austausch-Modus. Gelingt das nicht, bleibt
  der Launcher offen und zeigt den Fehler (Fehlerart „Übergabe“, Staging wird entfernt). Gelingt es, meldet er
  `restartRequested`, und die Anwendung beendet sich.
- **Der Launcher aktualisiert sich nur unter seinem Programmnamen** (`KERJAMI_Launcher.exe`). Läuft er unter einem
  anderen Dateinamen (umbenannt, aus einem Testordner), lehnt `apply()` die Übergabe ab, weil der Austausch die
  Datei sonst nicht fände und neben dem alten Programm eine zweite Kopie entstünde.
- **Anzeige:** Dashboard (Karte „Launcher“) und „Einstellungen → Über“ zeigen dasselbe Bedienfeld
  (`ui/LauncherUpdatePanel`). Texte stehen in `ui/OperationMessages`, die Rückfrage in `ui/Dialogs`.
  Beim Start zeigt die Anwendung nach dem Fenster eine Meldung über den Ausgang des letzten Updates oder über
  eine nachgeholte Wiederherstellung (`updateReportMessage`, `recoveryMessage`).

### D-42 Tag-Präfix im Registry-Eintrag (`tag_prefix`)

- **Anlass:** Die Projekte des Teams nennen ihre Release-Tags `TankstellenmealV0.12.3` und
  `KERS_SubsystemsV0.3.1` (E-07). Der Launcher las bis dahin nur `1.2.3` oder `v1.2.3` (D-21) und hätte
  beide Projekte als „ohne Release“ gezeigt.
- **Regel:** Ein Eintrag darf `tag_prefix` haben. Ist er gesetzt, zählt ein Release nur, wenn sein Tag so
  beginnt (Groß- und Kleinschreibung zählt). Der Rest wird wie bisher streng als SemVer gelesen (ein `v` davor
  ist erlaubt), eine Vorabversion macht das Release nicht stabil (D-21). Tags ohne das Präfix werden übergangen,
  auch ein schlichtes `v1.0.0`: Ein Projekt, das ein Präfix angibt, meint damit seine Tags. Ohne das Feld
  ändert sich nichts.
- **Form:** 1 bis 32 Zeichen aus Buchstaben, Ziffern, `.`, `_` und `-`, erstes Zeichen ein Buchstabe, kein
  `/`. So bleibt jedes Tag ein einzelner Pfadabschnitt der Download-Adresse. Ein vorhandenes, aber leeres oder
  ungültiges Feld lässt den Eintrag durchfallen (wie bei den anderen Feldern, D-16); fehlt es, ist es in
  Ordnung. Die Release-Verwaltung prüft das Präfix vor jeder Anfrage erneut (D-22).
- **Das Tag bleibt, wie es ist.** Die Download-Adresse wird mit dem echten Tag gebaut
  (`…/releases/download/TankstellenmealV0.12.3/…`, D-23), gespeichert wird es ebenfalls so. Nur die Version
  (`0.12.3`) kommt aus dem Rest. Installierte Version, Update-Vergleich und Anzeige arbeiten mit der Version.
- **Grenzen:** Ein Präfix je Projekt. Wechselt ein Projekt später die Namensart seiner Tags, passt der Eintrag
  nur zu einer davon. Die Versionsnummer danach muss streng SemVer sein, `0.12` oder `012.2` werden übergangen.

### D-43 Die Projektliste ist Teil des Programms

- **Entscheidung des Teams (2026-11-11):** Die Liste der freigegebenen Projekte, die „Registry“, steckt im
  Launcher. `resources/registry/projects.json` wird in die EXE einkompiliert (Qt-Ressource
  `:/registry/projects.json`), und der `RegistryManager` liest sie beim Start (`loadBuiltIn()`). Es gibt kein
  Registry-Repository, keine Registry-Adresse und keinen Abruf. D-18 und D-19 (Zwischenspeicher,
  Offline-Verhalten, Zeitplan) entfallen. Ein neues Projekt kommt mit einem neuen Release des Launchers, das
  das Selbstupdate (D-39 bis D-41) ausliefert.
- **Vorteile:** Eine Quelle weniger, die ausfallen kann: kein 404, keine Frage nach dem Anfragebudget, kein
  Zustand „offline“ für die Liste. Die Liste hat dieselbe Herkunft und dieselbe Prüfkette wie das Programm, und
  es braucht kein weiteres öffentliches Repository mit Dateien (E-06).
- **Kosten:** Auch ein einzelnes neues Projekt oder ein geänderter Pfad einer Startdatei braucht ein Release des
  Launchers. Wer das Update nicht installiert, sieht die neue Liste nicht. Dafür steht jede Änderung der Liste
  im Git-Verlauf.
- **Prüfung bleibt:** Die Datei wird wie vorher mit allen Regeln gelesen (D-16). Der Test
  `RegistryManagerTests::theBuiltInListIsInTheProgramAndValid` verlangt, dass kein Eintrag verworfen wird, und
  `theBuiltInProjectsAreUsable` prüft jedes Feld noch einmal mit den Einzelregeln. Ein Fehler in der Liste
  kommt so nicht in ein Release.
- **Im Code:** `RegistryManager` hat `loadBuiltIn()` und `load(json)` (für Tests) und kein Netzwerk. Entfallen
  sind: `BRAND_REGISTRY_URL` und `branding::registryUrl()`, `PathManager::registryCacheFile()`, die Einstellung
  „Registry automatisch prüfen“ (`registry.auto_check`; ein solcher Eintrag in einer alten Einstellungsdatei
  wird ignoriert), der Knopf „Aktualisieren“ und die Fehlerkarten der Bibliothek und der Host
  `raw.githubusercontent.com` in der Netzwerk-Policy.
- **Pflege:** Einen Eintrag ergänzen heißt `resources/registry/projects.json` ändern, bauen, testen, ein
  Release des Launchers veröffentlichen. Die Felder stehen in `docs/registry/README.md`.
- **Aktueller Inhalt: leer.** Für jeden Eintrag fehlen noch Angaben, die nur das Team kennt: der Pfad der
  Startdatei im ZIP, bei vier Projekten auch ein ZIP-Release (E-07).

### D-44 Einzel-EXE-Releases (ergänzt D-14)

- **Entscheidung (2026-11-11):** Ein Projekt darf seine Release-Datei als einzelne `.exe` veröffentlichen, nicht
  nur als ZIP, so wie KERS Overlay es tut. Das `asset_pattern` des Eintrags endet dann auf `.exe` (zum Beispiel
  `KERS_Subsystems.exe`). Die Art der Datei ergibt sich allein aus der Endung (`RegistryParser::isSingleFileAsset`).
- **`executable` ist dann ein Dateiname, kein Pfad:** der Name, unter dem das Programm installiert wird
  (`Overlay.exe`). Ein `/` darin lässt den Eintrag durchfallen (Parser) und die Installation scheitern
  (`InvalidProject`, Installer).
- **Ablauf:** wie bei einem ZIP bis zur Prüfsumme, die Pflicht bleibt (D-28: Digest oder Prüfsummendatei, sonst
  keine Installation). Statt der Archivprüfung gilt: Die Datei ist mindestens 64 Byte groß und beginnt mit der
  Windows-Programmkennung `MZ`, sonst `InstallError::NotAProgram` („kein Windows-Programm“). Danach kopiert der
  Installer sie als `<executable>` nach `temp/staging-<id>/` und geht dieselben Schritte wie sonst (D-30):
  Sicherung der alten Version, Austausch, Eintragen, Rollback bei einem Fehler. Platz: Dateigröße plus 16 MiB.
  Die Pfadlänge wird geprüft.
- **Warum das reicht:** Ein ZIP braucht Archiv- und Pfadregeln, weil es Pfade enthält. Eine einzelne Datei hat
  keine. Ihr Schutz ist die Prüfsumme. `MZ` ist keine Sicherheitsprüfung, es verhindert nur, dass etwas ganz
  anderes (ein ZIP, eine Fehlerseite) als Programm abgelegt und später gestartet wird.
- **Grenzen:** Ein Projekt aus mehreren Dateien braucht ein ZIP. Bei einem Update wird `current/` als Ganzes ersetzt
  (D-30); was das Programm neben sich anlegt, liegt danach in der Sicherung, außer der Eintrag nennt es in `keep`
  (D-47). Der Launcher selbst erscheint weiter als ZIP (D-39).
- **Tests:** `InstallerTests` (frisch, Update mit Sicherung und Rollback, „kein Programm“ als Tabelle, Hash, Pfad als
  `executable`), `InstallServiceTests` (die ganze Kette mit Präfix-Tag und Einzel-EXE), `RegistryTests`, und der
  Live-Test gegen KERS Overlay.

### D-45 Oberfläche: Detailseite, Dashboard, Speicher (Phase 7)

- **Detailseite** (`ProjectDetailPage`): Sie steht nicht in der Seitenleiste. Man erreicht sie über „Details“ auf der
  Projektkarte und im Dashboard; „← Zurück“ führt dorthin zurück, woher man kam, und die Seitenleiste behält die
  Markierung der Herkunft. Sie zeigt Name, Kategorie, Beschreibung, Tags, die installierte und die neueste Version
  (mit „Update verfügbar“ oder „aktuell“), Downloadgröße, Veröffentlichungsdatum, den Changelog (die Release-Notes
  als Rohtext in einem schreibgeschützten Feld, D-22), die GitHub-Adresse („Auf GitHub öffnen“, aus Owner und
  Repository des Eintrags gebaut) und dieselben Bedienelemente wie die Karte: Installieren, Starten, Aktualisieren,
  Deinstallieren und „Updates: Manuell/Automatisch“. Karte und Seite teilen sich dafür ein Widget
  (`ProjectControls`), damit sie sich nicht auseinanderentwickeln.
- **Abfrage:** Beim Öffnen fragt die Seite GitHub, wenn sie für das Projekt in den letzten 10 Minuten nichts erhalten
  hat (eine API-Anfrage, D-22). „Neu abfragen“ fragt immer. Ein Fehler steht im Klartext auf der Seite; Installieren
  geht trotzdem, es fragt selbst.
- **Dashboard:** Willkommen mit „Zur Bibliothek“; „Verfügbare Updates“ (installierte Projekte mit bekanntem Update:
  „Version a → b · Zustand“); „Installierte Projekte“ (Starten, Details); „Zuletzt gestartet“ (die fünf jüngsten
  Starts mit Zeit); „Empfohlene Projekte“ (höchstens vier, aus der Liste); der Launcher mit Version und
  Update-Hinweis. Die Listen werden einmal neu gebaut, kurz nachdem sich etwas geändert hat. Ein Abschnitt ohne
  Inhalt sagt, was fehlt; „Verfügbare Updates“ erscheint nur mit installierten Projekten, „Empfohlene Projekte“ nur,
  wenn es welche gibt.
- **Bibliothek:** Die Karte hat „Details“, und die Statuszeile sagt nach einer Prüfung „Installiert: Version 1.0.0 ·
  aktuell“.
- **Speicher** (Einstellungen → Speicherplatz, Cache-Verwaltung): zeigt, wie viel Platz Downloads und temporäre
  Dateien und die Sicherungen früherer Versionen belegen. „Aufräumen“ leert `downloads/` und `temp/`, „Löschen“ die
  Sicherungen aller Projekte (`library/*/backups/*`). Gemessen und gelöscht wird in einem Hintergrund-Thread, das
  Fenster bleibt bedienbar. Beides fragt vorher; die Knöpfe sind gesperrt, solange ein Download, eine Installation
  oder ein Launcher-Update läuft, denn in `temp/` liegt die Teildatei eines Downloads. Installierte Versionen werden
  nie angefasst. Links und Junctions werden weder betreten noch gezählt: Der Test mit einer echten Junction fand,
  dass `QDirIterator` Junctions folgt, deshalb läuft die Messung von Hand (`installer/Storage`).
- **Entfallen:** „Automatische Registry-Prüfung“ (D-43). **Theme** (E-05) bleibt offen: Eine Auswahl mit einem einzigen
  Eintrag hat keinen Nutzen. „Log-Verzeichnis öffnen“ gab es schon.
- **Fehler und Ladezustände:** Karten und Detailseite zeigen Fortschritt und Fehler in Klartext (D-34, D-36), die
  Bibliothek „leer“, „nicht lesbar“ und „keine Treffer“ (D-20), die Detailseite „wird abgefragt“ und den Grund eines
  fehlgeschlagenen Abrufs.

### D-46 Portable Release, Selbsttest und Paketprüfung (Phase 8)

- **Paket:** `scripts/package-portable.ps1` baut `KERJAMI-<Version>-Windows-x64-Portable.zip` und
  `….zip.sha256` (eine Zeile `<Hash>  <Name>`). Inhalt: `KERJAMI_Launcher.exe` und `update-manifest.json` (D-39).
  Mit dem statischen Qt ist das alles. Bei einem geteilten Qt (ein Qt mit `Qt6Core.dll`) kopiert `windeployqt` die
  Bibliotheken und Plugins daneben; dieser Zweig ist **nicht ausprobiert**, hier gibt es kein geteiltes Qt. Es gibt
  keinen Installer und keine Konfigurationsvorlagen: Die Einstellungen entstehen beim ersten Start mit
  Standardwerten. Die Version nimmt das Skript aus dem Programm selbst (`--self-test`), nicht aus einem zweiten Ort.
- **Das ZIP schreibt .NET `ZipArchive`** mit `/` in den Namen. `Compress-Archive` aus Windows PowerShell 5.1 schreibt
  Backslashes, die der strenge Leser des Launchers ablehnt (D-29).
- **Zwei Betriebsarten des Programms ohne Fenster** (`app/SelfTest`): `--self-test <Ergebnis.json>` prüft das Programm
  von innen: TLS-Backend (schannel), SHA-256 (Testvektor „abc“), zlib, die eingebauten Ressourcen (Style Sheet, Bilder,
  Projektliste), die Projektliste ohne verworfene Einträge, ein anlegbarer und beschreibbarer Ordner, lesbare
  Einstellungen. `--check-package --zip … --sha256 … --version … --work … --out …` lässt den Launcher ein Paket so
  prüfen, wie sein Selbstupdate es täte (`UpdatePackage::prepare`, D-39). Exitcodes: 0 gut, 1 Prüfung fehlgeschlagen,
  2 falsche Argumente.
- **`scripts/verify-package.ps1`** prüft ein Paket von außen in einem frischen Temp-Ordner: Dateiname nach dem Muster,
  Prüfsummendatei passt, Eintragsnamen sicher, das Manifest stimmt mit den Dateien überein (Größe, SHA-256, nichts
  Unaufgeführtes), keine Entwicklungsreste (`.pdb`, Einstellungen, Logs, Sperren), **keine Spur des Erstellers**
  (Benutzername, Profilordner, `\Users\` in keiner Datei), der Selbsttest besteht aus dem entpackten Ordner, das
  Programm schreibt nur in die eigenen Ordner (nichts ins Benutzerprofil, nichts neben sich), und das Paket besteht
  `--check-package`.
- **`scripts/test-package.ps1`** (CTest `PackageTests`) baut ein Paket und macht sechs kaputte daraus: mit einer
  Debug-Datei, einer nicht aufgeführten Datei, einer veränderten EXE, einem Manifest für eine andere Version, ohne
  Manifest und mit einer EXE, die einen Benutzernamen enthält. Jede muss abgelehnt werden, und zwar aus dem
  eingebauten Grund und nicht, weil die Prüfsummendatei fehlt. Eine Prüfung, die nicht „Nein“ sagen kann, ist nichts
  wert.
- **GitHub Actions** (`.github/workflows`): `ci.yml` baut und testet bei jedem Push, `release.yml` baut bei einem Tag
  `v<Version>` das Paket, prüft es, vergleicht Tag und Programmversion und legt ein **Entwurf-Release** an.
  **Beide sind noch nie gelaufen:** Sie bauen mit MSVC und einem geteilten Qt, hier gibt es nur llvm-mingw mit
  statischem Qt (E-01). Beim ersten Lauf sind Anpassungen zu erwarten.
- **Veröffentlichen ohne Actions:** `package-portable.ps1` ausführen und die beiden Dateien aus `dist/` von Hand als
  Assets eines Release hochladen (`docs/04-veroeffentlichen.md`). Das passt zu E-06: Dafür muss kein Quelltext auf
  GitHub liegen.

### D-47 Daten, die ein Projekt über Updates behält (`keep`)

- **Anlass:** Der Lauf mit KERS Overlay am 2026-11-11 zeigte, dass ein Projekt beim Start einen Ordner `data/` neben
  seine Programmdatei legt, mit den eigenen Einstellungen. Ein Update ersetzt `current/` als Ganzes (D-30); diese
  Einstellungen wären danach nur noch in der Sicherung gewesen, die neue Version hätte leer angefangen.
- **Entscheidung (Team, 2026-11-11):** Der Eintrag eines Projekts darf eine Liste `keep` haben: Dateien und Ordner
  unterhalb des Installationsordners, die dem Projekt gehören. Der Launcher nimmt sie bei jedem Update in die neue
  Version mit. KERS Overlay: `"keep": ["data"]`.
- **Form:** Höchstens 8 Einträge, jeder ein relativer Pfad mit `/` (`isValidKeepPath`: keine Laufwerke, kein `..`,
  kein `\`, kein `:`, keine leeren Teile, keine Namen, die Windows nicht speichern kann; höchstens 120 Zeichen).
  Doppelte Namen (auch in anderer Schreibweise) zählen einmal. Ein Eintrag, der die Startdatei nennt oder einen
  Ordner, der sie enthält, fällt durch, denn die alte Datei würde bei jedem Update das neue Programm ersetzen.
- **Ablauf im Installer:** Nach dem Entpacken und der Prüfung der Startdatei, **bevor** irgendetwas an der
  installierten Version geändert wird, kopiert er jeden genannten Pfad aus `current/` nach `temp/staging-<Projekt>/`.
  Ein Ordner wird zusammengeführt, eine Datei ersetzt eine gleichnamige. **Was der Nutzer angelegt hat, gewinnt**
  gegen eine gleichnamige Datei des neuen Pakets; Dateien, die nur das neue Paket hat, bleiben. Danach geht es wie
  immer weiter (D-30): Sicherung der alten Version, Austausch, Eintragen.
- **Warum kopieren und nicht verschieben:** Die alte Version behält ihre Daten in der Sicherung. Ein Rollback
  (`current/` ← Backup) findet deshalb alles, und ein Fehler beim Kopieren lässt die installierte Version unberührt.
- **Fehler:** Lässt sich eine Datei nicht kopieren (zum Beispiel weil ein Programm sie exklusiv hält), scheitert das
  Update mit `InstallError::CarryOverFailed` und der Meldung „Die Daten des Projekts … konnten nicht übernommen
  werden … die bisherige Version bleibt erhalten“. Es wird nichts verändert: keine Sicherung, kein Staging.
- **Links:** Eine Verknüpfung oder Junction wird weder betreten noch kopiert, auch nicht, wenn sie ein genannter Pfad
  selbst ist oder auf dem Weg zu ihm liegt. Sonst könnte ein Link in den Daten Dateien von außerhalb in die neue
  Version ziehen.
- **Ohne Daten:** Gibt es einen genannten Pfad (noch) nicht, zum Beispiel bei der ersten Installation, wird er
  übersprungen. Das ist kein Fehler.
- **Sichtbar für den Nutzer:** Die Rückfrage vor einem Update sagt, welche Daten übernommen werden („Deine Daten
  (data) werden in die neue Version übernommen.“).
- **Grenzen:** Die Namen stehen im Eintrag. Der Launcher sieht nicht, was ein Projekt anlegt. Große Datenordner werden
  bei jedem Update kopiert und liegen dann zweimal auf der Platte (neu und in der Sicherung); `keep` ist für
  Einstellungen gedacht, nicht für Spielstände von Gigabytes. Dateien in einem genannten Ordner, die das neue Paket
  mitbringt, überschreibt die alte Fassung. Ein Projekt, das aktualisierte Standarddateien in seinen Datenordner
  liefert, nennt sie also besser nicht in `keep` oder legt sie woanders ab.
- **Deinstallation:** Sie löscht den ganzen Projektordner, also auch die Daten aus `keep` und alle Sicherungen.
  Die Rückfrage sagt das ausdrücklich („Auch die Daten des Projekts (data) werden gelöscht.“) und nennt als
  unberührt nur die Einstellungen des Launchers. Eine Option „Daten behalten“ gibt es nicht.
- **Tests:** `RegistryTests` (Feld, Pfadregeln als Tabelle, Startdatei), `InstallerTests` (Ordner und Dateien,
  Nutzer gewinnt und Neues bleibt, nichts vorhanden, einzelne EXE wie bei KERS Overlay, gesperrte Datei stoppt das
  Update ohne Änderung, Links werden nie verfolgt, Startdatei in `keep` wird abgelehnt) und `RegistryManagerTests`
  (jeder Eintrag der eingebauten Liste hat gültige Pfade).

## Bekannte Grenzen

- `PathManager::isInside` vergleicht Pfade lexikalisch und löst Links und Junctions nicht auf. Die
  Installation fängt das an den Stellen ab, an denen es zählt: Der Extraktor und der Uninstaller
  prüfen jeden Ordner auf dem Weg (`FileOps::isLink`), und Links im Archiv gibt es nicht.
- Der Code ist nur mit clang (llvm-mingw) gebaut. Der MSVC-Zweig in `CMakeLists.txt` ist
  ungetestet.
- Ein Debug-Build linkt gegen die Release-Bibliotheken von Qt, weil es keine Debug-Variante
  gibt. In Qt-internen Code lässt sich damit nicht hineinsteppen.
- Das Intervall der Update-Prüfung gilt für Projekte und Launcher gemeinsam. Der Schalter „Nach
  Launcher-Updates suchen“ betrifft nur die automatische Prüfung; „Nach Updates suchen“ funktioniert
  auch ausgeschaltet.
- Marken-Bilder werden für die Bildschirmskalierung beim Seitenaufbau gerendert. Zieht man das
  Fenster auf einen Monitor mit anderer Skalierung, bleiben sie skaliert und sind etwas weicher.
- Windows meldet eine verweigerte Verbindung zu einem geschlossenen Port erst nach etwa zwei
  Sekunden. Ohne Internet ist das kein Thema, weil schon die Namensauflösung scheitert. Die Tests
  verwenden deshalb kurze Timeouts.
- Qt sendet eine Anfrage einmal selbst erneut, wenn der Server die Verbindung ohne Antwort schließt.
  Ein Server kann dadurch mehr Anfragen sehen, als der Client Versuche zählt.
- Qt verwirft einen `border-radius`, der größer als die halbe Höhe des Elements ist, statt ihn zu
  kappen. Das Style Sheet hält die Radien der Chips deshalb bei 8 px.
- Das Budget von 60 Anfragen pro Stunde ohne Anmeldung gilt für `api.github.com`, also für die
  Release-Abfragen ab Phase 3. Der Live-Test zeigt das Budget im Log an (`LAUNCHER_LIVE_TESTS=1`).
- Die Bibliothek baut bei jeder Änderung alle Karten neu. Bei der Obergrenze von 500 Projekten ist
  das unkritisch.
- Von der Release-Liste eines Repositories werden die ersten 30 Einträge gelesen. Eine Seitenabfrage
  gibt es nicht.
- Die Antwort der Release-Liste wird vollständig im Speicher gelesen (bis 4 MiB). Downloads dagegen
  werden direkt in die Datei geschrieben.
- Es gibt zwei Live-Tests, beide laufen nie von allein. `LAUNCHER_LIVE_TESTS=1` liest nur Metadaten: die
  Release-Liste von `nlohmann/json` und die von KERS Overlay, ohne eine Datei zu laden.
  `LAUNCHER_LIVE_DOWNLOAD_TESTS=1` (`InstallServiceTests::liveInstallOfKersOverlay`) lädt die Release-Datei von
  KERS Overlay, rund 56 MiB, installiert sie in einen temporären Ordner und entfernt sie wieder. Am 2026-11-11
  lief er mit Erlaubnis des Teams: Release-Abfrage 460 ms, Weiterleitung von `github.com` zu
  `release-assets.githubusercontent.com`, 58.974.720 Byte in 3,7 s, SHA-256 gegen den Digest von GitHub, gültiger
  Windows-Programmkopf, Installation in 4,8 s, danach nichts übrig. Damit ist die Kette (Adressen, Digest-Feld,
  Weiterleitung, Prüfsumme, Installation, Deinstallation) **für eine einzelne EXE** gegen echtes GitHub gemessen
  und nicht mehr nur nachgestellt. Für ein ZIP, ein Update und das Selbstupdate gilt das weiter nicht (E-02).
  Mit `LAUNCHER_LIVE_START_TESTS=1` zusätzlich startet der Test das installierte Programm über den
  `ProcessManager`, beobachtet es zehn Sekunden, bittet seine Fenster zu schließen (erst danach würde er es
  beenden), wartet auf das Prozessende und deinstalliert dann. **Lauf vom 2026-11-11** (mit Erlaubnis des
  Teams): Das Overlay startete aus `library/KERS_Overlay/current/` (das Abbild des Prozesses ist genau diese
  Datei), lief nach zehn Sekunden noch, zeigte zwei sichtbare Fenster, ließ sich durch die Aufforderung zum
  Schließen beenden, kein Prozess blieb übrig, und die Deinstallation ging danach durch. Es legte neben sich
  einen Ordner `data/` an. Der Start im Arbeitsordner des Projekts ist außerdem mit einem Hilfsprogramm getestet
  (`ProcessTests::aProjectIsStartedInItsOwnFolder`).
- Der Schutz vor dem Überschreiben eines laufenden Projekts beruht auf der Windows-Dateisperre: Die
  EXE eines laufenden Programms und jede geöffnete Datei in `current/` verhindern das Umbenennen
  des Ordners. Das ist getestet (Datei ohne Löschfreigabe offen halten). Ein Programm, das aus
  `current/` gestartet wurde, hält immer seine EXE offen; die Sperre ist deshalb verlässlich, aber
  eine Fehlermeldung „Datei in Benutzung“ kann auch ein Virenscanner auslösen. Davor steht seit Phase
  5 die Prozessüberwachung (D-35), die ein laufendes Projekt schon vorher erkennt und das Update
  vormerkt, statt es scheitern zu lassen.
- Die Update-Prüfung braucht eine API-Anfrage je installiertem Projekt und Runde. Mit vielen
  installierten Projekten und dem kürzesten Intervall (15 Minuten) kommt man nahe an das Limit von 60
  Anfragen pro Stunde; der Manager lässt dann Reserve (D-36), die Anzeige bleibt beim letzten Stand.
- Die Release-Notes erscheinen als Rohtext (Markdown-Quelle) in der Rückfrage zur Aktualisierung, nicht
  als formatierter Text. Eine Detailseite mit Darstellung folgt in Phase 7.
- Nach einer Aktualisierung startet der Launcher das Projekt nicht neu, auch wenn es vorher lief.
- Eine Aktualisierung ersetzt `current/` als Ganzes. Dateien, die ein Projekt zur Laufzeit in
  `current/` anlegt, liegen danach im Backup, nicht in der neuen Version (D-30), **außer sie stehen im Feld `keep`
  des Eintrags (D-47)**. Das war kein theoretischer Fall: KERS Overlay legt beim Start `data/` neben sich an
  (Lauf vom 2026-11-11), darin seine Einstellungen. Sein Eintrag nennt deshalb `"keep": ["data"]`. Ein Projekt
  muss diese Namen kennen und im Eintrag nennen; legt es Daten an einer Stelle an, die der Eintrag nicht nennt,
  gehen sie beim Update wie bisher in die Sicherung. Wer `keep` braucht, sagt es im Eintrag, der Launcher rät
  nicht.
- Das ZIP muss die Startdatei am Pfad der Registry enthalten und darf kein ZIP64, keine
  Verschlüsselung und keine Nicht-ASCII-Namen ohne UTF-8-Kennzeichen haben (D-27, D-29). Die Fehlermeldung
  nennt den Grund; die Ursache liegt dann beim Projekt, das neu packen muss.
- Die Rückfrage vor dem Deinstallieren ist in den Tests ersetzbar
  (`Dialogs::setUninstallConfirmationForTests`), weil sich ein modales Fenster nicht aus einem Test
  bedienen lässt. Die Tests prüfen, dass gefragt wird und dass „Nein“ nichts verändert. Das echte
  Fenster (Text, Schaltflächen, Standardauswahl „Abbrechen“) wurde noch nicht angesehen. Dasselbe
  gilt für die Rückfrage zum Launcher-Update (`Dialogs::setLauncherUpdateConfirmationForTests`) und für
  die Meldungsfenster des Austauschs.
- **Windows Defender hat die frühere, getrennte `LauncherUpdater.exe` fälschlich als Schadsoftware
  gemeldet** (Stand 2026-11-10, `Trojan:Win32/Bearfoos.A!ml`, eine Erkennung durch maschinelles Lernen):
  Quarantäne kurz nach dem Bauen, im Release- wie im Debug-Build und auch nach dem Verschlanken auf Qt Core
  allein. Das war, neben dem Wunsch nach einer einzigen Datei, der Grund, den Austausch in `KERJAMI_Launcher.exe`
  zu verlegen (D-40); die zweite EXE gibt es nicht mehr. `KERJAMI_Launcher.exe` wurde nie gemeldet. Ausgeschlossen
  ist es nicht, dass ein Scanner die Kopie in `temp/` beanstandet, wenn sie mit `--apply-update` startet:
  Getroffen wurde damals die Datei beim Scan, nicht ihr Verhalten. Bei allen Testläufen mit der echten
  `KERJAMI_Launcher.exe` (Release und Debug, die Tests starten sie als Kopie und tauschen echte Dateien aus) gab es
  keine Meldung. Falls es doch geschieht, meldet der Launcher „Übergabe fehlgeschlagen“ oder der Austausch
  kommt nicht zustande; das Update wird dann nicht angewendet, und nichts geht kaputt. Abhilfe wäre eine
  Code-Signatur (Phase 8, braucht ein Zertifikat und eine Entscheidung) oder eine Meldung als Fehlalarm an
  Microsoft. Der Launcher ändert keine Sicherheitseinstellungen.
- Der Austausch kommt aus dem *laufenden* Launcher, ebenso wie die Prüfung des Pakets. Eine Änderung am
  Austausch oder am Manifest-Schema wirkt deshalb erst, wenn die Version installiert ist, die sie
  enthält. Ein älterer Launcher versteht ein neues Schema nicht und lehnt das Manifest ab; ein solcher
  Sprung braucht einmal eine Aktualisierung von Hand. Deshalb bleibt Schema 1, bis es einen Grund gibt.
- Ob der neue Launcher nach dem Austausch tatsächlich startet, prüft der Austausch nicht. Er startet ihn und
  merkt es nur, wenn die Datei fehlt. Ein Launcher, der gleich wieder abstürzt, wird nicht zurückgerollt; die
  Dateien der alten Version liegen in `updater/backups/` (die zwei letzten Stände bleiben erhalten).
- Das Release-ZIP ist das ganze portable Paket (etwa 30 MB). Beim Austausch bleiben Dateien mit gleichem
  Hash unangetastet, geladen wird aber immer alles.
- Dateien, die ein früheres Release mitgebracht hat und die ein neues Manifest nicht in `remove` nennt,
  bleiben liegen. Der Austausch löscht nichts, was nicht ausdrücklich genannt ist.
- Der Austausch zeigt Probleme mit einem schlichten Windows-Meldungsfenster in der Sprache des Programms
  (Deutsch), nicht im Stil der Anwendung. Der Normalfall ist der Bericht, den der Launcher beim nächsten
  Start anzeigt.
- Ein Selbstupdate gegen ein echtes GitHub-Release ist noch nicht getestet (das Repository hat kein
  Release). Der Test `selfUpdateEndToEnd` fährt die ganze Kette mit der echten `KERJAMI_Launcher.exe` im Austausch-Modus
  gegen einen lokalen Server, der GitHub nachbildet.
- Die Workflows `ci.yml` und `release.yml` sind nie gelaufen. Ob der MSVC-Zweig von `CMakeLists.txt` baut, ob
  `Qt6::ZlibPrivate` aus den offiziellen Qt-Paketen verfügbar ist und ob die UI-Tests mit einem geteilten Qt laufen
  (`qt_import_plugins` für die Offscreen-Plattform), ist offen. Der Zweig für ein geteiltes Qt in
  `package-portable.ps1` (windeployqt, TLS-Plugin) ebenfalls; `--self-test` würde ein fehlendes TLS-Plugin im Paket
  sofort zeigen.
- Das Paket enthält keine Lizenztexte von Qt und nichts, was die Pflichten der LGPL beim statischen Linken erfüllt
  (E-01, E-06). Das muss vor dem ersten öffentlichen Release geklärt werden.
- Die EXE ist nicht signiert. Windows SmartScreen kann vor einer neu heruntergeladenen, unsignierten Datei warnen,
  und Virenscanner sind bei unsignierten Programmen misstrauischer (ein früherer Fehlalarm, D-40).
- `PackageTests` braucht PowerShell und dauert etwa eine halbe Minute: Es entpackt und startet das Programm
  siebenmal.
- Die Version eines Release steht in `CMakeLists.txt` (`project(… VERSION …)`). Das Paketskript liest sie aus dem
  gebauten Programm und verlangt `major.minor.patch`.
