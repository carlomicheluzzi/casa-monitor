# Monitor Shelly sul display ESP32

Firmware Arduino per **Waveshare ESP32-S3-Touch-LCD-4.3C**, 800×480, GT911, 16 MB Flash e 8 MB PSRAM, con o senza case. Le varianti 4.3 e 4.3B hanno driver diversi: questo binario non è destinato a quelle schede.

Il firmware implementa **monitor Shelly, display, touch, audio e configurazione Wi-Fi**. Dopo il trasferimento iniziale delle credenziali via USB funziona autonomamente, senza server Python o Mac acceso. I sorgenti e i binari compilati non contengono la chiave Shelly o la password Wi-Fi del proprietario; queste vengono salvate separatamente nella NVS del dispositivo.

## Versione 1.1.3 caricata via USB

Standby e luminosità configurabili, risveglio con tap/allarme, letture Shelly locali con scoperta mDNS e cloud di riserva opzionale, OTA firmato con controllo giornaliero e installazione manuale o automatica opzionale. La destinazione degli aggiornamenti è GitHub Releases di `carlomicheluzzi/casa-monitor`. Configurare l’URL stabile descritto nel [README principale](../README.md); senza URL l’OTA non esegue richieste.

Istruzioni, limiti e collaudo da completare sul dispositivo: **[Aggiornamento 1.1.3](UPGRADE-1.1.md)**. La revisione 1.1.3 è caricata sul pannello; Wi-Fi e letture cloud sono confermati in ufficio. L’utente conferma spegnimento e riaccensione con un singolo tap nella prova accelerata; ripristinato il timeout normale di 120 secondi. L’avviso per dati mancanti si attiva una volta per interruzione. LAN di casa, OTA e qualità video per periodi prolungati restano da verificare. I due buffer TLS grandi usano PSRAM, mantenendo margine nella memoria interna.

## Monitor dei consumi

- Quattro tile permanenti: Luce e Forza delle due zone, totale e margine rispetto al limite. L’associazione iniziale è importata dalla configurazione PC ed è ancora da confermare fisicamente.
- Shelly Cloud **API v2**, endpoint `/v2/devices/api/get`, solo lettura: HTTPS con verifica di certificato e nome server, bundle CA del framework e sincronizzazione dell’ora via SNTP. Nessun `setInsecure`, nessun redirect con la chiave, nessun comando ai relè.
- Letture locali nominalmente ogni secondo, con scadenza a 5 secondi. Il cloud interviene come riserva configurabile e si ferma quando tutti i canali associati sono locali e aggiornati. Per il cloud, una richiesta ogni 3 secondi, con ritento progressivo fino a 60 secondi. Il ritmo delle richieste non equivale alla frequenza di aggiornamento dei misuratori.
- Il task HTTPS gira sul core 1 a priorità 1, inferiore al task LVGL (priorità 2), sullo stesso core dell’ISR RGB. La connessione TLS verificata viene mantenuta fra richieste riuscite quando il server consente keep-alive; errori, cambio configurazione o perdita Wi-Fi la chiudono. Questo evita negoziazioni TLS ripetute a ogni lettura e riduce la contesa con il percorso RGB/PSRAM.
- Per i dati cloud, a ogni avvio si attende che `_updated` avanzi per ciascun misuratore. Anche una risposta HTTP riuscita può contenere una cache invariata. I dati mancanti, offline, invalidi o scaduti non diventano zero; il totale è disponibile solo con tutte le quattro misure valide. Il marcatore viene confrontato senza presumere il suo fuso orario.
- Soglia iniziale 4500 W, preavviso 4000 W, rientro a 4300 W o meno, dati cloud scaduti dopo 90 secondi (locali dopo 5 secondi), silenziamento per 120 secondi. Il superamento richiede un totale valido maggiore della soglia. Un allarme attivo non viene annullato dalla perdita dei dati.
- Altoparlante ES8311: allarme ripetuto, pulsante **Prova suono**, silenziamento temporaneo e flag separato per disabilitare l’audio mantenendo l’allarme visivo. Volume del codec 80/100 e ampiezza PCM 16000 su 32767, aumentati dopo la prova d’ascolto iniziale. Il driver apre solo il codec di uscita e non utilizza la registrazione del microfono.
- Toccare una tile apre potenza, energia cumulata in kWh (non consumo giornaliero), misuratore/canale e grafico della sessione. Lo storico conserva 300 campioni a intervalli di circa 3 secondi e rappresenta i dati assenti con interruzioni. Riparte dopo il riavvio.
- Il dettaglio distingue **Potenza attuale** ed **Energia cumulata**, con separatori italiani, tensione e stato/età del dato. La curva ha scala numerica in W o kW, minimo/massimo dei campioni disponibili e riferimenti temporali relativi approssimati dalla cadenza di campionamento. Luce usa un grafico più ampio; Forza mostra anche i riquadri dei sotto-contatori. Il pulsante **Misuratore** apre modello, identificativo e canale API. La vista viene creata quando si apre e liberata alla chiusura per conservare memoria interna nella dashboard.
- Nel dettaglio Forza sono presenti **boiler Carlo, boiler Gloria e lavatrice + asciugatrice**. Le tre associazioni partono vuote, senza attribuire i bagni a una zona presunta. La misura del gruppo lavaggio rimane aggregata; i dettagli non vengono sommati nuovamente al totale di casa.
- **Impostazioni → Monitor, canali e apparecchi**: modifica di soglie, durata dati/silenziamento, audio, associazione dei quattro canali principali, conferma delle zone e associazione dei tre apparecchi a misuratori dedicati del catalogo. I valori persistono nella NVS. Canali duplicati o riutilizzati come sotto-misure vengono rifiutati.
- **Simulazione / Torna ai dati reali**: dati, storico, allarme e silenziamento separati. Il pulsante Scenario passa fra regolare, preavviso, superamento e perdita dati. Il monitor reale continua ad aggiornarsi in background; l’audio e la schermata seguono la modalità selezionata. Le simulazioni non modificano apparecchi reali.

