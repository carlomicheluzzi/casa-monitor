#include "src/board/lvgl_port/lvgl_port.h"
#include "src/network_ui.h"
#include "src/cloud_service.h"
#include "src/alarm_audio.h"
#include "src/device_ui.h"
#include "src/device_platform.h"
#include "src/local_service.h"
#include "src/update_service.h"
#include "src/version.h"
#include "src/tls_memory.h"

casa::EspWifiPlatform wifi;
casa::WifiModel connection(wifi);
casa::NetworkUi screen;
casa::CloudService cloud;
casa::AlarmAudio audio;
casa::EnergyModel simulation;
casa::DeviceOptions options;
casa::DeviceUi deviceScreen;
casa::LocalService local;
casa::UpdateService updates;
bool demo=false;
int scenario=0;

void setup() {
    Serial.begin(115200);
    Serial.println(casa::FIRMWARE_MARKER);
    Serial.printf("CasaMonitor %s / Waveshare ESP32-S3-Touch-LCD-4.3C\n",casa::FIRMWARE_VERSION);
    // Board initialization follows Waveshare's 12_lvgl_transplant example.
    DEV_I2C_Init();
    IO_EXTENSION_Init();
    Serial.println("I2C e IO expander pronti");
    auto touch=touch_gt911_init(DEV_I2C_Get_Bus_Device());
    Serial.println("Touch GT911 pronto");
    auto display=waveshare_esp32_s3_rgb_lcd_init();
    ESP_ERROR_CHECK(lvgl_port_init(display,touch));
    Serial.println("LCD e LVGL pronti");
    casa::initTlsMemory();
    wifi.init();
    connection.start(millis());
    cloud.begin();
    casa::loadDeviceOptions(options);
    casa::configureDisplay(options);
    local.begin();local.configure(options,cloud.live.config);
    updates.begin(cloud);updates.configure(options);cloud.attachUpdates(options,updates);
    if(lvgl_port_lock(-1)) {
        screen.init();
        deviceScreen.init(screen.settingsPanel());deviceScreen.deferOpen=true;
        screen.update(connection);
        lvgl_port_unlock();
    }
    waveshare_rgb_lcd_bl_on();
    audio.begin();
    Serial.println("Interfaccia pronta; configurazione Wi-Fi disponibile sul display");
}

