#include "wifi_model.h"
#include <algorithm>
#include <cctype>

namespace casa {
bool ipv4(const std::string &text, uint32_t &value) {
    value = 0;
    size_t pos = 0;
    for (int i = 0; i < 4; ++i) {
        if (pos >= text.size()) return false;
        unsigned octet = 0, digits = 0;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
            octet = octet * 10 + text[pos++] - '0';
            if (++digits > 3 || octet > 255) return false;
        }
        if (!digits) return false;
        value = (value << 8) | octet;
        if (i < 3 && (pos >= text.size() || text[pos++] != '.')) return false;
    }
    return pos == text.size();
}

static bool unicast(uint32_t ip) {
    unsigned first = ip >> 24;
    return first > 0 && first < 224 && first != 127 &&
           (ip & 0xffff0000u) != 0xa9fe0000u;
}

std::string validate(const WifiConfig &c) {
    if (c.ssid.empty() || c.ssid.size() > 32 || c.ssid.find('\0') != std::string::npos ||
        c.ssid.find('\n') != std::string::npos || c.ssid.find('\r') != std::string::npos)
        return "Nome rete richiesto: massimo 32 byte, senza ritorni a capo.";
    if (!c.open) {
        bool hex = c.password.size() == 64 && std::all_of(c.password.begin(), c.password.end(),
            [](unsigned char v) { return std::isxdigit(v); });
        if (!hex && (c.password.size() < 8 || c.password.size() > 63))
            return "Password: da 8 a 63 caratteri, oppure 64 cifre esadecimali.";
        if (c.password.find('\0') != std::string::npos)
            return "La password contiene un carattere non valido.";
    }
    if (c.dhcp) return "";
    uint32_t ip, mask, gateway, dns;
    if (!ipv4(c.ip, ip) || !unicast(ip)) return "Indirizzo IP non valido.";
    if (!ipv4(c.mask, mask)) return "Maschera di rete non valida.";
    uint32_t host = ~mask;
    if (host < 3 || host > 0x7fffffffu || (host & (host + 1)) != 0)
        return "Maschera richiesta: contigua, da /1 a /30.";
    if ((ip & host) == 0 || (ip & host) == host)
        return "L’IP non può essere l’indirizzo di rete o broadcast.";
    if (!ipv4(c.gateway, gateway) || !unicast(gateway) || gateway == ip ||
        (gateway & mask) != (ip & mask) || (gateway & host) == 0 || (gateway & host) == host)
        return "Gateway richiesto nella stessa sottorete, diverso dall’IP.";
    if (!ipv4(c.dns, dns) || !unicast(dns)) return "Server DNS non valido.";
    return "";
}

void WifiModel::start(uint32_t now) {
    configured = io.load(saved) && validate(saved).empty();
    if (configured) {
        message = "Collegamento alla rete salvata…";
        begin(saved, now);
    } else {
        saved = {};
        message = "Configura il Wi-Fi per collegare il monitor.";
        state = Connection::Unconfigured;
        ++openRequest;
    }
}

void WifiModel::begin(const WifiConfig &config, uint32_t now) {
    attempted = config;
    since = now;
    state = Connection::Connecting;
    io.disconnect();
    if (!io.connect(config)) failed(now, "Impossibile avviare la connessione Wi-Fi.");
}

std::string WifiModel::apply(WifiConfig config, bool keepPassword, uint32_t now) {
    if (state == Connection::Connecting && trial)
        return "Attendi la fine del tentativo prima di cambiare rete.";
    if (keepPassword) {
        if (!configured || config.ssid != saved.ssid || config.open || saved.open)
            return "Inserisci la password per la nuova rete.";
        config.password = saved.password;
    }
    if (config.open) config.password.clear();
    std::string error = validate(config);
    if (!error.empty()) return error;
    trial = true;
    restoring = false;
    message = "Provo la nuova rete. Salvataggio dopo l’assegnazione dell’IP…";
    begin(config, now);
    return "";
}

void WifiModel::retry(uint32_t now) {
    if (!configured || state == Connection::Connecting) return;
    trial = restoring = false;
    message = "Riprovo la rete salvata…";
    begin(saved, now);
}

void WifiModel::failed(uint32_t now, const std::string &reason) {
    io.disconnect();
    state = Connection::Offline;
    since = now;
    message = reason;
    if (!reported || trial) ++openRequest;
    reported = true;
    if (trial) {
        trial = false;
        if (configured) {
            restoring = true;
            message += " Riprovo la configurazione precedente.";
            begin(saved, now);
        }
    }
}

void WifiModel::tick(uint32_t now) {
    if (state == Connection::Connecting) {
        if (io.hasIP()) {
            if (trial && !io.save(attempted)) {
                failed(now, "Rete raggiunta, ma salvataggio non riuscito.");
                return;
            }
            if (trial) {
                saved = attempted;
                configured = true;
                message = "Wi-Fi collegato. Configurazione salvata.";
            } else if (restoring) {
                message = "Nuove impostazioni non salvate. Ripristinata la rete precedente.";
            } else {
                message = "Wi-Fi collegato.";
            }
            trial = restoring = reported = false;
            state = Connection::Online;
        } else if (uint32_t(now - since) >= TIMEOUT_MS) {
            failed(now, "Rete non raggiunta entro 25 s. Controlla nome, password e parametri IP.");
        }
    } else if (state == Connection::Online && !io.hasIP()) {
        message = "Connessione persa. Riprovo la rete salvata…";
        begin(saved, now);
    } else if (state == Connection::Offline && configured && uint32_t(now - since) >= RETRY_MS) {
        // Do not erase credentials or repeatedly interrupt the form during an outage.
        message = "Rete assente. Nuovo tentativo in corso…";
        begin(saved, now);
    }
}
}
