---
layout: default
title: Voice of the Old Republic — Deutsch
permalink: /docs/README.de.html
---
<h1>Voice of the Old Republic</h1>

<p><a href="/voice-of-the-old-republic/">English</a> · <strong>Deutsch</strong> · <a href="/voice-of-the-old-republic/docs/README.fr.html">Français</a> · <a href="/voice-of-the-old-republic/docs/README.it.html">Italiano</a> · <a href="/voice-of-the-old-republic/docs/README.es.html">Español</a> · <a href="/voice-of-the-old-republic/docs/README.pl.html">Polski</a> · <a href="/voice-of-the-old-republic/docs/README.ru.html">Русский</a></p>

<h2>Was ist diese Mod</h2>

**Voice of the Old Republic** ist ein Projekt, das die Star Wars: Knights of the Old Republic-Spiele für vollständig blinde Spieler zugänglich macht. Es nutzt die Prism-Sprachbrücke, um jeden Screenreader zu unterstützen, und ergänzt Navigationshilfen, Menü-Barrierefreiheit und besondere Klangsignale, die die Minispiele zugänglich machen.

Im aktuellen Stand ist Knights of the Old Republic I vollständig spielbar und durchspielbar, mit allen Minispielen, Quests und Spielweisen. Es lässt sich mit der Tastatur oder mit einem Xbox-artigen Controller spielen — die Mod bringt ihre eigene Controller-Unterstützung mit (siehe den Controller-Abschnitt unten); nur die Minispiele erwarten vorerst noch die Tastatur.

Am Port für Teil II wird gearbeitet.

Die Mod ist in alle unterstützten Sprachen übersetzt — Englisch, Deutsch, Französisch, Italienisch, Spanisch — und unterstützt außerdem eine polnische und eine russische Übersetzung. Unterstützt werden die Steam- und GoG-Version 1.03 sowie die 2004er-Version, die von den polnischen und russischen Übersetzungen verwendet wird.

<h2>Was sind die Knights of the Old Republic-Spiele</h2>

Knights of the Old Republic ist ein Paar erzählgetriebener Star-Wars-Rollenspiele, die rund viertausend Jahre vor den Filmen spielen. Das erste stammt von BioWare aus dem Jahr 2003, das zweite — Knights of the Old Republic II: The Sith Lords — von Obsidian aus dem Jahr 2004, und es spielt einige Jahre nach Teil I. Beide laufen auf derselben Engine, weshalb die Mod von einem zum anderen übertragen werden kann.

In beiden Spielen erstellst du deinen eigenen Charakter, sammelst eine Gruppe von Gefährten, reist zwischen Planeten und formst die Geschichte durch deine Dialogentscheidungen und dadurch, ob du der hellen oder der dunklen Seite der Macht folgst.