La pagina Automazione mostra un flag disabilitato per il futuro distacco dei carichi. **La logica di distacco e ripristino non è attiva né collegata ai relè in questa versione.** Richiede ancora verifica dei carichi/cicli dell’asciugatrice, associazione dei tre dispositivi e realizzazione/collaudo su letture locali tempestive.

## Configurazione Shelly via USB

Con il monitor collegato via USB e senza un altro monitor seriale aperto:

```bash
python3 firmware/scripts/provision.py --port /dev/cu.usbmodem1101
```

Lo script legge la chiave dal file `.env` già usato dal progetto, le soglie e i canali da `.local/config.json`, e rileva il catalogo dei misuratori in sola lettura. Trasmette i dati direttamente via USB con un identificativo di richiesta e attende conferma dal dispositivo. Non stampa la richiesta o la chiave, non passa la chiave negli argomenti dei processi e non crea file che la contengano. Questa operazione **sostituisce la configurazione monitor sul display con quella del PC**; per aggiornamenti ordinari del firmware non va ripetuta se le credenziali restano valide.

Per rileggere un riepilogo del dispositivo senza chiavi, password o richieste aggiuntive al cloud:

```bash
python3 firmware/scripts/provision.py --port /dev/cu.usbmodem1101 --status
```

La porta USB può cambiare; omettendo `--port` lo script accetta una sola porta `cu.usb*`. Il catalogo può essere aggiornato dalla scoperta locale, senza sostituire la configurazione tramite USB. La chiave è conservata nel namespace NVS `casa-cloud`, separato da `casa-wifi`. L’ora trasferita via USB è usata solo per la sessione corrente; a ogni avvio il dispositivo avvia SNTP per la validazione HTTPS.

## Comportamento

- Primo avvio senza rete salvata: apertura immediata di **Rete Wi-Fi**.
- Avvio con configurazione salvata: tentativo non bloccante di 25 secondi. Se non viene ottenuto un IP, si apre automaticamente la configurazione.
- Accesso manuale in ogni momento: **Impostazioni → Rete Wi-Fi**. Un cambio manuale può interrompere un tentativo automatico; un test delle nuove impostazioni deve invece terminare prima del successivo.
- Ricerca asincrona delle reti a 2,4 GHz, segnale in dBm, indicazione aperta/protetta. È possibile scrivere manualmente il nome di una rete nascosta. WEP e reti Enterprise non sono selezionabili; il flusso è per reti domestiche WPA/WPA2/WPA3 Personal e reti aperte esplicitamente selezionate.
- Password nascosta, pulsante Mostra/Nascondi e tastiera touch con simboli. La password salvata non viene caricata nei campi: lasciando vuoto il campo si conserva quella precedente **solo se SSID e tipo di sicurezza non cambiano**.
- DHCP predefinito oppure IPv4 fisso con maschera, gateway e DNS. Il modulo verifica sintassi, maschera contigua /1–/30, indirizzi host e gateway nella stessa sottorete; non può rilevare in anticipo tutti i conflitti IP dell’impianto.
- **Salva e prova**: le nuove impostazioni restano provvisorie finché il Wi-Fi non ha ottenuto un IP. Solo allora vengono scritte in flash con una singola operazione NVS. Il successo Wi-Fi non certifica Internet o Shelly Cloud.
- Tentativo fallito o errore di salvataggio: le credenziali precedenti restano salvate e vengono riprovate. Il modulo non ripristina impostazioni di fabbrica e non svuota il modulo che l’utente sta compilando.
- Perdita della rete: primo tentativo di recupero di 25 secondi, poi apertura della configurazione; nuovi tentativi ogni 30 secondi dopo un fallimento. La schermata non viene riaperta continuamente se l’utente la chiude.
- La tastiera riduce il modulo e porta il campo selezionato nell’area visibile, anche per i parametri IP. Il tasto di conferma della tastiera la chiude; non salva automaticamente la rete.

