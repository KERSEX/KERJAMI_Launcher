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
- **Für llvm-mingw statisch:** Das Paket besteht aus zwei EXE-Dateien ohne DLLs und
  Plugin-Ordner. `KERJAMI.exe` importiert nur Windows-Systembibliotheken. Das Selbstupdate
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
- **Betrifft:** Phase 4 und 5 (echter Test). Phase 2 und 3 sind nicht blockiert.

### E-03 Registry-Repository

- Das Launcher-Repository ist `KERSEX/KERJAMI_Launcher` (angelegt, öffentlich, noch leer).
- Das Registry-Repository existiert nicht. Der Name `CommunityLauncher-Registry` aus dem Auftrag
  passt nicht mehr zum Launcher-Namen. Vorschlag: `KERSEX/KERJAMI_Registry`. Es muss öffentlich
  sein, weil der Launcher ohne Anmeldung arbeitet.
- Die Adresse steht in `cmake/Branding.cmake` (`BRAND_REGISTRY_URL`) und zeigt bis zur
  Entscheidung noch auf den Namen aus dem Auftrag.
- Die Startdatei (`projects.json` ohne Projekte) und eine README mit Feldreferenz liegen in
  `docs/registry/` und werden ins neue Repository kopiert. Die dort beschriebenen Prüfregeln sind
  der Entwurf für Phase 2; ändern sie sich, wird die README mit angepasst.
- **Betrifft:** Den echten Abruf in Phase 2 und die Launcher-Releases in Phase 6. Entwickelt und
  getestet wird Phase 2 auch ohne das Repository.

### E-05 Helles Theme

- Die Einstellungen sehen einen Punkt „Theme“ vor, das Designsystem beschreibt nur ein dunkles
  Theme. Die Einstellung ist im Schema vorhanden (`appearance.theme`), die Seite zeigt sie
  noch nicht, weil eine Auswahl mit einem einzigen Eintrag keinen Nutzen hat.
- **Betrifft:** Phase 7.

### E-06 Was wird auf GitHub veröffentlicht?

- **Stand (2026-11-10):** Das Team will vorerst keine Dateien hochladen, nur Releases. Gemeint ist
  offenbar: kein Quelltext und keine Projektdateien in den Repositories, nur Release-Assets. Ob das
  für das Launcher-Repository, das Registry-Repository oder beide gilt, ist noch zu bestätigen.
  Bis dahin wird nichts hochgeladen.
- **Nachtrag (2026-11-10):** Das Team hat nochmals darüber gesprochen und wünscht einen Commit des
  gesamten Stands, besonders von Plan und Stand der Doku. Das ist ein lokaler Commit. Ob und wohin
  gepusht wird, ist nicht entschieden; bis dahin wird nichts zu GitHub übertragen. Offen bleiben die
  Fragen zur Registry (Datei im Repository oder Release-Asset) und zur Qt-Lizenz.
- **Folgen, wenn der Quelltext nicht veröffentlicht wird:**
  - Das Selbstupdate (Phase 6) braucht nur Releases mit ZIP-Asset und ist nicht betroffen.
  - Statisches Linken gegen Qt (E-01) wird zum Lizenzproblem: Die LGPL verlangt, dass Empfänger die
    Anwendung gegen ein geändertes Qt neu linken können. Ohne veröffentlichten Quelltext oder
    Objektdateien bleiben dynamisches Linken mit mitgelieferten Qt-DLLs oder eine kommerzielle
    Qt-Lizenz. Das muss vor dem ersten Release geklärt werden (keine Rechtsberatung).
  - GitHub Actions (Phase 8) brauchen den Quelltext im Repository. Ohne ihn wird das Release lokal
    mit `package-portable.ps1` gebaut und von Hand hochgeladen.
- **Folgen für die Registry:** Die Adresse `raw.githubusercontent.com/.../projects.json` setzt eine
  Datei im Repository voraus. Alternative: `projects.json` als Release-Asset veröffentlichen und
  `https://github.com/<Owner>/<Repository>/releases/latest/download/projects.json` verwenden. Der
  Parser bleibt gleich, jede Änderung der Registry ist dann ein neues Release.
- **Betrifft:** Phase 2 (Registry-Adresse), Phase 6, Phase 8.

## Getroffen

### D-01 Produktname nur in `cmake/Branding.cmake`

