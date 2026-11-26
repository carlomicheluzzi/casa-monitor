#include "device_ui.h"
#include "energy_ui.h"
#include "version.h"
#include <algorithm>
#include <cstring>
#include <cstdlib>
namespace casa {
namespace {
constexpr unsigned BG=0x0e1216,SURFACE=0x171d23,LINE=0x303a44,TEXT=0xf2f5f7,BLUE=0x69b5ff;
lv_obj_t *label(lv_obj_t *p,int x,int y,int w,const char *s,const lv_font_t *font=&casa_font_14){auto *o=lv_label_create(p);lv_obj_set_pos(o,x,y);lv_obj_set_width(o,w);lv_label_set_text(o,s);lv_obj_set_style_text_color(o,lv_color_hex(TEXT),0);lv_obj_set_style_text_font(o,font,0);return o;}
lv_obj_t *panel(lv_obj_t *p,int x,int y,int w,int h){auto *o=lv_obj_create(p);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_obj_set_style_bg_color(o,lv_color_hex(BG),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE);return o;}
lv_obj_t *button(lv_obj_t *p,int x,int y,int w,const char *s){auto *o=lv_btn_create(p);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,44);lv_obj_set_style_bg_color(o,lv_color_hex(SURFACE),0);lv_obj_set_style_border_color(o,lv_color_hex(LINE),0);lv_obj_set_style_border_width(o,1,0);lv_obj_set_style_radius(o,9,0);lv_obj_set_style_shadow_width(o,0,0);auto *l=lv_label_create(o);lv_label_set_text(l,s);lv_obj_center(l);return o;}
lv_obj_t *field(lv_obj_t *p,int x,int y,int w,const char *caption,unsigned max){label(p,x,y,w,caption);auto *o=lv_textarea_create(p);lv_obj_set_pos(o,x,y+23);lv_obj_set_size(o,w,44);lv_textarea_set_one_line(o,true);lv_textarea_set_max_length(o,max);lv_obj_set_style_bg_color(o,lv_color_hex(SURFACE),0);lv_obj_set_style_text_color(o,lv_color_hex(TEXT),0);lv_obj_set_style_border_color(o,lv_color_hex(LINE),0);return o;}
lv_obj_t *check(lv_obj_t *p,int x,int y,const char *s,bool on){auto *o=lv_checkbox_create(p);lv_obj_set_pos(o,x,y);lv_checkbox_set_text(o,s);lv_obj_set_style_text_color(o,lv_color_hex(TEXT),0);lv_obj_set_style_pad_ver(o,9,0);lv_obj_set_style_bg_color(o,lv_color_hex(SURFACE),LV_PART_INDICATOR);lv_obj_set_style_bg_color(o,lv_color_hex(BLUE),LV_PART_INDICATOR|LV_STATE_CHECKED);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,LV_PART_INDICATOR|LV_STATE_CHECKED);lv_obj_set_style_text_color(o,lv_color_hex(BG),LV_PART_INDICATOR|LV_STATE_CHECKED);lv_obj_set_style_text_font(o,LV_FONT_DEFAULT,LV_PART_INDICATOR);if(on)lv_obj_add_state(o,LV_STATE_CHECKED);return o;}
void hidden(lv_obj_t *o,bool h){if(h)lv_obj_add_flag(o,LV_OBJ_FLAG_HIDDEN);else lv_obj_clear_flag(o,LV_OBJ_FLAG_HIDDEN);}
void text(lv_obj_t *o,const std::string &s){if(std::strcmp(lv_label_get_text(o),s.c_str()))lv_label_set_text(o,s.c_str());}
bool checked(lv_obj_t *o){return lv_obj_has_state(o,LV_STATE_CHECKED);}
void disabled(lv_obj_t *o,bool d){if(d)lv_obj_add_state(o,LV_STATE_DISABLED);else lv_obj_clear_state(o,LV_STATE_DISABLED);}
}
void DeviceUi::init(lv_obj_t *settings){displayButton=button(settings,16,220,768,"Display   →   Standby e luminosità");lanButton=button(settings,16,282,768,"Dispositivi locali   →   Rete Shelly e cloud di riserva");updateButton=button(settings,16,344,768,"Aggiornamenti   →   Versione e repository");for(auto *o:{displayButton,lanButton,updateButton})lv_obj_add_event_cb(o,event,LV_EVENT_CLICKED,this);}
void DeviceUi::close(){if(overlay){lv_obj_del(overlay);overlay=nullptr;}keyboard=nullptr;password=nullptr;checkButton=nullptr;installButton=nullptr;discoverButton=nullptr;saveButton=nullptr;deviceList=nullptr;endpoints.clear();listText.clear();transient=false;}
void DeviceUi::event(lv_event_t *e){static_cast<DeviceUi *>(lv_event_get_user_data(e))->handle(e);}
void DeviceUi::open(int which){close();page=which;overlay=panel(lv_scr_act(),0,0,800,480);
    label(overlay,16,20,580,which==1?"Display e standby":which==2?"Shelly nella rete di casa":"Aggiornamenti",&casa_font_24);
    auto *back=button(overlay,635,12,149,"Indietro");lv_obj_add_event_cb(back,event,LV_EVENT_CLICKED,this);
    status=label(overlay,16,69,768,"",&casa_font_14);lv_obj_set_height(status,38);
    form=panel(overlay,16,112,768,286);lv_obj_add_flag(form,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(form,LV_DIR_VER);
    actions=panel(overlay,16,416,768,48);saveButton=button(actions,545,0,223,"Salva impostazioni");lv_obj_set_style_bg_color(saveButton,lv_color_hex(BLUE),0);lv_obj_set_style_text_color(saveButton,lv_color_hex(BG),0);lv_obj_add_event_cb(saveButton,event,LV_EVENT_CLICKED,this);
    if(which==1){
        timeout=field(form,0,0,365,"Standby dopo (secondi, 0 = mai)",4);lv_textarea_set_accepted_chars(timeout,"0123456789");lv_textarea_set_text(timeout,std::to_string(cached.screenTimeout).c_str());
        brightness=field(form,395,0,365,"Luminosità (5–100%)",3);lv_textarea_set_accepted_chars(brightness,"0123456789");lv_textarea_set_text(brightness,std::to_string(cached.brightness).c_str());
        wakeCheck=check(form,0,91,"Riaccendi anche se mancano i dati per oltre 10 s",cached.wakeMissing);
        label(form,0,156,735,"Il monitoraggio e l’allarme restano attivi a schermo spento.\nUn tap riaccende il display senza azionare i pulsanti.\nIl superamento della soglia riaccende sempre lo schermo.");
        for(auto *o:{timeout,brightness}){lv_obj_add_event_cb(o,event,LV_EVENT_FOCUSED,this);lv_obj_add_event_cb(o,event,LV_EVENT_CLICKED,this);}
    }else if(which==2){
        result("Stessa rete del display · scorri per impostare le credenziali locali");
        localCheck=check(form,0,0,"Usa le API locali",cached.localEnabled);fallbackCheck=check(form,360,0,"Cloud come riserva",cached.cloudFallback);
        label(form,0,51,735,"Scoperta automatica; se necessario inserisci IP o nome .local.\nLe associazioni alle tile si modificano in Monitor, canali e apparecchi.");
        deviceList=lv_dropdown_create(form);lv_obj_set_pos(deviceList,0,102);lv_obj_set_size(deviceList,760,44);refreshList(true);
        lv_obj_set_style_bg_color(deviceList,lv_color_hex(SURFACE),0);lv_obj_set_style_text_color(deviceList,lv_color_hex(TEXT),0);auto *popup=lv_dropdown_get_list(deviceList);lv_obj_set_style_bg_color(popup,lv_color_hex(SURFACE),0);lv_obj_set_style_text_color(popup,lv_color_hex(TEXT),0);
        lv_obj_add_event_cb(deviceList,event,LV_EVENT_VALUE_CHANGED,this);
        deviceId=field(form,0,165,365,"ID / MAC Shelly (12 cifre esadecimali)",40);host=field(form,395,165,365,"Host (vuoto = scoperta automatica)",64);
        username=field(form,0,248,365,"Utente locale (normalmente admin)",32);password=field(form,395,248,365,"Password locale (vuota = mantieni)",64);lv_textarea_set_password_mode(password,true);lv_textarea_set_password_show_time(password,0);
        clearPassword=check(form,0,333,"Rimuovi la password locale salvata",false);
        label(form,0,390,735,"Le credenziali sono quelle del dispositivo Shelly, se protetto.\nNon sono email e password del cloud. Nessun comando ai relè.");
        for(auto *o:{deviceId,host,username,password}){lv_obj_add_event_cb(o,event,LV_EVENT_FOCUSED,this);lv_obj_add_event_cb(o,event,LV_EVENT_CLICKED,this);}fillEndpoint();
        discoverButton=button(actions,0,0,220,"Cerca dispositivi");lv_obj_add_event_cb(discoverButton,event,LV_EVENT_CLICKED,this);
    }else {
        label(form,0,0,735,(std::string("Versione installata: ")+FIRMWARE_VERSION).c_str(),&casa_font_16);
        url=field(form,0,42,760,"Indirizzo HTTPS del manifest firmato",512);lv_textarea_set_text(url,cached.manifestUrl.c_str());lv_obj_add_event_cb(url,event,LV_EVENT_FOCUSED,this);lv_obj_add_event_cb(url,event,LV_EVENT_CLICKED,this);
        dailyCheck=check(form,0,129,"Verifica una volta al giorno",cached.dailyCheck);autoCheck=check(form,0,181,"Installa automaticamente quando i consumi sono stabili",cached.autoInstall);
        label(form,0,238,735,"L’installazione riavvia il monitor e interrompe brevemente la sorveglianza.\nFirmware firmato; ritorno alla versione precedente se l’avvio fallisce.");
        progress=label(form,0,305,735,"",&casa_font_14);
        checkButton=button(actions,0,0,220,"Verifica adesso");installButton=button(actions,230,0,290,"Installa e riavvia");for(auto *o:{checkButton,installButton})lv_obj_add_event_cb(o,event,LV_EVENT_CLICKED,this);
    }
    keyboard=lv_keyboard_create(overlay);lv_obj_set_align(keyboard,LV_ALIGN_TOP_LEFT);lv_obj_set_pos(keyboard,0,280);lv_obj_set_size(keyboard,800,200);lv_obj_set_style_bg_color(keyboard,lv_color_hex(BG),0);lv_obj_set_style_bg_color(keyboard,lv_color_hex(SURFACE),LV_PART_ITEMS);lv_obj_set_style_text_color(keyboard,lv_color_hex(TEXT),LV_PART_ITEMS);lv_obj_set_style_pad_row(keyboard,4,0);lv_obj_set_style_pad_column(keyboard,5,0);hidden(keyboard,true);lv_obj_add_event_cb(keyboard,event,LV_EVENT_READY,this);lv_obj_add_event_cb(keyboard,event,LV_EVENT_CANCEL,this);
    update(cached,devices,updater);
}
void DeviceUi::refreshList(bool initial){
    if(!deviceList)return;auto index=lv_dropdown_get_selected(deviceList);std::string selected=index<endpoints.size()?endpoints[index].id:"";
    auto next=cached.devices;for(const auto &v:devices){bool found=false;for(const auto &d:next)if(d.id==v.endpoint.id)found=true;if(!found&&!v.endpoint.id.empty()&&next.size()<7)next.push_back(v.endpoint);}
    std::string list;for(const auto &d:next){if(!list.empty())list+='\n';list+=d.id;for(const auto &v:devices)if(v.endpoint.id==d.id)list+=" · "+v.model+" · "+v.status;}
    if(!list.empty())list+='\n';list+="Aggiungi dispositivo";
    if(list==listText)return;listText=list;endpoints=std::move(next);lv_dropdown_set_options(deviceList,list.c_str());
    unsigned chosen=initial?0:endpoints.size();for(unsigned i=0;i<endpoints.size();++i)if(endpoints[i].id==selected)chosen=i;lv_dropdown_set_selected(deviceList,chosen);
}
void DeviceUi::fillEndpoint(){auto index=lv_dropdown_get_selected(deviceList);LanDevice d;if(index<endpoints.size())d=endpoints[index];lv_textarea_set_text(deviceId,d.id.c_str());lv_textarea_set_text(host,d.host.c_str());lv_textarea_set_text(username,d.user.c_str());lv_textarea_set_text(password,"");lv_obj_clear_state(clearPassword,LV_STATE_CHECKED);}
void DeviceUi::keyboardVisible(bool on){hidden(keyboard,!on);hidden(actions,on);lv_obj_set_height(form,on?155:286);if(!on)lv_keyboard_set_textarea(keyboard,nullptr);}
void DeviceUi::handle(lv_event_t *e){auto *o=lv_event_get_target(e);auto code=lv_event_get_code(e);
    if(code==LV_EVENT_READY||code==LV_EVENT_CANCEL){keyboardVisible(false);return;}
    if(code==LV_EVENT_FOCUSED||(code==LV_EVENT_CLICKED&&lv_obj_check_type(o,&lv_textarea_class))){transient=false;lv_keyboard_set_mode(keyboard,page==1?LV_KEYBOARD_MODE_NUMBER:LV_KEYBOARD_MODE_TEXT_LOWER);lv_keyboard_set_textarea(keyboard,o);keyboardVisible(true);lv_obj_update_layout(form);lv_obj_scroll_to_view_recursive(o,LV_ANIM_OFF);return;}
    if(code==LV_EVENT_VALUE_CHANGED){if(o==deviceList){keyboardVisible(false);fillEndpoint();}return;}
    if(o==displayButton){if(deferOpen)request.action=DeviceAction::OpenDisplay;else open(1);}
    else if(o==lanButton){if(deferOpen)request.action=DeviceAction::OpenLocal;else open(2);}
    else if(o==updateButton){if(deferOpen)request.action=DeviceAction::OpenUpdates;else open(3);}
    else if(o==saveButton)submit();else if(o==discoverButton){request.action=DeviceAction::Discover;result("Ricerca avviata. L’elenco si aggiorna automaticamente.");}
    else if(o==checkButton){transient=false;request.action=DeviceAction::Check;}else if(o==installButton){transient=false;request.action=DeviceAction::Install;}else close();
}
void DeviceUi::submit(){keyboardVisible(false);DeviceOptions next=cached;
    if(page==1){auto a=std::string(lv_textarea_get_text(timeout)),b=std::string(lv_textarea_get_text(brightness));if(a.empty()||b.empty()){result("Inserisci standby e luminosità.");return;}next.screenTimeout=std::strtoul(a.c_str(),nullptr,10);next.brightness=std::strtoul(b.c_str(),nullptr,10);next.wakeMissing=checked(wakeCheck);}
    else if(page==2){next.localEnabled=checked(localCheck);next.cloudFallback=checked(fallbackCheck);auto id=normalizedDeviceId(lv_textarea_get_text(deviceId));std::string entered=lv_textarea_get_text(deviceId);if(!entered.empty()&&id.empty()){result("ID Shelly non valido: servono 12 cifre esadecimali.");return;}
        if(!id.empty()){LanDevice d;d.id=id;d.host=lv_textarea_get_text(host);d.user=lv_textarea_get_text(username);d.password=lv_textarea_get_text(password);for(const auto &old:cached.devices)if(old.id==id){d.generation=old.generation;if(d.password.empty()&&!checked(clearPassword))d.password=old.password;}
            auto it=std::find_if(next.devices.begin(),next.devices.end(),[&](const LanDevice &v){return v.id==id;});if(it==next.devices.end())next.devices.push_back(d);else *it=d;
        }
    }else {next.manifestUrl=lv_textarea_get_text(url);next.dailyCheck=checked(dailyCheck);next.autoInstall=checked(autoCheck);}
    auto error=validateOptions(next);if(!error.empty()){result(error);return;}request={DeviceAction::Save,next};
}
void DeviceUi::result(const std::string &message){if(overlay){transient=true;text(status,message);}}
void DeviceUi::update(const DeviceOptions &options,const std::vector<LocalDeviceView> &v,const UpdateView &u){if(updater.status!=u.status)transient=false;cached=options;devices=v;updater=u;if(overlay&&page==2)refreshList();if(overlay&&page==3){if(!transient)text(status,u.status);text(progress,u.available?"Disponibile: "+u.version+" · "+u.notes:u.busy?"Operazione in corso…":"Il controllo richiede Internet; le letture locali continuano.");disabled(checkButton,u.busy);disabled(installButton,u.busy||!u.available);disabled(saveButton,u.busy);}}
}
