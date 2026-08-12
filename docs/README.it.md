---
layout: default
title: Voice of the Old Republic — Italiano
permalink: /docs/README.it.html
---
<h1>Voice of the Old Republic</h1>

<p><a href="/voice-of-the-old-republic/">English</a> · <a href="/voice-of-the-old-republic/docs/README.de.html">Deutsch</a> · <a href="/voice-of-the-old-republic/docs/README.fr.html">Français</a> · <strong>Italiano</strong> · <a href="/voice-of-the-old-republic/docs/README.es.html">Español</a> · <a href="/voice-of-the-old-republic/docs/README.pl.html">Polski</a> · <a href="/voice-of-the-old-republic/docs/README.ru.html">Русский</a></p>

<h2>Che cos'è questa mod</h2>

**Voice of the Old Republic** è un progetto per rendere i giochi Star Wars: Knights of the Old Republic accessibili ai giocatori completamente ciechi. Usa il ponte vocale Prism per supportare ogni screen reader e aggiunge aiuti alla navigazione, accessibilità dei menu e segnali sonori dedicati che rendono accessibili i minigiochi.

Allo stato attuale Knights of the Old Republic I è completamente giocabile e completabile, con tutti i minigiochi, le missioni e gli stili di gioco. Si può giocare con la tastiera o con un controller in stile Xbox — la mod include il proprio supporto ai controller (vedi la sezione controller più sotto); solo i minigiochi richiedono ancora la tastiera.

Il port della parte II è in lavorazione.

La mod è tradotta in tutte le lingue supportate — inglese, tedesco, francese, italiano, spagnolo — e supporta inoltre una traduzione polacca e una russa. Supporta la versione 1.03 di Steam e GoG e la versione del 2004 usata dalle traduzioni polacca e russa.

<h2>Cosa sono i giochi Knights of the Old Republic</h2>

Knights of the Old Republic è una coppia di giochi di ruolo di Star Wars guidati dalla storia, ambientati circa quattromila anni prima dei film. Il primo è stato realizzato da BioWare nel 2003; il secondo — Knights of the Old Republic II: The Sith Lords — da Obsidian nel 2004, ed è ambientato qualche anno dopo la parte I. Entrambi girano sullo stesso motore, ed è per questo che la mod può passare dall'uno all'altro.

In entrambi i giochi crei il tuo personaggio, raduni un gruppo di compagni, viaggi tra i pianeti e dai forma alla storia con le tue scelte nei dialoghi e a seconda che tu segua il lato chiaro o il lato oscuro della Forza.

Il combattimento funziona allo stesso modo in entrambi: in tempo reale con pausa, costruito sulle regole da tavolo di Star Wars d20 — metti in coda le azioni e i dadi le risolvono. Ogni round dura 6 secondi reali e puoi mettere in pausa in qualsiasi momento per esaminare il campo di battaglia e accodare azioni speciali molto più forti degli attacchi automatici del personaggio. Puoi accodare fino a 4 azioni per personaggio prima di dover lasciar scorrere il combattimento, e puoi annullare alcune azioni se gli eventi dello scontro ti obbligano a cambiare strategia.

<h2>Requisiti</h2>

- Windows 10 o successivo
- Star Wars: Knights of the Old Republic, v1.0.3 (Steam o GoG; per i nostri scopi sono identici byte per byte)
- Uno screen reader. La voce passa da Prism, che supporta tutti gli screen reader in uso attivo; se il tuo screen reader funziona con qualunque altra cosa sul tuo sistema, funzionerà con questa mod
- Circa 200 MB di spazio libero su disco per il runtime del patcher, il K1 Community Patch e il runtime vocale incluso

<h3>Versioni del gioco non supportate in questa release</h3>

- Port Aspyr mobile / macOS (binario diverso)
- Eseguibili già modificati (modificati da UniWS, da KOTOR High-Resolution Menus)
- Build il cui SHA-256 di `swkotor.exe` non corrisponde agli hash Steam o GoG 1.0.3 riconosciuti

Se l'installer segnala una discrepanza di versione, apri una segnalazione indicando l'hash mostrato. Il database degli indirizzi copre Steam e GoG di serie, e aggiungere un nuovo repack identico byte per byte di solito richiede una sola riga di manifest.

<h2>Installazione</h2>

