#include "Aim.hpp"
#include <cassert>
#include <limits>
int main() {
    using namespace aim;
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
