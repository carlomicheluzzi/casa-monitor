#include "network_ui.h"
#include <cstdio>
#include <cstring>

namespace casa {
namespace {
constexpr uint32_t BG=0x0e1216, SURFACE=0x171d23, LINE=0x303a44;
constexpr uint32_t TEXT=0xf2f5f7, MUTED=0xa6b2bd, BLUE=0x69b5ff, GREEN=0x71e1b0, AMBER=0xffcd70;
void setText(lv_obj_t *object,const std::string &value) {
    if(std::strcmp(lv_label_get_text(object),value.c_str())!=0) lv_label_set_text(object,value.c_str());
}
lv_obj_t *label(lv_obj_t *p, int x, int y, int w, const char *text, uint32_t color=TEXT,
                const lv_font_t *font=&casa_font_16) {
    auto *o=lv_label_create(p);
    lv_obj_set_pos(o,x,y); lv_obj_set_width(o,w);
    lv_label_set_text(o,text);
    lv_obj_set_style_text_color(o,lv_color_hex(color),0);
    lv_obj_set_style_text_font(o,font,0);
    return o;
}
lv_obj_t *panel(lv_obj_t *p,int x,int y,int w,int h,uint32_t color=BG) {
    auto *o=lv_obj_create(p); lv_obj_remove_style_all(o);
    lv_obj_set_pos(o,x,y); lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_color(o,lv_color_hex(color),0);
    lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE);
    return o;
}
lv_obj_t *button(lv_obj_t *p,int x,int y,int w,const char *text,bool primary=false) {
    auto *o=lv_btn_create(p); lv_obj_set_pos(o,x,y); lv_obj_set_size(o,w,44);
    lv_obj_set_style_bg_color(o,lv_color_hex(primary?BLUE:SURFACE),0);
    lv_obj_set_style_radius(o,9,0); lv_obj_set_style_shadow_width(o,0,0);
    lv_obj_set_style_border_color(o,lv_color_hex(LINE),0); lv_obj_set_style_border_width(o,1,0);
    auto *l=lv_label_create(o); lv_label_set_text(l,text); lv_obj_center(l);
    lv_obj_set_style_text_color(l,lv_color_hex(primary?BG:TEXT),0);
    return o;
}
lv_obj_t *field(lv_obj_t *p,int x,int y,int w,const char *caption,bool numeric=false) {
    label(p,x,y,w,caption,MUTED,&casa_font_14);
    auto *o=lv_textarea_create(p); lv_obj_set_pos(o,x,y+23); lv_obj_set_size(o,w,44);
    lv_textarea_set_one_line(o,true);
    lv_obj_set_style_bg_color(o,lv_color_hex(BG),0);
    lv_obj_set_style_border_color(o,lv_color_hex(LINE),0);
    lv_obj_set_style_text_color(o,lv_color_hex(TEXT),0);
    lv_obj_set_style_radius(o,8,0);
    lv_obj_set_style_pad_all(o,10,0);
    if (numeric) lv_textarea_set_accepted_chars(o,"0123456789.");
    return o;
}
void disabled(lv_obj_t *o,bool value) {
    if(value) lv_obj_add_state(o,LV_STATE_DISABLED); else lv_obj_clear_state(o,LV_STATE_DISABLED);
}
void visible(lv_obj_t *o,bool value) {
    if(value) lv_obj_clear_flag(o,LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(o,LV_OBJ_FLAG_HIDDEN);
}
}

void NetworkUi::init() {
    lv_obj_set_style_bg_color(lv_scr_act(),lv_color_hex(BG),0);
    lv_obj_set_style_text_font(lv_scr_act(),&casa_font_16,0);
    home=panel(lv_scr_act(),0,0,800,480);
    settings=panel(lv_scr_act(),0,0,800,480);
    network=panel(lv_scr_act(),0,0,800,480);
    label(home,16,20,480,"Casa / Energia",TEXT,&casa_font_24);
    settingsButton=button(home,635,12,149,"Impostazioni");
    homeStatus=label(home,16,465,768,"Avvio Wi-Fi…",MUTED,&casa_font_12);

    label(settings,16,20,500,"Impostazioni",TEXT,&casa_font_24);
    auto *back=button(settings,635,12,149,"Torna al monitor");
    lv_obj_add_event_cb(back,event,LV_EVENT_CLICKED,this);
    networkButton=button(settings,16,97,768,"Rete Wi-Fi   →   Cambia rete e indirizzo IP");
    lv_obj_add_event_cb(networkButton,event,LV_EVENT_CLICKED,this);
    label(settings,16,426,730,"Le impostazioni Wi-Fi restano disponibili anche senza Internet.",MUTED);
    energy.init(home,settings);

    label(network,16,20,500,"Rete Wi-Fi",TEXT,&casa_font_24);
    auto *networkBack=button(network,635,12,149,"Indietro");
    lv_obj_add_event_cb(networkBack,event,LV_EVENT_CLICKED,this);
    status=label(network,16,74,768,"",AMBER);
    current=label(network,16,121,768,"",MUTED,&casa_font_14);
    listPanel=panel(network,16,164,310,235,SURFACE);
    lv_obj_set_style_radius(listPanel,12,0);
    label(listPanel,14,17,150,"Reti 2,4 GHz",MUTED);
    scanButton=button(listPanel,178,5,122,"Cerca reti");
    list=panel(listPanel,8,58,294,168,SURFACE);
    lv_obj_add_flag(list,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list,LV_DIR_VER);
    label(list,6,5,280,"Premi Cerca reti, oppure\ninserisci il nome a destra.",MUTED,&casa_font_14);

    form=panel(network,340,164,444,235,SURFACE);
    lv_obj_set_style_radius(form,12,0); lv_obj_set_style_pad_all(form,14,0);
    lv_obj_add_flag(form,LV_OBJ_FLAG_SCROLLABLE); lv_obj_set_scroll_dir(form,LV_DIR_VER);
    ssid=field(form,0,0,410,"Nome rete (anche nascosta)");
    lv_textarea_set_max_length(ssid,32);
    password=field(form,0,82,300,"Password");
    lv_textarea_set_password_mode(password,true); lv_textarea_set_max_length(password,64);
    lv_textarea_set_password_show_time(password,0);
    showPassword=button(form,310,105,100,"Mostra");
    openCheck=lv_checkbox_create(form); lv_obj_set_pos(openCheck,0,165);
    lv_checkbox_set_text(openCheck,"Rete senza password");
    lv_obj_set_style_text_color(openCheck,lv_color_hex(TEXT),0);
    lv_obj_set_style_pad_ver(openCheck,10,0);
    lv_obj_set_style_bg_color(openCheck,lv_color_hex(BG),LV_PART_INDICATOR);
    lv_obj_set_style_border_color(openCheck,lv_color_hex(MUTED),LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(openCheck,lv_color_hex(BLUE),LV_PART_INDICATOR|LV_STATE_CHECKED);
    label(form,0,213,410,"Indirizzo IP",MUTED,&casa_font_14);
    addressMode=lv_dropdown_create(form); lv_obj_set_pos(addressMode,0,236); lv_obj_set_size(addressMode,410,44);
    lv_dropdown_set_options(addressMode,"Automatico (DHCP)\nIP fisso");
    lv_obj_set_style_bg_color(addressMode,lv_color_hex(BG),0);
    lv_obj_set_style_text_color(addressMode,lv_color_hex(TEXT),0);
    lv_obj_set_style_border_color(addressMode,lv_color_hex(LINE),0);
    auto *dropdownList=lv_dropdown_get_list(addressMode);
    lv_obj_set_style_bg_color(dropdownList,lv_color_hex(SURFACE),0);
    lv_obj_set_style_text_color(dropdownList,lv_color_hex(TEXT),0);
    lv_obj_set_style_pad_ver(dropdownList,10,0);
    staticFields=panel(form,0,303,410,190,SURFACE);
    ip=field(staticFields,0,0,195,"Indirizzo IPv4",true);
    mask=field(staticFields,215,0,195,"Maschera",true);
    gateway=field(staticFields,0,91,195,"Gateway",true);
    dns=field(staticFields,215,91,195,"DNS",true);
    for(auto *f:{ip,mask,gateway,dns}) lv_textarea_set_max_length(f,15);

    actions=panel(network,16,415,768,49);
    retryButton=button(actions,0,0,225,"Riprova rete salvata");
    saveButton=button(actions,545,0,223,"Salva e prova",true);
    keyboard=lv_keyboard_create(network);
    lv_obj_set_align(keyboard,LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(keyboard,0,280); lv_obj_set_size(keyboard,800,200);
    lv_obj_set_style_bg_color(keyboard,lv_color_hex(BG),0);
    lv_obj_set_style_pad_all(keyboard,3,0);
    lv_obj_set_style_pad_row(keyboard,4,0); lv_obj_set_style_pad_column(keyboard,5,0);
    lv_obj_set_style_bg_color(keyboard,lv_color_hex(SURFACE),LV_PART_ITEMS);
    lv_obj_set_style_text_color(keyboard,lv_color_hex(TEXT),LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard,lv_color_hex(SURFACE),LV_PART_ITEMS|LV_STATE_USER_1);
    lv_obj_set_style_text_color(keyboard,lv_color_hex(TEXT),LV_PART_ITEMS|LV_STATE_USER_1);
    lv_obj_set_style_bg_color(keyboard,lv_color_hex(BLUE),LV_PART_ITEMS|LV_STATE_PRESSED);
    lv_obj_set_style_text_color(keyboard,lv_color_hex(BG),LV_PART_ITEMS|LV_STATE_PRESSED);
    lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN);
    for(auto *o:{settingsButton,scanButton,saveButton,retryButton,showPassword})
        lv_obj_add_event_cb(o,event,LV_EVENT_CLICKED,this);
    for(auto *o:{ssid,password,ip,mask,gateway,dns}) {
        lv_obj_add_event_cb(o,event,LV_EVENT_FOCUSED,this);
        lv_obj_add_event_cb(o,event,LV_EVENT_CLICKED,this);
        lv_obj_add_event_cb(o,event,LV_EVENT_VALUE_CHANGED,this);
    }
    lv_obj_add_event_cb(keyboard,event,LV_EVENT_READY,this);
    lv_obj_add_event_cb(keyboard,event,LV_EVENT_CANCEL,this);
    lv_obj_add_event_cb(openCheck,event,LV_EVENT_VALUE_CHANGED,this);
    lv_obj_add_event_cb(addressMode,event,LV_EVENT_VALUE_CHANGED,this);
    showHome();
}

void NetworkUi::event(lv_event_t *e) { static_cast<NetworkUi *>(lv_event_get_user_data(e))->handle(e); }

void NetworkUi::handle(lv_event_t *e) {
    auto *target=lv_event_get_target(e);
    auto code=lv_event_get_code(e);
    if(code==LV_EVENT_READY || code==LV_EVENT_CANCEL) { keyboardVisible(false); return; }
    if(code==LV_EVENT_FOCUSED || (code==LV_EVENT_CLICKED &&
        (target==ssid || target==password || target==ip || target==mask || target==gateway || target==dns))) {
        lv_keyboard_set_mode(keyboard,(target==ssid || target==password)?LV_KEYBOARD_MODE_TEXT_LOWER:LV_KEYBOARD_MODE_NUMBER);
        lv_keyboard_set_textarea(keyboard,target); keyboardVisible(true);
        lv_obj_update_layout(form); lv_obj_scroll_to_view_recursive(target,LV_ANIM_OFF);
        return;
    }
    if(code==LV_EVENT_VALUE_CHANGED) {
        if(transientError) { transientError=false; lv_label_set_text(status,lastMessage.c_str()); }
        if(target==addressMode) modeChanged();
        if(target==openCheck) {
            bool open=lv_obj_has_state(openCheck,LV_STATE_CHECKED);
            disabled(password,open); disabled(showPassword,open);
            if(open) { lv_textarea_set_text(password,""); keyboardVisible(false); }
        }
        return;
    }
    if(target==settingsButton) showSettings();
    else if(target==networkButton) showNetwork();
    else if(target==saveButton) submit();
    else if(target==retryButton) { request.action=UiAction::Retry; keyboardVisible(false); }
    else if(target==scanButton) queueScan();
    else if(target==showPassword) {
        bool hidden=lv_textarea_get_password_mode(password);
        lv_textarea_set_password_mode(password,!hidden);
        lv_label_set_text(lv_obj_get_child(showPassword,0),hidden?"Nascondi":"Mostra");
    } else if(lv_obj_get_parent(target)==list) {
        size_t index=lv_obj_get_index(target);
        if(index<found.size()) {
            lv_textarea_set_text(ssid,found[index].ssid.c_str());
            lv_textarea_set_text(password,"");
            if(found[index].open) lv_obj_add_state(openCheck,LV_STATE_CHECKED);
            else lv_obj_clear_state(openCheck,LV_STATE_CHECKED);
            disabled(password,found[index].open); disabled(showPassword,found[index].open);
            keyboardVisible(false);
        }
    } else if(lv_obj_get_parent(target)==network) {
        showSettings();
    } else { showHome(); }
}

void NetworkUi::showHome() {
    keyboardVisible(false); page=0;
    visible(home,true); visible(settings,false); visible(network,false);
    lv_textarea_set_text(password,"");
}
void NetworkUi::showSettings() {
    keyboardVisible(false); page=1;
    visible(home,false); visible(settings,true); visible(network,false);
    lv_textarea_set_text(password,"");
}
void NetworkUi::showNetwork() {
    if(page==2) return; // Never overwrite a form being edited when retrying.
    page=2;
    energy.closeOverlay();
    visible(home,false); visible(settings,false); visible(network,true);
    fill();
}
void NetworkUi::fill() {
    lv_textarea_set_text(ssid,cached.ssid.c_str());
    lv_textarea_set_text(password,""); lv_textarea_set_password_mode(password,true);
    lv_label_set_text(lv_obj_get_child(showPassword,0),"Mostra");
    lv_textarea_set_placeholder_text(password,configured&&!cached.open?"Vuota: mantieni quella salvata":"Password della rete");
    if(cached.open) lv_obj_add_state(openCheck,LV_STATE_CHECKED); else lv_obj_clear_state(openCheck,LV_STATE_CHECKED);
    disabled(password,cached.open); disabled(showPassword,cached.open);
    lv_dropdown_set_selected(addressMode,cached.dhcp?0:1);
    lv_textarea_set_text(ip,cached.ip.c_str()); lv_textarea_set_text(mask,cached.mask.c_str());
    lv_textarea_set_text(gateway,cached.gateway.c_str()); lv_textarea_set_text(dns,cached.dns.c_str());
    modeChanged(); lv_obj_scroll_to_y(form,0,LV_ANIM_OFF);
}
void NetworkUi::modeChanged() { visible(staticFields,lv_dropdown_get_selected(addressMode)==1); }
void NetworkUi::keyboardVisible(bool on) {
    if(!keyboard) return;
    visible(keyboard,on); visible(actions,!on);
    if(form) lv_obj_set_height(form,on?110:235);
    if(listPanel) lv_obj_set_height(listPanel,on?110:235);
    if(list) lv_obj_set_height(list,on?45:168);
    if(!on) lv_keyboard_set_textarea(keyboard,nullptr);
}
void NetworkUi::queueScan() {
    if(busy || scanning) return;
    request.action=UiAction::Scan; scanning=true;
    disabled(scanButton,true); lv_label_set_text(lv_obj_get_child(scanButton,0),"Ricerca…");
}
void NetworkUi::submit() {
    if(busy) return;
    keyboardVisible(false);
    WifiConfig c;
    c.ssid=lv_textarea_get_text(ssid); c.password=lv_textarea_get_text(password);
    c.open=lv_obj_has_state(openCheck,LV_STATE_CHECKED);
    c.dhcp=lv_dropdown_get_selected(addressMode)==0;
    c.ip=lv_textarea_get_text(ip); c.mask=lv_textarea_get_text(mask);
    c.gateway=lv_textarea_get_text(gateway); c.dns=lv_textarea_get_text(dns);
    bool keep=c.password.empty() && configured && c.ssid==cached.ssid && !c.open && !cached.open;
    // The model resolves a retained password; the UI never receives that password.
    if(!keep) { auto reason=validate(c); if(!reason.empty()) { error(reason); return; } }
    request={UiAction::Apply,c,keep};
}
UiRequest NetworkUi::takeRequest() { auto result=request; request={}; return result; }
void NetworkUi::error(const std::string &message) {
    transientError=true;
    lv_label_set_text(status,message.c_str());
    lv_obj_set_style_text_color(status,lv_color_hex(AMBER),0);
}
void NetworkUi::scanResults(const std::vector<ScanNetwork> &networks,const std::string &reason) {
    scanning=false; found=networks;
    disabled(scanButton,busy); lv_label_set_text(lv_obj_get_child(scanButton,0),"Cerca reti");
    lv_obj_clean(list);
    if(found.empty()) label(list,6,5,280,reason.empty()?"Nessuna rete visibile.\nPuoi inserire il nome a destra.":reason.c_str(),MUTED,&casa_font_14);
    for(size_t i=0;i<found.size();++i) {
        auto &n=found[i];
        auto *b=button(list,0,int(i)*58,278,""); lv_obj_set_height(b,54);
        lv_obj_clean(b);
        auto *name=label(b,0,0,245,n.ssid.c_str()); lv_label_set_long_mode(name,LV_LABEL_LONG_DOT);
        std::string detail=std::to_string(n.rssi)+" dBm · "+(n.open?"Aperta":n.supported?"Protetta":"Non supportata");
        label(b,0,25,245,detail.c_str(),MUTED,&casa_font_12);
        disabled(b,!n.supported);
        lv_obj_add_event_cb(b,event,LV_EVENT_CLICKED,this);
    }
}
void NetworkUi::update(WifiModel &m) {
    cached={m.saved.ssid,"",m.saved.open,m.saved.dhcp,m.saved.ip,m.saved.mask,m.saved.gateway,m.saved.dns};
    configured=m.configured;
    bool connecting=m.state==Connection::Connecting;
    busy=connecting && m.trial;
    if(connecting && scanning) scanResults({},"Ricerca sospesa durante il collegamento.");
    disabled(saveButton,busy); disabled(scanButton,connecting||scanning); disabled(retryButton,connecting||!configured);
    setText(lv_obj_get_child(saveButton,0),busy?"Collegamento…":"Salva e prova");
    if(m.message!=lastMessage) {
        transientError=false;
        lastMessage=m.message;
        lv_label_set_text(status,m.message.c_str());
        lv_obj_set_style_text_color(status,lv_color_hex(m.state==Connection::Online?GREEN:AMBER),0);
    }
    std::string summary;
    if(m.state==Connection::Online) summary="Collegata: "+m.attempted.ssid+"  ·  IP "+m.ip()+"  ·  "+(m.attempted.dhcp?"DHCP":"IP fisso");
    else summary=configured?"Salvata: "+cached.ssid+"  ·  "+(cached.dhcp?"DHCP":cached.ip)+"  ·  Nessun collegamento attivo":"Nessuna rete salvata  ·  Indirizzo automatico (DHCP)";
    setText(current,summary);
    setText(homeStatus,summary);
    if(m.openRequest!=seenOpen) { seenOpen=m.openRequest; showNetwork(); }
}
}
