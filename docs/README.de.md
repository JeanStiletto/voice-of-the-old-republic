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
- Shift+Enter — Das vereinheitlichte Aktionsmenü für das aktuelle Ziel öffnen (jede Aktion — angreifen, sprechen, Machtfähigkeiten, Gegenstände, Spezialfähigkeiten — in einem Menü)
- Shift+1 … Shift+7 — Eine Aktionskategorie zur Auswahl öffnen (1–3 sind die Aktionen des Ziels, 4–7 deine Machtfähigkeiten / Gegenstände / Minen)
- H — Eigene Gesundheit, aktive Effekte und ausgerüstete Waffe ansagen
- Ö (deutsches Tastaturlayout) / Backtick (US-Layout) — Untersuchen-Bildschirm für das aktuelle Ziel vorlesen
- Shift+H — Die Aktionswarteschlange öffnen (Warteschlange prüfen oder leeren)
- Shift+L — Stufenaufstieg-Bildschirm öffnen
- F1 — Die vollständige Tastenliste öffnen oder schließen; Ctrl+F1 — die Tasten für den aktuellen Bildschirm vorlesen

<h3>Mod-Tasten — Zyklus entdeckter Objekte</h3>

Ein zweiter Zyklus, zusätzlich zu Q / E, der die Objekte durchschreitet, die du im aktuellen Bereich bereits entdeckt hast — Türen, Behälter, Charaktere, Bereichsübergänge, Landmarken und deine eigenen Kartenmarker — nach Kategorie gruppiert. (Aktiviere „Erweitertes Zyklen" in den Mod-Einstellungen, um auch noch nicht gefundene Dinge einzuschließen.)

- `,` / `.` — Vorheriges / nächstes Objekt in der aktuellen Kategorie
- Shift+`,` / Shift+`.` — Vorherige / nächste Kategorie (Kreaturen, Türen, Behälter, Übergänge, Kartenmarker, …)
- Ctrl+`,` / Ctrl+`.` — Zum nächstgelegenen / entferntesten Objekt der Kategorie springen
- `-` (deutsches Tastaturlayout) oder `/` (US-Layout) — Das aktuell fokussierte Objekt ansagen
- Shift+`-` (Shift+`/`) — Automatisch zu diesem Objekt laufen
- Ctrl+`-` (Ctrl+`/`) — Eine Audio-Bake aktivieren, die den Weg beim Gehen pingt

<h3>Mod-Tasten — Orientierung und Gruppe</h3>

- AltGr (rechte Alt-Taste, allein) — Aktuelle Blickrichtung als Himmelsrichtung ansagen
- N — Kamera 90° im Uhrzeigersinn zur nächsten Himmelsrichtung drehen; ist eine Bake aktiv, stattdessen zum nächsten Wegpunkt der Bake ausrichten
- Tab — Den neuen Gruppenanführer ansagen, nachdem die Engine die Steuerung wechselt

<h3>Mod-Tasten — Sichtmodus</h3>

Drücke B, um den Sichtmodus zu aktivieren. Solange der Sichtmodus aktiv ist:

- A / D — Kamera schwenken, ohne den Charakter zu bewegen
- Enter — Mit dem Objekt interagieren, auf das die Kamera zeigt, oder dorthin automatisch laufen
- Shift+Enter — Das Aktionsmenü auf dem Kameraziel öffnen
- B erneut — Sichtmodus verlassen

<h3>Mod-Tasten — Kartenbildschirm</h3>

Bei geöffneter Spielkarte:

- Pfeiltasten / Hoch / Runter — Durch die Notizen und Landmarken der Karte zyklen
- `,` / `.` — Kartenmarker zyklen (gleiches Vokabular wie der Zyklus entdeckter Objekte)
- Shift+N — Persönlichen Kartenmarker an der aktuellen Cursor-Weltposition setzen (automatisch nach dem nächstgelegenen Raum oder Landmark benannt). Der neue Marker reiht sich sofort in den Zyklus ein und Ctrl+`-` setzt eine Bake darauf

<h3>Mod-Tasten — Submenüs</h3>

Wenn ein Mod-Submenü geöffnet ist (das vereinheitlichte Aktionsmenü, ein Kategoriemenü, die Aktionswarteschlange):

- Hoch / Runter — Fokus verschieben
- Links / Rechts — Zwischen Spalten oder Varianten wechseln
- Enter — Die fokussierte Zeile aktivieren
- Shift+Enter — (nur Aktionswarteschlange) Alle Warteschlangen-Aktionen leeren
- Esc — Submenü schließen

<h3>Mod-Tasten — kontextspezifisch</h3>

- Q oder E im Behälter-Panel — Alles nehmen / Gegenstände geben
- Q oder E im Shop-Panel — Zwischen Kaufen und Verkaufen wechseln

Im Namensfeld der Charaktererstellung (und anderen Texteingabefeldern):

- Hoch / Runter — Aktuellen Text vom Anfang neu vorlesen
- Enter — Bestätigen
- Esc — Abbrechen

<h2>Navigationssysteme im Überblick</h2>

KOTOR ist ein 3D-Rollenspiel, du verbringst also die meiste Zeit damit, dich durch Räume und um Objekte herum zu bewegen. Die Mod schichtet einige Systeme übereinander, damit du orientiert bleibst — jedes sagt sich beim Gebrauch von selbst an.

<h3>Zielwechsel — Q / E</h3>

Dein wichtigster Weg, Dinge zu finden und mit ihnen zu interagieren. Q / E schreiten durch die Kreaturen, Türen und benutzbaren Objekte, die die Kamera sehen kann; was anvisiert ist, ist das, worauf Enter und die Aktionstasten 1–7 wirken. Die Mod sagt jedes neue Ziel an.

<h3>Zyklus entdeckter Objekte — `,` / `.`</h3>

Um zu Dingen zurückzufinden, die du bereits entdeckt hast. `,` / `.` schreiten durch jedes Objekt, das du im aktuellen Bereich entdeckt hast — Türen, Behälter, Charaktere, Übergänge, Landmarken, deine eigenen Marker — nach Kategorie gruppiert. Sag eines an, lauf automatisch hin oder aktiviere eine Audio-Bake. (Mod-Einstellungen → „Erweitertes Zyklen" erweitert ihn um Dinge, die du noch nicht gefunden hast.)

<h3>Vereinheitlichtes Aktionsmenü — Shift+Enter</h3>

Ein Menü mit jeder Aktion für das aktuelle Ziel — angreifen, sprechen, Machtfähigkeiten, Gegenstände, Spezialfähigkeiten. Pfeiltasten bewegen den Fokus, Enter aktiviert. Es ersetzt die getrennten Radial-, Ziel- und Personal-Menüs des Spiels.

<h3>Karte — M</h3>

KOTORs spieleigene Karte, navigierbar gemacht. Bewege den Cursor mit den Pfeiltasten, um Gelände und Marker zu lesen, oder zykle die Kartenmarker mit `,` / `.` im selben Vokabular wie in der Welt. Der Kriegsnebel wird respektiert, und Shift+N setzt einen persönlichen Marker am Cursor.

<h3>Wandcues und Raumform-Beschreibungen</h3>

Während du dich bewegst, spielt eine kontinuierliche 3D-Audioschicht leise positionelle Klicks von den nächstgelegenen Wänden — nähere Wände klingen lauter — sodass du ein ständiges Gefühl für den Raum um dich herum behältst. Und beim Betreten eines Raumes werden sein Name, seine Form (Korridor, Kreuzung, Sackgasse, offener Raum) und die sichtbaren Ausgänge angesagt, alles live aus dem Walkmesh des Spiels berechnet.

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