Produktname, Anzeigename, EXE-Namen, GitHub-Koordinaten und Registry-URL stehen in einer Datei.
Daraus entsteht `BrandingConfig.h`, die nur `Branding.cpp` einbindet. Bezeichner im Code sind
neutral (`launcher::…`, Ziel `launcher`, Log-Kategorie `launcher.*`). Eine Umbenennung ändert
eine Datei und kompiliert eine Übersetzungseinheit neu.

### D-02 Drei Build-Ziele statt eines

`launcher_core` (ohne UI), `launcher_ui` (Widgets) und die EXE. So linkt der Updater später nur
den Kern, und Tests prüfen Logik ohne Fenster. Die Schichtentrennung aus dem Auftrag wird damit
vom Build-System erzwungen: `launcher_core` kann Widgets gar nicht einbinden.

Seit Phase 6 gibt es darunter noch `launcher_base` (nur Qt Core): Pfade, Logger, JSON, SemVer, Hash, Archivregeln,
Registry-Parser sowie Manifest, Transaktion und Auftrag des Updaters. `launcher_core` baut darauf auf
und fügt Netzwerk, Manager und Dienste hinzu. `LauncherUpdater.exe` linkt nur `launcher_base`: Es gibt
kein Netzwerk im Programm, das Dateien austauscht, und die EXE bleibt mit etwa 9 MB deutlich kleiner als
der Launcher (etwa 30 MB).

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
gehalten wird. Der Updater bekommt eine eigene Datei (`updater.log`).

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
    "registry": { "auto_check": true },
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

### D-14 Projekte werden als ZIP veröffentlicht

Version 1 installiert Projekte aus ZIP-Assets, wie im Auftrag beschrieben. Einzelne EXE-Assets,
wie sie KERS Overlay heute veröffentlicht, werden nicht unterstützt. Der Grund ist die
Installationskette (Archiv prüfen, in ein Staging-Verzeichnis entpacken, erwartete Dateistruktur
kontrollieren), die für eine einzelne Datei nichts zu prüfen hätte. Die Entscheidung lässt sich
später ändern: Das Registry-Schema (Phase 2) bekommt dafür ein erweiterbares Feld für den
Asset-Typ.

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
Funktion ohne Netzwerk und Dateizugriff, damit sich jede Regel direkt testen lässt.

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
- Unbekannte Felder werden ignoriert, damit die Registry wachsen kann.
- Die Prüfregeln stehen auch einzeln zur Verfügung (`isValidInstallDirectory` und andere). Der
  Installer prüft ein Projekt in Phase 4 damit noch einmal, bevor er das Dateisystem anfasst.

Die Feldreferenz für die Pflegenden steht in `docs/registry/README.md`.

### D-17 Netzwerkzugriff nur über den GitHubClient

- **Policy statt Vertrauen:** Jede Anfrage und jeder Redirect wird gegen eine feste Liste geprüft:
  nur HTTPS, nur fünf GitHub-Hosts, nur der Standardport, keine Zugangsdaten in der Adresse. Der
  Host muss exakt übereinstimmen (kein Präfix und kein Suffix).
- Redirects folgt der Client selbst (`ManualRedirectPolicy`), damit jede Station geprüft werden
  kann. Höchstens fünf. Der `Location`-Header wird als Rohtext gelesen, weil Qt für relative
  Adressen nichts liefert.
- **Wiederholungen:** höchstens drei Versuche, Pause 0,5 s und 1 s. Wiederholt wird bei
  Verbindungsproblemen, Timeouts, 408 und 5xx, nicht bei anderen 4xx und nicht bei TLS-Fehlern.
- **Rate Limit:** 429 sowie 403 mit `X-RateLimit-Remaining: 0` oder `Retry-After` gelten als
  „Limit erreicht“ und werden nicht wiederholt, denn jede Wiederholung verbraucht Budget. Das
  Budget aus den Headern ist über `GitHubClient::rateLimit()` abrufbar.
- **Grenzen:** Timeout bei 15 s ohne Daten, Antworten der Registry höchstens 1 MiB, sonst 4 MiB.
  Zu große Antworten werden schon während des Empfangs abgebrochen.
- Jede Anfrage trägt `User-Agent: <Produkt>/<Version>` (GitHub verlangt einen), keine Cookies,
  keine Anmeldung. Der Header `X-GitHub-Api-Version` geht nur an `api.github.com`.
- Das Ergebnis kommt immer asynchron, nie aus dem Aufruf von `get()` heraus, damit der Aufrufer
  vorher verbinden kann. Protokolliert werden Host und Pfad, nie die Parameter.