1. Scarica `VoiceOfTheOldRepublicInstaller.exe` dall'ultima release su GitHub
2. Chiudi KOTOR se è in esecuzione
3. Fai clic destro sull'installer e scegli **Esegui come amministratore**. Al primo avvio Windows SmartScreen avviserà di un "editore sconosciuto" — fai clic su **Ulteriori informazioni → Esegui comunque**. L'installer non è ancora firmato digitalmente, quindi questo avviso è normale (vedi la sezione Risoluzione dei problemi per verificare il download)
4. (Consigliato) Fai una copia della cartella dei salvataggi in `%USERPROFILE%\Documents\Swkotor\saves\` prima di installare, se hai una partita in corso
5. Segui le schermate dell'installer. Rileverà la tua installazione di KOTOR, installerà il framework di patch, applicherà la mod e, per impostazione predefinita, includerà il K1 Community Patch più le correzioni widescreen / menu ad alta risoluzione
6. Avvia il gioco dall'ultima schermata dell'installer o da Steam

<h2>Disinstallazione</h2>

Esegui di nuovo l'installer e scegli l'opzione di disinstallazione, oppure usa "Installazione applicazioni". Il programma di disinstallazione rimuove solo i file di questa mod — K1CP e le altre mod opzionali scelte durante l'installazione restano al loro posto. Per tornare a un KOTOR completamente originale, usa "Verifica integrità dei file di gioco" di Steam o reinstalla da GoG dopo la disinstallazione.

<h2>Primi passi</h2>

Quando inizi una nuova partita, KOTOR ti guida prima attraverso la **creazione del personaggio**: scegli una classe (Soldato, Esploratore o Canaglia), un ritratto, e imposti caratteristiche, abilità e talenti. La mod legge ogni pannello mentre lo percorri — prenditi il tuo tempo, non c'è nulla a tempo.

Poi ti risvegli sull'**Endar Spire**, una nave della Repubblica sotto attacco. È l'area tutorial del gioco. La mod sostituisce i riquadri tutorial a schermo con suggerimenti da tastiera scritti per chi usa uno screen reader, così impari i comandi strada facendo. Segui Trask, la tua guida, verso le capsule di salvataggio.

Alcune abitudini che rendono l'inizio del gioco molto più facile:

- **Trova le cose con Q / E** e torna a ciò che hai già trovato con il ciclo `,` / `.` (vedi le scorciatoie da tastiera più sotto).
- **Premi H** in qualsiasi momento per sentire salute e stato, e **F1** per l'elenco completo dei tasti.
- **Ascolta la stanza.** Entrare in una stanza ne annuncia nome, forma e uscite, e un leggero livello audio ti tiene informato sui muri più vicini mentre ti muovi.
- **Dai un'occhiata alle impostazioni fin da subito.** Premi O per le Opzioni del gioco. Sotto Gameplay trovi le opzioni di pausa automatica, che decidono quando il combattimento si ferma da solo — vale la pena impostarle prima del primo vero scontro — e l'assegnazione dei tasti.
- **E in fondo all'elenco delle Opzioni: Impostazioni mod.** Quasi tutto ciò che questa mod aggiunge può essere regolato o attivato e disattivato lì, i tasti della mod possono essere riassegnati, e il glossario audio riproduce ogni segnale sonoro con il suo nome così impari che cosa significa ciascuno.

Dopo l'Endar Spire raggiungi **Taris**, il primo grande mondo, e da lì la storia si apre.

<h2>Scorciatoie da tastiera</h2>

La mod lascia intatta la mappatura dei tasti predefinita del gioco, con una modifica ergonomica che l'installer applica su un'installazione nuova (vedi la nota su movimento laterale / rotazione della telecamera più sotto). Tutto ciò che non è elencato qui si comporta come nel gioco non moddato. I tasti del gioco si possono riassegnare in Opzioni → Gameplay → Assegnazione tasti, oppure direttamente in `swkotor.ini`. I tasti aggiunti dalla mod si possono riassegnare in Impostazioni mod → Combinazioni di tasti.

<h3>Tasti di gioco che userai di più</h3>

- W / S — Muoversi avanti / indietro
- A / D — Passo laterale a sinistra / destra
- Z / C — Ruotare la telecamera a sinistra / destra (**Y / C** su una tastiera tedesca QWERTZ)
- Q / E — Cambiare bersaglio a sinistra / destra
- R — Azione predefinita sul bersaglio corrente (attaccare, parlare, aprire)
- 1 / 2 / 3 — Usare le tre azioni del menu azioni del bersaglio
- 4 / 5 / 6 / 7 — Usare gli slot potere della Forza / medipacca / oggetto / mina del giocatore
- Tab — Cambiare capogruppo
- F — Annullare il combattimento, G — Furtività, V — Modalità solitaria, X — Roteare l'arma
- Barra spaziatrice — Pausa
- Esc — Menu di gioco
- F4 — Salvataggio rapido, F5 — Caricamento rapido (nel menu principale, F5 cerca invece un aggiornamento della mod)
- I — Inventario del gruppo, U — Equipaggiamento, P — Scheda personaggio, K — Abilità / talenti / poteri
- M — Mappa, L — Missioni, J — Messaggi, O — Opzioni
- Mouse 1 — Clic nel mondo 3D (raramente necessario; vedi la modalità visuale più sotto)

> **Passo laterale / rotazione telecamera:** i valori predefiniti di KOTOR sono Z / C per il passo laterale e A / D per ruotare la telecamera. L'installer di accessibilità li scambia su un'installazione completa nuova, così i due formano un gruppo comodo sulla fila inferiore — **passo laterale su A / D, rotazione telecamera su Z / C**. È la mappatura elencata sopra. Se hai aggiornato da un'installazione esistente, o se riassegni tu stesso i tasti in `swkotor.ini`, i tuoi tasti possono essere diversi.

<h3>Tasti della mod — interazione con il mondo</h3>

- Invio — Eseguire l'azione predefinita sul bersaglio attualmente annunciato (come un clic Mouse 1 nel mondo)
- Maiusc+Invio — Aprire il menu azioni unificato per il bersaglio corrente (ogni azione — attaccare, parlare, poteri della Forza, oggetti, abilità speciali — in un unico menu)
- Maiusc+1 … Maiusc+7 — Aprire una categoria di azioni per scegliere al suo interno (1–3 sono le azioni del bersaglio, 4–7 i tuoi poteri della Forza / oggetti / mine)
- H — Annunciare la tua salute, gli effetti attivi e l'arma equipaggiata
- Maiusc+H — Aprire la coda delle azioni (rivedere o svuotare le azioni in coda)
- Maiusc+L — Aprire il pannello di avanzamento di livello
- F1 — Aprire o chiudere l'elenco completo dei tasti; Ctrl+F1 — leggere i tasti della schermata corrente
- Ctrl+R — Copiare negli appunti l'ultimo testo letto (una battuta di dialogo, una voce del diario, il nome di un PNG: qualunque cosa sia stata letta per ultima)

<h3>Tasti della mod — ciclo degli oggetti scoperti</h3>

Un secondo ciclo, oltre a Q / E, che percorre gli oggetti già scoperti nell'area corrente — porte, contenitori, personaggi, transizioni d'area, punti di riferimento e i tuoi segnaposto sulla mappa — raggruppati per categoria. (Attiva "Selezione oggetti su tutta la mappa" nelle Impostazioni mod per includere anche ciò che non hai ancora trovato.)

- `,` / `.` — Oggetto precedente / successivo nella categoria corrente
- Maiusc+`,` / Maiusc+`.` — Categoria precedente / successiva (creature, porte, contenitori, transizioni, segnaposto, …)
- Ctrl+`,` / Ctrl+`.` — Saltare all'oggetto più vicino / più lontano della categoria
- `/` (layout USA) o `-` (layout tedesco) — Annunciare l'oggetto attualmente a fuoco
- Maiusc+`/` (Maiusc+`-`) — Camminare automaticamente verso quell'oggetto
- Ctrl+`/` (Ctrl+`-`) — Attivare un radiofaro sonoro che segnala la strada mentre ti muovi

<h3>Tasti della mod — orientamento e gruppo</h3>

- AltGr (Alt destro, da solo) — Annunciare la direzione attuale come punto cardinale
- N — Ruotare la telecamera di 90° in senso orario verso il punto cardinale successivo; se un radiofaro è attivo, puntare invece al suo prossimo waypoint
- Tab — Annuncia il nuovo capogruppo dopo che il motore ha cambiato controllo

<h3>Tasti della mod — modalità visuale</h3>

Premi B per entrare in modalità visuale. Mentre la modalità visuale è attiva:

- A / D — Ruotare la telecamera senza muovere il personaggio
- Invio — Interagire con ciò che la telecamera inquadra, o camminarci automaticamente
- Maiusc+Invio — Aprire il menu azioni sul bersaglio della telecamera
- B di nuovo — Uscire dalla modalità visuale

<h3>Tasti della mod — schermata della mappa</h3>

Mentre la mappa di gioco è aperta:

- Frecce / Su / Giù — Scorrere le note e i punti di riferimento della mappa
- `,` / `.` — Scorrere i segnaposto della mappa (stesso vocabolario del ciclo degli oggetti scoperti)
- Maiusc+N — Posare un segnaposto personale nella posizione attuale del cursore nel mondo (denominato automaticamente in base alla stanza o al punto di riferimento più vicino). Il nuovo segnaposto entra subito nel ciclo e Ctrl+`-` vi punta un radiofaro

<h3>Tasti della mod — sottomenu</h3>

Quando è aperto un sottomenu della mod (il menu azioni unificato, un menu di categoria, la coda delle azioni):

- Su / Giù — Spostare il fuoco
- Sinistra / Destra — Spostarsi tra colonne o varianti
- Invio — Attivare la riga a fuoco
- Maiusc+Invio — (solo coda delle azioni) Svuotare tutte le azioni in coda
- Esc — Chiudere il sottomenu

<h3>Tasti della mod — specifici del contesto</h3>

- Q o E in un pannello Contenitore — Prendere tutto / dare oggetti
- Q o E in un pannello Negozio — Passare tra Compra e Vendi

Nel campo del nome in creazione personaggio (e negli altri campi di testo):

- Su / Giù — Rileggere il testo corrente dall'inizio
- Invio — Confermare
- Esc — Annullare

<h3>Tasti della mod — minigioco Pazaak</h3>

Mentre il tavolo del Pazaak è aperto:

- Su / Giù — Spostarsi tra le zone: la tua mano, il tuo tavolo, il tavolo dell'avversario, le azioni (Stare / Fine turno)
- Sinistra / Destra — Spostarsi all'interno della zona corrente (gli slot vuoti della mano vengono saltati)
- Invio — Giocare la carta a fuoco, o attivare l'azione a fuoco
- S — Stare
- E — Fine turno
- C — Leggere la tua mano
- T — Leggere entrambi i tavoli con i rispettivi totali
- Maiusc+C — Quante carte ha ancora in mano l'avversario
- Carta più/meno — Invio apre la scelta del segno; Sinistra / Destra scelgono più o meno, Invio gioca con quel segno, Esc annulla

Nella schermata della puntata prima della partita, la prima voce legge la puntata attuale, il massimo del tavolo e i tuoi crediti; spostati su "Riduci puntata" / "Aumenta puntata" e premi Invio per cambiare la puntata, poi sul pulsante di puntata del gioco per piazzarla. L'editor del mazzo laterale legge ogni carta e ogni slot del mazzo.

<h2>Controller</h2>

La mod include il proprio supporto ai controller — KOTOR 1 non ha alcun codice per il gamepad, quindi è la mod a pilotare direttamente il pad. Funziona qualsiasi controller XInput con la disposizione dei tasti Xbox; collegalo prima di avviare il gioco. Tastiera e controller restano attivi fianco a fianco, e ogni azione di gioco lanciata dal pad passa per le tue assegnazioni dei tasti — le riassegnazioni vengono quindi rispettate. Con un pad collegato, l'elenco dei tasti (F1) guadagna una sezione Controller; senza pad, quella sezione scompare. I minigiochi (Pazaak, corse di swoop, la torretta) non hanno ancora assegnazioni per il pad e mantengono i loro tasti da tastiera.

I nomi dei tasti qui sotto seguono la disposizione Xbox: A, B, X, Y, i dorsali sinistro e destro (LB / RB), i grilletti sinistro e destro (LT / RT), e la pressione di una levetta (L3 / R3).

<h3>Movimento e orientamento</h3>

- Levetta sinistra — Muoversi (tutte le otto direzioni: avanti, indietro e passo laterale)
- Levetta destra — Ruotare la telecamera
- R3 (premere la levetta destra) — Ruotare la telecamera verso il prossimo waypoint del radiofaro, altrimenti verso il punto cardinale successivo (l'N della tastiera)
- Grilletto destro da solo — Annunciare l'orientamento in gradi

<h3>Oggetti e azioni</h3>

- Croce direzionale sinistra / destra — Oggetto scoperto precedente / successivo (il ciclo `,` / `.` della tastiera)
- Croce su / giù — Categoria di oggetti precedente / successiva
- A — Azione predefinita sul bersaglio a fuoco (attaccare, aprire, parlare, raccogliere)
- B — Chiudere il menu azioni se è aperto; altrimenti l'annullamento normale del motore
- Dorsale sinistro / destro — Cambiare bersaglio a sinistra / destra (il Q / E della tastiera)
- Grilletto sinistro da solo — Aprire il menu azioni unificato; premendolo di nuovo si chiude. Mentre è aperto: croce sinistra / destra cambiano categoria, su / giù la voce, A esegue l'azione scelta
- Grilletto sinistro + dorsale sinistro — Radiofaro sonoro verso l'oggetto a fuoco
- Grilletto destro + dorsale destro — Camminare automaticamente verso l'oggetto a fuoco

<h3>Gruppo, stato e gioco</h3>

- X — Cambiare capogruppo
- Grilletto sinistro + X — Il tuo stato (l'H della tastiera)
- Grilletto destro + X — La coda delle azioni (il Maiusc+H della tastiera). Al suo interno: croce su / giù scorrono le voci, A rimuove l'ultima azione in coda, grilletto sinistro + A svuota tutta la coda, B chiude
- Y — Menu rapido, con le voci: le schermate dei menu, capogruppo, modalità solitaria, furtività, salvataggio rapido e Aiuto, che apre l'elenco dei tasti della mod. Il personaggio si ferma finché è aperto
- L3 (premere la levetta sinistra) — Roteare l'arma
- Start — Pausa
- Pulsante Indietro (Back) — Opzioni
- Entrambi i grilletti (LT + RT) — Leggere i tasti della schermata corrente (il Ctrl+F1 della tastiera)

<h3>Nei menu</h3>

Nei menu del gioco il pad si comporta semplicemente come la tastiera:

- Croce direzionale o levetta sinistra — Spostare il fuoco
- A — Confermare, B — Indietro
- Dorsale sinistro / destro — Scorrere le sotto-schermate del menu di gioco (equipaggiamento, mappa, missioni, …) — è così che il pad raggiunge la mappa; in un contenitore o in un negozio cambiano invece la modalità (prendere / dare, comprare / vendere)
- Tenere premuto Y e premere la croce su o giù — Leggere la descrizione completa della voce a fuoco, blocco per blocco, senza spostarsi
- Nella schermata della mappa la levetta sinistra sposta il cursore della mappa e la croce scorre i segnaposto

<h2>I sistemi di navigazione in breve</h2>

KOTOR è un gioco di ruolo in 3D, quindi passerai gran parte del tempo a muoverti tra stanze e attorno agli oggetti. La mod sovrappone alcuni sistemi per tenerti orientato — ognuno si annuncia da sé mentre lo usi.

<h3>Cambio bersaglio — Q / E</h3>

Il tuo modo principale di trovare le cose e agire su di esse. Q / E percorrono le creature, le porte e gli oggetti utilizzabili che la telecamera può vedere; ciò che è selezionato è ciò su cui agiscono Invio e i tasti azione 1–7. La mod annuncia ogni nuovo bersaglio.

<h3>Ciclo degli oggetti scoperti — `,` / `.`</h3>

Per tornare a ciò che hai già trovato. `,` / `.` percorrono ogni oggetto scoperto nell'area corrente — porte, contenitori, personaggi, transizioni, punti di riferimento, i tuoi segnaposto — raggruppati per categoria. Annunciane uno, cammina automaticamente fin lì o attiva un radiofaro sonoro. (Impostazioni mod → "Selezione oggetti su tutta la mappa" lo allarga anche a ciò che non hai ancora trovato.)

<h3>Menu azioni unificato — Maiusc+Invio</h3>

Un unico menu con ogni azione per il bersaglio corrente — attaccare, parlare, poteri della Forza, oggetti, abilità speciali. Le frecce spostano il fuoco, Invio attiva. Sostituisce i menu radiale, del bersaglio e personale separati del gioco.

<h3>Mappa — M</h3>

La mappa di gioco di KOTOR, resa navigabile. Muovi il cursore con le frecce per leggere terreno e segnaposto, oppure scorri i segnaposto della mappa con `,` / `.` con lo stesso vocabolario usato nel mondo. La nebbia di guerra viene rispettata, e Maiusc+N posa un segnaposto personale al cursore.

<h3>Segnali dei muri e descrizioni della forma delle stanze</h3>

Mentre ti muovi, un livello audio 3D continuo riproduce lievi clic posizionali riflessi dai muri più vicini — i muri più vicini suonano più forte — così mantieni una percezione costante dello spazio attorno a te. E all'ingresso in una stanza vengono annunciati il nome, la forma (corridoio, incrocio, vicolo cieco, spazio aperto) e le uscite visibili, tutto calcolato dal vivo dalla mesh di camminata del gioco.

<h2>Impostazioni mod</h2>

Quasi ogni sistema aggiunto dalla mod può essere regolato o attivato e disattivato durante il gioco. Apri la schermata Opzioni del gioco (O) e scegli **Impostazioni mod** in fondo all'elenco. Su / Giù percorrono le righe, Invio commuta un'impostazione o apre un sottomenu, Sinistra / Destra muovono un cursore di regolazione, ed Esc torna indietro.

- Selezione oggetti su tutta la mappa — allarga il ciclo `,` / `.` agli oggetti non ancora scoperti
- Descrizioni della forma delle stanze — l'annuncio di nome, forma e uscite quando entri in una stanza
- Suoni dei muri — il livello continuo di segnali sonori posizionali sui muri
- Leggi i sottotitoli dei parlanti doppiati — annunciare i sottotitoli delle battute doppiate
- Mira automatica — l'aiuto alla mira nel minigioco della torretta
- Salta i video introduttivi — ha effetto al prossimo avvio
- Volume dei segnali sonori e Volume degli annunci vocali — cursori di volume per i suoni e la voce della mod
- Combinazioni di tasti — riassegnare i tasti aggiunti dalla mod
- Glossario audio — riproduce ogni segnale sonoro della mod, uno alla volta con il suo nome, così impari che cosa significa ciascun suono

<h2>Risoluzione dei problemi</h2>

<h3>Nessuna voce dopo l'avvio del gioco</h3>

- Assicurati che il tuo screen reader sia in esecuzione prima di avviare KOTOR.
- Il runtime vocale Prism è incluso nella mod e viene collocato automaticamente dall'installer. Se hai installato manualmente, verifica che i suoi file siano presenti nella cartella del gioco.
- Controlla il log di patch più recente in `<installazione>\logs\patch-*.log` (il pulsante **Raccogli log** dell'installer lo raccoglie per te).

<h3>Il gioco si blocca all'avvio, o la mod non si carica</h3>

- Esegui l'installer come amministratore — installa il proxy `dinput8.dll` che carica automaticamente la mod all'avvio del gioco.
- Verifica che la tua versione del gioco sia supportata (vedi "Versioni del gioco non supportate" sopra). L'installer controlla l'hash di `swkotor.exe` e ti avvisa se non corrisponde.
- Se il gioco è stato aggiornato di recente, riesegui l'installer — un aggiornamento può sovrascrivere il loader.

<h3>La mod funzionava ma si è fermata dopo un aggiornamento del gioco</h3>

- Gli aggiornamenti di Steam e GoG possono sovrascrivere i file del loader della mod. Riesegui l'installer per riapplicare la mod.

<h3>Le scorciatoie da tastiera non funzionano</h3>

- Assicurati che la finestra del gioco abbia il fuoco (passa ad essa con Alt+Tab).
- Premi F1. Se senti l'elenco dei tasti, la mod è attiva.
- Alcuni tasti funzionano solo in un contesto preciso (i tasti Pazaak solo al tavolo del Pazaak, i tasti dei sottomenu solo dentro un sottomenu della mod e così via).

<h3>La telecamera non ruota più con i tasti</h3>

- Probabilmente hai attivato la modalità sguardo libero del gioco. La mappatura predefinita del gioco indica il Blocco maiuscole come interruttore dello sguardo libero, quindi è facile premerlo per sbaglio — prova a premere di nuovo il Blocco maiuscole. Altrimenti controlla le impostazioni di mouse e telecamera nelle Opzioni.
- Verifica anche di non essere nella modalità visuale della mod (B), dove A / D ruotano la telecamera invece di muovere il personaggio. Premi di nuovo B per uscirne.

<h3>Lingua sbagliata</h3>

- La mod sceglie la propria lingua automaticamente in base alla lingua del gioco (letta dal `dialog.tlk` del gioco). Non c'è ancora un selettore di lingua nel gioco, quindi per cambiare la lingua della mod installa il gioco in quella lingua. Le cinque lingue in cui KOTOR viene pubblicato — inglese, francese, tedesco, italiano, spagnolo — sono supportate.

<h3>Windows segnala l'installer o la DLL come non sicuri</h3>

L'installer e la mod non sono firmati digitalmente. I certificati di firma del codice costano centinaia di euro all'anno, cifra non realistica per un progetto di accessibilità gratuito, quindi Windows SmartScreen ti avviserà la prima volta che esegui l'installer e potrebbe segnalare i file come provenienti da un editore sconosciuto.

Per verificare che il file scaricato corrisponda a quello pubblicato su GitHub, ogni release riporta un checksum SHA-256. Puoi calcolare l'hash del tuo download e confrontarlo:

- PowerShell: `Get-FileHash <nomefile> -Algorithm SHA256`
- Prompt dei comandi: `certutil -hashfile <nomefile> SHA256`

Se l'hash corrisponde a quello nelle note di rilascio, il file è autentico. Per superare l'avviso di SmartScreen, scegli "Ulteriori informazioni" e poi "Esegui comunque".

<h2>Segnalare bug</h2>

La schermata post-installazione dell'installer ha un pulsante **Raccogli log** che comprime il log di patch più recente e gli eventuali dump di Windows Error Reporting nella tua cartella Download. Allega quello zip a una [segnalazione GitHub](https://github.com/JeanStiletto/voice-of-the-old-republic/issues) e descrivi che cosa stavi facendo. Se riesci a riprodurre un crash, indica in quale area ti trovavi — l'annuncio della stanza o dell'area l'avrà nominata poco prima.

<h2>Problemi noti</h2>

Per l'elenco attuale di bug, funzionalità pianificate e spigoli, vedi [docs/known-issues.md](known-issues.md).

<h2>Precisazioni</h2>

<h3>Altre accessibilità</h3>

Per ora questa è una mod di accessibilità per screen reader. Sono uno sviluppatore completamente cieco, e l'accesso tramite screen reader è l'ambito che conosco. Mi piacerebbe davvero coprire più disabilità — ipovisione, disabilità motorie e così via — ma questioni come colore, contrasto e carattere restano astratte per me da persona completamente cieca. Se ti serve qualcosa in quella direzione e puoi descrivere chiaramente le tue esigenze e aiutare a testare il risultato, mettiti in contatto. Sarei felice di far sì che la mod sia all'altezza del suo nome per più persone.

<h3>Uso dell'IA</h3>

Il codice di questa mod è scritto con l'assistenza sostanziale di Claude di Anthropic, con i modelli Opus (lo sviluppo ha attraversato le generazioni Opus 4.5, 4.6 e 4.7). Conosco i dibattiti sullo sviluppo assistito dall'IA. Ma in un momento in cui l'industria dei videogiochi non ha mai fornito l'accessibilità di cui abbiamo bisogno — né in qualità né in quantità — per titoli come KOTOR, questi strumenti sono ciò che rende fattibile un progetto di queste dimensioni per un singolo sviluppatore cieco. Ogni modifica viene rivista e testata nel gioco, a orecchio, prima di essere pubblicata.

<h2>Come contribuire</h2>

I contributi sono benvenuti — in particolare correzioni per lingue, configurazioni di sistema o screen reader che non posso testare in locale. Accetto volentieri anche richieste di funzionalità. Prima di iniziare, scorri il file dei problemi noti qui sopra per vedere se la tua idea è già nel backlog.

- Guida ai contributi: [CONTRIBUTING.md](../CONTRIBUTING.md)
- Panoramica dell'architettura: [ARCHITECTURE.md](../ARCHITECTURE.md)
- Guida al modding per l'accessibilità (mestiere generale + specificità dei binari nativi): [ACCESSIBILITY_MODDING_GUIDE.md](../ACCESSIBILITY_MODDING_GUIDE.md)
- Tradurre gli annunci vocali della mod: [docs/CONTRIBUTING_TRANSLATIONS.md](CONTRIBUTING_TRANSLATIONS.md)

<h2>Ringraziamenti</h2>

E ora voglio ringraziare un bel po' di persone. Prima di tutto, questa mod si appoggia enormemente al lavoro della comunità, e molte persone hanno fatto le cose davvero difficili, così io ho solo dovuto raccogliere i loro strumenti e creare la mia cosa con quelli. Inoltre, per fortuna, non eravamo solo io e l'IA in una scatola nera, ma tutta una rete attorno a me, che ha dato una mano, incoraggiato ed è stata semplicemente sociale e gentile.

Scrivimi in privato se ti ho dimenticato, o se vuoi comparire con un altro nome o non comparire affatto.

Il reverse engineering e il framework di patch su cui gira questa mod vengono da **Lane Dibello**, il cui [Kotor-Patch-Manager](https://github.com/LaneDibello/Kotor-Patch-Manager) e il cui lavoro con Ghidra hanno reso possibile agganciare il gioco. Grazie anche alla comunità di modding di KOTOR attorno a **DeadlyStream**, che ha capito gli strumenti e i formati che io ho solo dovuto raccogliere e usare.

Molte persone mi hanno aiutato a testare il gioco, hanno trovato tutti i bug che io non potevo trovare e mi hanno dato riscontri per individuare i punti deboli e le scomodità del progetto. Senza di loro non sarebbe mai diventato un progetto. Quindi voglio ringraziare:

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

**Fondamenta e dipendenze:**

- **Lane Dibello** — [Kotor-Patch-Manager](https://github.com/LaneDibello/Kotor-Patch-Manager), il database Ghidra ottenuto con reverse engineering e il framework di patch
- **Prism** (Ethin P.) — ponte vocale multipiattaforma che copre tutti i principali screen reader, con ripiego su SAPI
- Team del **K1 Community Patch** (KOTORCommunityPatches) — livello di correzioni incluso
- **xoreos / xoreos-tools** — reimplementazione open source del motore; riferimento incrociato per i formati di file
- Comunità **DeadlyStream** — base di conoscenza sul modding

<h3>Strumenti usati</h3>

- Claude (Anthropic) — partner di programmazione in coppia lungo le generazioni Opus 4.5, 4.6 e 4.7
- Kotor-Patch-Manager — framework di patch con iniezione di DLL a runtime
- Prism — ponte vocale per screen reader
- Tolk — libreria per screen reader (percorso di ripiego)
- Ghidra — reverse engineering
- xoreos-tools — estrazione senza interfaccia dei formati di file del gioco
- K1 Community Patch — livello di correzioni della comunità incluso

<h2>Sostieni il tuo modder</h2>

Costruire questa mod è stato molto divertente e una vera fonte di autonomia, ma è costato anche molto tempo e denaro reale in abbonamenti a Claude. Intendo tenerli attivi per mantenere il progetto e migliorarlo negli anni a venire. Se puoi e vuoi fare una donazione una tantum o ricorrente, la apprezzerei profondamente — riconosce il lavoro e mi dà una base stabile per continuare a migliorare Voice of the Old Republic e, si spera, altri grandi progetti di accessibilità.

[Ko-fi: ko-fi.com/jeanstiletto](https://ko-fi.com/jeanstiletto)

<h2>Licenza</h2>

Il codice sorgente della mod è rilasciato sotto GNU General Public License v3 (vedi [LICENSE](../LICENSE)). Le dipendenze incluse in `third_party/` mantengono le proprie licenze (Prism è MPL-2.0; Tolk è LGPL; Kotor-Patch-Manager è incluso secondo i termini del suo progetto originale; dsoal e OpenAL Soft, quando il percorso audio spaziale opzionale è attivo, sono LGPL-2.1). Il gioco stesso e i file di dati di BioWare non vengono ridistribuiti da questo progetto.

<h2>Link</h2>

- [GitHub](https://github.com/JeanStiletto/voice-of-the-old-republic)
- [Segnala un problema](https://github.com/JeanStiletto/voice-of-the-old-republic/issues)
- [Kotor-Patch-Manager](https://github.com/LaneDibello/Kotor-Patch-Manager)
- [K1 Community Patch (DeadlyStream)](https://deadlystream.com/)
- [Ko-fi (sostieni il progetto)](https://ko-fi.com/jeanstiletto)

<h2>Altre lingue</h2>

- [English](/voice-of-the-old-republic/)
- [Deutsch](/voice-of-the-old-republic/docs/README.de.html)
- [Français](/voice-of-the-old-republic/docs/README.fr.html)
- [Español](/voice-of-the-old-republic/docs/README.es.html)
- [Polski](/voice-of-the-old-republic/docs/README.pl.html)
- [Русский](/voice-of-the-old-republic/docs/README.ru.html)

Le traduzioni si trovano in `docs/README.{de,fr,it,es,pl,ru}.md`. Per migliorare o aggiungere una traduzione, vedi [docs/CONTRIBUTING_TRANSLATIONS.md](CONTRIBUTING_TRANSLATIONS.md).
