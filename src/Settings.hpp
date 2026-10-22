#pragma once
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>

namespace aim {
struct Settings {
    bool enabled=false, adaptive=true, head=false;
    bool players=true, mobs=false;
    float fov=8, range=4, strength=50, speed=70;

    bool accepts(bool player, bool mob) const {
        // Players can also have the Mob category; never let the mobs switch
        // accidentally override a disabled players switch.
        return player ? players : (mob && mobs);
    }
    bool update(std::string_view key, std::string_view value) {
        bool* flag=nullptr;
        if(key=="adaptive") flag=&adaptive;
        if(key=="head") flag=&head;
        if(key=="players") flag=&players;
        if(key=="mobs") flag=&mobs;
        if(flag) {
            if(value!="true" && value!="false") return false;
            *flag=value=="true";
            return true;
        }
        if(value.empty() || value.size()>64) return false;
        const std::string text(value);
        char* end=nullptr;
        errno=0;
        const float number=std::strtof(text.c_str(), &end);
        if(errno==ERANGE || end==text.c_str() || end!=text.c_str()+text.size() || !std::isfinite(number)) return false;
        if(key=="fov") fov=std::clamp(number,1.f,20.f);
        else if(key=="range") range=std::clamp(number,1.f,6.f);
        else if(key=="strength") strength=std::clamp(number,0.f,100.f);
        else if(key=="speed") speed=std::clamp(number,5.f,90.f);
        else return false;
        return true;
    }
};
}
