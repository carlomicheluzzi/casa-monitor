#include "update_service.h"
#include "crypto_util.h"
#include "update_key.h"
#include "version.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>
#include <ctime>
#include <Preferences.h>
extern const uint8_t rootBundleStart[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t rootBundleEnd[] asm("_binary_x509_crt_bundle_end");
extern "C" uint32_t casa_lvgl_heartbeat();
// Arduino otherwise marks a pending image valid before setup() can test it.
bool verifyRollbackLater(){return true;}
namespace casa {
namespace {
class Body:public Stream {public:std::string text;uint32_t at=millis();
    size_t write(uint8_t b)override{return write(&b,1);}size_t write(const uint8_t *p,size_t n)override{if(text.size()+n>12000||uint32_t(millis()-at)>20000)return 0;text.append(reinterpret_cast<const char *>(p),n);return n;}
    int available()override{return 0;}int read()override{return -1;}int peek()override{return -1;}void flush()override{}
};
bool open(HTTPClient &http,NetworkClientSecure &client,std::string url){
    http.setTimeout(5000);http.setConnectTimeout(5000);http.setReuse(false);http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    for(unsigned hop=0;hop<4;++hop){if(!httpsUrlAllowed(url,4096))return false;const char *headers[]={"Location"};http.collectHeaders(headers,1);
        if(!http.begin(client,url.c_str()))return false;int code=http.GET();
        if(code==200)return true;
        if(code==301||code==302||code==303||code==307||code==308){url=http.header("Location").c_str();http.end();client.stop();continue;}
        http.end();client.stop();return false;
    }return false;
}
bool pendingCode(unsigned code){Preferences p;if(!p.begin("casa-device",false))return false;bool ok=p.putUInt("pending_code",code)==sizeof(uint32_t);p.end();return ok;}
void setupTls(NetworkClientSecure &client){client.setCACertBundle(rootBundleStart,size_t(rootBundleEnd-rootBundleStart));client.setHandshakeTimeout(10);}
}
void UpdateService::begin(CloudService &c){cloud=&c;mutex=xSemaphoreCreateMutex();bootAt=millis();esp_ota_img_states_t state;
    pendingBoot=esp_ota_get_state_partition(esp_ota_get_running_partition(),&state)==ESP_OK&&state==ESP_OTA_IMG_PENDING_VERIFY;
    Preferences metadata;if(metadata.begin("casa-device",false)){
        auto pending=metadata.getUInt("pending_code",0);blockedCode=metadata.getUInt("blocked_code",0);
        if(pending&&pending!=FIRMWARE_CODE){esp_ota_img_states_t previous;auto *other=esp_ota_get_next_update_partition(nullptr);
            if(other&&esp_ota_get_state_partition(other,&previous)==ESP_OK&&(previous==ESP_OTA_IMG_INVALID||previous==ESP_OTA_IMG_ABORTED)){blockedCode=pending;metadata.putUInt("blocked_code",pending);}
            metadata.putUInt("pending_code",0);
        }metadata.end();
    }
    bootReady.store(!pendingBoot);if(pendingBoot)enableLoopWDT();
    // OTA runs on the existing cloud worker after it releases its TLS socket.
    // No additional permanent task stack and no simultaneous TLS handshakes.
}
void UpdateService::configure(const DeviceOptions &v){xSemaphoreTake(mutex,portMAX_DELAY);bool changed=options.manifestUrl!=v.manifestUrl;options=v;++generation;if(changed){release={};view.available=false;view.version.clear();view.notes.clear();}if(!view.busy)view.status=v.manifestUrl.empty()?"Indirizzo del repository da configurare":"Pronto per verificare gli aggiornamenti";xSemaphoreGive(mutex);}
UpdateView UpdateService::snapshot(){xSemaphoreTake(mutex,portMAX_DELAY);auto v=view;v.maintenance=dark.load();xSemaphoreGive(mutex);return v;}
void UpdateService::status(const std::string &s,bool busy){xSemaphoreTake(mutex,portMAX_DELAY);view.status=s;view.busy=busy;xSemaphoreGive(mutex);}
void UpdateService::tick(bool safe,bool quiet){safeNow.store(safe);
    auto now=millis();if(quiet&&!wasQuiet)quietAt=now;wasQuiet=quiet;
    DeviceOptions cfg;UpdateView current;xSemaphoreTake(mutex,portMAX_DELAY);cfg=options;current=view;xSemaphoreGive(mutex);
    bool manual=checkRequested.exchange(false),install=installRequested.exchange(false);
    bool due=cfg.dailyCheck&&(!attempted||uint32_t(now-lastCheck)>=retry.load());
    bool automatic=cfg.autoInstall&&quiet&&uint32_t(now-quietAt)>=300000&&(!installAttempted||uint32_t(now-lastInstall)>=3600000);
    bool doInstall=current.available&&(install||automatic);
    if(!current.busy&&(manual||due||doInstall)){
        if(cfg.manifestUrl.empty()){if(manual||install)status("Configura prima l’indirizzo HTTPS del repository");}
        else if(WiFi.status()!=WL_CONNECTED){if(manual||install)status("Wi-Fi non collegato");}
        else if(doInstall&&(!safe||!bootReady.load())){if(install)status("Attendi dati validi e la verifica di avvio del monitor");}
        else {scheduledInstall.store(doInstall);status(doInstall?"Preparazione aggiornamento…":"Verifica aggiornamenti…",true);
            if(cloud->schedule(entry,this)){attempted=true;lastCheck=now;if(doInstall){wasQuiet=false;lastInstall=now;installAttempted=true;}}
            else {attempted=true;lastCheck=now;retry.store(60000);status("Connessione occupata: riprova tra poco");}
        }
    }
    if(pendingBoot){auto beat=casa_lvgl_heartbeat();if(uint32_t(now-bootAt)>10000&&(!beat||uint32_t(now-beat)>5000))esp_ota_mark_app_invalid_rollback_and_reboot();
        if(uint32_t(now-bootAt)>=60000&&beat&&uint32_t(now-beat)<2000){if(esp_ota_mark_app_valid_cancel_rollback()==ESP_OK){pendingBoot=false;bootReady.store(true);disableLoopWDT();pendingCode(0);Serial.println("OTA: avvio validato");}}
    }
}
void UpdateService::entry(void *self){static_cast<UpdateService *>(self)->task();}
bool UpdateService::fetch(const std::string &url,std::string &body){NetworkClientSecure client;setupTls(client);HTTPClient http;bool ok=false;
    if(open(http,client,url)&&http.getSize()<=12000){Body response;ok=http.writeToStream(&response)>0;if(ok)body=std::move(response.text);}http.end();client.stop();return ok;
}
bool UpdateService::apply(const ReleaseInfo &v){
    if(!safeNow.load()||!bootReady.load()){status("Installazione sospesa: attendi dati validi e consumi sotto soglia");return false;}
    status("Installazione: lo schermo si spegne, poi il monitor si riavvia",true);vTaskDelay(pdMS_TO_TICKS(1200));
    dark.store(true);vTaskDelay(pdMS_TO_TICKS(150));
    NetworkClientSecure client;setupTls(client);HTTPClient http;esp_ota_handle_t handle=0;bool started=false,ok=false;
    const esp_partition_t *slot=esp_ota_get_next_update_partition(nullptr);
    if(slot&&v.size<=slot->size&&open(http,client,v.url)&&(http.getSize()==-1||size_t(http.getSize())==v.size)&&safeNow.load()){
        if(esp_ota_begin(slot,v.size,&handle)==ESP_OK){started=true;
            class Writer:public Stream {public:esp_ota_handle_t handle;size_t size=0,expected;uint32_t at=millis();std::atomic<bool> &safe;mbedtls_sha256_context hash;UpdateService &owner;
                Writer(esp_ota_handle_t h,size_t n,std::atomic<bool> &s,UpdateService &o):handle(h),expected(n),safe(s),owner(o){mbedtls_sha256_init(&hash);mbedtls_sha256_starts(&hash,0);}
                ~Writer(){mbedtls_sha256_free(&hash);}size_t write(uint8_t b)override{return write(&b,1);}
                size_t write(const uint8_t *p,size_t n)override{if(!safe.load()||size+n>expected||uint32_t(millis()-at)>180000)return 0;if(esp_ota_write(handle,p,n)!=ESP_OK||mbedtls_sha256_update(&hash,p,n))return 0;size+=n;xSemaphoreTake(owner.mutex,portMAX_DELAY);owner.view.progress=unsigned(size*100/expected);xSemaphoreGive(owner.mutex);return n;}
                int available()override{return 0;}int read()override{return -1;}int peek()override{return -1;}void flush()override{}
            } writer(handle,v.size,safeNow,*this);
            int bytes=http.writeToStream(&writer);unsigned char digest[32];mbedtls_sha256_finish(&writer.hash,digest);const char *hex="0123456789abcdef";std::string actual;for(auto b:digest){actual+=hex[b>>4];actual+=hex[b&15];}
            ok=bytes>0&&writer.size==v.size&&actual==v.sha256&&safeNow.load();
            if(ok){ok=esp_ota_end(handle)==ESP_OK;started=false;}
        }
    }
    http.end();client.stop();if(started)esp_ota_abort(handle);
    if(ok&&safeNow.load()&&pendingCode(v.code)&&safeNow.load()&&esp_ota_set_boot_partition(slot)==ESP_OK){status("Aggiornamento verificato. Riavvio…",true);vTaskDelay(pdMS_TO_TICKS(200));ESP.restart();}
    pendingCode(0);dark.store(false);status("Aggiornamento non installato: firmware attuale conservato");return false;
}
void UpdateService::task(){
    DeviceOptions cfg;uint32_t gen;ReleaseInfo candidate;
    xSemaphoreTake(mutex,portMAX_DELAY);cfg=options;gen=generation;candidate=release;xSemaphoreGive(mutex);
    bool success=false,install=scheduledInstall.load();
    if(!install){xSemaphoreTake(mutex,portMAX_DELAY);release={};view.available=false;view.version.clear();view.notes.clear();xSemaphoreGive(mutex);}
    configTime(0,0,"pool.ntp.org","time.cloudflare.com");auto waitAt=millis();
    while(time(nullptr)<1735689600&&uint32_t(millis()-waitAt)<10000)vTaskDelay(pdMS_TO_TICKS(100));
    if(time(nullptr)<1735689600)status("Ora non sincronizzata: riprova con Internet disponibile");
    else if(install){apply(candidate);}
    else {std::string envelope,payload,error;ReleaseInfo next;auto *slot=esp_ota_get_next_update_partition(nullptr);
        if(!fetch(cfg.manifestUrl,envelope))status("Repository non raggiungibile o risposta troppo grande");
        else if(!verifySignedPayload(envelope,UPDATE_PUBLIC_KEY,payload))status("Firma aggiornamento non valida: pacchetto rifiutato");
        else if(slot&&parseRelease(payload.c_str(),FIRMWARE_CODE,slot->size,next,error)){
            if(next.code==blockedCode){status("Questa versione ha già fallito l’avvio. Attendo una nuova release");retry.store(86400000);return;}
            xSemaphoreTake(mutex,portMAX_DELAY);if(gen==generation){release=next;view.available=true;view.version=next.version;view.notes=next.notes;view.status="Aggiornamento disponibile: "+next.version;}view.busy=false;xSemaphoreGive(mutex);success=true;
        }else {status(error.empty()?"Partizione OTA non disponibile":"Nessun aggiornamento compatibile più recente");success=true;}
    }
    retry.store(success?86400000:3600000);
}
}
