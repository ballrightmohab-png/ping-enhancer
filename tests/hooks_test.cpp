#include "FakeHost.hpp"
int main() {
    using S=game::Signature;
    std::string status;
    game::Runtime runtime;
    assert(!runtime.resolve(status));
    assert(status.find("NormalTick")!=std::string::npos);
    assert(fakehost::hookCount()==0);
    fakehost::gameReady=true;
    fakehost::missing=static_cast<int>(S::LocalPlayerApplyTurnDelta);
    assert(!runtime.resolve(status));
    assert(status.find("LocalPlayerApplyTurnDelta")!=std::string::npos);
    fakehost::missing=-1;
    assert(runtime.resolve(status));
    int ticks=0; bool paused=false;
    auto tick=[&](game::Player*){++ticks;};
    auto ui=[&](bool value){paused=value;};
    fakehost::failHook=2;
    assert(!runtime.install(tick,ui,status));
    assert(fakehost::hookCount()==0); // Partial install rolled back.
    fakehost::failHook=-1;
    assert(runtime.install(tick,ui,status));
    assert(fakehost::hookCount()==5);
    int actor=0, container1=1, container2=2, chat=3;
    fakehost::tick(&actor);
    assert(ticks==1 && fakehost::originalTicks==1);
    fakehost::screen(S::ContainerScreenControllerOpen,&container1); assert(paused);
    fakehost::screen(S::ContainerScreenControllerOpen,&container2); assert(paused);
    fakehost::screen(S::ContainerScreenControllerDtor,&container1); assert(paused);
    fakehost::screen(S::ChatScreenOpen,&chat); assert(paused);
    fakehost::screen(S::ContainerScreenControllerDtor,&container2); assert(paused);
    fakehost::screen(S::ChatScreenDtor,&chat); assert(!paused);
    runtime.stop();
    assert(fakehost::hookCount()==0);
    assert(runtime.install(tick,ui,status));
    assert(fakehost::hookCount()==5);
    runtime.stop();
    assert(fakehost::hookCount()==0);
}
