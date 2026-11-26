#include "fake_wifi.h"
#include "network_ui.h"
#include "device_ui.h"
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <cmath>

#define CHECK(x) do { if(!(x)) throw std::runtime_error("UI check failed, line " + std::to_string(__LINE__) + ": " #x); } while(0)
using namespace casa;
static std::array<unsigned char,800*480*3> pixels{};
static lv_color_t buffer[800*60];
static void flush(lv_disp_drv_t *driver,const lv_area_t *area,lv_color_t *colors) {
    for(int y=area->y1;y<=area->y2;++y) for(int x=area->x1;x<=area->x2;++x) {
        lv_color32_t rgb;
        rgb.full=lv_color_to32(*colors++);
        if(x<0 || x>=800 || y<0 || y>=480) continue;
        auto pos=(y*800+x)*3;
        pixels[pos]=rgb.ch.red; pixels[pos+1]=rgb.ch.green; pixels[pos+2]=rgb.ch.blue;
    }
    lv_disp_flush_ready(driver);
}
static void render(const std::string &path) {
    lv_obj_update_layout(lv_scr_act());
    lv_refr_now(nullptr);
    std::ofstream f(path,std::ios::binary);
    f<<"P6\n800 480\n255\n";
    f.write(reinterpret_cast<const char *>(pixels.data()),pixels.size());
    CHECK(f.good());
}
static void click(lv_obj_t *o) { lv_event_send(o,LV_EVENT_CLICKED,nullptr); }
static void layout() { lv_obj_update_layout(lv_scr_act()); }
static void fitsScreen(lv_obj_t *o) {
    lv_area_t r; lv_obj_get_coords(o,&r);
    if(r.x1<0||r.y1<0||r.x2>=800||r.y2>=480){std::cerr<<"Widget outside: "<<r.x1<<","<<r.y1<<"-"<<r.x2<<","<<r.y2<<" children="<<lv_obj_get_child_cnt(o)<<"\n";}
    CHECK(r.x1>=0); CHECK(r.y1>=0); CHECK(r.x2<800); CHECK(r.y2<480);
}
static bool visible(lv_obj_t *o) {
    for(auto *p=o;p;p=lv_obj_get_parent(p))if(lv_obj_has_flag(p,LV_OBJ_FLAG_HIDDEN))return false;
    return true;
}
static lv_obj_t *findText(lv_obj_t *root,const std::string &needle) {
    if(lv_obj_check_type(root,&lv_label_class)&&visible(root)&&std::string(lv_label_get_text(root)).find(needle)!=std::string::npos)return root;
    for(uint32_t i=0;i<lv_obj_get_child_cnt(root);++i)if(auto *o=findText(lv_obj_get_child(root,i),needle))return o;
    return nullptr;
}

