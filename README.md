# CasaMonitor

Monitor dei consumi Shelly per **Waveshare ESP32-S3-Touch-LCD-4.3C**, con quattro tile touch, soglia di potenza, allarme acustico, standby e aggiornamenti OTA firmati. Le letture nella LAN hanno precedenza sul cloud di riserva. È inclusa una dashboard Python opzionale per il Mac.

Firmware corrente: **1.1.3**. [Release e download](https://github.com/carlomicheluzzi/casa-monitor/releases/latest) · [Documentazione del display](firmware/README.md).

Per gli aggiornamenti, inserire in **Impostazioni → Aggiornamenti** questo indirizzo:

```text
https://github.com/carlomicheluzzi/casa-monitor/releases/latest/download/manifest.json
```

Il controllo legge il manifest firmato; l’installazione automatica è inizialmente disabilitata. Wi-Fi, chiave Shelly e associazioni sono salvati sul dispositivo. La procedura OTA e il rollback richiedono ancora un collaudo fisico completo; la 1.1.3 è stata caricata via USB e sono stati confermati letture cloud, spegnimento e risveglio con un tap. Per preparare nuove release vedere [Pubblicazione firmware](firmware/PUBLISHING.md).

Gli identificativi dei misuratori e le associazioni predefinite corrispondono all’installazione iniziale: vanno adattati da **Monitor, canali e apparecchi** oppure mediante il provisioning USB. Nel repository e nei pacchetti non sono incluse password, chiavi cloud, backup flash o la chiave privata di firma.

Firmware ESP32: [novità e aggiornamento USB 1.1.3](firmware/UPGRADE-1.1.md).

Dashboard locale per quattro circuiti misurati da due Shelly EM: potenza di ogni circuito, somma, margine rispetto al limite, preavviso e allarme acustico. È disponibile anche il [firmware autonomo per Waveshare 4.3C](firmware/README.md): letture Shelly Cloud v2 via HTTPS, quattro tile, dettagli e storico della sessione, soglie, audio, silenziamento, simulazione separata e configurazione Wi-Fi touch.

## Dashboard desktop opzionale

Per il primo avvio copiare `.env.example` in `.env` e inserire la propria chiave Shelly nel file locale, escluso da Git.

Richiede Python 3.10 o successivo e un browser moderno. Nessuna libreria aggiuntiva e nessun servizio da installare.

```bash
cd casa-monitor
python3 app.py --env-file .env
```

Aprire [il monitor locale](http://127.0.0.1:8765). Arrestare con `Ctrl+C` nel terminale.

Su macOS si può anche fare doppio clic su `Avvia monitor.command`, quindi aprire lo stesso indirizzo. Avviare una sola istanza sulla stessa porta; se il monitor è già aperto basta tornare alla pagina.

All'avvio si legge `SHELLY_AUTH_KEY` dal file già esistente in `~/Documents/workspace-python/ShellyMonitor/.env`. La chiave non viene copiata nel progetto, inviata al browser o stampata nei log. Per scegliere un altro file o server:

```bash
python3 app.py --env-file /percorso/credenziali/.env --server https://shelly-52-eu.shelly.cloud
```

Il file deve contenere `SHELLY_AUTH_KEY=...`. Non inserire la chiave negli argomenti del comando. Il server accetta solo indirizzi HTTPS sotto `shelly.cloud`. Se Shelly sposta l'account su un altro server, usare il server indicato nell'app nelle informazioni della chiave cloud.

Per lavorare senza rete o credenziali:

```bash
python3 app.py --demo
```

## Uso

- **Cloud / Simulazione:** le due modalità hanno dati, storico e allarme separati. Le simulazioni non comandano gli Shelly e non cambiano l'allarme cloud. Il monitor cloud continua in background quando è stato avviato senza `--demo`.
- **Attiva audio:** serve un clic per consentire al browser di emettere suoni. Il suono di prova conferma l'attivazione; il controllo “Prova suono” permette di ripeterlo. L'abilitazione riguarda quella finestra e va ripetuta al caricamento. Un browser sospeso, un computer in stop o una finestra chiusa non garantiscono un avviso: il dispositivo ESP32 servirà per il funzionamento autonomo.
- **Simulazione:** “Superamento” attiva l'allarme; “Perdita dati” permette di verificare che un allarme già attivo resti visibile; “Regolare” conferma il rientro. Ogni scenario è indicato esplicitamente come simulato.
- **Impostazioni:** soglia, preavviso, margine di rientro, scadenza delle misure, durata del silenziamento e associazione dei canali. I valori salvati persistono in `.local/config.json`.
- **Schermo intero:** il layout include una variante compatta per 800×480 e una per telefono.
- **Dettaglio tile:** un clic su una delle quattro tile apre potenza, contatore cumulato di energia in kWh (se fornito dal misuratore) e andamento della sessione. L'energia cumulata non viene presentata come consumo giornaliero.
- **Apparecchi nella zona Forza:** il dettaglio permette di associare lavatrice, asciugatrice e i due boiler ai rispettivi misuratori dedicati. Le associazioni si salvano separatamente in `.local/appliances.json`; inizialmente sono tutte vuote. I bagni non vengono assegnati a una zona presunta. Nella simulazione la suddivisione è solo un esempio dichiarato.

Per misurare lavatrice e asciugatrice separatamente servono due canali dedicati, anche se il comando elettrico è comune. Un misuratore a due pinze, ad esempio Shelly EM Gen3, può fornire le due misure; non sostituisce automaticamente il comando a parete e il relativo progetto di commutazione. Se lo Shelly comanda la bobina di un contattore, misurare quella bobina non misura il consumo degli elettrodomestici.

I dati degli apparecchi sono dettagli subordinati: **non vengono aggiunti ai quattro contatori di zona** per il totale o per l'allarme. Un misuratore di zona non può essere riutilizzato come se fosse quello di un singolo apparecchio e due apparecchi non possono usare lo stesso canale. La mancanza del misuratore di un apparecchio non disabilita il monitoraggio del contatore di zona.

## Associazione iniziale da verificare

L'API cloud ha restituito nomi nulli nelle impostazioni degli EM. L'associazione è provvisoria, desunta dal confronto con le schermate fornite:

| Tile | ID Shelly EM | Canale |
|---|---|---|
| Zona 1 · Luce | `485519d9cf2f` | 1 |
| Zona 1 · Forza | `485519d9cf2f` | 0 |
| Zona 2 · Luce | `485519d9502e` | 1 |
| Zona 2 · Forza | `485519d9502e` | 0 |

Confermare o correggere dalle impostazioni quando si può verificare l'impianto. Il programma rifiuta un canale duplicato per evitare doppi conteggi. La somma riguarda i quattro circuiti selezionati: non dimostra che siano inclusi tutti i carichi del contatore. Presa e sensore presenti nell'inventario non vengono sommati automaticamente.

## Letture e allarme

- Una richiesta cloud ogni 3 secondi, condivisa da tutte le finestre; ritento progressivo fino a 60 secondi in caso di errore. Il browser legge il backend locale ogni secondo. Il ritmo rispetta il limite documentato di una richiesta al secondo, ma non determina la frequenza con cui gli Shelly aggiornano il cloud.
- Un HTTP riuscito può restituire una cache vecchia. All'avvio si attende che il marcatore `_updated` di **ciascun EM** avanzi prima di considerare valide le sue letture. Si misura poi il tempo locale trascorso dall'ultimo avanzamento. Non si attribuisce un fuso orario al timestamp cloud. Anche uno scatto all'indietro del marcatore richiede una nuova verifica.
- Dopo 90 secondi senza avanzamento, oppure con dispositivo offline, misura non valida o errore di rete, il totale diventa indisponibile. Le tile conservano separatamente l'ultimo valore per riferimento; non sostituiscono le letture mancanti con zero. La soglia iniziale di 90 secondi tiene conto degli intervalli di circa un minuto osservati nel collaudo cloud a carico stabile; potrà richiedere un adattamento dopo prove a casa. Il collegamento locale previsto per l'ESP32 consentirà di evitare questa attesa del cloud.
- Allarme per una somma valida **maggiore di 4.500 W**; preavviso da 4.000 W. Rientro a **4.300 W o meno**, con margine iniziale di 200 W. Il silenziamento dura 120 secondi e mantiene l'avviso visivo. Un nuovo episodio riattiva il suono. L'allarme non si annulla se mancano i dati.
- Gli Shelly non vengono azionati: sono usati solo endpoint di lettura. Il limite è quello configurato nel monitor, non una previsione esatta del distacco del contatore.
- Le letture cloud non sono simultanee e la loro freschezza è inferita dall'avanzamento del relativo stato, non certificata per il singolo campione di potenza. L'andamento nelle tile usa solo i dati raccolti nella sessione, conservati in memoria per circa 15 minuti.

## Verifiche

```bash
python3 -m unittest discover -s tests -v
```

I test verificano misure mancanti, cache immutata, clock che arretra, soglia, isteresi, silenziamento, perdita della rete durante un allarme, isolamento della simulazione e salvataggio della configurazione.

I test dei dettagli verificano anche contatori Wh di EM Gen1, componenti Switch/1PM e componenti EM1/EM1Data, associazioni persistenti, canali distinti e assenza di doppi conteggi nel totale di casa. Il supporto dei nuovi misuratori è verificato con risposte rappresentative delle API; andrà collaudato sui dispositivi effettivamente installati.

Lo script indipendente per rileggere l'inventario è:

```bash
python3 cloud_inventory.py --output .local/shelly-inventory.json
```

`.local/` e i file `.env` sono esclusi da Git. Il server web è limitato a `127.0.0.1`, serve solo i tre file dell'interfaccia e non espone la cartella del progetto.

## Riferimenti

- [Shelly Cloud: autorizzazione e limite delle richieste](https://shelly-api-docs.shelly.cloud/cloud-control-api/)
- [Elenco degli stati e metadati dei dispositivi](https://shelly-api-docs.shelly.cloud/cloud-control-api/real-time-events/)
- [Shelly EM: API e canali](https://shelly-api-docs.shelly.cloud/gen1/#shelly-em)

Il firmware in `firmware/` funziona senza il backend Python dopo il trasferimento iniziale della configurazione via USB. Usa i driver ufficiali Waveshare per display, touch e codec ES8311. I tre apparecchi subordinati sono ora boiler Carlo, boiler Gloria e gruppo lavatrice/asciugatrice: il gruppo su un solo misuratore ha un consumo aggregato. La dashboard Python conserva le sue associazioni precedenti. La gestione automatica dei relè non è attiva: l’interfaccia contiene soltanto un’opzione disabilitata, in attesa di verifiche elettriche, associazioni e implementazione/collaudo locale.