- Die Policy ist nur im Code austauschbar (Konstruktorparameter). Tests setzen damit einen Server
  auf dem eigenen Rechner ein. Keine Einstellung und keine Datei kann sie ändern.

### D-18 Cache und Offline-Verhalten der Registry

- Jede gültige Antwort wird als `cache/registry.json` gespeichert (atomar). Das Format:
  `cache_schema`, `fetched_at`, `registry_url` und das Registry-Dokument selbst.
- Beim Start liest `loadCache()` den Cache, noch bevor das Netzwerk antwortet. Ohne Verbindung
  zeigt die Bibliothek also die letzte gültige Liste.
- Der Cache wird beim Laden **erneut mit den Regeln dieser Version geprüft**. Er gilt als Daten von
  der Festplatte, nicht als vertrauenswürdiges Ergebnis.
- Ein Cache von einer anderen Adresse (zum Beispiel nach einer Umbenennung), mit unbekanntem
  Layout oder ohne gültigen Zeitstempel wird ignoriert. Ein nicht lesbarer Cache wird als
  `registry.corrupt-<Zeit>.json` beiseitegelegt.
- Eine fehlgeschlagene Aktualisierung (keine Verbindung, HTTP-Fehler, Rate Limit, unbrauchbarer
  Inhalt, zu neue Version) lässt die vorhandene Liste unverändert und überschreibt den Cache
  nicht. Die Ursache steht in `problem()`.
- Eine **gültige, aber leere** Registry ersetzt die Liste. Die Pflegenden können alle Projekte
  zurückgezogen haben, und eine alte Liste würde das verbergen.
- Kann der Cache nicht geschrieben werden, läuft alles weiter und es wird nur protokolliert.

### D-19 Zeitplan der Registry-Abfrage

Beim Start (wenn „Registry automatisch prüfen“ an ist) und danach im „Prüfintervall“ der
Einstellungen, also im selben Takt wie später die Projekt-Updates. Der Knopf „Aktualisieren“ in der
Bibliothek funktioniert immer. Eine Änderung der Einstellung setzt den Timer neu, löst aber keine
Abfrage aus. Läuft schon eine Abfrage, werden weitere ignoriert.

### D-20 Bibliothek

- Suche (Name, Beschreibung, Kategorie, Tags, ohne Beachtung der Groß- und Kleinschreibung),
  Kategorie, Tag und „Nur Empfohlene“ wirken zusammen. Empfohlene Projekte stehen zuerst, danach
  alphabetisch.
- Die Filterlisten enthalten, was die Registry hergibt, ohne Doppelte und sortiert.
- Tags erscheinen als Chips in einem Fluss-Layout, das bei wenig Platz umbricht.
- Zustände: Laden, Liste, Liste mit Hinweis („Keine Verbindung … Angezeigt wird die gespeicherte
  Liste vom …“), Fehlerkarte mit „Erneut versuchen“, leere Registry, keine Treffer mit „Filter
  zurücksetzen“.
- Installationsstatus, Versionen und Aktionen auf den Karten kommen mit den Phasen 3 bis 5, die
  Detailseite mit Phase 7.

### D-21 Versionen und Tags

- Versionen folgen Semantic Versioning 2.0.0 streng, mit einem optionalen `v` oder `V` davor (so
  heißen Git-Tags). Andere Präfixe wie `KERS_SubsystemsV0.3.1` gelten nicht als Version (siehe D-14).
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
  Aktualisierung mit der alten Version ins Backup. Projekte sollten Nutzerdaten außerhalb von
  `current/` ablegen; das ist eine Anforderung an Projekte, nicht an den Launcher.
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
  (Schema 1) nennt `product`, `version`, `launcher`, `updater`, `files` (`path`, `size`, `sha256`) und
  `remove` (Dateien früherer Versionen, die nicht mehr dazugehören). Nur Dateien aus `files` werden je
  geschrieben, nur Dateien aus `remove` je gelöscht. Was sonst im ZIP liegt, wird nicht installiert.
- **Das Manifest ist unbeglaubigte Eingabe.** Der Parser verlangt: gleiches Produkt, die Version des
  Releases, die Programmdateien von Launcher und Updater in der Liste, 1 bis 200 Dateien, höchstens 200
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
- **Grund für ein Manifest statt „alles im ZIP ersetzen“:** Der Updater darf nie raten, was zum Programm
  gehört. Die Liste mit Hashes macht den Austausch prüfbar (vorher und nachher) und schützt Nutzerdaten
  auch dann, wenn ein Release-ZIP einmal falsch gepackt wird.

