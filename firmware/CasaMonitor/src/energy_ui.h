#pragma once
#include "energy_model.h"
#include <lvgl.h>

namespace casa {
enum class EnergyAction {None,Mute,TestAudio,ToggleDemo,Scenario,SaveConfig};
struct EnergyRequest {EnergyAction action=EnergyAction::None;int scenario=0;EnergyConfig config;};
class EnergyUi {
public:
    void init(lv_obj_t *home,lv_obj_t *settings);
    void update(const EnergyModel &model,bool demo,int scenario,bool audioAvailable,
                const std::vector<MeterSource> &catalog);
    EnergyRequest takeRequest();
    void error(const std::string &message);
    void closeOverlay();
    lv_obj_t *demoButton=nullptr,*muteButton=nullptr,*testButton=nullptr,*settingsButton=nullptr;
    lv_obj_t *detailInfoButton=nullptr;
    std::array<lv_obj_t *,4> tiles{},powers{};
private:
    lv_obj_t *totalLabel=nullptr,*remaining=nullptr,*alarmLabel=nullptr,*sourceLabel=nullptr,*demoScenario=nullptr;
    lv_obj_t *detail=nullptr,*detailTitle=nullptr,*detailStatus=nullptr,*detailPower=nullptr,*detailEnergy=nullptr;
    lv_obj_t *detailPowerInfo=nullptr,*detailChart=nullptr,*detailGraphTitle=nullptr,*detailGraphStats=nullptr,*detailGraphEmpty=nullptr;
    lv_obj_t *detailInfoDialog=nullptr,*detailInfoText=nullptr,*detailInfoClose=nullptr;
    std::array<lv_obj_t *,3> detailAxis{},detailTime{},detailApplianceCards{},detailApplianceNames{},detailAppliancePowers{},detailApplianceEnergy{};
    lv_chart_series_t *series=nullptr;
    bool detailGraphCached=false;
    int detailGraphTop=0;
    std::vector<float> detailGraphSamples;
    lv_obj_t *configuration=nullptr,*tabs=nullptr,*configError=nullptr,*keyboard=nullptr;
    lv_obj_t *confirmed=nullptr,*audioCheck=nullptr,*save=nullptr,*automationFlag=nullptr;
    std::array<lv_obj_t *,5> fields{};
    std::array<lv_obj_t *,METER_COUNT> mappings{};
    std::array<lv_obj_t *,3> zones{};
    std::array<lv_obj_t *,4> names{},ages{};
    std::vector<MeterSource> choices;
    EnergyConfig cached;
    EnergyRequest request;
    int detailIndex=-1;int selectedScenario=0;
    static void event(lv_event_t *e);
    void handle(lv_event_t *e);
    void openConfig();
    void openDetail(int index);
    void createDetail();
    void keyboardVisible(bool on,lv_obj_t *field=nullptr);
    void populateMappings();
    void submit();
};
}
