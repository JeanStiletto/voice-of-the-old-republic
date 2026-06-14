---
layout: default
title: Voice of the Old Republic — Italiano
permalink: /docs/README.it.html
---
<h1>Voice of the Old Republic</h1>

<p><a href="/voice-of-the-old-republic/">English</a> · <a href="/voice-of-the-old-republic/docs/README.de.html">Deutsch</a> · <a href="/voice-of-the-old-republic/docs/README.fr.html">Français</a> · <strong>Italiano</strong> · <a href="/voice-of-the-old-republic/docs/README.es.html">Español</a></p>

<h2>Cos'è questa mod</h2>

**Voice of the Old Republic** è una mod di lettore dello schermo e navigazione da tastiera per **Star Wars: Knights of the Old Republic 1** (BioWare, 2003, versione Steam) che permette ai giocatori completamente non vedenti di giocare a KOTOR 1 con qualsiasi lettore dello schermo moderno. La sintesi vocale passa attraverso il ponte vocale Prism, che supporta ogni principale lettore dello schermo su ogni principale piattaforma.

La mod è scritta da uno sviluppatore non vedente. Ogni flusso di lavoro — installazione, gioco, contribuzione — è progettato per essere eseguibile con solo un lettore dello schermo e una tastiera.

<h2>Requisiti</h2>

- Windows 10 o successivo
- Star Wars: Knights of the Old Republic, v1.0.3 (Steam o GoG; entrambi sono identici a livello di byte per i nostri scopi)
- Un lettore dello schermo. La sintesi vocale passa attraverso Prism, che supporta l'intera gamma di lettori dello schermo in uso attivo; se il tuo lettore dello schermo funziona con qualsiasi altra cosa sul tuo sistema, funzionerà con questa mod
- Circa 200 MB di spazio libero su disco per il runtime del patcher, il K1 Community Patch e il runtime vocale incluso

<h3>Versioni del gioco non supportate in questa release</h3>

- Port Aspyr mobile / macOS (binario diverso)
- Eseguibili già patchati (modificati con UniWS, modificati con KOTOR High-Resolution Menus)
- Build il cui SHA-256 di `swkotor.exe` non corrisponde agli hash Steam o GoG 1.0.3 riconosciuti

Se l'installer segnala una versione non corrispondente, apri una issue con l'hash mostrato. Il database degli indirizzi copre Steam e GoG di default, e aggiungere un nuovo repack identico a livello di byte è di solito una modifica di una riga nel manifest.

<h2>Installazione</h2>

