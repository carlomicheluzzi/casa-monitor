# CasaMonitor 1.1.4 — aggiornamento USB e uso a casa

La versione iniziale 1.1.0 è stata preparata il 25 novembre 2026. Il 26 novembre è stato eseguito il caricamento USB e la revisione 1.1.2 legge correttamente le quattro misure dal cloud sulla rete dell’ufficio. Lo standby e il risveglio con tap della successiva 1.1.3 sono confermati dall’utente; la stabilità video prolungata, letture locali nella LAN di casa e aggiornamento/rollback OTA restano da collaudare. La build e le simulazioni desktop non sostituiscono queste verifiche.

## Cosa cambia

**Display.** Standby predefinito dopo 120 secondi, luminosità 70%. Si spegne la retroilluminazione: ESP32, Wi-Fi, touch, campionamento e allarme continuano. Il primo tap riaccende soltanto il display e viene consumato fino al rilascio. Un allarme lo riaccende sempre; facoltativamente anche la perdita dei dati per più di 10 secondi. Impostazioni → Display consente di modificare tempo e luminosità; 0 secondi disabilita lo standby. Durante configurazione e aggiornamento il timer è sospeso. Il consumo non diventa zero e non usiamo il deep sleep.

**Shelly locali.** Precedenza alle letture HTTP nella LAN, senza Internet. Scoperta mDNS dei dispositivi Shelly, controllando ID/MAC, e host IPv4 privato o `.local` configurabile per ciascun dispositivo. I due Shelly EM mantengono le quattro associazioni esistenti. I nuovi misuratori vengono aggiunti al catalogo ma non associati automaticamente a boiler o zone. La schermata Monitor, canali e apparecchi resta il punto per associarli.

- Gen1: identità `/shelly`, letture `/status`, eventuale autenticazione Basic.
- Gen2/3/4: identità `/shelly`, letture `/rpc/Shelly.GetStatus`, Digest SHA-256 quando protetto.
- Sono usate le credenziali locali dello Shelly, diverse dall’account cloud. Prima di ogni lettura si verifica l’identità senza inviare la password. HTTP locale richiede una LAN fidata; non esporre queste porte su Internet.
- Campionamento nominale ogni secondo per dispositivo; timeout e scoperta possono allungarlo. Un dato locale scade dopo 5 secondi senza risposta, anche se la potenza era costante. Non diventa zero.
- Il cloud di riserva è abilitato inizialmente e si può disabilitare. Quando tutti i canali associati hanno letture locali valide, le richieste al cloud vengono sospese e il socket TLS viene chiuso. In caso di ritorno al cloud rimane il controllo sui timestamp della cache; il totale può restare temporaneamente non disponibile.
- Sotto le tile e nel dettaglio è indicata la provenienza LAN/Cloud. I sotto-contatori non si sommano nuovamente al totale della casa.

**Aggiornamenti.** Impostazioni → Aggiornamenti contiene indirizzo HTTPS del manifest, controllo giornaliero, Verifica adesso, Installa e riavvia, e installazione automatica opzionale (inizialmente disabilitata). Senza URL configurato non vengono eseguite verifiche. Dopo errori il controllo automatico riprova dopo un’ora; dopo una verifica riuscita attende 24 ore. Un riavvio avvia un nuovo controllo.

Il dispositivo verifica HTTPS con CA, firma ECDSA P-256/SHA-256 del manifest, modello scheda, codice versione crescente, dimensione e SHA-256 dell’applicazione. Sono ammessi solo redirect HTTPS, con limite di quattro richieste. Il download scrive soltanto la partizione applicativa inattiva; NVS, bootloader e tabella partizioni non vengono aggiornati via OTA. La nuova partizione viene selezionata soltanto dopo le verifiche. Un’interruzione durante il download conserva la partizione attiva.