Der Kampf funktioniert in beiden gleich: Echtzeit mit Pause, aufgebaut auf den Star-Wars-d20-Regeln vom Tisch — du reihst Aktionen ein und die Würfel entscheiden sie. Jede Runde braucht 6 reale Sekunden, und du kannst jederzeit pausieren, um das Schlachtfeld zu prüfen und Spezialaktionen einzureihen, die deutlich stärker sind als die Auto-Angriffe des Charakters. Du kannst bis zu 4 Aktionen pro Charakter einreihen, bevor du den Kampf laufen lassen musst, und du kannst einige der Aktionen abbrechen, wenn der Verlauf des Kampfes eine andere Strategie verlangt.

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
3. Rechtsklick auf den Installer und **Als Administrator ausführen** wählen. Beim ersten Start warnt Windows SmartScreen vor einem „Unbekannten Herausgeber" — klicke auf **Weitere Informationen → Trotzdem ausführen**. Der Installer ist noch nicht codesigniert, diese Warnung ist also zu erwarten (im Abschnitt Fehlerbehebung steht, wie du den Download überprüfst)
4. (Empfohlen) Sichere deinen Speicherordner unter `%USERPROFILE%\Documents\Swkotor\saves\`, bevor du installierst, falls du einen laufenden Spielstand hast
5. Folge den Installer-Schritten. Der Installer erkennt deine KOTOR-Installation, installiert das Patch-Framework, spielt die Mod ein und bündelt standardmäßig den K1 Community Patch sowie die Widescreen- / Hochauflösungsmenü-Fixes
6. Starte das Spiel vom letzten Installer-Bildschirm oder über Steam

<h2>Deinstallation</h2>

Führe den Installer erneut aus und wähle die Deinstallationsoption, oder benutze „Programme und Features". Der Deinstaller entfernt nur die Dateien dieser Mod — K1CP und andere optionale Mods, die du bei der Installation gewählt hast, bleiben erhalten. Um zu einem komplett unmodifizierten KOTOR zurückzukehren, führe nach der Deinstallation in Steam „Spieldateien überprüfen" aus oder installiere bei GoG neu.

<h2>Erste Schritte</h2>

Wenn du ein neues Spiel startest, führt KOTOR dich zuerst durch die **Charaktererstellung**: Du wählst eine Klasse (Soldat, Späher oder Gauner), ein Porträt und stellst Attribute, Fertigkeiten und Vorzüge ein. Die Mod liest jeden Bildschirm vor, während du dich durch ihn bewegst — lass dir Zeit, nichts läuft gegen die Uhr.

Danach erwachst du auf der **Endar Spire**, einem Schiff der Republik, das angegriffen wird. Das ist der Tutorialbereich des Spiels. Die Mod ersetzt die Tutorial-Einblendungen des Spiels durch eigene Tastaturhinweise, die für Screenreader-Nutzer geschrieben sind, sodass du die Steuerung nebenbei lernst. Folge Trask, deinem Begleiter, in Richtung Rettungskapseln.

Ein paar Gewohnheiten, die den Spielanfang deutlich leichter machen:

- **Finde Dinge mit Q / E** und komm mit dem Zyklus `,` / `.` zu bereits gefundenen Dingen zurück (siehe die Tastenkürzel weiter unten).
- **Drücke H**, um jederzeit Gesundheit und Status zu hören, und **F1** für die vollständige Tastenliste.
- **Hör auf den Raum.** Beim Betreten eines Raumes werden Name, Form und Ausgänge angesagt, und eine leise Audioschicht hält dich beim Gehen über die nächstgelegenen Wände auf dem Laufenden.
- **Sieh dir früh die Einstellungen an.** Drücke O für die Optionen des Spiels. Unter Gameplay findest du die Auto-Pause-Optionen, die bestimmen, wann der Kampf von selbst stoppt — es lohnt sich, sie vor dem ersten echten Kampf einzurichten — und die Tastenbelegung.
- **Und ganz unten in der Optionsliste: Mod-Einstellungen.** Nahezu alles, was diese Mod hinzufügt, lässt sich dort anpassen oder ein- und ausschalten, die Tasten der Mod lassen sich neu belegen, und das Audio-Glossar spielt jedes Klangsignal mit seinem Namen ab, damit du lernst, was jedes bedeutet.

Nach der Endar Spire erreichst du **Taris**, die erste große Welt, und von dort öffnet sich die Geschichte.

<h2>Tastenkürzel</h2>

Die Mod lässt das Standard-Tastaturlayout des Spiels unverändert, mit einer ergonomischen Änderung, die der Installer bei einer Neuinstallation vornimmt (siehe den Hinweis zu Seitwärtsgehen / Kameradrehung unten). Alles, was hier nicht aufgeführt ist, verhält sich wie im unmodifizierten Spiel. Die Tasten des Spiels lassen sich unter Optionen → Gameplay → Tastenbelegung oder direkt in `swkotor.ini` neu belegen. Die Tasten, die die Mod hinzufügt, lassen sich unter Mod-Einstellungen → Tastenbelegung neu belegen.

<h3>Spieltasten, die du am häufigsten verwenden wirst</h3>

- W / S — Vorwärts / rückwärts bewegen
- A / D — Links / rechts seitwärts gehen
- Z / C — Kamera links / rechts drehen (auf einer deutschen QWERTZ-Tastatur **Y / C**)
- Q / E — Ziel links / rechts durchwechseln
- R — Standardaktion auf das aktuelle Ziel (angreifen, sprechen, öffnen)
- 1 / 2 / 3 — Die drei Aktionen im Aktionsmenü des aktuellen Ziels verwenden
- 4 / 5 / 6 / 7 — Slots für Machtfähigkeit / Medpac / Gegenstand / Mine des Spielers verwenden
- Tab — Gruppenanführer wechseln
- F — Kampf abbrechen, G — Tarnung, V — Solo-Modus, X — Waffe schwingen
- Leertaste — Pause
- Esc — Spielmenü
- F4 — Schnellspeichern, F5 — Schnellladen (im Hauptmenü sucht F5 stattdessen nach einem Mod-Update)
- I — Gruppeninventar, U — Ausrüstung, P — Charakterblatt, K — Fertigkeiten / Vorzüge / Mächte
- M — Karte, L — Aufgaben, J — Nachrichten, O — Optionen
- Maus 1 — Klick in die 3D-Welt (selten nötig; siehe Sichtmodus unten)

> **Seitwärtsgehen / Kameradrehung:** KOTORs eigene Vorgaben sind Z / C zum Seitwärtsgehen und A / D zum Drehen der Kamera. Der Barrierefreiheits-Installer tauscht das bei einer vollständigen Neuinstallation, damit beides einen bequemen Block in der unteren Tastenreihe bildet — **Seitwärtsgehen auf A / D, Kameradrehung auf Z / C**. Das ist die oben aufgeführte Belegung. Wenn du von einer bestehenden Installation aktualisiert hast oder die Tasten selbst in `swkotor.ini` änderst, können deine Tasten abweichen.

<h3>Mod-Tasten — Weltinteraktion</h3>

- Enter — Standardaktion auf dem aktuell angesagten Ziel auslösen (entspricht einem Maus-1-Klick in der Welt)
- Shift+Enter — Das vereinheitlichte Aktionsmenü für das aktuelle Ziel öffnen (jede Aktion — angreifen, sprechen, Machtfähigkeiten, Gegenstände, Spezialfähigkeiten — in einem Menü)
- Shift+1 … Shift+7 — Eine Aktionskategorie zur Auswahl öffnen (1–3 sind die Aktionen des Ziels, 4–7 deine Machtfähigkeiten / Gegenstände / Minen)
- H — Eigene Gesundheit, aktive Effekte und ausgerüstete Waffe ansagen
- Shift+H — Die Aktionswarteschlange öffnen (Warteschlange prüfen oder leeren)
- Shift+L — Stufenaufstieg-Bildschirm öffnen
- F1 — Die vollständige Tastenliste öffnen oder schließen; Ctrl+F1 — die Tasten für den aktuellen Bildschirm vorlesen
- Strg+R — Den zuletzt gesprochenen Text in die Zwischenablage kopieren (eine Dialogzeile, einen Journaleintrag, einen NPC-Namen — was zuletzt vorgelesen wurde)

<h3>Mod-Tasten — Zyklus entdeckter Objekte</h3>

Ein zweiter Zyklus, zusätzlich zu Q / E, der die Objekte durchschreitet, die du im aktuellen Bereich bereits entdeckt hast — Türen, Behälter, Charaktere, Bereichsübergänge, Landmarken und deine eigenen Kartenmarker — nach Kategorie gruppiert. (Aktiviere „Kartenweite Objektauswahl" in den Mod-Einstellungen, um auch noch nicht gefundene Dinge einzuschließen.)

- `,` / `.` — Vorheriges / nächstes Objekt in der aktuellen Kategorie
- Shift+`,` / Shift+`.` — Vorherige / nächste Kategorie (Kreaturen, Türen, Behälter, Übergänge, Kartenmarker, …)
- Ctrl+`,` / Ctrl+`.` — Zum nächstgelegenen / entferntesten Objekt der Kategorie springen
- `-` (deutsches Layout) oder `/` (US-Layout) — Das aktuell fokussierte Objekt ansagen
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

<h3>Mod-Tasten — Pazaak-Minispiel</h3>

Bei geöffnetem Pazaak-Brett:

- Hoch / Runter — Zwischen den Zonen wechseln: deine Hand, dein Tisch, der Tisch des Gegners, die Aktionen (Stehen bleiben / Zug beenden)
- Links / Rechts — Innerhalb der aktuellen Zone bewegen (leere Handplätze werden übersprungen)
- Enter — Die fokussierte Handkarte spielen oder die fokussierte Aktion auslösen
- S — Stehen bleiben
- E — Zug beenden
- C — Deine Hand vorlesen
- T — Beide Tische mit ihren Summen vorlesen
- Shift+C — Wie viele Karten der Gegner noch auf der Hand hat
- Plus/Minus-Wendekarte — Enter öffnet eine Vorzeichenauswahl; Links / Rechts wählen Plus oder Minus, Enter spielt mit diesem Vorzeichen, Esc bricht ab

Auf dem Einsatzbildschirm vor der Partie liest der oberste Eintrag deinen aktuellen Einsatz, das Tischmaximum und deine Credits vor; gehe auf „Einsatz verringern" / „Einsatz erhöhen" und drücke Enter, um den Einsatz zu ändern, dann auf die Einsatz-Schaltfläche des Spiels, um ihn zu setzen. Der Zusatzdeck-Editor liest jede Karte und jeden Deckplatz vor.

<h2>Controller</h2>

Die Mod bringt ihre eigene Controller-Unterstützung mit — KOTOR 1 hat keinen eigenen Gamepad-Code, also steuert die Mod das Pad direkt an. Jeder XInput-Controller mit Xbox-Tastenlayout funktioniert; stecke ihn vor dem Spielstart ein. Tastatur und Controller bleiben nebeneinander aktiv, und jede Spielaktion, die das Pad auslöst, läuft über deine eigene Tastenbelegung — Umbelegungen werden also übernommen. Mit verbundenem Pad erhält die Tastenliste (F1) einen Controller-Abschnitt; ohne Pad verschwindet dieser Abschnitt. Die Minispiele (Pazaak, Swoop-Rennen, das Geschütz) haben noch keine Pad-Belegung und behalten ihre Tastaturtasten.

Die Tastennamen unten folgen dem Xbox-Layout: A, B, X, Y, die linke und rechte Schultertaste (LB / RB), der linke und rechte Trigger (LT / RT) und das Drücken eines Sticks (L3 / R3).

<h3>Bewegung und Orientierung</h3>

- Linker Stick — Bewegen (alle acht Richtungen: vorwärts, rückwärts und seitwärts)
- Rechter Stick — Kamera drehen
- R3 (rechten Stick drücken) — Kamera zum nächsten Wegpunkt der Bake drehen, sonst zur nächsten Himmelsrichtung (das N der Tastatur)
- Rechter Trigger allein — Blickrichtung in Grad ansagen

<h3>Objekte und Aktionen</h3>

- Steuerkreuz links / rechts — Vorheriges / nächstes entdecktes Objekt (der `,` / `.`-Zyklus der Tastatur)
- Steuerkreuz hoch / runter — Vorherige / nächste Objektkategorie
- A — Standardaktion auf dem fokussierten Ziel (angreifen, öffnen, sprechen, aufheben)
- B — Das Aktionsmenü schließen, wenn es offen ist; sonst das normale Abbrechen der Engine
- Linke / rechte Schultertaste — Ziele links / rechts durchwechseln (das Q / E der Tastatur)
- Linker Trigger allein — Das vereinheitlichte Aktionsmenü öffnen; erneutes Drücken schließt es. Solange es offen ist: Steuerkreuz links / rechts wechseln die Kategorie, hoch / runter den Eintrag, A löst die gewählte Aktion aus
- Linker Trigger + linke Schultertaste — Audio-Bake zum fokussierten Objekt
- Rechter Trigger + rechte Schultertaste — Automatisch zum fokussierten Objekt laufen

<h3>Gruppe, Status und Spiel</h3>

- X — Gruppenanführer wechseln
- Linker Trigger + X — Eigener Zustand (das H der Tastatur)
- Rechter Trigger + X — Die Aktionswarteschlange (das Shift+H der Tastatur). Darin: Steuerkreuz hoch / runter gehen die Einträge durch, A entfernt die zuletzt eingereihte Aktion, linker Trigger + A leert die ganze Warteschlange, B schließt
- Y — Schnellmenü mit den Einträgen: Menübildschirme, Gruppenanführer, Solo-Modus, Tarnung, Schnellspeichern und Hilfe, die die Tastenliste der Mod öffnet. Solange es offen ist, bleibt die Spielfigur stehen
- L3 (linken Stick drücken) — Waffe schwingen
- Start — Pause
- Zurück-Taste (Back) — Optionen
- Beide Trigger (LT + RT) — Die Tasten für den aktuellen Bildschirm vorlesen (das Ctrl+F1 der Tastatur)

<h3>In Menüs</h3>

In den Menüs des Spiels verhält sich das Pad einfach wie die Tastatur:

- Steuerkreuz oder linker Stick — Fokus bewegen
- A — Bestätigen, B — Zurück
- Linke / rechte Schultertaste — Durch die Unterbildschirme des Spielmenüs blättern (Ausrüstung, Karte, Aufgaben, …) — so erreicht das Pad die Karte; im Behälter und im Laden wechseln sie stattdessen den Modus (nehmen / geben, kaufen / verkaufen)
- Y halten und Steuerkreuz hoch oder runter drücken — Die vollständige Beschreibung des fokussierten Eintrags blockweise vorlesen, ohne den Fokus zu bewegen
- Auf dem Kartenbildschirm schwenkt der linke Stick den Kartencursor, und das Steuerkreuz durchläuft die Kartenmarker

<h2>Navigationssysteme im Überblick</h2>

KOTOR ist ein 3D-Rollenspiel, du verbringst also die meiste Zeit damit, dich durch Räume und um Objekte herum zu bewegen. Die Mod schichtet einige Systeme übereinander, damit du orientiert bleibst — jedes sagt sich beim Gebrauch von selbst an.

<h3>Zielwechsel — Q / E</h3>

Dein wichtigster Weg, Dinge zu finden und mit ihnen zu interagieren. Q / E schreiten durch die Kreaturen, Türen und benutzbaren Objekte, die die Kamera sehen kann; was anvisiert ist, ist das, worauf Enter und die Aktionstasten 1–7 wirken. Die Mod sagt jedes neue Ziel an.

<h3>Zyklus entdeckter Objekte — `,` / `.`</h3>

Um zu Dingen zurückzufinden, die du bereits entdeckt hast. `,` / `.` schreiten durch jedes Objekt, das du im aktuellen Bereich entdeckt hast — Türen, Behälter, Charaktere, Übergänge, Landmarken, deine eigenen Marker — nach Kategorie gruppiert. Sag eines an, lauf automatisch hin oder aktiviere eine Audio-Bake. (Mod-Einstellungen → „Kartenweite Objektauswahl" erweitert ihn um Dinge, die du noch nicht gefunden hast.)

<h3>Vereinheitlichtes Aktionsmenü — Shift+Enter</h3>

Ein Menü mit jeder Aktion für das aktuelle Ziel — angreifen, sprechen, Machtfähigkeiten, Gegenstände, Spezialfähigkeiten. Pfeiltasten bewegen den Fokus, Enter aktiviert. Es ersetzt die getrennten Radial-, Ziel- und Personal-Menüs des Spiels.

<h3>Karte — M</h3>

KOTORs spieleigene Karte, navigierbar gemacht. Bewege den Cursor mit den Pfeiltasten, um Gelände und Marker zu lesen, oder zykle die Kartenmarker mit `,` / `.` im selben Vokabular wie in der Welt. Der Kriegsnebel wird respektiert, und Shift+N setzt einen persönlichen Marker am Cursor.

<h3>Wandcues und Raumform-Beschreibungen</h3>

Während du dich bewegst, spielt eine kontinuierliche 3D-Audioschicht leise positionelle Klicks von den nächstgelegenen Wänden — nähere Wände klingen lauter — sodass du ein ständiges Gefühl für den Raum um dich herum behältst. Und beim Betreten eines Raumes werden sein Name, seine Form (Korridor, Kreuzung, Sackgasse, offener Raum) und die sichtbaren Ausgänge angesagt, alles live aus dem Walkmesh des Spiels berechnet.

<h2>Mod-Einstellungen</h2>

Nahezu jedes System, das die Mod hinzufügt, lässt sich im laufenden Spiel anpassen oder ein- und ausschalten. Öffne den Optionsbildschirm des Spiels (O) und wähle ganz unten in der Liste **Mod-Einstellungen**. Hoch / Runter bewegen sich durch die Zeilen, Enter schaltet eine Einstellung um oder öffnet ein Submenü, Links / Rechts bewegen einen Regler, und Esc geht zurück.

- Kartenweite Objektauswahl — erweitert den Zyklus `,` / `.` um Objekte, die du noch nicht entdeckt hast
- Raumform-Beschreibungen — die Ansage von Raumname, Form und Ausgängen beim Betreten eines Raumes
- Wandgeräusche — die kontinuierliche positionelle Wandcue-Schicht
- Untertitel vertonter Sprecher vorlesen — die Untertitel vertonter Zeilen ansagen
- Automatisches Zielen — die Zielhilfe im Geschützturm-Minispiel
- Startvideos überspringen — wirkt beim nächsten Start
- Lautstärke der Hinweistöne und Lautstärke der Sprachansagen — Regler für die Klänge und die Sprachausgabe der Mod
- Tastenbelegung — die Tasten neu belegen, die die Mod hinzufügt
- Audio-Glossar — spielt jedes Klangsignal der Mod einzeln mit seinem Namen ab, damit du lernst, was jeder Klang bedeutet

<h2>Fehlerbehebung</h2>

<h3>Keine Sprachausgabe nach dem Spielstart</h3>

- Stelle sicher, dass dein Screenreader läuft, bevor du KOTOR startest.
- Die Prism-Sprachlaufzeit wird mit der Mod ausgeliefert und vom Installer automatisch abgelegt. Bei einer manuellen Installation prüfe, ob ihre Dateien im Spielordner vorhanden sind.
- Prüfe das neueste Patch-Log unter `<Installation>\logs\patch-*.log` auf Fehler (der **Logs sammeln**-Button des Installers sammelt es für dich).

<h3>Das Spiel stürzt beim Start ab, oder die Mod lädt nicht</h3>

- Führe den Installer als Administrator aus — er legt den `dinput8.dll`-Proxy ab, der die Mod beim Spielstart automatisch lädt.
- Prüfe, ob deine Spielversion unterstützt wird (siehe „In dieser Version nicht unterstützte Spielversionen" oben). Der Installer prüft den `swkotor.exe`-Hash und meldet eine Abweichung.
- Wenn das Spiel kürzlich aktualisiert wurde, führe den Installer erneut aus — ein Update kann den Loader überschreiben.

<h3>Die Mod lief, hörte aber nach einem Spiel-Update auf</h3>

- Steam- und GoG-Updates können die Loader-Dateien der Mod überschreiben. Führe den Installer erneut aus, um die Mod neu einzuspielen.

<h3>Tastenkürzel funktionieren nicht</h3>

- Stelle sicher, dass das Spielfenster den Fokus hat (mit Alt+Tab dorthin wechseln).
- Drücke F1. Wenn du die Tastenliste hörst, ist die Mod aktiv.
- Manche Tasten wirken nur in einem bestimmten Kontext (die Pazaak-Tasten nur am Pazaak-Brett, die Submenü-Tasten nur in einem Mod-Submenü und so weiter).

<h3>Die Kamera lässt sich nicht mehr mit den Tasten drehen</h3>

- Wahrscheinlich hast du den freien Umsehen-Modus des Spiels eingeschaltet. Die Standardbelegung des Spiels führt Feststelltaste als Umschalter für den freien Blick, sie lässt sich also leicht versehentlich treffen — drücke die Feststelltaste noch einmal. Prüfe ansonsten die Maus- und Kameraeinstellungen in den Optionen.
- Prüfe außerdem, ob du nicht im Sichtmodus der Mod (B) bist, wo A / D die Kamera schwenken, statt den Charakter zu bewegen. Drücke B erneut, um ihn zu verlassen.

<h3>Falsche Sprache</h3>

- Die Mod wählt ihre Sprache automatisch anhand der Sprache deines Spiels (aus der `dialog.tlk` des Spiels gelesen). Es gibt noch keine Sprachumschaltung im Spiel; um die Sprache der Mod zu ändern, installiere das Spiel in dieser Sprache. Die fünf Sprachen, in denen KOTOR erscheint — Englisch, Französisch, Deutsch, Italienisch, Spanisch — werden unterstützt.

<h3>Windows warnt, der Installer oder die DLL sei unsicher</h3>

Der Installer und die Mod sind nicht codesigniert. Zertifikate zum Codesignieren kosten mehrere hundert Euro pro Jahr, was für ein kostenloses Barrierefreiheitsprojekt nicht realistisch ist. Deshalb warnt Windows SmartScreen beim ersten Start des Installers und markiert die Dateien möglicherweise als von einem unbekannten Herausgeber stammend.

Um zu prüfen, ob die heruntergeladene Datei mit der auf GitHub veröffentlichten übereinstimmt, führt jedes Release eine SHA-256-Prüfsumme auf. Du kannst den Hash deines Downloads berechnen und vergleichen:

- PowerShell: `Get-FileHash <Dateiname> -Algorithm SHA256`
- Eingabeaufforderung: `certutil -hashfile <Dateiname> SHA256`

Stimmt der Hash mit dem in den Release-Notes überein, ist die Datei echt. Um die SmartScreen-Warnung zu übergehen, wähle „Weitere Informationen" und dann „Trotzdem ausführen".

<h2>Fehler melden</h2>

Der Bildschirm nach der Installation hat einen **Logs sammeln**-Button, der das neueste Patch-Log und etwaige Windows-Error-Reporting-Dumps in deinen Downloads-Ordner zippt. Hänge dieses Zip an ein [GitHub-Issue](https://github.com/JeanStiletto/voice-of-the-old-republic/issues) an und beschreibe, was du gerade getan hast. Wenn du einen Absturz reproduzieren kannst, erwähne, in welchem Bereich du warst — die Raum- oder Bereichsansage wird ihn unmittelbar davor genannt haben.

<h2>Bekannte Probleme</h2>

Für den aktuellen Stand von Fehlern, geplanten Features und Ecken siehe [docs/known-issues.md](known-issues.md).

<h2>Hinweise</h2>

<h3>Andere Barrierefreiheiten</h3>

Vorerst ist dies eine Screenreader-Barrierefreiheits-Mod. Ich bin ein vollständig blinder Entwickler, und Screenreader-Zugang ist der Bereich, den ich kenne. Ich würde wirklich gern mehr Behinderungen abdecken — Sehbehinderung, motorische Einschränkungen und so weiter —, aber Fragen wie Farbe, Kontrast und Schrift sind für mich als vollständig blinde Person abstrakt. Wenn du etwas in dieser Richtung brauchst und deine Bedürfnisse klar beschreiben und beim Testen helfen kannst, melde dich bitte. Ich würde die Mod gern für mehr Menschen ihrem Namen gerecht werden lassen.

<h3>KI-Einsatz</h3>

Der Code dieser Mod wird mit umfangreicher Unterstützung von Anthropics Claude geschrieben, mit den Opus-Modellen (die Entwicklung umspannte die Generationen Opus 4.5, 4.6 und 4.7). Mir sind die Debatten um KI-gestützte Entwicklung bewusst. Aber in einer Zeit, in der die Spielebranche die Barrierefreiheit, die wir brauchen, für Titel wie KOTOR nie geliefert hat — weder in Qualität noch in Menge —, sind diese Werkzeuge das, was ein Projekt dieser Größe für einen einzelnen blinden Entwickler machbar macht. Jede Änderung wird vor der Auslieferung im Spiel geprüft und nach Gehör getestet.

<h2>Mitwirken</h2>

Beiträge sind willkommen — insbesondere Korrekturen für Sprachen, Systemkonfigurationen oder Screenreader, die ich lokal nicht testen kann. Featurewünsche nehme ich ebenfalls gern. Bevor du anfängst, überfliege die Datei mit den bekannten Problemen oben, um zu sehen, ob deine Idee schon im Backlog steht.

- Beitragshandbuch: [CONTRIBUTING.md](../CONTRIBUTING.md)
- Architekturüberblick: [ARCHITECTURE.md](../ARCHITECTURE.md)
- Handbuch für Barrierefreiheits-Modding (allgemeines Handwerk + Besonderheiten nativer Binaries): [ACCESSIBILITY_MODDING_GUIDE.md](../ACCESSIBILITY_MODDING_GUIDE.md)
- Die gesprochenen Ansagen der Mod übersetzen: [docs/CONTRIBUTING_TRANSLATIONS.md](CONTRIBUTING_TRANSLATIONS.md)

<h2>Danksagungen</h2>

Und jetzt möchte ich einer ganzen Menge Menschen danken. Zuerst: Diese Mod stützt sich massiv auf Community-Arbeit, und viele Leute haben die wirklich schweren Dinge getan, sodass ich ihre Werkzeuge nur aufgreifen und mein eigenes Ding damit bauen musste. Außerdem war das zum Glück nicht nur ich und die KI in einer Blackbox, sondern ein ganzes Netzwerk um mich herum, das ausgeholfen, bestärkt und einfach sozial und nett gewesen ist.

Bitte schreib mir, wenn ich dich vergessen habe, oder wenn du unter einem anderen Namen genannt oder gar nicht genannt werden möchtest.

Das Reverse Engineering und das Patch-Framework, auf denen diese Mod aufsetzt, stammen von **Lane Dibello**, dessen [Kotor-Patch-Manager](https://github.com/LaneDibello/Kotor-Patch-Manager) und Ghidra-Arbeit es überhaupt erst möglich gemacht haben, das Spiel zu hooken. Dank auch an die KOTOR-Modding-Community rund um **DeadlyStream**, die die Werkzeuge und Formate herausgefunden hat, die ich nur aufgreifen und benutzen musste.

Viele Menschen haben mir beim Testen des Spiels geholfen, alle Fehler gefunden, die ich nicht finden konnte, und mir Rückmeldung gegeben, um die Schwächen und Unbequemlichkeiten des Projekts zu benennen. Ohne sie wäre daraus überhaupt kein Projekt geworden. Deshalb möchte ich danken:

- Kenny
- Berenion
- unexplained entity
- Grinvold
- dansc93
- Mojsior
- stirlock
- Druidah
- SightlessKombat
- Destranis
- Ozuaw
- mkdbzfan
- Kamilana
- ABlindFellow
- zargontheevilgod
- zersiax

**Grundlagen und Abhängigkeiten:**

- **Lane Dibello** — [Kotor-Patch-Manager](https://github.com/LaneDibello/Kotor-Patch-Manager), die reverse-engineerte Ghidra-Datenbank und das Patch-Framework
- **Prism** (Ethin P.) — plattformübergreifende Sprachbrücke, die jeden gängigen Screenreader abdeckt, mit SAPI-Fallback
- **K1 Community Patch**-Team (KOTORCommunityPatches) — gebündelte Bugfix-Schicht
- **xoreos / xoreos-tools** — Open-Source-Engine-Reimplementierung; Querverweis für Dateiformate
- **DeadlyStream**-Community — Modding-Wissensbasis

<h3>Verwendete Werkzeuge</h3>

- Claude (Anthropic) — Pair-Programming-Partner über die Opus-4.5-, 4.6- und 4.7-Generationen hinweg
- Kotor-Patch-Manager — Patch-Framework mit DLL-Injektion zur Laufzeit
- Prism — Screenreader-Sprachbrücke
- Tolk — Screenreader-Bibliothek (Fallback-Pfad)
- Ghidra — Reverse Engineering
- xoreos-tools — Headless-Extraktion von Spieldateiformaten
- K1 Community Patch — gebündelte Bugfix-Schicht der Community

<h2>Unterstütze deinen Modder</h2>

Diese Mod zu bauen hat viel Spaß gemacht und war eine echte Quelle von Selbstermächtigung, aber es hat auch viel Zeit und echtes Geld für Claude-Abonnements gekostet. Ich habe vor, diese weiterlaufen zu lassen, um das Projekt zu pflegen und es über die kommenden Jahre zu verbessern. Wenn du in der Lage und bereit bist, einmalig oder regelmäßig zu spenden, würde ich das sehr zu schätzen wissen — es würdigt die Arbeit und gibt mir eine stabile Basis, um Voice of the Old Republic und hoffentlich weitere große Barrierefreiheitsprojekte weiter zu verbessern.

[Ko-fi: ko-fi.com/jeanstiletto](https://ko-fi.com/jeanstiletto)

<h2>Lizenz</h2>

Der Quellcode der Mod steht unter der GNU General Public License v3 (siehe [LICENSE](../LICENSE)). Eingebundene Abhängigkeiten unter `third_party/` behalten ihre eigenen Lizenzen (Prism ist MPL-2.0; Tolk ist LGPL; Kotor-Patch-Manager wird unter den Bedingungen seines Upstreams gebündelt; dsoal und OpenAL Soft sind, wenn der optionale Spatial-Audio-Pfad aktiviert ist, LGPL-2.1). Das Spiel selbst und BioWares Datendateien werden von diesem Projekt nicht weiterverbreitet.

<h2>Links</h2>

- [GitHub](https://github.com/JeanStiletto/voice-of-the-old-republic)
- [Ein Problem melden](https://github.com/JeanStiletto/voice-of-the-old-republic/issues)
- [Kotor-Patch-Manager](https://github.com/LaneDibello/Kotor-Patch-Manager)
- [K1 Community Patch (DeadlyStream)](https://deadlystream.com/)
- [Ko-fi (das Projekt unterstützen)](https://ko-fi.com/jeanstiletto)

<h2>Andere Sprachen</h2>

- [English](/voice-of-the-old-republic/)
- [Français](/voice-of-the-old-republic/docs/README.fr.html)
- [Italiano](/voice-of-the-old-republic/docs/README.it.html)
- [Español](/voice-of-the-old-republic/docs/README.es.html)
- [Polski](/voice-of-the-old-republic/docs/README.pl.html)
- [Русский](/voice-of-the-old-republic/docs/README.ru.html)

Die Übersetzungen liegen in `docs/README.{de,fr,it,es,pl,ru}.md`. Um eine Übersetzung zu verbessern oder hinzuzufügen, siehe [docs/CONTRIBUTING_TRANSLATIONS.md](CONTRIBUTING_TRANSLATIONS.md).
