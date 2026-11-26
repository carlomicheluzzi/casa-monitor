# Pubblicazione del firmware

Repository: https://github.com/carlomicheluzzi/casa-monitor

Il display deve usare questo URL stabile, da configurare una sola volta:

```text
https://github.com/carlomicheluzzi/casa-monitor/releases/latest/download/manifest.json
```

I pacchetti sono pubblicati come asset di una release GitHub normale. Il manifest contiene versione, modello, URL del binario di quella specifica release, dimensione, SHA-256 e note, racchiusi in un payload con firma ECDSA P-256/SHA-256. Il display possiede già la chiave pubblica e verifica il manifest prima di accettarne i contenuti. Un aggiornamento con lo stesso codice versione non viene proposto.

## Preparazione di una nuova versione

1. Incrementare `version` e `version_code` in `firmware/version.json`.
2. Completare la modifica, i test pertinenti e la compilazione con `python3 firmware/scripts/build.py`. La build non programma il dispositivo.
3. Committare i sorgenti verificati e pubblicarli su `main`.
4. Preparare il pacchetto firmato con lo script sottostante, sostituendo versione e note.

```bash
python3 firmware/scripts/release.py \
  --base-url https://github.com/carlomicheluzzi/casa-monitor/releases/download/v1.1.3 \
  --notes 'Descrizione della versione'
```

Lo script verifica che il binario corrisponda alla versione e alla chiave pubblica compilate, poi crea in `.local/releases/VERSIONE/` `firmware.bin`, `manifest.json`, `SHA256SUMS` e `public-key.pem`. La chiave privata rimane in `.local/release-signing/private.pem` e non va aggiunta a Git, a una release o a un log. Conservare un backup privato: una chiave diversa non è accettata dai dispositivi già installati.

## Caricamento della release

Esempio per la 1.1.3, con CLI `gh` autenticata sull’account personale:

```bash
gh release create v1.1.3 \
  .local/releases/1.1.3/firmware.bin \
  .local/releases/1.1.3/manifest.json \
  .local/releases/1.1.3/SHA256SUMS \
  .local/releases/1.1.3/public-key.pem \
  --repo carlomicheluzzi/casa-monitor \
  --target main --draft --title 'CasaMonitor 1.1.3' \
  --notes-file .local/releases/1.1.3/release-notes.md
```

Verificare i quattro asset della bozza e poi pubblicare come release normale (non prerelease). La bozza consente di caricare tutti gli asset prima di esporre il nuovo manifest al display. Verificare inoltre che il manifest e il binario siano scaricabili senza autenticazione dall’URL stabile e da quello della versione; confrontare SHA-256 e firma con gli originali.

Per la prima pubblicazione 1.1.3, il binario è quello caricato e verificato via USB il 26 novembre 2026. La disponibilità pubblica dei file non certifica il collaudo fisico dell’OTA: download sul display, riavvio, mantenimento delle impostazioni e rollback devono ancora essere verificati prima di affidarsi all’installazione automatica.

## File esclusi

`.local/` (inclusi backup, credenziali di esercizio e chiave privata), `.env` e varianti, cache Python e materiali locali dell’installazione non vengono pubblicati. Le licenze dei driver e dei font inclusi restano nei rispettivi percorsi.

## Impostare l’URL via USB (firmware 1.1.4 o successivo)

Con il display collegato al Mac, il comando seguente modifica soltanto l’URL aggiornamenti e conserva Wi-Fi, chiave Shelly, associazioni e preferenze:

```bash
python3 firmware/scripts/provision.py --updates-url https://github.com/carlomicheluzzi/casa-monitor/releases/latest/download/manifest.json
python3 firmware/scripts/provision.py --check-updates
python3 firmware/scripts/provision.py --status
```

La sezione `updates` dello stato mostra URL salvato, controllo giornaliero, installazione automatica, contatore verifiche, HTTP/TLS, redirect, firma verificata, compatibilità e versione trovata su GitHub. La richiesta `--check-updates` avvia il controllo sul dispositivo: attendere `busy: false` e verificare `http_code: 200`, `signature_verified: true` e `manifest_compatible: true`. Nessun firmware viene installato da questi comandi.

L’URL viene salvato nella NVS e caricato al riavvio. L’impostazione USB è rifiutata mentre è in corso una verifica o installazione. È richiesto un URL HTTPS valido senza credenziali. Il comando non carica né ritrasmette le credenziali cloud.
