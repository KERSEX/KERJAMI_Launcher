# KERJAMI Projektliste (Registry)

Diese Liste ist die einzige Quelle für die Projekte, die der
[KERJAMI Launcher](https://github.com/KERSEX/KERJAMI_Launcher) anzeigt. Ein Projekt, das hier
nicht steht, kann im Launcher weder gefunden noch installiert werden.

Die Liste ist Teil des Launchers: Die Datei
[`resources/registry/projects.json`](../../resources/registry/projects.json) wird beim Bauen in die EXE
eingebaut. Es gibt keinen Abruf und kein eigenes Repository (`docs/03-entscheidungen.md`, D-43). Ein neuer
oder geänderter Eintrag erreicht die Nutzer mit dem nächsten Release des Launchers, das sich per
Selbstupdate installiert.

## So kommt ein Eintrag in den Launcher

1. Den Eintrag in `resources/registry/projects.json` ergänzen (Felder unten).
2. `.\scripts\build.ps1 -Test` ausführen. Der Test `RegistryManagerTests` schlägt fehl, wenn ein Eintrag einen
   Fehler hat, denn ein solcher Eintrag würde im Programm still verworfen.
3. Ein Release des Launchers veröffentlichen.

## Ein Projekt eintragen

Ein Eintrag gehört in die Liste `projects` in `projects.json`. Ein Projekt wird nur angezeigt, wenn
`enabled` auf `true` steht. Neue Projekte werden nie automatisch installiert; wer sie installieren
will, tut das im Launcher selbst.

```json
{
  "id": "beispiel-projekt",
  "name": "Beispiel-Projekt",
  "description": "Ein Satz, was das Projekt macht.",
  "owner": "KERSEX",
  "repository": "Beispiel_Projekt",
  "category": "Tools",
  "tags": ["Beispiel", "Tools"],
  "enabled": true,
  "featured": false,
  "release_channel": "stable",
  "tag_prefix": "BeispielProjektV",
  "asset_pattern": "Beispiel_Projekt-Windows-x64.zip",
  "executable": "Beispiel_Projekt.exe",
  "install_directory": "Beispiel_Projekt"
}
```

## Felder

| Feld | Pflicht | Bedeutung |
|---|---|---|
| `id` | ja | Eindeutige Kennung. Kleinbuchstaben, Ziffern und einzelne Bindestriche, höchstens 48 Zeichen. Sie wird lokal für Einstellungen verwendet und darf sich nach der Veröffentlichung nicht mehr ändern. |
| `name` | ja | Anzeigename, 1 bis 80 Zeichen. |
| `description` | ja | Kurzbeschreibung, höchstens 400 Zeichen. |
| `owner` | ja | GitHub-Konto oder -Organisation, das das Repository besitzt. |
| `repository` | ja | Name des GitHub-Repositories. Aus `owner` und `repository` baut der Launcher die Adressen selbst. Fertige Download-Adressen werden nicht übernommen. |
| `category` | ja | Kategorie für die Filterung, 1 bis 32 Zeichen. |
| `tags` | nein | Bis zu 8 Schlagwörter mit je höchstens 24 Zeichen. |
| `enabled` | ja | `true` zeigt das Projekt an, `false` blendet es aus. |
| `featured` | nein | `true` hebt das Projekt im Dashboard hervor. Standard `false`. |
| `release_channel` | nein | Version 1 kennt nur `stable`. Das ist auch der Standard. Pre-Releases werden nicht berücksichtigt. |
| `tag_prefix` | nein | Text, mit dem die Release-Tags des Projekts vor der Versionsnummer beginnen, zum Beispiel `KERS_SubsystemsV` für das Tag `KERS_SubsystemsV0.3.1`. Der Launcher schneidet ihn ab und liest den Rest als Version. Er besteht aus 1 bis 32 Buchstaben, Ziffern, `.`, `_` oder `-` und beginnt mit einem Buchstaben, ohne `/`. Groß- und Kleinschreibung zählt. Ohne dieses Feld ist das ganze Tag die Version (`1.2.0` oder `v1.2.0`). Mit dem Feld zählen nur Tags, die so beginnen. |
| `asset_pattern` | ja | Dateiname der Release-Datei auf GitHub. Ein `*` steht für beliebige Zeichen, zum Beispiel `Projekt-*-Windows-x64.zip`. Das Muster endet auf `.zip` (ein Archiv mit dem Programm und seinen Dateien) oder auf `.exe` (das Programm selbst, als einzelne Datei) und darf keinen Pfad enthalten. Genau eine Datei des neuesten Releases muss passen. |
| `executable` | ja | Bei einem ZIP: Pfad der Start-Datei innerhalb des Archivs, mit `/` als Trenner und auf `.exe` endend, ohne absolute Pfade und `..`. Bei einer einzelnen `.exe`: der Dateiname, unter dem das Programm installiert wird, ohne Pfad. |
| `install_directory` | ja | Ordnername unterhalb der Projektbibliothek des Launchers. Buchstaben, Ziffern, `_`, `-` und `.`, höchstens 64 Zeichen. Jedes Projekt braucht einen eigenen Ordner. |

## Was der Launcher prüft

Ungültige Einträge werden abgelehnt, ohne die übrigen Projekte zu beeinträchtigen. Abgelehnt wird
ein Eintrag unter anderem bei:

- fehlenden Pflichtfeldern oder Werten des falschen Typs,
- einer `id` oder einem `install_directory`, das ein anderer Eintrag ebenfalls verwendet (Groß- und
  Kleinschreibung zählt nicht, ein deaktivierter Eintrag zählt mit). Es werden **alle** beteiligten
  Einträge abgelehnt, denn welcher der richtige ist, lässt sich nicht sagen,
- einem `install_directory`, das einen Pfad enthält, mit einem Punkt endet oder ein reservierter
  Windows-Name ist (`CON`, `NUL`, `COM1` und so weiter),
- einem unbekannten `release_channel`,
- einem `asset_pattern` oder einer `executable`, die aus dem erlaubten Rahmen fallen,
- einem `tag_prefix`, der vorhanden, aber leer oder ungültig ist (fehlt das Feld, ist das in Ordnung),
- ungültigen Zeichen in `owner` oder `repository`.

Ein verworfener Eintrag erscheint nicht in der Bibliothek, und im Log (`launcher.registry`) steht der Grund.
Ist die ganze Datei kein gültiges JSON oder hat sie eine unbekannte `schema_version`, ist die Liste leer. Beides
fängt der Test `RegistryManagerTests` ab, bevor ein Release entsteht.

## Die Veröffentlichung eines Projekts

Damit sich ein eingetragenes Projekt installieren lässt, braucht das Repository ein GitHub-Release
(kein Pre-Release), dessen Tag eine Version nach Semantic Versioning enthält, zum Beispiel
`v1.2.0`. Heißen die Tags anders (`MeinProjektV1.2.0`), trägt das Projekt den Anfang als
`tag_prefix` ein (`MeinProjektV`). Die Versionsnummer danach muss streng aus drei Zahlen bestehen
(`1.2.0`, nicht `1.2` und nicht `01.2.0`); Tags, bei denen das nicht stimmt, werden übergangen. Das Release enthält ein ZIP-Archiv, das zu `asset_pattern` passt. Der Launcher prüft das
Archiv gegen den SHA-256-Wert, den GitHub für das Asset ausweist. Ohne vertrauenswürdige Prüfsumme
wird nichts installiert.