### D-40 Der Updater (`LauncherUpdater.exe`) und die Transaktion

- **Eigenes Programm, gestartet als Kopie.** Der Launcher kopiert die `LauncherUpdater.exe` des *neuen*
  Releases nach `temp/updater-<Version>/`, schreibt dort `job.json` und startet die Kopie getrennt
  (`--job job.json`). So kann der Updater auch die eigene Datei und die EXE des Launchers ersetzen, und
  es gilt immer der Austausch-Code der neuen Version. Der Updater gehört zu `launcher_base` (nur Qt Core):
  kein Netzwerk, keine Fenster (D-02).
- **Reihenfolge im Updater:** Sperre `updater/updater.lock` → Log `logs/updater.log` → der Auftrag muss
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
  Dateien aus `remove` wandern ebenfalls ins Backup. **Die Programmdatei des Updaters kommt vor der des
  Launchers, die des Launchers zuletzt:** Solange sie die alte ist, lässt sich die alte Version noch
  starten. (4) Alle platzierten Dateien noch einmal gegen die Hashes prüfen. (5) Das Journal löschen.
- **Fehler:** Scheitert ein Schritt, wird der alte Zustand wiederhergestellt (`RolledBack`, Exitcode 1).
  Gelingt das nicht vollständig (`RollbackFailed`, Exitcode 3), startet der Updater den Launcher nicht
  und nennt den Backup-Ordner. Der Launcher beendet die Wiederherstellung beim nächsten Start selbst.
- **Absturz mittendrin:** Das Journal bleibt liegen. `UpdateTransaction::recover` entscheidet beim nächsten
  Start des Launchers anhand der Dateien selbst: sind alle neu, gilt das Update als abgeschlossen, sonst
  wird zurückgerollt. Getestet wird der Abbruch nach jedem einzelnen Schritt (`simulateCrashAfter`).
- **Bericht:** `updater/last-update.json` (Erfolg, zurückgerollt, nicht angewendet, fehlgeschlagen; Versionen,
  Detail, Backup-Ordner, ob der Launcher gestartet wurde). Der Launcher zeigt ihn beim nächsten Start einmal
  und löscht ihn.
- **Exitcodes:** 0 Erfolg, 1 zurückgerollt, 2 nichts geändert, 3 fehlgeschlagen, 4 ungültiger Auftrag.
  `--quiet` unterdrückt die Meldungsfenster (für Tests); ohne `--quiet` zeigt der Updater bei Problemen
  ein Windows-Meldungsfenster.
- **Beim Start des Launchers** (`UpdateStartup::run`, vor allem anderen): Läuft noch ein Updater (Sperre),
  wartet der Launcher bis zu 60 Sekunden; ist er dann nicht fertig, meldet er es und beendet sich, statt
  ein zweites Programm in einen halb ausgetauschten Ordner zu starten. Dann `recover`, dann der Bericht.
  `LibraryLayout::cleanupTemp` löscht übrig gebliebene `update-*` und `updater-*` aus `temp/`.
- **Aufräumen:** Nach Erfolg löscht der Updater den Staging-Ordner, er behält die zwei neuesten Backups in
  `updater/backups/`. Die eigene Kopie in `temp/updater-<Version>/` löscht der Launcher beim nächsten Start.

### D-41 Ablauf und Oberfläche des Launcher-Updates (`LauncherUpdateManager`)

- **Prüfung:** Drei Sekunden nach dem Start und dann im eingestellten Intervall (`updates.check_interval_minutes`,
  wie bei Projekten), aber nur, wenn „Nach Launcher-Updates suchen“ eingeschaltet ist
  (`launcherUpdateAutoCheck`). Der Knopf „Nach Updates suchen“ geht immer. Die Prüfung ist eine einzige
  API-Anfrage und wird übersprungen, wenn weniger als 10 Anfragen übrig sind (die Projekte brauchen den Rest, D-36).