Per installare servono dati reali completi e assenza di allarme. L’installazione automatica richiede inoltre cinque minuti continuativi sotto il preavviso. La simulazione non può autorizzare l’installazione. La perdita dei dati o un allarme durante il download ne impediscono l’attivazione. Durante la scrittura la retroilluminazione viene spenta e i tap bloccati; l’applicazione si riavvia al termine, con una breve interruzione del monitoraggio.

Il bootloader già previsto dal core 3.3.11 supporta il rollback. Dopo OTA, il firmware nuovo rimane in verifica per 60 secondi: loop e heartbeat LVGL devono funzionare. Il watchdog è attivo in questa fase; un reset prima della validazione permette al bootloader di ritornare al precedente firmware. L’assenza di Internet non rende invalido un avvio: la configurazione Wi-Fi deve restare utilizzabile anche offline. La versione che ha già causato un rollback viene memorizzata e non viene reinstallata; serve una release con un nuovo codice. I tentativi di installazione automatica falliti sono distanziati di almeno un’ora. Il recupero da guasti reali va ancora collaudato sulla scheda.

Durante le nuove schermate di configurazione il cloud è temporaneamente sospeso e il suo socket TLS viene liberato prima di creare la pagina; le letture locali restano attive. Alla chiusura riprende il comportamento di riserva configurato. Le operazioni OTA riutilizzano il task HTTPS esistente, dopo la chiusura del socket Shelly: non vengono mantenute due sessioni TLS né un secondo stack OTA permanente. Rimangono invariati clock RGB a 16 MHz, bounce buffer da 16 righe e full refresh. Queste scelte conservano la base del precedente collaudo, ma il nuovo carico deve essere osservato sul pannello fisico.

## Procedura USB e verifiche

1. Collegare il display al Mac con il cavo USB dati. Non usare il vecchio firmware diagnostico.
2. Ricompilare: `python3 firmware/scripts/build.py`.
3. Per vedere il piano senza accesso USB: `python3 firmware/scripts/flash_usb.py`.
4. Identificata la porta, caricare con `python3 firmware/scripts/flash_usb.py --flash --port /dev/cu.usbmodem…`. Lo script richiede il MAC atteso `44:b1:76:b7:8f:70`, confronta la tabella partizioni, esegue un backup completo privato e scrive con verifica esptool. Nessuna cancellazione completa della flash. Inizializza la selezione OTA sull’applicazione app0 e preserva le tre configurazioni NVS `casa-wifi`, `casa-cloud`, `casa-device`. Non caricare il file `merged.bin` per un aggiornamento ordinario.
5. Leggere lo stato con `python3 firmware/scripts/provision.py --status --port /dev/cu.usbmodem…`: controllare versione, memoria, provenienza delle misure e allarme. Non eseguire la normale riprovisionatura Shelly: potrebbe ripristinare le associazioni dal PC al posto di quelle salvate sul display.
6. A casa: verificare scoperta dei due EM e quattro badge LAN; se necessario inserire IP riservati dal router in Dispositivi locali. Associare separatamente eventuali 1PM dopo averli identificati.
7. Provare standby breve, tap senza azioni involontarie, risveglio in simulazione di allarme e ritorno ai dati reali. Verificare la perdita dati scollegando la sola rete di prova, senza provocare sovraccarichi elettrici.
8. Lasciare il monitor in funzione e navigare nei dettagli per verificare che non ricompaiano artefatti. Controllare memoria minima e `cloud_stack_free_min` e `local_stack_free_min`, soprattutto durante il controllo OTA.
9. Verificare letture LAN con Internet temporaneamente assente ma Wi-Fi attivo; provare ritorno al cloud quando un host locale è irraggiungibile. Ripristinare la rete.
10. Collaudare un vero aggiornamento OTA di prova, avvio, mantenimento delle impostazioni e rollback, prima di abilitare installazioni automatiche.

## Pubblicare un rilascio

