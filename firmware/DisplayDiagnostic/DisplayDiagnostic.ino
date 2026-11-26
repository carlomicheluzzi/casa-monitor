// Temporary hardware test. Existing settings are read only when a service is
// explicitly enabled over USB; no configuration is saved and no relay is used.
#include "src/board/lvgl_port/lvgl_port.h"
#include "src/wifi_platform.h"
#include "src/cloud_service.h"
#include "src/alarm_audio.h"
#include <Arduino.h>
#include <WiFi.h>
#include <cJSON.h>
#include <string>
#include <cstring>

esp_lcd_panel_handle_t panel;
lv_obj_t *heading;
lv_obj_t *serviceLabel;
std::string input;
unsigned clockMHz=16;
bool wifiOn=false,cloudOn=false,audioOn=false,refreshOn=false;
casa::EspWifiPlatform wifi;
casa::CloudService cloud;
casa::AlarmAudio audio;

void statusHeading() {
    if(lvgl_port_lock(-1)) {
        lv_label_set_text_fmt(heading,"TEST DISPLAY STATICO - %u MHz",clockMHz);
        lv_label_set_text_fmt(serviceLabel,"Wi-Fi %s / Cloud %s / Audio %s / Refresh %s",
            wifiOn?"ON":"OFF",cloudOn?"ON":"OFF",audioOn?"ON":"OFF",refreshOn?"ON":"OFF");
        lvgl_port_unlock();
    }
}

void box(int x,int y,int w,int h,uint32_t color) {
    auto *o=lv_obj_create(lv_scr_act());lv_obj_remove_style_all(o);
    lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
}
void caption(int x,int y,const char *value) {
    auto *o=lv_label_create(lv_scr_act());lv_obj_set_pos(o,x,y);lv_label_set_text(o,value);
    lv_obj_set_style_text_color(o,lv_color_white(),0);lv_obj_set_style_text_font(o,&casa_font_24,0);
}
void setup() {
    Serial.begin(115200);DEV_I2C_Init();IO_EXTENSION_Init();
    panel=waveshare_esp32_s3_rgb_lcd_init();
    ESP_ERROR_CHECK(lvgl_port_init(panel,nullptr));
    if(lvgl_port_lock(-1)) {
        lv_obj_set_style_bg_color(lv_scr_act(),lv_color_black(),0);
        heading=lv_label_create(lv_scr_act());lv_obj_set_pos(heading,24,18);
        lv_obj_set_style_text_color(heading,lv_color_white(),0);
        lv_obj_set_style_text_font(heading,&casa_font_24,0);
        lv_label_set_text(heading,"TEST DISPLAY STATICO - 16 MHz");
        serviceLabel=lv_label_create(lv_scr_act());lv_obj_set_pos(serviceLabel,24,62);
        lv_obj_set_style_text_color(serviceLabel,lv_color_white(),0);
        lv_obj_set_style_text_font(serviceLabel,&casa_font_16,0);
        lv_label_set_text(serviceLabel,"Wi-Fi OFF / Cloud OFF / Audio OFF / Refresh OFF");
        caption(24,112,"0123456789   ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        caption(24,162,"0123456789   ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        caption(24,212,"0123456789   ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        const uint32_t colors[]={0xffffff,0xffff00,0x00ffff,0x00ff00,0xff00ff,0xff0000,0x0000ff,0x808080};
        for(int i=0;i<8;++i)box(i*100,270,100,105,colors[i]);
        for(int i=0;i<80;++i)box(i*10,390,5,55,0xffffff);
        lvgl_port_unlock();
    }
    Serial.println("DIAG_READY clock_mhz=16 wifi=off audio=off");
}
void loop() {
    while(Serial.available()) {
        char c=char(Serial.read());if(c=='\r')continue;
        if(c!='\n'){if(input.size()<128)input+=c;continue;}
        auto *root=cJSON_Parse(input.c_str());
        auto *value=cJSON_GetObjectItemCaseSensitive(root,"casa_diag_clock");
        auto *service=cJSON_GetObjectItemCaseSensitive(root,"casa_diag_service");
        auto *nonce=cJSON_GetObjectItemCaseSensitive(root,"request_id");
        bool valid=cJSON_IsNumber(value)&&(value->valuedouble==16||value->valuedouble==25)&&cJSON_IsString(nonce)&&strlen(nonce->valuestring)==16;
        if(valid) {
            unsigned next=unsigned(value->valuedouble);
            bool ok=esp_lcd_rgb_panel_set_pclk(panel,next*1000000)==ESP_OK;
            if(ok) {clockMHz=next;statusHeading();}
            Serial.printf("CASA_ACK %s %s\n",nonce->valuestring,ok?"OK":"INVALID_CONFIG");
        } else if(cJSON_IsString(service)&&cJSON_IsString(nonce)&&strlen(nonce->valuestring)==16) {
            std::string action=service->valuestring;
            bool ok=true;
            if(action=="wifi"&&!wifiOn) {
                casa::WifiConfig config;
                ok=wifi.load(config);
                if(ok) {wifi.init();ok=wifi.connect(config);wifiOn=ok;}
            } else if(action=="cloud"&&!cloudOn&&wifiOn) {cloud.begin();cloudOn=true;}
            else if(action=="audio"&&!audioOn) {audio.begin();audioOn=true;}
            else if(action=="refresh") refreshOn=true;
            else if(action=="wifi_off"&&wifiOn) {wifi.disconnect();WiFi.mode(WIFI_OFF);wifiOn=false;}
            else if(action!="reset") ok=false;
            statusHeading();
            Serial.printf("CASA_ACK %s %s\n",nonce->valuestring,ok?"OK":"INVALID_CONFIG");
            if(action=="reset") {Serial.flush();delay(100);ESP.restart();}
        }
        cJSON_Delete(root);input.clear();
    }
    if(cloudOn)cloud.tick(millis(),wifiOn&&wifi.hasIP());
    static uint32_t lastRefresh=0;
    if(refreshOn&&uint32_t(millis()-lastRefresh)>=500&&lvgl_port_lock(-1)) {
        lv_obj_invalidate(lv_scr_act());lvgl_port_unlock();lastRefresh=millis();
    }
    delay(20);
}
