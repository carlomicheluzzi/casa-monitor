# Direzione dell'interfaccia

Prototipo locale per verificare quattro circuiti Shelly EM, totale e allarme prima del firmware ESP32. Riferimento principale: le due schermate Shelly fornite dall'utente. Percorso: implementazione diretta con riferimenti esistenti.

La ricerca Refero è stata tentata: il servizio restituisce `NO_SUBSCRIPTION`. Nessuno stile Refero è stato recuperato. Si applicano le schermate fornite e i riferimenti locali della skill Refero Design, in particolare `craft-details.md` e `visual-workflow.md`.

| Scelta | Riferimento | Ruolo e motivo |
|---|---|---|
| Fondo scuro, superfici grafite, cifre bianche grandi | Schermate Shelly dell'utente | Lettura continua, senza fotografie dietro ai dati |
| Quattro tile permanenti, due colonne per le zone | Richiesta dell'utente | Luce e forza confrontabili nello stesso colpo d'occhio |
| Blu per le azioni, verde per stato regolare, ambra per preavviso, rosso per allarme | Blu delle azioni Shelly; stati espliciti richiesti | Il colore comunica una funzione, sempre accompagnata da testo |
| Totale e margine prima dei dettagli | Scopo dell'allarme di 4,5 kW | La decisione principale è se ridurre il carico |
| Focus visibile, pulsanti di almeno 44 px, dialog nativo, etichette | Refero craft-details | Uso con mouse, tastiera e touch |
| Simulazione separata e sempre riconoscibile | Necessità di testare prima dell'ESP32 | Le prove non cambiano dati o allarmi del monitor cloud |
| Andamento solo dai campioni della sessione | Evidenze effettivamente disponibili | Nessuno storico inventato |

Token: fondo #0e1216; superficie #171d23; bordo #303a44; testo #f2f5f7; secondario #a6b2bd; azione #69b5ff; regolare #71e1b0; preavviso #ffcd70; allarme #ff8282. Raggio 16 px sulle tile, 10 px sui controlli. Font di sistema, cifre tabulari. Nessuna immagine necessaria.

Vincoli di verifica: desktop, telefono e 800×480; quattro tile sempre presenti; nessuna lettura assente sostituita con zero; allarme visibile anche con audio disabilitato o silenziato.

## Verifica della realizzazione

Collaudo del 30 ottobre 2026, browser integrato di Codex:

- 1280×720, cloud: quattro tile, totale, audio e avviso di associazione visibili senza scorrimento. Confronto con la direzione approvata: fondo scuro, blu per le azioni, dati grandi e gerarchia totale → zone → audio rispettati.
- 800×480, simulazione di sovraccarico: quattro tile, allarme, silenziamento e scenari presenti nello stesso viewport, senza scorrimento orizzontale o verticale.
- 390×844: tutte le tile entro il viewport; nessuno scorrimento orizzontale. I controlli accessori restano accessibili con lo scorrimento verticale.
- Stati verificati: cloud reale, regolare, preavviso, superamento, silenziamento, perdita dati con allarme mantenuto, rientro, audio attivato e assenza di errori nella console.
- Impostazioni: rifiutato il doppio conteggio di un canale; verificato il salvataggio di una configurazione valida.

Immagini locali del collaudo in `.local/preview.png` e `.local/preview-alarm.png`. Le credenziali non fanno parte degli screenshot.

## Dettaglio dei consumi

Richiesta successiva dell'utente: aprire una tile Forza per leggere i consumi dei singoli apparecchi. Si conserva il linguaggio visivo della dashboard e dei dialog già verificati: dialog nativo, chiusura esplicita, letture grandi, controlli con etichette. Le tile diventano attivabili con mouse, tastiera e touch tramite un pulsante esteso con focus visibile.

Il dettaglio distingue potenza istantanea in W, energia cumulata in kWh e andamento della sessione. La suddivisione usa solo canali dedicati associati esplicitamente. Le quattro associazioni reali partono vuote; gli esempi della simulazione sono segnalati come tali. Non si inventano consumi giornalieri o assegnazioni dei bagni alle zone. Il totale generale rimane la somma dei quattro canali principali.