- **Nie ohne Zustimmung.** Der Launcher meldet das Update, mehr nicht. „Aktualisieren“ öffnet eine Rückfrage
  mit Version und Changelog; sie sagt, dass der Launcher schließt und neu startet, dass laufende Projekte
  geöffnet bleiben und dass Einstellungen, Projekte und Downloads unverändert bleiben. Mit „Aktualisieren“
  stimmt der Nutzer dem ganzen Ablauf zu: laden, prüfen (D-39), neu starten. Ist das Paket bereit und läuft
  keine Installation, übergibt der Launcher von selbst an den Updater. Eine zweite Rückfrage gäbe es
  nur, damit der Nutzer ein Fenster mehr wegklickt. Es gibt keinen Modus „automatisch“ für den Launcher.
- **Zustände:** Idle, Prüfung, Aktuell, Verfügbar, Lädt, Prüft („Preparing“), Bereit, Wird angewendet,
  Fehlgeschlagen. Fehlerarten: Release nicht abrufbar, keine Prüfsumme, Prüfsummendatei, Download, Paket,
  Installationen laufen noch („Busy“), Übergabe an den Updater. Nach einem Fehler gilt „Erneut versuchen“
  für dieselbe Version ohne neue Rückfrage; eine neuere Version beginnt von vorn.
- **Busy:** Laufende Installationen oder Updates von Projekten (`InstallService::busyCount() > 0`) halten den
  Neustart auf, denn er würde sie beenden. Der Zustand „Bereit“ bleibt dann stehen, der Hinweis erklärt den
  Grund, und „Jetzt neu starten“ geht, sobald die Installationen fertig sind. Laufende *Projekte* stören
  nicht: Der Austausch fasst nur Dateien des Launchers an.
- **Abbrechen** geht beim Laden, beim Prüfen und im Zustand „Bereit“; der Staging-Ordner wird entfernt. Ab der
  Übergabe an den Updater ist es zu spät.
- **Übergabe:** `apply()` schreibt den Auftrag und startet die Kopie des Updaters. Gelingt das nicht, bleibt
  der Launcher offen und zeigt den Fehler (Fehlerart „Übergabe“, Staging wird entfernt). Gelingt es, meldet er
  `restartRequested`, und die Anwendung beendet sich.
- **Der Launcher aktualisiert sich nur unter seinem Programmnamen** (`KERJAMI.exe`). Läuft er unter einem
  anderen Dateinamen (umbenannt, aus einem Testordner), lehnt `apply()` die Übergabe ab, weil der Updater die
  Datei sonst nicht fände und neben dem alten Programm eine zweite Kopie entstünde.
- **Anzeige:** Dashboard (Karte „Launcher“) und „Einstellungen → Über“ zeigen dasselbe Bedienfeld
  (`ui/LauncherUpdatePanel`). Texte stehen in `ui/OperationMessages`, die Rückfrage in `ui/Dialogs`.
  Beim Start zeigt die Anwendung nach dem Fenster eine Meldung über den Ausgang des letzten Updates oder über
  eine nachgeholte Wiederherstellung (`updateReportMessage`, `recoveryMessage`).

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
  Release-Abfragen ab Phase 3. Der Abruf der Registry über `raw.githubusercontent.com` gehört nach
  bisherigem Wissen nicht dazu, ist aber nicht belegt. Der Live-Test zeigt das Budget im Log an
  (`LAUNCHER_LIVE_TESTS=1`); damit lässt sich prüfen, ob die Registry es verbraucht.
- Die Bibliothek baut bei jeder Änderung alle Karten neu. Bei der Obergrenze von 500 Projekten ist
  das unkritisch.
- Von der Release-Liste eines Repositories werden die ersten 30 Einträge gelesen. Eine Seitenabfrage
  gibt es nicht.
- Die Antwort der Release-Liste wird vollständig im Speicher gelesen (bis 4 MiB). Downloads dagegen
  werden direkt in die Datei geschrieben.
- Der Live-Test der Release-Abfrage (`LAUNCHER_LIVE_TESTS=1`) liest die Release-Liste von
  `nlohmann/json`, einem fremden öffentlichen Repository, und lädt keine Datei herunter. Es gibt
  keinen Test, der eine echte Datei von GitHub lädt: Dafür fehlt noch das Testprojekt (E-02). Die
  ganze Installationskette ist bisher nur gegen einen lokalen Server geprüft, der GitHub nachbildet
  (Adressen, Digest-Feld, Weiterleitungen sind dort nachgestellt, nicht gemessen).
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
  `current/` anlegt, liegen danach im Backup, nicht in der neuen Version (D-30).
