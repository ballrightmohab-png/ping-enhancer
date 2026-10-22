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
    assert(fakehost::hookCount()==0); // Menu hook rolled back if tick hook fails.
    fakehost::failHook=-1;
    assert(runtime.install(tick,ui,status));
    assert(fakehost::hookCount()==3);
    assert(paused); // Unknown UI state fails closed.
    int actor=0;
    fakehost::physicalLook(&actor,{3,-4});
    assert(fakehost::turns==1 && fakehost::lastDelta.x==3 && fakehost::lastDelta.y==-4);
    assert(runtime.sendAutomaticTurn(reinterpret_cast<game::Player*>(&actor),{1,2}));
    assert(fakehost::turns==2 && fakehost::lastDelta.x==1 && fakehost::lastDelta.y==2);
    fakehost::tick(&actor);
    assert(ticks==1 && fakehost::originalTicks==1);
    assert(!fakehost::queryMenu(false)); assert(!paused);
    assert(fakehost::queryMenu(true)); assert(paused);
    assert(!fakehost::queryMenu(false)); assert(!paused);
    runtime.stop();
    assert(!runtime.sendAutomaticTurn(reinterpret_cast<game::Player*>(&actor),{1,2}));
    assert(fakehost::hookCount()==0);
    assert(runtime.install(tick,ui,status));
    assert(paused && fakehost::hookCount()==3);
    runtime.stop();
    // Unsupported menu lookup must not be silently bypassed.
    fakehost::menuAvailable=false;
    { game::Runtime unavailable;
      assert(!unavailable.resolve(status));
      assert(status.find("isShowingMenu")!=std::string::npos);
      assert(!unavailable.install(tick,ui,status));
      assert(fakehost::hookCount()==0);
    }
}