Verifica eseguita: apertura da tile, lettura di potenza ed energia cumulata dei due EM reali, elenco degli apparecchi dimostrativo in simulazione, salvataggio delle associazioni inizialmente vuote, allarme generale e silenziamento anche nel dettaglio. Console senza errori. Layout controllato a 800×480 e 390×844: dialog scorrevole in verticale e senza fuoriuscite orizzontali. I dettagli degli apparecchi precedono il grafico per privilegiare lo scopo della schermata. Anteprima reale in `.local/preview-detail.png`.

## Wi-Fi sul display — 11 novembre 2026

Percorso di realizzazione diretta sul sistema esistente: stessi colori, gerarchia, sans serif, bordi e azione blu. Nuova UI LVGL a 800×480; il monitor Python resta separato. La ricerca Refero ora risponde: la ricerca di stile non ha fornito una direzione più pertinente della dashboard approvata, che resta il riferimento dominante. Sono stati letti i dettagli delle schermate [Cosmos: password e tastiera](https://refero.design/screens/54dcb286-5a70-4313-ade7-b352a38e1eb7) e [Nike Training Club: assenza Internet](https://refero.design/screens/a69ade3c-9099-4e70-9569-c6c833f0ec9f). Se ne adattano rispettivamente campo password con controllo di visibilità e tastiera, e messaggio di errore esplicito con via d’uscita. Nessuna palette o tipografia di quei prodotti sostituisce i token del monitor.

| Scelta | Fonte e ruolo | Motivo |
|---|---|---|
| Grafite, cifre/testi chiari e blu riservato alla conferma | Dashboard esistente e schermate Shelly | Continuità con il prodotto già verificato |
| Elenco reti a sinistra e modulo scorrevole a destra | Richiesta dell’utente e formato fisico 800×480 | Si sceglie una rete mantenendo visibili stato e azioni |
| Password nascosta, Mostra/Nascondi e tastiera integrata | Pattern Cosmos e craft-details della skill Refero | Configurazione autonoma dal touch |
| Motivo del problema in alto e configurazione modificabile | Richiesta dell’utente e pattern errore Nike | Si comprende l’errore e si può correggerlo |
| Conferma separata dall’inserimento, salvataggio dopo l’IP | Requisito di recupero e limiti del dispositivo | Si evita di perdere una configurazione valida dopo un errore |
| Il campo attivo scorre sopra la tastiera | Craft-details: input e touch | Parametri IPv4 modificabili sullo schermo piccolo |

Verifica con la stessa UI LVGL renderizzata su host: primo avvio, reti trovate, password mascherata, tastiera testo/numeri, rete collegata, rete assente e passaggio dalle impostazioni. I test verificano i limiti dello schermo e il campo DNS visibile con la tastiera aperta. Anteprime in `.local/firmware-host/wifi-*.png`; sono render, non prove sulla scheda. Restano necessari i collaudi su display e access point reali.

## Monitor completo sul display — 13 novembre 2026

Trasposizione della dashboard approvata nella UI LVGL esistente, con gli stessi token grafite, blu e colori di stato. Quattro tile permanenti a 800×480, totale e margine in alto, azioni audio in basso. Il dettaglio resta una pagina a schermo intero, con ritorno esplicito e grafico della sessione; i tre sotto-contatori seguono la richiesta aggiornata: due boiler distinti e un unico gruppo lavatrice/asciugatrice.

Le impostazioni separano soglie, canali, apparecchi e futura automazione. I selettori mostrano modello, parte finale dell’identificativo e canale per distinguere dispositivi simili. L’associazione delle zone richiede conferma; i tre apparecchi restano da associare. La pagina Automazione dichiara che distacco e ripristino non sono attivi e il flag non è selezionabile.

La simulazione ha indicazione persistente e storico/allarme separati dal cloud. Una lettura non verificata o scaduta mostra un trattino e il motivo; l’ultimo valore può comparire solo come informazione secondaria. Un totale incompleto non diventa zero e non annulla un allarme già attivo.

Verificati i render della stessa UI compilata: regolare, allarme, perdita dati con allarme mantenuto, dettaglio e impostazioni. Test automatici LVGL confermano quattro tile visibili, navigazione, simulazione, silenziamento, azione Prova suono e impostazioni. Anteprime in `.local/firmware-host/energy-*.png`, tutte su dati simulati. Il firmware caricato sul dispositivo riceve inoltre le quattro letture cloud reali; la prova visiva e d’ascolto resta distinta dai render e dai log.

## Revisione del dettaglio — fotografia IMG_3491.JPG

Obiettivo: leggere subito potenza e contatore cumulato su 800×480, interpretare la curva e distinguere stato del dato, sotto-contatori e informazioni tecniche. Percorso diretto sulla UI esistente. Fonte dominante: fotografia dell’utente e token già adottati; nessuna nuova direzione di marca o immagine decorativa.

Ricerca Refero: tre ricerche di stile (strumenti scuri, energia, Linear), due ricerche di schermate (consumi e serie temporali). Studiati SoFi «luminous connected frontiers» e ON.energy «industrial voltage cinema»: utili rispettivamente gerarchia tra titolo/etichetta e numeri tecnici leggibili; esclusi fotografia, scenografie, giallo decorativo e controlli da marketing. Schermate studiate: [Gentler Streak](https://refero.design/screens/b2418809-ae31-4418-b61e-cee8eeb982ed), [Mercury Insights](https://refero.design/pages/ada46fa4-e23c-4528-8775-8ac97d4682ea) e [Mapbox Statistics](https://refero.design/pages/36ae0e41-073c-4243-a881-d56416267c6b).

| Decisione | Fonte e ruolo | Applicazione |
|---|---|---|
| Conservare grafite, Montserrat e blu | Foto utente e dashboard approvata; font/palette dominanti | Nessun cambio ai significati dei colori: verde dato valido, ambra attesa, rosso allarme |
| Etichetta esplicita sopra ciascun valore | Mercury KPI e gerarchia tipografica SoFi | Potenza attuale in W, energia cumulata in kWh con separatori italiani; il contatore non è presentato come consumo giornaliero |
| Grafico ampio e griglia secondaria | Gentler Streak: titolo, intervallo e assi; Mapbox: etichette chiare | Scala in watt, riferimenti relativi approssimati dai campioni, minimo/massimo del tratto disponibile; buchi per letture mancanti |
| Densità diversa per Luce e Forza | Richiesta aggiornata e spazio sprecato nella foto | Luce usa lo spazio per il grafico; Forza mostra i tre apparecchi e una curva più compatta |
| Identificativi nel comando Misuratore | Richiesta di leggibilità e separazione gerarchica della UI esistente | Finestra informativa con dispositivo/canale, senza codici sulla vista principale; comandi touch di almeno 44 px |

Vincoli: nessun dato inventato; curva solo della sessione corrente, non storico cloud; tempo relativo indicato come approssimativo; nessun consumo giornaliero calcolato; nessuna modifica a soglie, associazioni, Wi-Fi o gestione dei relè. Target di verifica: render della stessa UI LVGL per Luce, Forza, dati mancanti e informazioni misuratore, poi compilazione e prova sulla scheda.

Verifica della revisione: render effettivi LVGL 800×480 in `.local/firmware-host/energy-detail-luce.png`, `energy-detail.png`, `energy-detail-info.png` e `energy-detail-offline.png`, tutti su dati di simulazione espliciti. Controllati gerarchia tipografica, spazio asse/curva, sottocontatori, stato mancante e finestra tecnica. Corretto il caso iniziale con più etichette «Ora» sovrapposte semanticamente: prima dei primi intervalli compare solo l’estremo corrente. I test verificano «2.045,849 kWh», potenza, scala, riferimenti relativi, mancanti come trattini, codici tecnici visibili solo nella finestra dedicata e rilascio della vista alla chiusura. Compilazione ESP32 e caricamento con quattro hash verificati; l’utente approva la nuova vista sul display («direi ottimo»). Nessun rilievo P0/P1/P2 nei render controllati. La verifica prolungata del percorso video è registrata separatamente nel verbale del dispositivo.
