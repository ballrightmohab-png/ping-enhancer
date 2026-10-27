#include "Aim.hpp"
#include "Settings.hpp"
#include "CameraSmoother.hpp"
#include "LookCalibration.hpp"
#include "Strength.hpp"
#include <cassert>
#include <limits>
int main() {
    using namespace aim;
    // Every displayed decade is stronger, and none overshoots the target.
    float previous=0;
    for(int strength=10;strength<=100;strength+=10) {
        CameraSmoother smoother;
        auto correction=strengthCorrection({15,20},.05f,float(strength),70,true,true,50,smoother);
        assert(magnitude(correction)>previous);
        assert(magnitude(correction)<=25.f);
        previous=magnitude(correction);
    }
    CameraSmoother strongest;
    strongest.advance({5,5},.05f,95,70); // No residual lag at 100.
    auto locked=strengthCorrection({15,20},.05f,100,5,true,true,95,strongest);
    assert(std::abs(locked.pitch-15)<.001f && std::abs(locked.yaw-20)<.001f);
    assert(magnitude(strongest.pending())==0);
    assert(magnitude(strengthCorrection({100,100},.05f,100,70,true,true,50,strongest))<=36.001f);
    assert(magnitude(strengthCorrection({15,20},.05f,0,70,true,true,50,strongest))==0);
    assert(magnitude(strengthCorrection({15,20},.3f,100,70,true,true,50,strongest))==0);
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
    // Calibrate all axis-order/sign combinations and two different gains.
    for(int swap=0;swap<2;++swap) for(int sign0:{-1,1}) for(int sign1:{-1,1}) {
        LookCalibration calibration;
        Angles view{};
        Angles c0=swap?Angles{0,2.f*sign0}:Angles{.5f*sign0,0};
        Angles c1=swap?Angles{.5f*sign1,0}:Angles{0,2.f*sign1};
        for(int i=0;i<100 && !calibration.ready();++i) {
            auto probe=calibration.update(view,i*.02,false);
            if(probe) {
                view.pitch+=c0.pitch*probe->first+c1.pitch*probe->second;
                view.yaw+=c0.yaw*probe->first+c1.yaw*probe->second;
            }
        }
        assert(calibration.ready());
        auto command=calibration.encode({.3f,-.7f}); assert(command);
        assert(std::abs(c0.pitch*command->first+c1.pitch*command->second-.3f)<.001f);
        assert(std::abs(c0.yaw*command->first+c1.yaw*command->second+.7f)<.001f);
    }
    // Cross-coupled responses invert correctly; parallel axes are rejected.
    for(bool singular:{false,true}) {
        LookCalibration calibration;
        Angles view{};
        const Angles c0{1,.3f}, c1=singular?Angles{2,.6f}:Angles{-.2f,2};
        for(int i=0;i<100 && !calibration.ready() && !calibration.failed();++i) {
            auto probe=calibration.update(view,i*.02,false);
            if(probe) {
                view.pitch+=c0.pitch*probe->first+c1.pitch*probe->second;
                view.yaw+=c0.yaw*probe->first+c1.yaw*probe->second;
            }
        }
        assert(calibration.failed()==singular);
        if(!singular) {
            auto input=calibration.encode({.7f,-.4f}); assert(input);
            assert(std::abs(c0.pitch*input->first+c1.pitch*input->second-.7f)<.001f);
            assert(std::abs(c0.yaw*input->first+c1.yaw*input->second+.4f)<.001f);
            input=calibration.encode({100,100}); assert(input);
            assert(std::hypot(input->first,input->second)<=10.001f);
            assert(!calibration.encode({NAN,0}));
        }
    }
    LookCalibration noResponse;
    for(int i=0;i<100;++i) noResponse.update({},i*.02,false);
    assert(noResponse.failed());
    assert(!noResponse.encode({1,1}));
    assert(!noResponse.update({},3,false)); // Failure never repeats probes indefinitely.
    LookCalibration manual;
    assert(manual.update({},0,false));
    assert(!manual.update({5,6},.1,true));
    assert(!manual.update({5,6},.2,false)); // Manual movement invalidated the probe.
    assert(manual.update({5,6},.4,false));
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