La destinazione degli aggiornamenti è `carlomicheluzzi/casa-monitor`, nelle GitHub Releases. Il firmware non contiene URL fittizi né token GitHub. Le credenziali Shelly non sono nel pacchetto OTA. Per un repository privato serve un endpoint di download accessibile al display senza incorporare un token personale; si può usare un repository pubblico dedicato ai soli binari firmati.

La chiave privata è in `.local/release-signing/private.pem`, con permessi 0600 e directory 0700, esclusa da Git. Va conservato un backup privato: perdendola, non potremo firmare altri aggiornamenti accettati dai display installati. Nel sorgente e nel firmware compare soltanto la chiave pubblica (`src/update_key.h`). Il rilascio rifiuta una chiave diversa da quella compilata.

Per preparare il pacchetto locale, dopo la compilazione:

```bash
python3 firmware/scripts/release.py
```

Per firmare il manifest quando conosciamo la directory HTTPS del rilascio:

```bash
python3 firmware/scripts/release.py --base-url https://HOST/PERCORSO/VERSIONE
```

I file sono in `.local/releases/1.1.3/`: `firmware.bin`, `SHA256SUMS`, `public-key.pem` e, quando è specificata una destinazione, `manifest.json`. Il manifest contiene payload Base64 e firma DER Base64. Non usare il file merged, bootloader o partitions come firmware OTA.

Con GitHub Releases: directory del binario `https://github.com/carlomicheluzzi/casa-monitor/releases/download/v1.1.3`; URL stabile da inserire nel display `https://github.com/carlomicheluzzi/casa-monitor/releases/latest/download/manifest.json`. Pubblicare come release normale i due asset `firmware.bin` e `manifest.json`, più eventualmente checksum e chiave pubblica. Il manifest firmato punta al binario della versione specifica, non all’asset latest modificabile.

Per il rilascio successivo incrementare `firmware/version.json` (versione e `version_code`), compilare, testare, firmare e pubblicare i due asset. Il build genera l’header versione; il confezionamento controlla che versione, modello e chiave pubblica siano effettivamente presenti nel binario. Un rilascio non viene proposto a un dispositivo che ha già lo stesso codice versione o uno superiore.

Non pubblicare `.env`, `.local`, backup flash, chiave privata o configurazioni del monitor. La preparazione e verifica del pacchetto precedono il caricamento degli asset nella release.

## Verifiche sul Mac

`firmware/scripts/test_host.py` compila i modelli C++ reali e l’interfaccia LVGL 800×480; le firme dei test usano chiavi temporanee separate da quella dei rilasci. `python3 -m unittest discover -s tests -v` verifica il monitor Python esistente. Anteprime e log sono in `.local/firmware-host/` e `.local/`. Queste verifiche non dimostrano il consumo elettrico effettivo, la raggiungibilità dei dispositivi nella LAN di casa o il funzionamento del rollback fisico.

Esito della preparazione sul Mac: 19 test di rete, 19 del modello energia, 20 di standby/API locali/firma, 6 dello strumento di rilascio e 23 del monitor Python: **87 test passati**, oltre a tre gruppi di prove LVGL per rete, monitor e nuove impostazioni. Le schermate sono state renderizzate a 800×480 e ispezionate. Compilazione ESP32-S3 riuscita, applicazione sotto il limite di 3 MiB e immagine verificata con esptool. La procedura USB è stata eseguita soltanto in modalità piano, senza accesso alla porta. Restano da completare sul dispositivo: standby reale, LAN di casa, durata/stabilità video, margine memoria e aggiornamento/rollback OTA.

## Caricamento USB del 26 novembre 2026

Backup completo di 16.777.216 byte e confronto tabella partizioni riusciti sul display con MAC atteso. La prima scrittura ha superato la verifica hash ma lo script forzava erroneamente QIO: il bootloader generato e quello precedente richiedono DIO. Il log ROM mostrava riavvii TG0WDT_SYS_RST prima dell’applicazione. Corretto il solo bootloader con DIO/80m/16MB e hash verificato. Lo script ora usa i parametri del file `flash_args` generato e li confronta con l’header del bootloader; quattro test aggiunti coprono il problema.

