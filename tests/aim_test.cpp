#include "Aim.hpp"
#include "Settings.hpp"
#include "CameraSmoother.hpp"
#include <cassert>
#include <limits>
int main() {
    using namespace aim;
    auto air=[](int,int,int){return false;};
    assert(clearVoxels({0,1.68f,0},{-1,.385f,3},air));
    assert(clearVoxels({0,1.68f,0},{1,.385f,3},air));
    assert(clearVoxels({0,1.68f,0},{-1,.385f,-3},air));
    assert(!clearVoxels({0,1.68f,0},{-1,.385f,3},[](int x,int,int z){return x==-1 && z==3;}));
    assert(!clearVoxels({0,1.68f,0},{1,.385f,3},[](int,int,int z){return z==1;}));
    assert(!clearVoxels({NAN,0,0},{1,0,0},air));
    assert(!clearVoxels({1e30f,0,0},{1,0,0},air));
    auto yawFirst=toLookDelta({5,-12},true);
    assert(yawFirst.first==-12 && yawFirst.second==5);
    auto pitchFirst=toLookDelta({5,-12},false);
    assert(pitchFirst.first==5 && pitchFirst.second==-12);
    CameraSmoother smoother;
    auto first=smoother.advance({10,-20},.01f,50,90);
    assert(first.pitch>0 && first.yaw<0);
    assert(magnitude(first)<=.90001f);
    Angles released=first;
    // Automatic draining even with zero further input; angle conservation.
    for(int i=0;i<1000;++i) {
        auto next=smoother.advance({},.01f,50,90);
        released.pitch+=next.pitch; released.yaw+=next.yaw;
    }
    assert(std::abs(released.pitch+smoother.pending().pitch-10)<.001f);
    assert(std::abs(released.yaw+smoother.pending().yaw+20)<.001f);
    smoother.advance({4,5},.01f,50,90);
    smoother.reset();
    assert(magnitude(smoother.advance({},.01f,50,90))==0);
    smoother.advance({4,5},.01f,50,90);
    assert(magnitude(smoother.advance({},1.f,50,90))==0);
    assert(magnitude(smoother.pending())==0);
    assert(magnitude(smoother.advance({NAN,1},.01f,50,90))==0);
    Settings settings;
    assert(!settings.yawFirst && settings.cameraSmoothing);
    assert(settings.update("yawFirst","false") && !settings.yawFirst);
    assert(settings.accepts(true,true));
    assert(!settings.accepts(false,true));
    assert(settings.update("players","false"));
    assert(settings.update("mobs","true"));
    assert(!settings.accepts(true,true)); // Players are mobs too; players switch wins.
    assert(settings.accepts(false,true));
    assert(!settings.accepts(false,false)); // Not items, projectiles, etc.
    assert(settings.update("mobs","false"));
    assert(!settings.accepts(false,true));
    assert(settings.update("strength","0") && settings.strength==0);
    assert(settings.update("strength","75") && settings.strength==75);
    assert(settings.update("strength","150") && settings.strength==100);
    assert(settings.update("strength","-5") && settings.strength==0);
    assert(!settings.update("strength","nan"));
    assert(!settings.update("strength","50junk"));
    assert(!settings.update("players","1"));
    assert(!settings.update("unknown","50"));
    assert(std::abs(towards({0,0,0},{0,0,2})->yaw) < 0.001f);
    assert(std::abs(towards({0,0,0},{2,0,0})->yaw + 90) < 0.001f);
    assert(towards({0,0,0},{0,1,1})->pitch < 0);
    assert(!towards({0,0,0},{0,0,0}));
    assert(!towards({0,0,0},{NAN,0,1}));
    assert(std::abs(error({0,179},{0,-179}).yaw-2) < .001f);
    auto delta = step({30,40},.05f,8,25,true);
    assert(magnitude(delta) <= 1.25001f);
    assert(magnitude(step({0,0},.05f,8,25,true)) == 0);
    assert(magnitude(step({1,1},-1,8,25,true)) == 0);
    assert(magnitude(step({NAN,1},.05f,8,25,true)) == 0);
    Angles a{0,20}, b=a;
    for(int i=0;i<20;++i) { auto s=step(a,.05f,4,120,false); a={a.pitch-s.pitch,a.yaw-s.yaw}; }
    for(int i=0;i<100;++i) { auto s=step(b,.01f,4,120,false); b={b.pitch-s.pitch,b.yaw-s.yaw}; }
    assert(std::abs(a.yaw-b.yaw)<.001f);
}
