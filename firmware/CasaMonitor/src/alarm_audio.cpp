#include "alarm_audio.h"
#include "board/audio/speaker_microphone.h"
#include <Arduino.h>
#include <cmath>

namespace casa {
namespace {
constexpr int OUTPUT_VOLUME=80;
constexpr int TONE_AMPLITUDE=16000; // Keep headroom within signed 16-bit PCM.
}
void AlarmAudio::begin() {
    xTaskCreatePinnedToCore(taskEntry,"AllarmeAudio",8192,this,2,nullptr,0);
}
void AlarmAudio::taskEntry(void *self) {static_cast<AlarmAudio *>(self)->task();}
void AlarmAudio::task() {
    // Only initialize the output codec. The microphone is not opened or read.
    auto handle=speaker_init();
    if(!handle) {Serial.println("Audio: codec non disponibile");vTaskDelete(nullptr);return;}
    esp_codec_dev_sample_info_t format{};
    format.bits_per_sample=16;format.channel=2;format.sample_rate=44100;
    if(esp_codec_dev_open(handle,&format)!=ESP_OK || esp_codec_dev_set_out_vol(handle,OUTPUT_VOLUME)!=ESP_OK) {
        Serial.println("Audio: inizializzazione non riuscita");vTaskDelete(nullptr);return;
    }
    ready.store(true); Serial.println("Audio: altoparlante pronto");
    int16_t samples[1024]; uint32_t started=0; bool testing=false;
    uint32_t sample=0;
    for(;;) {
        if(testRequested.exchange(false)) {started=millis();testing=true;}
        auto elapsed=uint32_t(millis()-started);
        if(testing&&elapsed>=700) testing=false;
        bool on=(testing&&elapsed%350<170)||(active.load()&&millis()%1000<220);
        if(!on) {vTaskDelay(pdMS_TO_TICKS(20));continue;}
        for(size_t i=0;i<512;++i) {
            auto value=int16_t(TONE_AMPLITUDE*std::sin(6.28318530718*880.0*sample++/44100.0));
            samples[2*i]=samples[2*i+1]=value;
        }
        if(esp_codec_dev_write(handle,samples,sizeof(samples))!=ESP_OK) {
            ready.store(false);Serial.println("Audio: errore di riproduzione");vTaskDelete(nullptr);return;
        }
    }
}
}