- Das ZIP muss die Startdatei am Pfad der Registry enthalten und darf kein ZIP64, keine
  Verschlüsselung und keine Nicht-ASCII-Namen ohne UTF-8-Kennzeichen haben (D-27, D-29). Die Fehlermeldung
  nennt den Grund; die Ursache liegt dann beim Projekt, das neu packen muss.
- Die Rückfrage vor dem Deinstallieren ist in den Tests ersetzbar
  (`Dialogs::setUninstallConfirmationForTests`), weil sich ein modales Fenster nicht aus einem Test
  bedienen lässt. Die Tests prüfen, dass gefragt wird und dass „Nein“ nichts verändert. Das echte
  Fenster (Text, Schaltflächen, Standardauswahl „Abbrechen“) wurde noch nicht angesehen. Dasselbe
  gilt für die Rückfrage zum Launcher-Update (`Dialogs::setLauncherUpdateConfirmationForTests`) und für
  die Meldungsfenster des Updaters.
- **Windows Defender meldet `LauncherUpdater.exe` fälschlich als Schadsoftware** (Stand 2026-11-10,
  `Trojan:Win32/Bearfoos.A!ml`, eine Erkennung durch maschinelles Lernen). Auf dem Entwicklungsrechner wurde
  die EXE kurz nach dem Bauen in die Quarantäne verschoben, im Release- und im Debug-Build und auch
  nach dem Verschlanken des Updaters auf Qt Core allein (`launcher_base`); `KERJAMI.exe` wird nicht
  gemeldet. Ursache ist vermutlich, dass ein kleines, nicht signiertes Programm ohne Fenster Dateien
  tauscht und Programme startet. Folgen: Auf diesem Rechner schlagen die Tests, die die EXE brauchen
  (`UpdaterTests`, `LauncherUpdateTests`), fehl, bis sie neu gebaut ist. Auf dem Rechner eines Nutzers kann
  dasselbe ein Selbstupdate verhindern (der Launcher meldet dann „Übergabe“ fehlgeschlagen, das Update wird
  nicht angewendet, nichts geht kaputt). Abhilfe ist eine Code-Signatur (Phase 8, braucht ein Zertifikat
  und eine Entscheidung), eine Meldung als Fehlalarm an Microsoft oder eine Ausnahme, die der Nutzer selbst
  einrichtet. Der Launcher ändert keine Sicherheitseinstellungen, und diese Arbeit hat sie ebenfalls
  nicht geändert. Nicht umgesetzt: den Austausch als Betriebsart von `KERJAMI.exe` selbst laufen zu
  lassen (die Kopie in `temp/` startet mit `--apply-update`). Das würde die eigene EXE vermeiden, weicht aber
  vom Auftrag ab, der `LauncherUpdater.exe` nennt.
- Die Austauschlogik kommt aus dem *neuen* Release (die Kopie des Updaters), die Prüfung des Pakets aus
  dem *laufenden* Launcher. Ändert ein künftiges Release das Manifest-Schema, kann ein älterer Launcher
  es nicht lesen und lehnt es ab; ein solcher Sprung braucht einmal eine Aktualisierung von Hand. Deshalb
  bleibt Schema 1, bis es einen Grund gibt.
- Ob der neue Launcher nach dem Austausch tatsächlich startet, prüft der Updater nicht. Er startet ihn und
  merkt es nur, wenn die Datei fehlt. Ein Launcher, der gleich wieder abstürzt, wird nicht zurückgerollt; die
  Dateien der alten Version liegen in `updater/backups/` (die zwei letzten Stände bleiben erhalten).
- Das Release-ZIP ist das ganze portable Paket (etwa 30 MB). Beim Austausch bleiben Dateien mit gleichem
  Hash unangetastet, geladen wird aber immer alles.
- Dateien, die ein früheres Release mitgebracht hat und die ein neues Manifest nicht in `remove` nennt,
  bleiben liegen. Der Updater löscht nichts, was nicht ausdrücklich genannt ist.
- Der Updater zeigt Probleme mit einem schlichten Windows-Meldungsfenster in der Sprache des Programms
  (Deutsch), nicht im Stil der Anwendung. Der Normalfall ist der Bericht, den der Launcher beim nächsten
  Start anzeigt.
- Ein Selbstupdate gegen ein echtes GitHub-Release ist noch nicht getestet (das Repository hat kein
  Release). Der Test `selfUpdateEndToEnd` fährt die ganze Kette mit der echten `LauncherUpdater.exe` gegen
  einen lokalen Server, der GitHub nachbildet.
