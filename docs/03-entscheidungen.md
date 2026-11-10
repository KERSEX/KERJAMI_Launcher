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

### E-04 ZIP-Bibliothek

- libarchive oder QuaZip, beide müssen selbst gebaut werden. Entscheidung in Phase 4.

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

## Bekannte Grenzen

- `PathManager::isInside` vergleicht Pfade lexikalisch. Symbolische Links und Junctions werden
  nicht aufgelöst. Für die Archiv-Extraktion in Phase 4 kommt eine Prüfung der tatsächlichen
  Ziele hinzu.
- Der Code ist nur mit clang (llvm-mingw) gebaut. Der MSVC-Zweig in `CMakeLists.txt` ist
  ungetestet.
- Ein Debug-Build linkt gegen die Release-Bibliotheken von Qt, weil es keine Debug-Variante
  gibt. In Qt-internen Code lässt sich damit nicht hineinsteppen.
- Die Einstellungen für Registry-Prüfung, Intervall und Launcher-Updates werden gespeichert,
  haben aber erst ab Phase 2, 5 und 6 eine Wirkung.
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
