---
layout: default
title: Voice of the Old Republic — Deutsch
permalink: /docs/README.de.html
---
<h1>Voice of the Old Republic</h1>

<p><a href="/voice-of-the-old-republic/">English</a> · <strong>Deutsch</strong> · <a href="/voice-of-the-old-republic/docs/README.fr.html">Français</a> · <a href="/voice-of-the-old-republic/docs/README.it.html">Italiano</a> · <a href="/voice-of-the-old-republic/docs/README.es.html">Español</a></p>

<h2>Was ist diese Mod</h2>

**Voice of the Old Republic** ist eine Screenreader- und Tastaturnavigations-Mod für **Star Wars: Knights of the Old Republic 1** (BioWare, 2003, Steam-Veröffentlichung), die es vollständig blinden Spielern ermöglicht, KOTOR 1 mit jedem modernen Screenreader zu spielen. Die Sprachausgabe läuft über die Prism-Sprachbrücke, die jeden gängigen Screenreader auf jeder gängigen Plattform unterstützt.

Die Mod wird von einem blinden Entwickler geschrieben. Jeder Arbeitsablauf — Installation, Spiel, Mitwirkung — ist so gestaltet, dass er allein mit Screenreader und Tastatur durchführbar ist.

<h2>Voraussetzungen</h2>

- Windows 10 oder neuer
- Star Wars: Knights of the Old Republic, v1.0.3 (Steam oder GoG; beide sind für unsere Zwecke byte-identisch)
- Ein Screenreader. Die Sprachausgabe läuft über Prism, das alle aktiv genutzten Screenreader unterstützt; wenn dein Screenreader mit irgendetwas anderem auf deinem System funktioniert, funktioniert er auch mit dieser Mod
- Etwa 200 MB freier Speicherplatz für die Patcher-Laufzeit, den K1 Community Patch und die mitgelieferte Sprachlaufzeit

<h3>In dieser Version nicht unterstützte Spielversionen</h3>

- Aspyr Mobile- / macOS-Ports (andere Binärdatei)
- Vorab gepatchte EXE-Dateien (UniWS-modifiziert, KOTOR-High-Resolution-Menus-modifiziert)
- Builds, deren `swkotor.exe` SHA-256 nicht mit den erkannten Steam- oder GoG-1.0.3-Hashes übereinstimmt

Wenn der Installer eine Versionsabweichung meldet, eröffne ein Issue mit dem angezeigten Hash. Die Adressdatenbank deckt Steam und GoG von Haus aus ab, und das Hinzufügen eines neuen byte-identischen Repacks ist meist eine einzeilige Manifest-Änderung.

<h2>Installation</h2>