La pagina funziona sul display anche senza Internet. Non viene creato un hotspot e non è previsto Ethernet, coerentemente con la scelta Wi-Fi. Non si modificano le impostazioni di rete del Mac.

## Compilazione locale

Su questo Mac Apple Silicon, con Python 3.10+, curl e accesso Internet per la prima preparazione:

```bash
python3 firmware/scripts/build.py
```

Lo script prepara tutto sotto `.local/`: Arduino CLI **1.5.1**, core ESP32 **3.3.11**, LVGL **8.4.0**, cJSON **1.7.19** per i test host e configurazione della scheda. Gli archivi iniziali sono verificati tramite SHA-256. Il firmware usa cJSON del framework della stessa versione. Driver display, touch e audio sono inclusi nei sorgenti, dalla revisione ufficiale Waveshare indicata in `CasaMonitor/src/board/upstream.json`, con modifiche locali registrate separatamente dagli hash originali. Il clock RGB resta quello originale di 16 MHz; la prova corrente usa doppio buffer con full refresh LVGL, invece del modo direct dell’esempio. Il font comprende gli accenti italiani; licenze Montserrat e DejaVu sono nella cartella `src/fonts/`.

I binari vengono prodotti in `.local/firmware-output/`. La compilazione **non programma alcuna scheda**. Il file `CasaMonitor.ino.merged.bin` è l’immagine completa, da indirizzo flash **0x0**; `CasaMonitor.ino.bin` è solo l’applicazione e non va usato a 0x0. Per il caricamento via USB si può usare Arduino CLI `upload` con lo stesso FQBN dello script e la porta effettiva della scheda. Prima di caricare, verificare il modello 4.3C e conservare l’eventuale firmware precedente.

Le credenziali sono conservate nella NVS dell’ESP32, namespace `casa-wifi`, chiave `config`, non nei sorgenti o nei file del Mac. Questa build non abilita la cifratura della flash. Un aggiornamento ordinario conserva la configurazione se non si cancella la NVS; l’immagine completa e un’eventuale cancellazione della flash richiedono attenzione perché possono azzerarla.

## Verifiche

Test della logica e della **stessa UI LVGL compilata nel firmware**, eseguiti sul computer:

```bash
python3 firmware/scripts/build.py --host-only
python3 firmware/scripts/test_host.py
python3 -m unittest discover -s tests -v
```

I test host richiedono un compilatore C/C++ (su questo Mac: gli strumenti Apple già presenti). La preparazione `--host-only` scarica LVGL e cJSON, senza toolchain ESP32. Con Pillow installato vengono prodotte anche le anteprime PNG; altrimenti restano i render PPM. Gli artefatti sono in `.local/firmware-host/`.

Il test Wi-Fi usa una piattaforma fittizia e controlla timeout, primo avvio, credenziali mantenute, salvataggio dopo l’IP, riavvio, recupero, errore flash, cambio DHCP/IP fisso e rollover di `millis()`. I test UI controllano navigazione, ricerca reti, tastiera riapribile, visibilità dei campi e assenza di cancellazioni del modulo durante i tentativi. Le anteprime sono render LVGL a 800×480, non fotografie del dispositivo.

**Collaudo iniziale sulla scheda, 13 novembre 2026:** etichetta 4.3C-BOX confermata dalle fotografie; rilevati ESP32-S3 revision v0.2, 16 MB Flash e 8 MB PSRAM. Salvata copia completa di fabbrica prima della scrittura. Il primo tentativo di lettura a 460800 baud è fallito senza produrre un backup completo; la lettura a 115200 baud è riuscita per tutti i 16 MB. Firmware caricato a 115200 baud con verifica hash positiva dei quattro segmenti (bootloader, partizioni, boot_app0, applicazione). I messaggi seriali confermano inizializzazione I2C/IO expander, GT911, LCD/LVGL e interfaccia; il programma rimane in esecuzione con Wi-Fi da configurare.

