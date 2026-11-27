#include "energy_ui.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cstdlib>

namespace casa {
namespace {
constexpr uint32_t BG=0x0e1216,SURFACE=0x171d23,LINE=0x303a44,TEXT=0xf2f5f7,MUTED=0xa6b2bd;
constexpr uint32_t BLUE=0x69b5ff,GREEN=0x71e1b0,AMBER=0xffcd70,RED=0xff8282;
void text(lv_obj_t *obj,const std::string &value) {if(value!=lv_label_get_text(obj)) lv_label_set_text(obj,value.c_str());}
// LVGL 8 invalidates objects even when the same local style is set again.
// In full-refresh mode that needlessly redraws 800x480 pixels in PSRAM.
void textColor(lv_obj_t *obj,uint32_t color) {
    const auto next=lv_color_hex(color);
    if(lv_obj_get_style_text_color(obj,0).full!=next.full)lv_obj_set_style_text_color(obj,next,0);
}
void textFont(lv_obj_t *obj,const lv_font_t *font) {
    if(lv_obj_get_style_text_font(obj,0)!=font)lv_obj_set_style_text_font(obj,font,0);
}
void show(lv_obj_t *obj,bool on) {if(!obj||lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN)==!on)return;if(on)lv_obj_clear_flag(obj,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(obj,LV_OBJ_FLAG_HIDDEN);}
lv_obj_t *panel(lv_obj_t *parent,int x,int y,int w,int h) {
    auto *o=lv_obj_create(parent);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_color(o,lv_color_hex(BG),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE);return o;
}
lv_obj_t *label(lv_obj_t *p,int x,int y,int w,const char *s,const lv_font_t *font=&casa_font_16,uint32_t color=TEXT) {
    auto *o=lv_label_create(p);lv_obj_set_pos(o,x,y);lv_obj_set_width(o,w);lv_label_set_text(o,s);
    lv_obj_set_style_text_font(o,font,0);lv_obj_set_style_text_color(o,lv_color_hex(color),0);return o;
}
lv_obj_t *button(lv_obj_t *p,int x,int y,int w,const char *s,bool primary=false) {
    auto *o=lv_btn_create(p);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,44);
    lv_obj_set_style_bg_color(o,lv_color_hex(primary?BLUE:SURFACE),0);lv_obj_set_style_radius(o,9,0);
    lv_obj_set_style_border_width(o,1,0);lv_obj_set_style_border_color(o,lv_color_hex(LINE),0);lv_obj_set_style_shadow_width(o,0,0);
    auto *l=label(o,0,0,w-18,s,&casa_font_14,primary?BG:TEXT);
    lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(l);return o;
}
std::string value(double number,const char *unit,int decimals=0) {
    if(!std::isfinite(number))return std::string("— ")+unit;
    char buffer[64];snprintf(buffer,sizeof(buffer),"%.*f %s",decimals,number,unit);return buffer;
}
std::string italianValue(double number,const char *unit,int decimals=0) {
    if(!std::isfinite(number))return std::string("— ")+unit;
    char buffer[96];snprintf(buffer,sizeof(buffer),"%.*f",decimals,number);
    std::string result=buffer;
    auto dot=result.find('.');size_t end=dot==std::string::npos?result.size():dot;
    if(dot!=std::string::npos)result[dot]=',';
    size_t first=result[0]=='-'?1:0;
    for(size_t at=end;at>first+3;){at-=3;result.insert(at,".");}
    return result+" "+unit;
}
double niceCeiling(double v) {
    if(v<=0||!std::isfinite(v))return 100;
    double power=std::pow(10,std::floor(std::log10(v))),part=v/power;
    return (part<=1?1:part<=2?2:part<=5?5:10)*power;
}
std::string timeAgo(size_t seconds) {
    if(!seconds)return "Ora";
    return "~"+std::to_string(seconds<60?seconds:size_t(std::lround(seconds/60.0)))+(seconds<60?" s fa":" min fa");
}
lv_obj_t *input(lv_obj_t *p,int x,int y,const char *caption) {
    label(p,x,y,330,caption,&casa_font_14,MUTED);
    auto *o=lv_textarea_create(p);lv_obj_set_pos(o,x,y+24);lv_obj_set_size(o,330,44);lv_textarea_set_one_line(o,true);
    lv_textarea_set_max_length(o,5);lv_textarea_set_accepted_chars(o,"0123456789");
    lv_obj_set_style_bg_color(o,lv_color_hex(BG),0);lv_obj_set_style_text_color(o,lv_color_hex(TEXT),0);
    lv_obj_set_style_border_color(o,lv_color_hex(LINE),0);return o;
}
void checkboxStyle(lv_obj_t *o) {
    lv_obj_set_style_bg_color(o,lv_color_hex(BG),LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(o,LV_OPA_COVER,LV_PART_INDICATOR);
    lv_obj_set_style_border_color(o,lv_color_hex(MUTED),LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(o,lv_color_hex(BLUE),LV_PART_INDICATOR|LV_STATE_CHECKED);
    lv_obj_set_style_text_color(o,lv_color_hex(BG),LV_PART_INDICATOR|LV_STATE_CHECKED);
}
}
void EnergyUi::init(lv_obj_t *home,lv_obj_t *settings) {
    demoButton=button(home,470,12,150,"Simulazione");
    label(home,16,72,300,"POTENZA TOTALE",&casa_font_12,MUTED);
    totalLabel=label(home,16,93,370,"— kW",&casa_font_32);
    remaining=label(home,400,78,380,"Limite 4,50 kW\nMargine — kW",&casa_font_16,MUTED);
    alarmLabel=label(home,16,139,768,"Collegamento a Shelly Cloud…",&casa_font_16,AMBER);
    for(size_t i=0;i<4;++i) {
        auto *tile=lv_btn_create(home);tiles[i]=tile;
        lv_obj_set_pos(tile,16+(i%2)*392,182+(i/2)*104);lv_obj_set_size(tile,376,96);
        lv_obj_set_style_bg_color(tile,lv_color_hex(SURFACE),0);lv_obj_set_style_shadow_width(tile,0,0);
        lv_obj_set_style_border_width(tile,1,0);lv_obj_set_style_border_color(tile,lv_color_hex(LINE),0);
        lv_obj_set_style_pad_all(tile,12,0);lv_obj_set_style_radius(tile,12,0);
        names[i]=label(tile,0,0,350,"",&casa_font_14,MUTED);
        lv_obj_set_height(names[i],18);lv_label_set_long_mode(names[i],LV_LABEL_LONG_DOT);
        powers[i]=label(tile,0,24,350,"— W",&casa_font_32);
        ages[i]=label(tile,0,64,350,"In attesa",&casa_font_12,MUTED);
        lv_obj_set_height(ages[i],16);lv_label_set_long_mode(ages[i],LV_LABEL_LONG_DOT);
        lv_obj_add_event_cb(tile,event,LV_EVENT_CLICKED,this);
    }
    muteButton=button(home,16,394,230,"Silenzia allarme");
    testButton=button(home,260,394,160,"Prova suono");
    demoScenario=button(home,436,394,348,"Scenario: regolare");
    sourceLabel=label(home,16,443,768,"Shelly Cloud · dati da verificare",&casa_font_12,MUTED);
    lv_obj_set_height(sourceLabel,16);lv_label_set_long_mode(sourceLabel,LV_LABEL_LONG_DOT);
    settingsButton=button(settings,16,158,768,"Monitor, canali e apparecchi   →");
    for(auto *o:{demoButton,muteButton,testButton,demoScenario,settingsButton})lv_obj_add_event_cb(o,event,LV_EVENT_CLICKED,this);

    configuration=panel(lv_scr_act(),0,0,800,480);show(configuration,false);
    label(configuration,16,18,600,"Impostazioni monitor",&casa_font_24);
    auto *cancel=button(configuration,635,12,149,"Annulla");lv_obj_add_event_cb(cancel,event,LV_EVENT_CLICKED,this);
    tabs=lv_tabview_create(configuration,LV_DIR_TOP,44);lv_obj_set_pos(tabs,16,70);lv_obj_set_size(tabs,768,320);
    lv_obj_set_style_bg_color(tabs,lv_color_hex(SURFACE),0);
    auto *thresholds=lv_tabview_add_tab(tabs,"Soglie");
    auto *channels=lv_tabview_add_tab(tabs,"Canali");
    auto *appliances=lv_tabview_add_tab(tabs,"Apparecchi");
    auto *automation=lv_tabview_add_tab(tabs,"Automazione");
    for(auto *page:{thresholds,channels,appliances,automation}) {
        lv_obj_set_style_bg_color(page,lv_color_hex(SURFACE),0);
        lv_obj_set_style_bg_opa(page,LV_OPA_COVER,0);lv_obj_set_style_text_color(page,lv_color_hex(TEXT),0);
    }
    auto *tabButtons=lv_tabview_get_tab_btns(tabs);
    lv_obj_set_style_bg_color(tabButtons,lv_color_hex(SURFACE),0);
    lv_obj_set_style_text_color(tabButtons,lv_color_hex(TEXT),LV_PART_ITEMS);
    lv_obj_set_style_bg_color(tabButtons,lv_color_hex(BLUE),LV_PART_ITEMS|LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(tabButtons,LV_OPA_COVER,LV_PART_ITEMS|LV_STATE_CHECKED);
    lv_obj_set_style_text_color(tabButtons,lv_color_hex(BG),LV_PART_ITEMS|LV_STATE_CHECKED);
    fields[0]=input(thresholds,0,0,"Allarme (W)");fields[1]=input(thresholds,368,0,"Preavviso (W)");
    fields[2]=input(thresholds,0,92,"Margine di rientro (W)");fields[3]=input(thresholds,368,92,"Dati scaduti dopo (s)");
    fields[4]=input(thresholds,0,184,"Silenzia per (s)");
    audioCheck=lv_checkbox_create(thresholds);lv_obj_set_pos(audioCheck,368,214);lv_checkbox_set_text(audioCheck,"Allarme acustico");
    for(auto *o:fields) {lv_obj_add_event_cb(o,event,LV_EVENT_FOCUSED,this);lv_obj_add_event_cb(o,event,LV_EVENT_CLICKED,this);}
    for(size_t i=0;i<METER_COUNT;++i) {
        auto *parent=i<4?channels:appliances;int row=i<4?int(i):int(i-4);
        label(parent,0,row*89,730,cached.sources[i].name.c_str(),&casa_font_14,MUTED);
        mappings[i]=lv_dropdown_create(parent);lv_obj_set_pos(mappings[i],0,row*89+23);lv_obj_set_size(mappings[i],i<4?714:484,44);
        lv_obj_set_style_bg_color(mappings[i],lv_color_hex(BG),0);lv_obj_set_style_text_color(mappings[i],lv_color_hex(TEXT),0);
        if(i>=4) {zones[i-4]=lv_dropdown_create(parent);lv_obj_set_pos(zones[i-4],500,row*89+23);lv_obj_set_size(zones[i-4],214,44);lv_dropdown_set_options(zones[i-4],"Zona 1\nZona 2");}
    }
    confirmed=lv_checkbox_create(channels);lv_obj_set_pos(confirmed,0,360);lv_checkbox_set_text(confirmed,"Associazione delle zone verificata");
    label(appliances,0,278,714,"I dettagli non si sommano al totale. Lavatrice e asciugatrice sullo stesso Shelly hanno una misura aggregata.",&casa_font_14,MUTED);
    automationFlag=lv_checkbox_create(automation);lv_obj_set_pos(automationFlag,0,0);lv_checkbox_set_text(automationFlag,"Distacco automatico carichi");
    for(auto *o:{audioCheck,confirmed,automationFlag})checkboxStyle(o);
    lv_obj_add_state(automationFlag,LV_STATE_DISABLED);
    label(automation,0,55,710,"Disattivato. Da abilitare dopo verifica elettrica dei carichi, associazione dei relè e collaudo locale. Questa versione legge i consumi e segnala gli allarmi.",&casa_font_16,AMBER);
    configError=label(configuration,16,394,530,"",&casa_font_14,RED);
    save=button(configuration,560,412,224,"Salva impostazioni",true);lv_obj_add_event_cb(save,event,LV_EVENT_CLICKED,this);
    keyboard=lv_keyboard_create(configuration);lv_obj_set_align(keyboard,LV_ALIGN_TOP_LEFT);lv_obj_set_pos(keyboard,0,280);lv_obj_set_size(keyboard,800,200);
    lv_keyboard_set_mode(keyboard,LV_KEYBOARD_MODE_NUMBER);show(keyboard,false);
    lv_obj_add_event_cb(keyboard,event,LV_EVENT_READY,this);lv_obj_add_event_cb(keyboard,event,LV_EVENT_CANCEL,this);
}
void EnergyUi::createDetail() {
    detail=panel(lv_scr_act(),0,0,800,480);show(detail,false);
    detailTitle=label(detail,16,18,480,"Dettaglio",&casa_font_24);
    lv_obj_set_height(detailTitle,30);lv_label_set_long_mode(detailTitle,LV_LABEL_LONG_DOT);
    detailInfoButton=button(detail,506,12,117,"Misuratore");lv_obj_add_event_cb(detailInfoButton,event,LV_EVENT_CLICKED,this);
    auto *back=button(detail,635,12,149,"Indietro");lv_obj_add_event_cb(back,event,LV_EVENT_CLICKED,this);
    detailStatus=label(detail,16,57,768,"",&casa_font_14,AMBER);
    auto *powerCard=panel(detail,16,86,376,114),*energyCard=panel(detail,408,86,376,114);
    for(auto *card:{powerCard,energyCard}) {lv_obj_set_style_bg_color(card,lv_color_hex(SURFACE),0);lv_obj_set_style_radius(card,12,0);}
    label(powerCard,14,12,348,"POTENZA ATTUALE",&casa_font_12,MUTED);
    detailPower=label(powerCard,14,36,348,"— W",&casa_font_32);
    detailPowerInfo=label(powerCard,14,86,348,"Tensione — V",&casa_font_12,MUTED);
    label(energyCard,14,12,348,"ENERGIA CUMULATA",&casa_font_12,MUTED);
    detailEnergy=label(energyCard,14,36,348,"— kWh",&casa_font_32);
    lv_obj_set_height(detailEnergy,40);lv_label_set_long_mode(detailEnergy,LV_LABEL_LONG_DOT);
    label(energyCard,14,86,348,"Totale del misuratore · non giornaliero",&casa_font_12,MUTED);
    for(size_t j=0;j<3;++j) {
        auto *card=panel(detail,16+int(j)*260,208,248,88);detailApplianceCards[j]=card;
        lv_obj_set_style_bg_color(card,lv_color_hex(SURFACE),0);lv_obj_set_style_radius(card,10,0);
        detailApplianceNames[j]=label(card,12,9,224,"",&casa_font_14,MUTED);
        lv_obj_set_height(detailApplianceNames[j],18);lv_label_set_long_mode(detailApplianceNames[j],LV_LABEL_LONG_DOT);
        detailAppliancePowers[j]=label(card,12,31,224,"",&casa_font_24);
        detailApplianceEnergy[j]=label(card,12,65,224,"",&casa_font_12,MUTED);
        lv_obj_set_height(detailApplianceEnergy[j],15);lv_label_set_long_mode(detailApplianceEnergy[j],LV_LABEL_LONG_DOT);
        show(card,false);
    }
    detailGraphTitle=label(detail,16,218,300,"Potenza nel tempo",&casa_font_16);
    detailGraphStats=label(detail,328,221,456,"",&casa_font_12,MUTED);
    lv_obj_set_style_text_align(detailGraphStats,LV_TEXT_ALIGN_RIGHT,0);
    detailChart=lv_chart_create(detail);lv_obj_set_pos(detailChart,80,250);lv_obj_set_size(detailChart,688,184);
    lv_chart_set_type(detailChart,LV_CHART_TYPE_LINE);lv_chart_set_point_count(detailChart,300);
    lv_obj_set_style_bg_opa(detailChart,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(detailChart,0,0);
    lv_obj_set_style_pad_all(detailChart,0,0);lv_obj_set_style_radius(detailChart,0,0);
    lv_chart_set_div_line_count(detailChart,3,0);
    lv_obj_set_style_line_color(detailChart,lv_color_hex(LINE),LV_PART_MAIN);
    lv_obj_set_style_line_width(detailChart,1,LV_PART_MAIN);lv_obj_set_style_line_dash_width(detailChart,3,LV_PART_MAIN);
    lv_obj_set_style_line_dash_gap(detailChart,4,LV_PART_MAIN);
    lv_obj_set_style_line_width(detailChart,2,LV_PART_ITEMS);
    lv_obj_set_style_size(detailChart,0,LV_PART_INDICATOR);series=lv_chart_add_series(detailChart,lv_color_hex(BLUE),LV_CHART_AXIS_PRIMARY_Y);
    for(size_t j=0;j<3;++j) {
        detailAxis[j]=label(detail,16,0,56,"",&casa_font_12,MUTED);
        lv_obj_set_style_text_align(detailAxis[j],LV_TEXT_ALIGN_RIGHT,0);
        detailTime[j]=label(detail,80+int(j)*269,442,150,"",&casa_font_12,MUTED);
        lv_obj_set_style_text_align(detailTime[j],j==0?LV_TEXT_ALIGN_LEFT:j==1?LV_TEXT_ALIGN_CENTER:LV_TEXT_ALIGN_RIGHT,0);
    }
    detailGraphEmpty=label(detail,120,315,600,"Raccolta dei primi campioni…",&casa_font_14,MUTED);
    lv_obj_set_style_text_align(detailGraphEmpty,LV_TEXT_ALIGN_CENTER,0);
    label(detail,16,465,768,"Sessione corrente · fino a 15 min · lo storico riparte al riavvio",&casa_font_12,MUTED);
    detailInfoDialog=panel(detail,80,100,640,270);show(detailInfoDialog,false);
    lv_obj_set_style_bg_color(detailInfoDialog,lv_color_hex(SURFACE),0);lv_obj_set_style_radius(detailInfoDialog,12,0);
    lv_obj_set_style_border_width(detailInfoDialog,1,0);lv_obj_set_style_border_color(detailInfoDialog,lv_color_hex(LINE),0);
    label(detailInfoDialog,20,20,460,"Informazioni misuratore",&casa_font_24);
    detailInfoText=label(detailInfoDialog,20,70,600,"",&casa_font_16);
    detailInfoClose=button(detailInfoDialog,448,208,172,"Chiudi");lv_obj_add_event_cb(detailInfoClose,event,LV_EVENT_CLICKED,this);

}
void EnergyUi::event(lv_event_t *e){static_cast<EnergyUi *>(lv_event_get_user_data(e))->handle(e);}
void EnergyUi::handle(lv_event_t *e) {
    auto *o=lv_event_get_target(e);auto code=lv_event_get_code(e);
    if(o==keyboard&&(code==LV_EVENT_READY||code==LV_EVENT_CANCEL)){keyboardVisible(false);return;}
    for(auto *f:fields) if(o==f){keyboardVisible(true,f);return;}
    for(size_t i=0;i<tiles.size();++i)if(o==tiles[i]){openDetail(int(i));return;}
    if(o==demoButton)request.action=EnergyAction::ToggleDemo;
    else if(o==muteButton)request.action=EnergyAction::Mute;
    else if(o==testButton)request.action=EnergyAction::TestAudio;
    else if(o==demoScenario){request.action=EnergyAction::Scenario;request.scenario=(selectedScenario+1)%4;}
    else if(o==settingsButton)openConfig();
    else if(o==detailInfoButton){show(detailInfoDialog,true);return;}
    else if(o==detailInfoClose){show(detailInfoDialog,false);return;}
    else if(o==save)submit();
    else closeOverlay();
}
void EnergyUi::keyboardVisible(bool on,lv_obj_t *field) {
    show(keyboard,on);show(save,!on);show(configError,!on);lv_obj_set_height(tabs,on?204:320);
    if(on){lv_keyboard_set_textarea(keyboard,field);lv_obj_update_layout(tabs);lv_obj_scroll_to_view_recursive(field,LV_ANIM_OFF);}
    else lv_keyboard_set_textarea(keyboard,nullptr);
}
void EnergyUi::closeOverlay(){
    if(detail){lv_obj_del(detail);detail=nullptr;detailInfoDialog=nullptr;detailInfoButton=nullptr;}
    show(configuration,false);keyboardVisible(false);detailIndex=-1;
}
void EnergyUi::openDetail(int i){if(!detail)createDetail();detailGraphCached=false;detailGraphSamples.clear();detailIndex=i;show(detail,true);show(detailInfoDialog,false);show(configuration,false);}
void EnergyUi::populateMappings() {
    for(size_t i=0;i<METER_COUNT;++i) {
        std::string options=i<4?"Scegli canale":"Da associare";int selected=0;
        for(size_t j=0;j<choices.size();++j) {
            options+="\n"+choices[j].name+" · "+choices[j].device.substr(6)+" / "+choices[j].source;
            if(choices[j].device==cached.sources[i].device&&choices[j].source==cached.sources[i].source) selected=int(j)+1;
        }
        lv_dropdown_set_options(mappings[i],options.c_str());lv_dropdown_set_selected(mappings[i],selected);
        if(i>=4)lv_dropdown_set_selected(zones[i-4],cached.sources[i].zone==1?1:0);
    }
}
void EnergyUi::openConfig() {
    const double values[]={cached.limit,cached.warning,cached.hysteresis,double(cached.staleSeconds),double(cached.muteSeconds)};
    for(size_t i=0;i<5;++i){char v[20];snprintf(v,sizeof(v),"%.0f",values[i]);lv_textarea_set_text(fields[i],v);}
    if(cached.mappingConfirmed)lv_obj_add_state(confirmed,LV_STATE_CHECKED);else lv_obj_clear_state(confirmed,LV_STATE_CHECKED);
    if(cached.audioEnabled)lv_obj_add_state(audioCheck,LV_STATE_CHECKED);else lv_obj_clear_state(audioCheck,LV_STATE_CHECKED);
    populateMappings();text(configError,"");lv_tabview_set_act(tabs,0,LV_ANIM_OFF);show(configuration,true);show(detail,false);
}
void EnergyUi::submit() {
    EnergyConfig c=cached;double v[5];
    for(size_t i=0;i<5;++i){const char *s=lv_textarea_get_text(fields[i]);v[i]=*s?std::strtod(s,nullptr):MISSING;}
    c.limit=v[0];c.warning=v[1];c.hysteresis=v[2];
    if(!std::isfinite(v[3])||v[3]<15||v[3]>300||!std::isfinite(v[4])||v[4]<10||v[4]>900){error("Controlla durata dei dati e silenziamento.");return;}
    c.staleSeconds=uint32_t(v[3]);c.muteSeconds=uint32_t(v[4]);
    c.mappingConfirmed=lv_obj_has_state(confirmed,LV_STATE_CHECKED);c.audioEnabled=lv_obj_has_state(audioCheck,LV_STATE_CHECKED);
    for(size_t i=0;i<METER_COUNT;++i){unsigned selected=lv_dropdown_get_selected(mappings[i]);
        if(selected&&selected<=choices.size()){c.sources[i].device=choices[selected-1].device;c.sources[i].source=choices[selected-1].source;}
        else {c.sources[i].device.clear();c.sources[i].source.clear();}
        if(i>=4)c.sources[i].zone=int(lv_dropdown_get_selected(zones[i-4]));
    }
    auto reason=validateEnergyConfig(c);if(!reason.empty()){error(reason);return;}
    request={EnergyAction::SaveConfig,0,c};
}
void EnergyUi::error(const std::string &message){text(configError,message);}
EnergyRequest EnergyUi::takeRequest(){auto r=request;request={};return r;}
void EnergyUi::update(const EnergyModel &m,bool demo,int scenario,bool audioAvailable,const std::vector<MeterSource> &catalog) {
    cached=m.config;choices=catalog;selectedScenario=scenario;
    uint32_t color=m.alarm?RED:!m.complete?AMBER:m.total>=m.config.warning?AMBER:GREEN;
    text(totalLabel,value(m.total/1000,"kW",2));
    text(remaining,"Limite "+value(m.config.limit/1000,"kW",2)+"\nMargine "+value((m.config.limit-m.total)/1000,"kW",2));
    std::string state=m.alarm?"ALLARME · soglia superata":!m.complete?((m.transport=="Shelly Cloud collegato"||m.transport=="Rete locale collegata"||m.transport=="Locale + Shelly Cloud")?"Attendo letture aggiornate":m.transport):m.total>=m.config.warning?"PREAVVISO · vicino alla soglia":"Consumi entro la soglia";
    if(m.alarm&&!m.complete)state+=" · dati non disponibili";
    text(alarmLabel,(demo?"SIMULAZIONE · ":"")+state);textColor(alarmLabel,color);
    for(size_t i=0;i<4;++i){const auto &r=m.readings[i];text(names[i],cached.sources[i].name);
        text(powers[i],value(r.available?r.power:MISSING,"W"));
        textColor(powers[i],r.available?TEXT:MUTED);
        std::string info=r.available?value(r.voltage,"V",0)+" · aggiornato "+std::to_string(r.age)+" s fa · "+(r.local?"LAN":"Cloud"):r.reason;
        if(!r.available&&std::isfinite(r.power))info+=" · ultimo "+value(r.power,"W");
        text(ages[i],info);
    }
    text(lv_obj_get_child(demoButton,0),demo?"Torna ai dati reali":"Simulazione");
    text(lv_obj_get_child(muteButton,0),m.mutedSeconds?"Silenziato "+std::to_string(m.mutedSeconds)+" s":"Silenzia allarme");
    if(m.alarm)lv_obj_clear_state(muteButton,LV_STATE_DISABLED);else lv_obj_add_state(muteButton,LV_STATE_DISABLED);
    if(audioAvailable)lv_obj_clear_state(testButton,LV_STATE_DISABLED);else lv_obj_add_state(testButton,LV_STATE_DISABLED);
    show(demoScenario,demo);
    const char *scenarios[]={"regolare","preavviso","superamento","perdita dati"};
    text(lv_obj_get_child(demoScenario,0),std::string("Scenario: ")+scenarios[std::max(0,std::min(scenario,3))]);
    text(sourceLabel,std::string(demo?"Dati simulati":m.transport)+
        (cached.mappingConfirmed?"":" · verifica zone")+(audioAvailable?"":" · audio non disponibile"));
    if(detailIndex>=0){int i=detailIndex;const auto &r=m.readings[i];
        text(detailTitle,cached.sources[i].name);
        text(detailStatus,(demo?"SIMULAZIONE · ":r.local?"LOCALE · ":"CLOUD · ")+r.reason+(r.available?" · ultimo dato "+std::to_string(r.age)+" s fa":"")+(m.alarm?" · ALLARME CASA":""));
        textColor(detailStatus,m.alarm?RED:r.available?GREEN:AMBER);
        text(detailPower,italianValue(r.available?r.power:MISSING,"W"));
        text(detailEnergy,italianValue(r.available?r.energyWh/1000:MISSING,"kWh",3));
        text(detailPowerInfo,"Tensione "+italianValue(r.available?r.voltage:MISSING,"V"));
        std::string model="Shelly";
        for(const auto &source:catalog)if(source.device==cached.sources[i].device){model=source.name;break;}
        text(detailInfoText,"Modello: "+model+"\nIdentificativo: "+cached.sources[i].device+"\nCanale API: "+cached.sources[i].source+"\n\nIl totale energia è cumulato dal misuratore.");
        size_t shown=0;
        if(i>=2)for(size_t j=4;j<METER_COUNT;++j){const auto &child=m.readings[j];
            if(cached.sources[j].zone!=cached.sources[i].zone&&child.assigned)continue;
            auto at=shown++;show(detailApplianceCards[at],true);
            text(detailApplianceNames[at],cached.sources[j].name);
            text(detailAppliancePowers[at],child.assigned?italianValue(child.available?child.power:MISSING,"W"):"Da associare");
            textFont(detailAppliancePowers[at],child.assigned?&casa_font_24:&casa_font_14);
            text(detailApplianceEnergy[at],child.assigned?"Totale "+italianValue(child.available?child.energyWh/1000:MISSING,"kWh",3):"Configura nelle impostazioni");
        }
        for(size_t j=shown;j<3;++j)show(detailApplianceCards[j],false);
        int graphTop=shown?306:218,plotY=graphTop+32,plotHeight=434-plotY;
        auto samples=m.history(size_t(i));
        // History is sampled every three seconds. NaN gaps compare equal here
        // so a missing reading cannot force a redraw every 500 ms either.
        bool sameSamples=detailGraphCached&&detailGraphTop==graphTop&&samples.size()==detailGraphSamples.size()&&
            std::equal(samples.begin(),samples.end(),detailGraphSamples.begin(),[](float a,float b){return a==b||(std::isnan(a)&&std::isnan(b));});
        if(sameSamples)return;
        detailGraphSamples=samples;detailGraphCached=true;detailGraphTop=graphTop;
        lv_obj_set_y(detailGraphTitle,graphTop);lv_obj_set_y(detailGraphStats,graphTop+3);
        lv_obj_set_y(detailChart,plotY);lv_obj_set_height(detailChart,plotHeight);
        lv_obj_set_y(detailGraphEmpty,plotY+plotHeight/2-8);
        double low=0,high=0,minimum=MISSING,maximum=MISSING;size_t valid=0;
        for(auto v:samples)if(std::isfinite(v)){
            minimum=valid?std::min(minimum,double(v)):double(v);maximum=valid?std::max(maximum,double(v)):double(v);++valid;
            low=std::min(low,double(v));high=std::max(high,double(v));
        }
        double bottom=low<0?-niceCeiling(-low*1.1):0,top=niceCeiling(high*1.1);
        double extent=std::max(-bottom,top),scale=extent>28000?28000/extent:1;
        lv_chart_set_point_count(detailChart,std::max(size_t(2),samples.size()));
        lv_chart_set_range(detailChart,LV_CHART_AXIS_PRIMARY_Y,int32_t(std::lround(bottom*scale)),int32_t(std::lround(top*scale)));
        for(size_t p=0;p<std::max(size_t(2),samples.size());++p)lv_chart_set_value_by_id(detailChart,series,int(p),p<samples.size()&&std::isfinite(samples[p])?lv_coord_t(samples[p]*scale):LV_CHART_POINT_NONE);
        for(size_t j=0;j<3;++j) {
            double bound=top+(bottom-top)*j/2;
            text(detailAxis[j],extent>=1000?italianValue(bound/1000,"kW",std::abs(bound)<10000?1:0):italianValue(bound,"W"));
            lv_obj_set_y(detailAxis[j],plotY+plotHeight*int(j)/2-(j==2?14:7));
        }
        size_t span=samples.empty()?0:(samples.size()-1)*3;
        text(detailTime[0],span?timeAgo(span):"");text(detailTime[1],span>=6?timeAgo(span/2):"");text(detailTime[2],"Ora");
        text(detailGraphStats,valid?"Min "+italianValue(minimum,"W")+"   Max "+italianValue(maximum,"W"):"Dati non ancora disponibili");
        show(detailGraphEmpty,valid<2);
        text(detailGraphEmpty,valid?"Raccolta dei primi campioni…":"Ancora nessun campione valido");
        lv_chart_refresh(detailChart);
    }
}
}