Dopo la correzione, la lettura USB conferma firmware 1.1.0, configurazione Shelly caricata e uptime 45 secondi senza riavvii; Wi-Fi ancora scollegato. Memoria interna libera 59.832 byte, minimo 59.048; margine minimo stack LAN 3.096 e cloud 8.972 byte. Il backup e il verbale privato sono in `.local/device-backups/44b176b78f70/20261126-091107/`. I collaudi con rete e sul display vengono registrati dopo l’osservazione effettiva.

Quattro letture USB successive, da uptime 122 a 153 secondi, confermano nessun riavvio nel periodo osservato. Memoria minima 54.896 byte e stack LAN minimo 3.064 byte, con Wi-Fi ancora scollegato. Le conferme di display/touch e le prove con dati reali restano in attesa dell’utente.

## Revisione 1.1.2: memoria HTTPS

Dopo il collegamento al Wi-Fi dell’ufficio la revisione 1.1.0 non completava HTTPS. La 1.1.1 ha aggiunto diagnostica TLS e conserva sul display il vero errore cloud al posto di nasconderlo con la ricerca locale. È stata osservata una prima risposta HTTP 200, seguita da errori TLS -12288 (X.509 fatal error); la memoria interna minima è scesa a 824 byte. Il Mac raggiungeva lo stesso server con certificato verificato sia in TLS 1.3 sia in TLS 1.2. Questo quadro è compatibile con la pressione sulla memoria interna del firmware.

