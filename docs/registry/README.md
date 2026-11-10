# KERJAMI Project Registry

Diese Registry ist die einzige Quelle für die Projekte, die der
[KERJAMI Launcher](https://github.com/KERSEX/KERJAMI_Launcher) anzeigt. Ein Projekt, das hier
nicht steht, kann im Launcher weder gefunden noch installiert werden.

Der Launcher lädt die Datei [`projects.json`](projects.json) vom Branch `main`. Die Adresse ist im
Launcher fest eingebaut. Das Repository muss öffentlich bleiben, weil der Launcher ohne Anmeldung
arbeitet.

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
| `asset_pattern` | ja | Dateiname des ZIP-Archivs im GitHub-Release. Ein `*` steht für beliebige Zeichen, zum Beispiel `Projekt-*-Windows-x64.zip`. Das Muster muss auf `.zip` enden und darf keinen Pfad enthalten. |
| `executable` | ja | Pfad der Start-Datei innerhalb des Archivs, mit `/` als Trenner und auf `.exe` endend. Absolute Pfade und `..` sind nicht erlaubt. |
| `install_directory` | ja | Ordnername unterhalb der Projektbibliothek des Launchers. Buchstaben, Ziffern, `_`, `-` und `.`, höchstens 64 Zeichen. Jedes Projekt braucht einen eigenen Ordner. |

## Was der Launcher prüft

Ungültige Einträge werden abgelehnt, ohne die übrigen Projekte zu beeinträchtigen. Abgelehnt wird
ein Eintrag unter anderem bei:

- fehlenden Pflichtfeldern oder Werten des falschen Typs,
- einer `id` oder einem `install_directory`, das ein anderer Eintrag schon verwendet (Groß- und
  Kleinschreibung zählt nicht),
- einem `install_directory`, das einen Pfad enthält, mit einem Punkt endet oder ein reservierter
  Windows-Name ist (`CON`, `NUL`, `COM1` und so weiter),
- einem unbekannten `release_channel`,
- einem `asset_pattern` oder einer `executable`, die aus dem erlaubten Rahmen fallen,
- ungültigen Zeichen in `owner` oder `repository`.

Ist die ganze Datei kein gültiges JSON oder hat sie eine unbekannte `schema_version`, behält der
Launcher die zuletzt geladene gültige Fassung. Prüft den Eintrag deshalb, bevor ihr ihn
veröffentlicht, zum Beispiel mit einem JSON-Validator.

## Die Veröffentlichung eines Projekts

Damit sich ein eingetragenes Projekt installieren lässt, braucht das Repository ein GitHub-Release
(kein Pre-Release), dessen Tag eine Version nach Semantic Versioning enthält, zum Beispiel
`v1.2.0`. Das Release enthält ein ZIP-Archiv, das zu `asset_pattern` passt. Der Launcher prüft das
Archiv gegen den SHA-256-Wert, den GitHub für das Asset ausweist. Ohne vertrauenswürdige Prüfsumme
wird nichts installiert.
