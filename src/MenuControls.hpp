#pragma once
#include "Settings.hpp"
#include <pl/ModMenu.hpp>

namespace aim {
inline std::vector<pl::modmenu::ConfigEntry> menuControls() {
    using Type=pl::modmenu::ConfigType;
    return {
        {"players","Target players",Type::Toggle,"true","","",""},
        {"mobs","Target mobs",Type::Toggle,"false","","",""},
        {"strength","Aim strength (100 = AIM BOT!)",Type::SliderInt,"50","10","100",""},
        {"head","Aim at head (off = torso)",Type::Toggle,"false","","",""},
        {"adaptive","Adaptive slowdown",Type::Toggle,"true","","",""},
        {"fov","Aim cone (degrees)",Type::SliderFloat,"60","1","180",""},
        {"range","Range (blocks)",Type::SliderFloat,"4","1","6",""},
        {"pickRayFeedback","Use pick-ray feedback (advanced)",Type::Toggle,"false","","",""},
        {"autoCalibrate","Auto-calibrate camera axes",Type::Toggle,"true","","",""},
        {"cameraSmoothing","Frame-gen camera smoothing",Type::Toggle,"true","","",""},
        {"smoothness","Camera smoothness",Type::SliderFloat,"50","0","95",""},
        {"yawFirst","Camera input: yaw first (advanced)",Type::Toggle,"false","","",""},
        {"speed","Base turn speed (below 100 strength)",Type::SliderFloat,"70","5","90",""}
    };
}
inline std::string settingValue(const Settings& s,std::string_view key) {
    if(key=="players") return s.players?"true":"false";
    if(key=="mobs") return s.mobs?"true":"false";
    if(key=="head") return s.head?"true":"false";
    if(key=="adaptive") return s.adaptive?"true":"false";
    if(key=="pickRayFeedback") return s.pickRayFeedback?"true":"false";
    if(key=="autoCalibrate") return s.autoCalibrate?"true":"false";
    if(key=="cameraSmoothing") return s.cameraSmoothing?"true":"false";
    if(key=="yawFirst") return s.yawFirst?"true":"false";
    if(key=="strength") return std::to_string(static_cast<int>(s.strength));
    if(key=="fov") return std::to_string(s.fov);
    if(key=="range") return std::to_string(s.range);
    if(key=="smoothness") return std::to_string(s.smoothness);
    if(key=="speed") return std::to_string(s.speed);
    return {};
}
inline std::string menuSchema(const Settings& settings,const std::string& status) {
    using namespace pl::modmenu;
    ConfigSchemaBuilder builder;
    builder.category("aim","Aim settings").defaultCategory("aim");
    ConfigNodeV2 info;
    info.id="tracking_status"; info.category="aim";
    info.type=ConfigControlTypeV2::Info; info.title="Tracking status (menu only)";
    info.description=status;
    builder.node(info);
    ConfigNodeV2 retry;
    retry.id="retry"; retry.key="retry"; retry.category="aim";
    retry.type=ConfigControlTypeV2::Button; retry.title="Retry camera calibration";
    builder.node(retry);
    for(const auto& control:menuControls()) {
        ConfigNodeV2 node;
        node.id=control.key; node.key=control.key; node.category="aim";
        node.title=control.displayName; node.defaultValue=control.defaultValue;
        node.currentValue=settingValue(settings,control.key); node.hasCurrentValue=true;
        node.minValue=control.minValue; node.maxValue=control.maxValue;
        node.type=control.type==ConfigType::Toggle?ConfigControlTypeV2::Toggle:
            control.type==ConfigType::SliderInt?ConfigControlTypeV2::SliderInt:ConfigControlTypeV2::SliderFloat;
        builder.node(node);
    }
    return builder.toJson();
}
}