La 1.1.2 installa l’allocatore mbedTLS prima dell’avvio Wi-Fi. Soltanto il task esistente cloud/OTA sposta in PSRAM le allocazioni TLS di almeno 4096 byte; gli oggetti piccoli e tutte le altre attività restano sull’allocazione interna standard. Il free è quello compatibile con gli heap ESP-IDF. I driver RGB, i framebuffer, i tempi del pannello e la verifica dei certificati non vengono modificati. L’interfaccia prevista dal core è documentata nel [port mbedTLS Espressif](https://github.com/espressif/esp-idf/blob/v5.5.5/components/mbedtls/port/include/mbedtls/esp_config.h) e nell’[allocatore di riferimento](https://github.com/espressif/esp-idf/blob/v5.5.5/components/mbedtls/port/esp_mem.c).

Con 1.1.2, il primo controllo a 21 secondi dall’avvio mostra quattro misure aggiornate, 7 richieste HTTP riuscite e nessun errore; memoria interna minima 30.920 byte e due allocazioni TLS in PSRAM senza fallimenti. Le osservazioni prolungate e il riscontro sul pannello sono registrati separatamente nel verbale privato.

Ripetute le verifiche dopo la revisione: 19 test rete, 19 energia, 20 dispositivo/firma, 10 strumenti di rilascio e flash e 23 monitor Python: **91 test passati**, oltre alle tre suite LVGL. Nuovo binario in `.local/releases/1.1.3/`; il manifest pubblico usa l’URL stabile delle release indicato sopra. I soli caricamenti applicativi 1.1.1 e 1.1.2 sono stati eseguiti a 0x10000 con verifica hash; bootloader, partizioni e configurazioni non sono stati riscritti in questi due passaggi.

Nel successivo controllo in ufficio, 13 riepiloghi USB da uptime 70 a 251 secondi registrano 79 richieste HTTP totali dall’avvio e zero errori HTTP. Memoria interna minima 26.072 byte; margine stack LAN 2.320 byte e cloud 5.956 byte. I buffer esterni sono stati ricreati durante l’osservazione senza fallimenti. Un campione a uptime 145 s mostra correttamente Zona 2 scaduta (età 93 s), con entrambe le misure nuovamente valide al campione seguente; il totale non è stato sostituito con zero. Tutte le misure sono valide all’ultimo controllo. L’esito visivo di standby/riaccensione è ancora in attesa dell’utente; questo controllo non dimostra stabilità di molte ore.

## Revisione 1.1.3: standby e dati cloud scaduti

L’utente ha segnalato che il display non si spegneva. La lettura USB mostrava Zona 2 scaduta da 296 secondi, mentre le richieste HTTPS continuavano a riuscire. La policy riaccendeva il display a ogni ciclo finché persisteva la mancanza dati, azzerando continuamente il timer. Ora l’avviso dopo 10 secondi è emesso una sola volta per interruzione; una ricezione valida riabilita l’avviso per l’episodio successivo. L’allarme di potenza e le pagine di configurazione continuano invece a sospendere lo standby. La freschezza delle misure resta controllata: un dato scaduto non diventa valido o zero per permettere lo spegnimento.

Aggiunti quattro test di regressione: perdita dati persistente, riarmo dopo recupero, opzione di risveglio disabilitata e timeout temporaneo ripristinabile. Passano 24 test dispositivo oltre ai 19 rete, 19 energia e alle tre suite LVGL. I 10 test strumenti e i 23 Python erano già passati con 1.1.2 e non sono interessati dalla modifica: 95 test complessivi.

Il protocollo USB ora riporta timer, inattività, sospensione della policy, conteggio pressioni, stato logico di standby e comando retroilluminazione. Per il collaudo è disponibile `casa_display_test:1` con `timeout_s` 15–120: non modifica la NVS, e si ripristina automaticamente dopo cinque minuti. `timeout_s:0` ripristina immediatamente il timeout salvato. Nella prova a 15 secondi, il firmware entra in standby e invia il comando di spegnimento, mantenendolo per tutti i campioni successivi senza tocchi. La conferma visiva e del risveglio resta da ricevere.

**Esito della prova 1.1.3:** l’utente conferma che lo schermo diventa nero e un solo tap ripristina le quattro tile. I riepiloghi USB confermano stato logico di standby dopo 15 secondi, nessuna pressione fantasma e una sola pressione durante il risveglio. Il timeout temporaneo è stato poi disattivato via USB e quello normale verificato a 120 secondi, uguale al valore salvato. All’ultimo controllo: Wi-Fi collegato, 55 richieste HTTP, zero errori e memoria interna minima 28.120 byte. Non è stata provocata una condizione elettrica di sovraccarico: il risveglio per allarme è coperto dai test della policy ma non da una prova di carico reale. La qualità video protratta e la LAN di casa rimangono da osservare.

## Revisione 1.1.4: configurazione e controllo aggiornamenti via USB

Aggiunti due comandi USB con identificativo di richiesta: impostazione del solo URL manifest e richiesta di controllo aggiornamenti. Il primo valida HTTPS e conserva le altre opzioni; il secondo avvia il task di verifica dell’ESP32. Lo stato riporta contatore controlli, HTTP, TLS, redirect, firma, compatibilità e versione online. Il confronto distingue un manifest firmato valido che contiene una versione già installata o precedente da un manifest incompatibile.

Test aggiunti: due casi C++ per i comandi USB (inclusi input non validi) e quattro casi Python per impedire caricamenti di credenziali o azioni ambigue durante la configurazione degli aggiornamenti. Passano 26 test dispositivo, 19 rete, 19 energia, 14 strumenti e 23 Python: 101 test complessivi e tre suite LVGL.

Primo controllo reale dalla 1.1.4: URL GitHub salvato via USB, Wi-Fi attivo, HTTP 200 dopo due redirect, firma verificata, manifest compatibile e versione GitHub 1.1.3. Il controllo giornaliero è abilitato, l’installazione automatica disabilitata; il firmware non installa la versione precedente. Il riavvio e il controllo della release allineata vengono registrati nel verbale privato dopo l’osservazione.