Backup privato e verbale JSON in `.local/device-backups/44b176b78f70/`, esclusi da Git. Il backup può contenere impostazioni del produttore e non va pubblicato. Il caricamento ha usato i quattro segmenti della build, senza cancellazione completa della flash.

**Confermato dall’utente sulla scheda fisica:** uso della configurazione touch, connessione Wi-Fi e riconnessione automatica dopo RESET senza reinserire la password. La versione con il monitor completo è stata successivamente caricata con quattro verifiche hash positive e la configurazione Shelly è stata confermata dal protocollo USB. Il riepilogo letto dall’ESP32 conferma autenticazione e quattro letture reali aggiornate dalla API v2: 29,11 W, 19,99 W, 37,01 W e 1,78 W, totale 87,89 W al controllo. Il log conferma inizializzazione riuscita dell’altoparlante; ciò non sostituisce la prova d’ascolto.

**Riavvio della versione completa verificato via USB:** Wi-Fi e autenticazione HTTPS Shelly recuperati senza ripetere la configurazione. Come previsto, dopo il riavvio ogni misuratore attende un nuovo avanzamento della cache prima di rendere disponibile il totale.

**Conferma successiva dell’utente:** le quattro tile sono visibili e i segnali di Prova suono si sentono, ma il volume iniziale era troppo basso. La revisione con volume e ampiezza del tono aumentati è stata compilata e caricata con quattro verifiche hash positive.

