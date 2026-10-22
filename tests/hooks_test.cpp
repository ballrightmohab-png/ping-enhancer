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
    fakehost::failHook=1;
    assert(!runtime.install(tick,ui,status));
    assert(fakehost::hookCount()==0); // Menu hook rolled back if tick hook fails.
    fakehost::failHook=-1;
    assert(runtime.install(tick,ui,status));
    assert(fakehost::hookCount()==2);
    assert(paused); // Unknown UI state fails closed.
    int actor=0;
    fakehost::tick(&actor);
    assert(ticks==1 && fakehost::originalTicks==1);
    assert(!fakehost::queryMenu(false)); assert(!paused);
    assert(fakehost::queryMenu(true)); assert(paused);
    assert(!fakehost::queryMenu(false)); assert(!paused);
    runtime.stop();
    assert(fakehost::hookCount()==0);
    assert(runtime.install(tick,ui,status));
    assert(paused && fakehost::hookCount()==2);
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