void loop() {
    // Keep asynchronous results until the display task can accept them.
    static bool pendingScan=false;
    static std::vector<casa::ScanNetwork> found;
    static std::string scanError, pendingError;
    casa::UiRequest command;
    casa::EnergyRequest energyCommand;
    casa::DeviceRequest deviceCommand;
    if(lvgl_port_lock(50)) { command=screen.takeRequest(); energyCommand=screen.energy.takeRequest(); deviceCommand=deviceScreen.takeRequest(); lvgl_port_unlock(); }
    if(command.action==casa::UiAction::Apply) {
        pendingError=connection.apply(command.config,command.keepPassword,millis());
    } else if(command.action==casa::UiAction::Retry) {
        connection.retry(millis());
    } else if(command.action==casa::UiAction::Scan) {
        wifi.scan();
    }
    connection.tick(millis());
    cloud.localStackFree=local.freeStack();cloud.serialInput();
    static uint32_t localRevision=0;
    if(localRevision!=cloud.revision()){localRevision=cloud.revision();local.configure(options,cloud.live.config);}
    static int pendingDevicePage=0;
    std::string deviceResult;
    if(deviceCommand.action==casa::DeviceAction::OpenDisplay||deviceCommand.action==casa::DeviceAction::OpenLocal||deviceCommand.action==casa::DeviceAction::OpenUpdates){
        pendingDevicePage=deviceCommand.action==casa::DeviceAction::OpenDisplay?1:deviceCommand.action==casa::DeviceAction::OpenLocal?2:3;cloud.pause(true);
    }else if(deviceCommand.action==casa::DeviceAction::Save){
        if(casa::saveDeviceOptions(deviceCommand.options)){
            options=deviceCommand.options;casa::configureDisplay(options);local.configure(options,cloud.live.config);updates.configure(options);deviceResult="Impostazioni salvate";
        }else deviceResult="Salvataggio non riuscito";
    }else if(deviceCommand.action==casa::DeviceAction::Discover)local.discover();
    else if(deviceCommand.action==casa::DeviceAction::Check)updates.check();
    else if(deviceCommand.action==casa::DeviceAction::Install)updates.install();
    casa::RawReadings localReadings;std::vector<casa::LocalDeviceView> localDevices;std::vector<casa::MeterSource> localCatalog;
    local.snapshot(localReadings,localDevices,localCatalog);cloud.mergeCatalog(localCatalog);
    cloud.tick(millis(),connection.state==casa::Connection::Online,&localReadings,&options);
    static uint32_t lastDemo=0,lastDisplay=0;
    if(energyCommand.action==casa::EnergyAction::ToggleDemo) {demo=!demo;lastDemo=0;}
    if(energyCommand.action==casa::EnergyAction::Scenario) {scenario=energyCommand.scenario;lastDemo=0;}
    if(energyCommand.action==casa::EnergyAction::Mute) (demo?simulation:cloud.live).mute(millis());
    if(energyCommand.action==casa::EnergyAction::TestAudio) audio.test();
    bool saveFailed=false;
    if(energyCommand.action==casa::EnergyAction::SaveConfig) saveFailed=!cloud.saveConfig(energyCommand.config);
    if(demo&&(!lastDemo||uint32_t(millis()-lastDemo)>=3000)) {
        simulation.configure(cloud.live.config);simulation.simulate(scenario,millis());lastDemo=millis();
    }
    simulation.tick(millis());
    audio.setAlarm((demo?simulation:cloud.live).audible);
    bool safe=!demo&&cloud.live.complete&&!cloud.live.alarm;
    updates.tick(safe,safe&&cloud.live.total<cloud.live.config.warning);
    auto updateView=updates.snapshot();
    static int previousState=-1;
    static uint32_t lastDiagnostic=0;
    int state=static_cast<int>(connection.state);
    if(state!=previousState || uint32_t(millis()-lastDiagnostic)>=10000) {
        const char *names[]={"da configurare","collegamento in corso","collegato","non raggiungibile"};
        Serial.printf("Wi-Fi: %s\n",names[state]);
        previousState=state;
        lastDiagnostic=millis();
    }
    if(!pendingScan) {
        scanError.clear();
        pendingScan=wifi.scanFinished(found,scanError);
    }
    if(lvgl_port_lock(50)) {
        screen.update(connection);
        if(screen.networkVisible()){if(deviceScreen.visible())deviceScreen.close();pendingDevicePage=0;}
        if(pendingDevicePage&&!updateView.busy&&cloud.paused()){deviceScreen.update(options,localDevices,updateView);deviceScreen.open(pendingDevicePage);pendingDevicePage=0;}
        cloud.pause(deviceScreen.visible()||pendingDevicePage!=0);
        if(uint32_t(millis()-lastDisplay)>=500||energyCommand.action!=casa::EnergyAction::None) {
            screen.energy.update(demo?simulation:cloud.live,demo,scenario,audio.available(),cloud.catalog);
            deviceScreen.update(options,localDevices,updateView);
            lastDisplay=millis();
        }
        if(energyCommand.action==casa::EnergyAction::SaveConfig) {
            if(saveFailed) screen.energy.error("Salvataggio non riuscito. Riprova.");
            else screen.energy.closeOverlay();
        }
        if(!deviceResult.empty())deviceScreen.result(deviceResult);
        casa::tickDisplay((demo?simulation:cloud.live).alarm,!cloud.live.complete,!screen.homeVisible()||deviceScreen.visible()||pendingDevicePage!=0||updateView.busy,updates.maintenance());
        if(pendingScan) { screen.scanResults(found,scanError); pendingScan=false; }
        if(!pendingError.empty()) { screen.error(pendingError); pendingError.clear(); }
        lvgl_port_unlock();
    }
    delay(20);
}
