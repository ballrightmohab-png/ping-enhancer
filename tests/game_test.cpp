// Literal MC 1.26.60 fixture offsets, independent of the production constants.
#include "game/Game.hpp"
#include <array>
#include <cassert>
#include <cstring>
int main() {
    alignas(16) std::array<unsigned char,2048> actor{}, level{}, dimension{};
    game::Vec2 rotation{12,-34}; game::AABB bounds{{1,2,3},{2,4,5}};
    game::ViewRay ray{{1.5f,3.88f,4},{0,0,1}};
    int manager=0;
    auto put=[](auto& bytes,std::size_t offset,auto value) { std::memcpy(bytes.data()+offset,&value,sizeof(value)); };
    put(actor,0x238,&rotation); put(actor,0x230,&bounds);
    put(actor,0x1F0,level.data()); put(actor,0x1E0,dimension.data());
    put(actor,0x220,std::uint32_t{2}); put(level,0x478,&manager);
    auto* a=reinterpret_cast<game::Actor*>(actor.data());
    auto* l=reinterpret_cast<game::Level*>(level.data());
    assert(a->rotation().x==12 && a->rotation().y==-34);
    assert(a->bounds().min.x==1 && a->bounds().max.y==4);
    assert(a->level()==l && a->dimension()==reinterpret_cast<game::Dimension*>(dimension.data()));
    assert(a->hasCategory(2) && l->actorManager()==&manager);
    assert(l->viewRay().direction.z==0); // Null wrapper is a missing ray, not a crash.
    put(level,0x1D0,&ray);
    assert(l->viewRay().origin.y==3.88f && l->viewRay().direction.z==1);
    put(level,0x1D0,static_cast<void*>(nullptr));
    assert(l->viewRay().origin.x==0 && l->viewRay().direction.z==0);
}