1. Scarica `VoiceOfTheOldRepublicInstaller.exe` dall'ultima release su GitHub
2. Chiudi KOTOR se è in esecuzione
3. Clicca con il tasto destro sull'installer e scegli **Esegui come amministratore**. Al primo avvio Windows SmartScreen avviserà di un "Editore sconosciuto" — clicca su **Ulteriori informazioni → Esegui comunque**. L'installer non è ancora firmato digitalmente, quindi questo avviso è atteso
4. (Consigliato) Esegui il backup della cartella dei salvataggi in `%USERPROFILE%\Documents\Swkotor\saves\` prima di installare se hai una partita in corso
5. Segui le schermate dell'installer. Rileverà la tua installazione di KOTOR, installerà il framework di patch, dispiegherà la mod e (per impostazione predefinita) includerà il K1 Community Patch più le correzioni widescreen / menu ad alta risoluzione
6. Avvia il gioco dalla schermata finale dell'installer o da Steam

Per disinstallare, esegui di nuovo l'installer e scegli l'opzione di disinstallazione, oppure usa "Programmi e funzionalità". Il disinstallatore rimuove solo i file di questa mod — K1CP e qualsiasi altra mod opzionale che hai scelto all'installazione restano al loro posto. Per tornare a un KOTOR completamente vanilla, usa "Verifica integrità dei file di gioco" di Steam o reinstalla da GoG dopo la disinstallazione.

<h2>Scorciatoie da tastiera</h2>

La mod mantiene intatta la mappa dei tasti predefinita del gioco. Tutto ciò che non è elencato qui sotto si comporta come nel gioco non modificato. Ogni azione qui sotto può essere riassegnata in `swkotor.ini` (tasti del gioco) o, per i tasti aggiunti dalla mod, sarà riassegnabile da una schermata di impostazioni in-game in una release successiva.

<h3>Tasti del gioco che userai di più</h3>

- W / S — Muovi avanti / indietro
- A / D — Ruota la telecamera a sinistra / destra
- Z / C — Spostati lateralmente a sinistra / destra
- Q / E — Cicla bersaglio a sinistra / destra
- R — Azione predefinita sul bersaglio attuale (attaccare, parlare, aprire)
- 1 / 2 / 3 — Usa le tre azioni del menu azioni del bersaglio attuale
- 4 / 5 / 6 / 7 — Usa gli slot potere della Forza / medpac / oggetto / mina del giocatore
- Tab — Cambia capogruppo
- F — Annulla combattimento, G — Furtività, V — Modalità solo, X — Florilegio dell'arma
- Barra spaziatrice — Pausa
- Esc — Menu di gioco
- F4 — Salvataggio rapido, F5 — Caricamento rapido
- I — Inventario del gruppo, U — Equipaggiamento, P — Scheda del personaggio, K — Abilità / talenti / poteri
- M — Mappa, L — Missioni, J — Messaggi, O — Opzioni
- Mouse 1 — Clic nel mondo 3D (raramente necessario; vedi modalità vista qui sotto)

<h3>Tasti della mod — interazione con il mondo</h3>

- Invio — Attiva l'azione predefinita sul bersaglio attualmente annunciato (equivalente a un clic Mouse 1 nel mondo)
- Shift+Invio — Apri il menu azioni unificato per il bersaglio attuale (ogni azione — attacca, parla, poteri della Forza, oggetti, abilità speciali — in un solo menu)
- Shift+1 … Shift+7 — Apri una categoria di azioni per scegliere al suo interno (1–3 sono le azioni del bersaglio, 4–7 i tuoi poteri della Forza / oggetti / mine)
- H — Annuncia la tua salute, gli effetti attivi e l'arma equipaggiata
- Ö (layout tedesco) / Accento grave (layout US) — Leggi il pannello Esamina per il bersaglio attuale
- Shift+H — Apri la coda azioni (rivedi o svuota le azioni in coda)
- Shift+L — Apri il pannello di passaggio di livello
- F1 — Apri o chiudi l'elenco completo dei tasti; Ctrl+F1 — leggi i tasti per la schermata attuale

<h3>Tasti della mod — ciclo degli oggetti scoperti</h3>

Un secondo ciclo, sopra Q / E, che scorre gli oggetti che hai già scoperto nell'area attuale — porte, contenitori, personaggi, transizioni di area, punti di riferimento e i tuoi marcatori di mappa — raggruppati per categoria. (Attiva "Ciclo esteso" nelle Impostazioni della mod per includere anche ciò che non hai ancora trovato.)

- `,` / `.` — Oggetto precedente / successivo nella categoria corrente
- Shift+`,` / Shift+`.` — Categoria precedente / successiva (creature, porte, contenitori, transizioni, marcatori di mappa, …)
- Ctrl+`,` / Ctrl+`.` — Salta all'oggetto più vicino / più lontano della categoria
- `/` (layout US) o `-` (layout tedesco) — Annuncia l'oggetto attualmente focalizzato
- Shift+`/` (Shift+`-`) — Cammino automatico verso quell'oggetto
- Ctrl+`/` (Ctrl+`-`) — Arma una baliza audio che pinga il percorso mentre ti muovi

<h3>Tasti della mod — orientamento e gruppo</h3>

- AltGr (Alt destro, da solo) — Annuncia l'orientamento attuale come direzione cardinale
- N — Ruota la telecamera di 90° in senso orario alla prossima direzione cardinale; se una baliza è armata, punta invece al prossimo waypoint della baliza
- Tab — Annuncia il nuovo capogruppo dopo che il motore ha ciclato il controllo

<h3>Tasti della mod — modalità vista</h3>

Premi B per entrare in modalità vista. Mentre la modalità vista è attiva:

- A / D — Panoramica della telecamera senza muovere il personaggio
- Invio — Interagisci con qualunque cosa la telecamera stia puntando, o cammino automatico verso quel punto
- Shift+Invio — Apri il menu azioni sul bersaglio della telecamera
- B di nuovo — Esci dalla modalità vista

<h3>Tasti della mod — schermata mappa</h3>

Mentre la mappa di gioco è aperta:

- Frecce / Su / Giù — Cicla attraverso le note e i punti di riferimento della mappa
- `,` / `.` — Cicla i marcatori di mappa (stesso vocabolario del ciclo degli oggetti scoperti)
- Shift+N — Posiziona un marcatore di mappa personale alla posizione mondo attuale del cursore (denominato automaticamente in base alla stanza o al punto di riferimento più vicino). Il nuovo marcatore entra immediatamente nel ciclo e Ctrl+`-` posiziona una baliza su di esso

<h3>Tasti della mod — sottomenu</h3>

Quando un sottomenu della mod è aperto (il menu azioni unificato, una categoria di azioni, la coda azioni):

- Su / Giù — Sposta il focus
- Sinistra / Destra — Passa tra colonne o varianti
- Invio — Attiva la riga focalizzata
- Shift+Invio — (solo coda azioni) Svuota tutte le azioni in coda
- Esc — Chiudi il sottomenu

<h3>Tasti della mod — specifici per contesto</h3>

- Q o E dentro un pannello Contenitore — Prendi tutto / dai oggetti
- Q o E dentro un pannello Negozio — Passa tra Compra e Vendi

Dentro il campo nome della creazione personaggio (e altre caselle di input testo):

- Su / Giù — Rileggi il testo attuale dall'inizio
- Invio — Invia
- Esc — Annulla

<h2>Sistemi di navigazione a colpo d'occhio</h2>

KOTOR è un GdR 3D, quindi passi la maggior parte del tempo muovendoti attraverso stanze e attorno a oggetti. La mod sovrappone alcuni sistemi per tenerti orientato — ognuno si annuncia da solo mentre lo usi.

<h3>Ciclo bersaglio — Q / E</h3>

Il tuo modo principale per trovare le cose e agire su di esse. Q / E scorrono le creature, le porte e gli oggetti usabili che la telecamera può vedere; ciò che è bersagliato è ciò su cui agiscono Invio e i tasti azione 1–7. La mod annuncia ogni nuovo bersaglio.

<h3>Ciclo degli oggetti scoperti — `,` / `.`</h3>

Per ritrovare le cose che hai già scoperto. `,` / `.` scorrono ogni oggetto che hai scoperto nell'area attuale — porte, contenitori, personaggi, transizioni, punti di riferimento, i tuoi marcatori — raggruppati per categoria. Annunciane uno, raggiungilo col cammino automatico, o arma una baliza audio. (Impostazioni della mod → "Ciclo esteso" lo amplia per includere ciò che non hai ancora trovato.)

<h3>Menu azioni unificato — Shift+Invio</h3>

Un solo menu con ogni azione per il bersaglio attuale — attacca, parla, poteri della Forza, oggetti, abilità speciali. Le frecce spostano il focus, Invio attiva. Sostituisce i menu radiale, bersaglio e personale separati del gioco.

<h3>Mappa — M</h3>

La mappa integrata in KOTOR, resa navigabile. Sposta il cursore con le frecce per leggere il terreno e i marcatori, o scorri i marcatori della mappa con `,` / `.` con lo stesso vocabolario usato nel mondo. La nebbia di guerra è rispettata, e Shift+N posiziona un marcatore personale al cursore.

<h3>Indicatori dei muri e descrizioni della forma delle stanze</h3>

Mentre ti muovi, uno strato audio 3D continuo riproduce morbidi clic posizionali dai muri più vicini — i muri più vicini suonano più forte — così mantieni un senso costante dello spazio attorno a te. Ed entrare in una stanza ne pronuncia il nome, la forma (corridoio, incrocio, vicolo cieco, spazio aperto) e le uscite visibili, tutto calcolato in tempo reale dal walkmesh del gioco.

<h2>Segnalare bug</h2>

La schermata post-installazione dell'installer ha un pulsante **Raccogli log** che zippa il log di patch più recente e qualsiasi dump di Windows Error Reporting nella tua cartella Download. Allega quel zip a una [issue su GitHub](https://github.com/JeanStiletto/voice-of-the-old-republic/issues) e descrivi cosa stavi facendo. Se puoi riprodurre un crash, menziona in quale area ti trovavi — l'annuncio di stanza o area l'avrà detto subito prima.

<h2>Problemi noti</h2>

Per il backlog attuale di bug, funzionalità pianificate e asperità, vedi [docs/known-issues.md](known-issues.md).

<h2>Contribuire</h2>

I contributi sono benvenuti — specialmente correzioni per lingue, configurazioni di sistema o lettori dello schermo che lo sviluppatore non può testare localmente. Prima di iniziare il lavoro, scorri il file dei problemi noti sopra per vedere se la tua idea è già nel backlog.

- Guida ai contributi: [CONTRIBUTING.md](../CONTRIBUTING.md)
- Panoramica dell'architettura: [ARCHITECTURE.md](../ARCHITECTURE.md)

<h2>Uso dell'IA</h2>

Il codice della mod è scritto con un'assistenza importante da Claude di Anthropic (serie Opus). In un'industria dei videogiochi che ha storicamente rifiutato di consegnare accessibilità nativa per titoli come KOTOR, il modding assistito da IA è ciò che rende un progetto di queste dimensioni fattibile per un singolo sviluppatore non vedente. Ogni modifica viene revisionata e testata in-game dall'autore prima di essere consegnata.

<h2>Licenza</h2>

Il codice sorgente della mod è licenziato sotto la GNU General Public License v3 (vedi [LICENSE](../LICENSE)). Le dipendenze incorporate sotto `third_party/` mantengono le proprie licenze (Prism è MPL-2.0; Tolk è LGPL; Kotor-Patch-Manager è incorporato secondo i termini del suo upstream; dsoal e OpenAL Soft, quando il percorso spatial-audio opzionale è abilitato, sono LGPL-2.1). Il gioco stesso e i file dati di BioWare non sono ridistribuiti da questo progetto.

<h2>Riconoscimenti</h2>

- **Lane Dibello** — [Kotor-Patch-Manager](https://github.com/LaneDibello/Kotor-Patch-Manager), il database Ghidra derivato dal reverse engineering, e il framework di patch su cui questa mod gira
- **Prism** (Ethin P.) — ponte vocale multipiattaforma che copre ogni principale lettore dello schermo, con fallback SAPI
- Il team del **K1 Community Patch** (KOTORCommunityPatches) — strato di correzione bug incluso
- **xoreos / xoreos-tools** — reimplementazione open-source del motore; riferimento incrociato per i formati di file
- La comunità **DeadlyStream** — base di conoscenza del modding
- **Claude (Anthropic)** — partner di pair-programming attraverso le generazioni Opus 4.5, 4.6 e 4.7