**Disturbo video segnalato:** in `IMG_3490.MOV` sono visibili righe spezzate e frammenti di testo soprattutto nel margine sinistro; l’utente li conferma anche a occhio nudo. La causa è ancora da isolare: il sintomo è compatibile con problemi di trasferimento RGB/PSRAM sotto carico, descritti nella [documentazione Espressif](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html#bounce-buffer-with-single-psram-frame-buffer). La prova diagnostica a 12 MHz è stata compilata e caricata con quattro verifiche hash positive, ma l’utente ha visto soltanto schermate a pieno colore. La modifica è stata annullata, ripristinando i 16 MHz originali; l’utente ha confermato il ritorno delle tile. La prova successiva passa dal modo direct LVGL al full refresh con gli stessi due framebuffer, senza modificare pin, tempi RGB o dimensioni dei buffer. L’isolamento della causa del disturbo resta necessario; compilazione e log seriale non certificano la qualità dell’immagine.

**Risultati successivi:** l’utente conferma volume aggiornato sufficiente, ma il full refresh non elimina gli artefatti. Un’immagine diagnostica statica, senza avviare Wi-Fi/cloud/audio, è invece pulita sia a 16 sia a 25 MHz. Il datasheet ST7262 riporta 23–27 MHz come intervallo di clock RGB (25 MHz tipico); questo dato da solo non identifica la causa del disturbo nel programma completo.

**Isolamento e prima revisione HTTPS:** il test statico resta pulito con il solo audio e con audio + Wi-Fi; attivando anche le letture HTTPS Shelly compaiono gli artefatti. Aumentare il clock a 25 MHz sotto questo carico peggiora il disturbo. Ripristinati i 16 MHz, sono stati introdotti riuso della connessione TLS e task HTTPS sul core 1 a priorità inferiore a LVGL. Ricompilato e ricaricato il monitor completo con quattro verifiche hash positive; l’utente ha inizialmente riferito la scomparsa del disturbo. Le due modifiche HTTPS sono state provate insieme: non è stato misurato separatamente il contributo di ciascuna. Il monitor torna a leggere le API v2; la configurazione e il volume aggiornato sono conservati. La diagnosi di contesa delle risorse RGB/PSRAM è un’inferenza coerente con questi confronti, non una misura diretta dei mancati riempimenti DMA.

**Ricorrenza dopo il primo controllo:** l’utente segnala il ritorno del disturbo. Il riepilogo USB, con uptime di 203 secondi, mostra ancora quattro letture valide, cloud collegato e circa 52,7 kB di memoria interna libera: la prima conferma visiva non stabiliva quindi la tenuta nel tempo. La revisione successiva amplia i due bounce buffer da 10 a 16 righe, aggiungendo 19.200 byte totali di SRAM, e mantiene 16 MHz, full refresh e gestione HTTPS precedente. Il riepilogo USB aggiunge memoria minima, maggiore blocco interno libero, PSRAM libera, numero richieste, richieste avviate con socket chiuso, errori HTTP, ultimo codice e durata dell’ultima richiesta. Il contatore dei socket chiusi descrive lo stato prima di `POST`, non misura direttamente le negoziazioni TLS. Il risultato di questa revisione richiede un’osservazione prolungata del pannello reale.

**Esito del controllo prolungato, buffer da 16 righe:** l’utente riferisce immagine pulita e navigazione nelle impostazioni regolare. Tredici riepiloghi USB su 360 secondi registrano 124 richieste HTTP, nessun errore, una richiesta avviata a socket chiuso e memoria interna libera intorno a 33 kB (minimo storico 18.260 byte). Successivamente è stato caricato il dettaglio ridisegnato e verificato con gli stessi test LVGL e compilazione ESP32: altri undici riepiloghi su 300 secondi, 107 richieste HTTP totali dall’avvio, nessun errore, una richiesta partita con socket chiuso, memoria libera tra 36.376 e 36.572 byte e minimo storico 13.404 byte. All’ultimo riepilogo le quattro misure sono valide. L’utente approva la nuova vista e conferma che lo schermo è rimasto pulito dopo questi minuti con il dettaglio aperto. Questi sono collaudi circoscritti alla durata e agli stati osservati, non una prova di funzionamento continuo di molte ore.

Il test temporaneo è in `DisplayDiagnostic/DisplayDiagnostic.ino`. Si compila separatamente con `python3 firmware/scripts/build_display_diagnostic.py`, senza sovrascrivere la build del monitor. Parte con tutti i servizi disattivati; `display_diagnostic.py --clock 16` oppure `--clock 25` cambia solo il clock corrente. `--service audio`, `wifi`, `cloud` (dopo wifi) e `refresh` attivano singolarmente i servizi o il ridisegno periodico della stessa immagine; `wifi_off` disattiva la radio e `reset` riparte senza servizi a 16 MHz. Il test può leggere le configurazioni esistenti per connettersi, ma non le salva e non comanda relè. Dopo il collaudo va ricaricata l’applicazione CasaMonitor.

**Da completare sulla scheda:** prova di IP fisso e conservazione/configurazione dopo uno spegnimento completo. Gli esiti successivi sono registrati nel verbale privato `.local/device-backups/44b176b78f70/deployment.json`.

## Struttura

- `CasaMonitor.ino`: inizializzazione board e collegamento fra controller e display.
- `src/wifi_model.*`: regole di connessione e salvataggio, indipendenti dall’hardware.
- `src/wifi_platform.*`: driver Arduino WiFi e persistenza NVS.
- `src/network_ui.*`: schermate LVGL, ricerca reti e tastiera touch.
- `src/energy_model.*`: validità delle misure, JSON, storico, allarme e simulazione indipendenti dall’hardware.
- `src/energy_ui.*`: quattro tile, dettaglio e impostazioni monitor sul display.
- `src/cloud_service.*`: HTTPS su task separato, protocollo USB e NVS Shelly.
- `src/tls_memory.*`: buffer TLS grandi in PSRAM, limitati al task cloud/OTA, con diagnostica delle allocazioni.
- `src/device_model.*`, `device_platform.*`, `device_ui.*`: opzioni persistenti, standby/touch e schermate display/LAN/aggiornamenti.
- `src/local_service.*`: scoperta mDNS, identità e letture HTTP locali.
- `src/update_service.*`, `crypto_util.*`: manifest firmati, download HTTPS, scrittura OTA e verifica differita di avvio.
- `src/alarm_audio.*`: task dell’allarme audio tramite codec ES8311.
- `src/board/`: driver ufficiali Waveshare, con licenza e revisione.
- `lv_conf.h`: configurazione LVGL condivisa fra ESP32 e test host.

Fonti: [scheda Waveshare](https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3C), [esempio LVGL ufficiale](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-4.3C/tree/44e412f08c1e68c0da826bc5f51436f156a02eea/examples/arduino/12_lvgl_transplant), [Wi-Fi Arduino ESP32](https://github.com/espressif/arduino-esp32/tree/3.3.11/libraries/WiFi), [Preferences/NVS](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/preferences.html).

## Pubblicazione GitHub

Sorgenti: https://github.com/carlomicheluzzi/casa-monitor

Manifest stabile: `https://github.com/carlomicheluzzi/casa-monitor/releases/latest/download/manifest.json`. [Procedura di rilascio](PUBLISHING.md). La chiave privata di firma resta sul Mac in `.local/release-signing/`, esclusa da Git; il firmware contiene soltanto quella pubblica.