1. Lade `VoiceOfTheOldRepublicInstaller.exe` aus dem neuesten Release auf GitHub herunter
2. Schließe KOTOR, falls es läuft
3. Rechtsklick auf den Installer und **Als Administrator ausführen** wählen. Beim ersten Start warnt Windows SmartScreen vor einem „Unbekannten Herausgeber" — klicke auf **Weitere Informationen → Trotzdem ausführen**. Der Installer ist noch nicht codesigniert, diese Warnung ist also zu erwarten
4. (Empfohlen) Sichere deinen Speicherordner unter `%USERPROFILE%\Documents\Swkotor\saves\`, bevor du installierst, falls du einen laufenden Spielstand hast
5. Folge den Installer-Schritten. Der Installer erkennt deine KOTOR-Installation, installiert das Patch-Framework, spielt die Mod ein und (standardmäßig) bündelt den K1 Community Patch sowie die Widescreen- / Hochauflösungsmenü-Fixes
6. Starte das Spiel vom letzten Installer-Bildschirm oder über Steam

Zum Deinstallieren den Installer erneut ausführen und die Deinstallationsoption wählen, oder „Programme und Features" verwenden. Der Deinstaller entfernt nur die Dateien dieser Mod — K1CP und andere optionale Mods, die du bei der Installation gewählt hast, bleiben erhalten. Um zu einem komplett unmodifizierten KOTOR zurückzukehren, nach der Deinstallation „Spieldateien überprüfen" in Steam ausführen oder bei GoG neu installieren.

<h2>Tastenkürzel</h2>

Die Mod lässt das Standard-Tastaturlayout des Spiels unverändert. Alles, was unten nicht aufgeführt ist, verhält sich wie im unmodifizierten Spiel. Jede Aktion unten kann in `swkotor.ini` (Spieltasten) neu zugewiesen werden, oder — für Mod-eigene Tasten — in einem späteren Release über einen Mod-Einstellungsbildschirm im Spiel.

<h3>Spieltasten, die du am häufigsten verwenden wirst</h3>

- W / S — Vorwärts / rückwärts bewegen
- A / D — Kamera links / rechts drehen
- Z / C — Links / rechts seitwärts gehen
- Q / E — Ziel links / rechts durchwechseln
- R — Standardaktion auf das aktuelle Ziel (angreifen, sprechen, öffnen)
- 1 / 2 / 3 — Die drei Aktionen im Aktionsmenü des aktuellen Ziels verwenden
- 4 / 5 / 6 / 7 — Slots für Machtfähigkeit / Medpac / Gegenstand / Mine des Spielers verwenden
- Tab — Gruppenanführer wechseln
- F — Kampf abbrechen, G — Tarnung, V — Solo-Modus, X — Waffe schwingen
- Leertaste — Pause
- Esc — Spielmenü
- F4 — Schnellspeichern, F5 — Schnellladen
- I — Gruppeninventar, U — Ausrüstung, P — Charakterblatt, K — Fertigkeiten / Vorzüge / Mächte
- M — Karte, L — Aufgaben, J — Nachrichten, O — Optionen
- Maus 1 — Klick in die 3D-Welt (selten nötig; siehe Sichtmodus unten)

<h3>Mod-Tasten — Weltinteraktion</h3>

- Enter — Standardaktion auf dem aktuell angesagten Ziel auslösen (entspricht einem Maus-1-Klick in der Welt)
- Shift+Enter — Radial-Aktionsmenü auf dem aktuellen Ziel öffnen
- Shift+H — Untersuchen-Bildschirm für das aktuelle Ziel vorlesen
- Shift+S — Vollständigen Statistikblock des ausgewählten Charakters vorlesen
- Shift+L — Stufenaufstieg-Bildschirm öffnen
- Shift+K — Kampfwarteschlangen-Submenü öffnen (Warteschlange prüfen oder leeren)

<h3>Mod-Tasten — Welt-Zyklus</h3>

Ein paralleler Zielzyklus, der auch Türen, Behälter, Bereichsübergänge und Kartenmarker abdeckt — Dinge, die das Q / E des Spiels nicht erfasst.

- `,` / `.` — Vorheriges / nächstes Objekt in der aktuellen Kategorie
- Shift+`,` / Shift+`.` — Vorherige / nächste Kategorie (Kreaturen, Türen, Behälter, Übergänge, Kartenmarker, …)
- `-` (deutsches Tastaturlayout) oder `/` (US-Layout) — Das aktuell fokussierte Zyklusziel ansagen
- Shift+`-` — Automatisch zu diesem Ziel laufen
- Ctrl+`-` — Eine Audio-Bake aktivieren, die den Weg zum Ziel beim Gehen pingt

<h3>Mod-Tasten — Orientierung und Gruppe</h3>

- AltGr (rechte Alt-Taste, allein) — Aktuelle Blickrichtung als Himmelsrichtung ansagen
- N — Kamera 90° im Uhrzeigersinn zur nächsten Himmelsrichtung drehen; ist eine Bake aktiv, stattdessen zum nächsten Wegpunkt der Bake ausrichten
- Tab — Den neuen Gruppenanführer ansagen, nachdem die Engine die Steuerung wechselt

<h3>Mod-Tasten — Sichtmodus</h3>

Drücke B, um den Sichtmodus zu aktivieren. Solange der Sichtmodus aktiv ist:

- A / D — Kamera schwenken, ohne den Charakter zu bewegen
- Enter — Mit dem Objekt interagieren, auf das die Kamera zeigt, oder dorthin automatisch laufen
- Shift+Enter — Radialmenü auf dem Kameraziel erzwingen
- B erneut — Sichtmodus verlassen

<h3>Mod-Tasten — Kartenbildschirm</h3>

Bei geöffneter Spielkarte:

- Pfeiltasten / Hoch / Runter — Durch die Notizen und Landmarken der Karte zyklen
- `,` / `.` — Kartenmarker zyklen (gleiches Vokabular wie der Welt-Zyklus)
- Shift+N — Persönlichen Kartenmarker an der aktuellen Cursor-Weltposition setzen (automatisch nach dem nächstgelegenen Raum oder Landmark benannt). Der neue Marker reiht sich sofort in den Zyklus ein und Ctrl+`-` setzt eine Bake darauf

<h3>Mod-Tasten — Submenüs</h3>

Wenn ein Mod-Submenü geöffnet ist (Aktionsleisten-Submenü, Kampfwarteschlangen-Submenü, Radialmenü):

- Hoch / Runter — Fokus verschieben
- Links / Rechts — Fokus über das 4×3-Raster des Radials verschieben
- Enter — Die fokussierte Zeile aktivieren
- Shift+Enter — (nur Kampfwarteschlange) Alle Warteschlangen-Aktionen leeren
- Esc — Submenü schließen

<h3>Mod-Tasten — kontextspezifisch</h3>

- Q oder E im Behälter-Panel — Alles nehmen / Gegenstände geben
- Q oder E im Shop-Panel — Zwischen Kaufen und Verkaufen wechseln

Im Namensfeld der Charaktererstellung (und anderen Texteingabefeldern):

- Hoch / Runter — Aktuellen Text vom Anfang neu vorlesen
- Enter — Bestätigen
- Esc — Abbrechen

<h2>Navigationssysteme im Überblick</h2>

KOTOR ist ein 3D-Rollenspiel, sodass die meiste Spielzeit damit verbracht wird, einen Charakter durch Räume und um Objekte herum zu bewegen. Die Mod bietet dir mehrere Navigationsweisen, die übereinander geschichtet sind.

<h3>Zielwechsel (Q / E)</h3>

Der spieleigene Zielzyklus. Erfasst Kreaturen, Türen und benutzbare Platzierbare im Umkreis von etwa 30 Metern, die die Kamera sehen kann. Q schreitet nach links, E nach rechts. Was anvisiert ist, ist auch das, worauf R, 1, 2, 3 und die Aktionsleiste wirken. Die Mod sagt jedes neue Ziel an.

<h3>Welt-Zyklus (`,` / `.`)</h3>

Ein zweiter Zyklus, den die Mod über Q / E legt. Deckt ab, was Q / E nicht erfasst — Bereichsübergänge, Wegpunkte, deine eigenen Kartenmarker — und gruppiert alles nach Kategorie, damit du eine Art Dinge auf einmal durchgehen kannst. Verwende `-` zum Ansagen, Shift+`-` zum automatischen Hingehen, Ctrl+`-` zum Aktivieren einer Audio-Bake.

<h3>Raumform-Beschreibungen</h3>

Beim Betreten eines neuen Raumes nennt die Mod den Bereichsnamen, die Form des Raumes (Korridor, Kreuzung, Sackgasse, offener Raum) und die von deiner aktuellen Stelle aus sichtbaren Ausgänge. Die Form wird live aus dem Walkmesh des Spiels berechnet, nicht aus einer von Hand verfassten Beschreibung, sodass sie über jeden Bereich des Spiels hinweg präzise bleibt.

<h3>Wandabstands-Audiocues</h3>

Eine kontinuierliche 3D-Audioschicht spielt leise positionelle Klicks an den dir nächstgelegenen Wänden. Je näher die Wand, desto lauter der Cue; je weiter du entfernt bist, desto leiser. Beim Bewegen ändern sich Tonhöhe, Lautstärke und Richtung der Cues und geben dir ein ständiges räumliches Gefühl dafür, wo die Wände sind, ohne dass du sie abfragen musst. Das ist das Haupt-„Sieh den Raum"-Feature der Mod und am nützlichsten in engen Innenräumen.

<h3>Sichtmodus (B)</h3>

Ein „Schau ohne zu gehen"-Modus. Drücke B und deine W / S-Bewegung wird eingefroren; A / D schwenkt nun die Kamera frei, ohne deinen Charakter zu drehen. Von hier aus kannst du die Kamera auf ein entferntes Objekt richten, Enter drücken, um automatisch dorthin zu laufen, oder Shift+Enter, um das Radial darauf zu öffnen. Nützlich für entfernte Platzierbare, die nicht im Q / E- oder `,` / `.`-Zyklus sind, und um einen großen Raum zu vermessen.

<h3>Kartenmodus (M)</h3>

KOTORs spieleigene Karte. Die Mod macht jeden Kartenmarker (Türen, Übergänge, Aufgabenmarker, deine eigenen Shift+N-Marker) mit `,` / `.` zyklbar und sagt sie mit demselben Vokabular wie in der Welt an. Der Kriegsnebel wird respektiert — unerforschte Marker bleiben verborgen, bis du sie gesehen hast. Shift+N setzt einen persönlichen Marker am Cursor, der bestehen bleibt, bis das Panel geschlossen wird, und sich sofort in den Zyklus einreiht.

<h2>Fehler melden</h2>

Der Bildschirm nach der Installation hat einen **Logs sammeln**-Button, der das neueste Patch-Log und etwaige Windows-Error-Reporting-Dumps in deinen Downloads-Ordner zippt. Hänge dieses Zip an ein [GitHub-Issue](https://github.com/JeanStiletto/voice-of-the-old-republic/issues) an und beschreibe, was du gerade getan hast. Wenn du einen Absturz reproduzieren kannst, erwähne, in welchem Bereich du warst — die Raum- oder Bereichsansage wird ihn unmittelbar davor genannt haben.

<h2>Bekannte Probleme</h2>

Für den aktuellen Stand von Fehlern, geplanten Features und Ecken siehe [docs/known-issues.md](known-issues.md).

<h2>Mitwirken</h2>

Beiträge sind willkommen — insbesondere Korrekturen für Sprachen, Systemkonfigurationen oder Screenreader, die der Entwickler nicht lokal testen kann. Bevor du anfängst, überfliege die bekannten Probleme oben, um zu sehen, ob deine Idee schon im Backlog steht.

- Beitragshandbuch: [CONTRIBUTING.md](../CONTRIBUTING.md)
- Architekturüberblick: [ARCHITECTURE.md](../ARCHITECTURE.md)

<h2>KI-Einsatz</h2>

Der Code der Mod wird mit umfangreicher Unterstützung von Anthropics Claude (Opus-Reihe) geschrieben. In einer Spielebranche, die sich historisch geweigert hat, native Barrierefreiheit für Titel wie KOTOR auszuliefern, ist KI-unterstütztes Modden das, was ein Projekt dieser Größenordnung für einen einzelnen blinden Entwickler überhaupt machbar macht. Jede Änderung wird vom Autor vor der Auslieferung im Spiel geprüft und getestet.

<h2>Lizenz</h2>

Der Quellcode der Mod steht unter der GNU General Public License v3 (siehe [LICENSE](../LICENSE)). Eingebundene Abhängigkeiten unter `third_party/` behalten ihre eigenen Lizenzen (Prism ist MPL-2.0; Tolk ist LGPL; Kotor-Patch-Manager wird unter den Bedingungen seines Upstreams gebündelt; dsoal und OpenAL Soft sind, wenn der optionale Spatial-Audio-Pfad aktiviert ist, LGPL-2.1). Das Spiel selbst und BioWares Datendateien werden von diesem Projekt nicht weiterverbreitet.

<h2>Danksagungen</h2>

- **Lane Dibello** — [Kotor-Patch-Manager](https://github.com/LaneDibello/Kotor-Patch-Manager), die reverse-engineerte Ghidra-Datenbank und das Patch-Framework, auf dem diese Mod aufbaut
- **Prism** (Ethin P.) — plattformübergreifende Sprachbrücke, die jeden gängigen Screenreader abdeckt, mit SAPI-Fallback
- **K1 Community Patch**-Team (KOTORCommunityPatches) — gebündelte Bugfix-Schicht
- **xoreos / xoreos-tools** — Open-Source-Engine-Reimplementierung; Querverweis für Dateiformate
- **DeadlyStream**-Community — Modding-Wissensbasis
- **Claude (Anthropic)** — Pair-Programming-Partner über die Opus-4.5-, 4.6- und 4.7-Generationen hinweg