int main(int argc,char **argv) {
    CHECK(argc==2);
    std::string out=argv[1];
    lv_init();
    static lv_disp_draw_buf_t drawBuffer;
    lv_disp_draw_buf_init(&drawBuffer,buffer,nullptr,800*60);
    static lv_disp_drv_t driver;
    lv_disp_drv_init(&driver); driver.hor_res=800; driver.ver_res=480;
    driver.flush_cb=flush; driver.draw_buf=&drawBuffer; lv_disp_drv_register(&driver);
    FakeWifi io; WifiModel model(io); NetworkUi ui;
    model.start(0); ui.init(); ui.update(model);
    CHECK(ui.networkVisible()); CHECK(lv_textarea_get_password_mode(ui.password));
    CHECK(std::string(lv_textarea_get_text(ui.password)).empty());
    layout(); fitsScreen(ui.saveButton); fitsScreen(ui.scanButton);
    render(out+"/wifi-first-boot.ppm");

    click(ui.saveButton); CHECK(ui.takeRequest().action==UiAction::None);
    click(ui.scanButton); CHECK(ui.takeRequest().action==UiAction::Scan);
    CHECK(lv_obj_has_state(ui.scanButton,LV_STATE_DISABLED));
    ui.scanResults({{"Casa",-48,false,true},{"Ufficio",-68,false,true},{"Ospiti",-76,true,true}},"");
    auto *list=lv_obj_get_child(lv_obj_get_parent(ui.scanButton),2);
    click(lv_obj_get_child(list,0));
    CHECK(std::string(lv_textarea_get_text(ui.ssid))=="Casa");
    click(ui.password); CHECK(!lv_obj_has_flag(ui.keyboard,LV_OBJ_FLAG_HIDDEN));
    lv_textarea_set_text(ui.password,"password-di-prova");
    lv_event_send(ui.keyboard,LV_EVENT_READY,nullptr);
    CHECK(lv_obj_has_flag(ui.keyboard,LV_OBJ_FLAG_HIDDEN));
    click(ui.password); CHECK(!lv_obj_has_flag(ui.keyboard,LV_OBJ_FLAG_HIDDEN));
    layout(); fitsScreen(ui.keyboard);
    render(out+"/wifi-keyboard.ppm");
    lv_event_send(ui.keyboard,LV_EVENT_READY,nullptr);
    render(out+"/wifi-config.ppm");

    click(ui.saveButton); auto req=ui.takeRequest(); CHECK(req.action==UiAction::Apply);
    CHECK(model.apply(req.config,req.keepPassword,10).empty()); ui.update(model);
    CHECK(lv_obj_has_state(ui.saveButton,LV_STATE_DISABLED)); CHECK(io.saves==0);
    io.connected=true; model.tick(11); ui.update(model); CHECK(io.saves==1);
    ui.showHome(); click(ui.settingsButton); CHECK(!ui.networkVisible());
    click(ui.networkButton); CHECK(ui.networkVisible());
    CHECK(std::string(lv_textarea_get_text(ui.password)).empty());
    click(ui.saveButton); req=ui.takeRequest(); CHECK(req.keepPassword);
    CHECK(model.apply(req.config,req.keepPassword,12).empty());
    io.connected=true; model.tick(13); ui.update(model);
    render(out+"/wifi-connected.ppm");

    // Static fields and on-screen numeric keyboard stay reachable in the form.
    lv_dropdown_set_selected(ui.addressMode,1);
    lv_event_send(ui.addressMode,LV_EVENT_VALUE_CHANGED,nullptr);
    lv_textarea_set_text(ui.ip,"192.168.1.80"); lv_textarea_set_text(ui.mask,"255.255.255.0");
    lv_textarea_set_text(ui.gateway,"192.168.1.1"); lv_textarea_set_text(ui.dns,"1.1.1.1");
    click(ui.dns); layout();
    lv_area_t coords; lv_obj_get_coords(ui.dns,&coords); CHECK(coords.y1>=164); CHECK(coords.y2<280);
    CHECK(lv_keyboard_get_mode(ui.keyboard)==LV_KEYBOARD_MODE_NUMBER);
    render(out+"/wifi-static-keyboard.ppm");
    lv_event_send(ui.keyboard,LV_EVENT_READY,nullptr);
    click(ui.saveButton); req=ui.takeRequest(); CHECK(req.action==UiAction::Apply); CHECK(!req.config.dhcp);
    CHECK(model.apply(req.config,req.keepPassword,14).empty());
    model.tick(25014); ui.update(model); CHECK(io.target.dhcp); // Rollback to previous DHCP.
    CHECK(std::string(lv_textarea_get_text(ui.ip))=="192.168.1.80"); // Retry did not wipe edits.
    io.connected=true; model.tick(25015); ui.update(model);

    // A long link outage opens configuration, only once, even if the user leaves it.
    ui.showHome(); io.connected=false; model.tick(26000); ui.update(model);
    CHECK(!ui.networkVisible());
    model.tick(51000); ui.update(model); CHECK(ui.networkVisible());
    render(out+"/wifi-offline.ppm");
    ui.showHome(); model.tick(81000); ui.update(model); model.tick(106000); ui.update(model);
    CHECK(!ui.networkVisible());
    ui.showSettings(); click(ui.networkButton); CHECK(ui.networkVisible());
    std::cout<<"PASS LVGL: first boot, scan selection, touch keyboard, retained password, settings, static IP, rollback, outage recovery\n";

    ui.showHome();
    EnergyModel energy;energy.simulate(0,1000);
    std::vector<MeterSource> catalog(energy.config.sources.begin(),energy.config.sources.begin()+4);
    for(auto &source:catalog)source.name="Shelly EM";
    ui.energy.update(energy,true,0,true,catalog);layout();
    for(auto *tile:ui.energy.tiles)fitsScreen(tile);
    fitsScreen(ui.energy.demoButton);fitsScreen(ui.energy.muteButton);fitsScreen(ui.energy.testButton);
    render(out+"/energy-normal.ppm");
    click(ui.energy.tiles[2]);ui.energy.update(energy,true,0,true,catalog);render(out+"/energy-detail.ppm");
    ui.energy.closeOverlay();
    // Readable units, bounded chart labels, hidden technical details and gaps in
    // session data are checked against the rendered LVGL objects.
    EnergyModel graph;RawReadings raw{};
    for(unsigned step=0;step<90;++step) {
        for(size_t i=0;i<4;++i)raw[i]={true,true,i?180.0:34.0+6*std::sin(step/7.0),239,2045849,std::to_string(1000000+step)};
        if(step>=30&&step<36)raw[0].valid=false;
        graph.ingest(raw,step*3000+1000);
    }
    graph.readings[0].power=37;
    click(ui.energy.tiles[0]);ui.energy.update(graph,true,0,true,catalog);layout();
    auto *detailRoot=lv_obj_get_parent(ui.energy.detailInfoButton);
    CHECK(findText(detailRoot,"POTENZA ATTUALE"));CHECK(findText(detailRoot,"ENERGIA CUMULATA"));
    CHECK(findText(detailRoot,"2.045,849 kWh"));CHECK(findText(detailRoot,"37 W"));
    CHECK(findText(detailRoot,"50 W"));CHECK(findText(detailRoot,"~4 min fa"));
    CHECK(!findText(detailRoot,"emeters[1]"));CHECK(!findText(detailRoot,"Da associare"));
    fitsScreen(ui.energy.detailInfoButton);fitsScreen(findText(detailRoot,"50 W"));fitsScreen(findText(detailRoot,"Ora"));
    render(out+"/energy-detail-luce.ppm");
    click(ui.energy.detailInfoButton);CHECK(findText(detailRoot,"emeters[1]"));
    render(out+"/energy-detail-info.ppm");
    click(lv_obj_get_parent(findText(detailRoot,"Chiudi")));CHECK(!findText(detailRoot,"emeters[1]"));
    graph.fail("Wi-Fi non collegato",280000);ui.energy.update(graph,true,0,true,catalog);
    CHECK(findText(detailRoot,"— W"));CHECK(findText(detailRoot,"— kWh"));
    CHECK(findText(detailRoot,"Connessione assente"));render(out+"/energy-detail-offline.ppm");
    ui.energy.closeOverlay();CHECK(ui.energy.detailInfoButton==nullptr);
    energy.simulate(2,4000);ui.energy.update(energy,true,2,true,catalog);CHECK(energy.alarm);
    render(out+"/energy-alarm.ppm");
    click(ui.energy.muteButton);CHECK(ui.energy.takeRequest().action==EnergyAction::Mute);
    energy.mute(4000);energy.simulate(3,7000);ui.energy.update(energy,true,3,true,catalog);
    CHECK(energy.alarm);CHECK(!energy.complete);CHECK(energy.mutedSeconds>0);render(out+"/energy-offline-alarm.ppm");
    click(ui.energy.testButton);CHECK(ui.energy.takeRequest().action==EnergyAction::TestAudio);
    click(ui.energy.demoButton);CHECK(ui.energy.takeRequest().action==EnergyAction::ToggleDemo);
    ui.showSettings();click(ui.energy.settingsButton);layout();render(out+"/energy-settings.ppm");
    ui.energy.closeOverlay();ui.showNetwork();CHECK(ui.networkVisible());
    DeviceUi device;DeviceOptions opt;opt.devices.push_back({"aabbccddeeff","192.168.1.8","admin","retained-secret",4});UpdateView update;
    device.init(ui.settingsPanel());device.update(opt,{{opt.devices[0],"Shelly 1PM Gen4","Locale collegato"}},update);
    ui.showSettings();layout();fitsScreen(device.displayButton);fitsScreen(device.updateButton);render(out+"/device-settings.ppm");
    device.deferOpen=true;click(device.displayButton);CHECK(!device.visible());CHECK(device.takeRequest().action==DeviceAction::OpenDisplay);device.open(1);device.deferOpen=false;CHECK(device.visible());layout();fitsScreen(device.saveButton);render(out+"/display-standby.ppm");
    lv_textarea_set_text(device.timeout,"1");click(device.saveButton);CHECK(device.takeRequest().action==DeviceAction::None);
    lv_textarea_set_text(device.timeout,"60");click(device.saveButton);auto settings=device.takeRequest();CHECK(settings.action==DeviceAction::Save);CHECK(settings.options.screenTimeout==60);device.close();
    click(device.lanButton);layout();render(out+"/device-lan.ppm");CHECK(lv_textarea_get_password_mode(device.password));CHECK(std::string(lv_textarea_get_text(device.password)).empty());
    click(device.host);layout();fitsScreen(device.keyboard);render(out+"/device-lan-keyboard.ppm");lv_event_send(device.keyboard,LV_EVENT_READY,nullptr);
    click(device.saveButton);settings=device.takeRequest();CHECK(settings.options.devices[0].password=="retained-secret");
    lv_textarea_set_text(device.deviceId,"112233445566");click(device.saveButton);settings=device.takeRequest();CHECK(settings.options.devices.back().password.empty());device.close();
    click(device.updateButton);layout();CHECK(lv_obj_has_state(device.installButton,LV_STATE_DISABLED));render(out+"/device-updates.ppm");
    update.available=true;update.version="1.2.0";update.status="Aggiornamento disponibile: 1.2.0";update.notes="Miglioramenti al monitor";device.update(opt,{},update);CHECK(!lv_obj_has_state(device.installButton,LV_STATE_DISABLED));click(device.installButton);CHECK(device.takeRequest().action==DeviceAction::Install);
    update.busy=true;device.update(opt,{},update);CHECK(lv_obj_has_state(device.installButton,LV_STATE_DISABLED));CHECK(lv_obj_has_state(device.saveButton,LV_STATE_DISABLED));device.close();
    std::cout<<"PASS LVGL device: standby validation, local credentials, new-device isolation, keyboard and OTA actions\n";
    std::cout<<"PASS LVGL monitor: four tiles, detail, separate simulation, alarm, mute, sound-test action and monitor settings\n";
}
