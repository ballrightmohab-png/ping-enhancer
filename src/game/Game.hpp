// MC 1.26.60 only. Layout constants adapted from BedrockTools, MIT; see THIRD_PARTY_NOTICES.md.
#pragma once
#include <cstddef>
#include <cstdint>
namespace game {
struct Vec2 { float x,y; };
struct Vec3 { float x,y,z; };
struct AABB { Vec3 min,max; };
struct ViewRay { Vec3 origin,direction; };
struct BlockPos { int x,y,z; };
namespace offsets {
namespace Actor {
inline constexpr std::size_t mEntityContext = 0x8;
inline constexpr std::size_t mEntityData = 0x140;
inline constexpr std::size_t mStateVectorComponent = 0x228;
inline constexpr std::size_t mActorRotationComponent = 0x238;
inline constexpr std::size_t mLevel = 0x1F0;
inline constexpr std::size_t mDimension = 0x1E0;
inline constexpr std::size_t mHurtTime = 0x1B4;
inline constexpr std::size_t mCategories = 0x220;
inline constexpr std::size_t mNameTagHash = 0x1A0;
inline constexpr std::size_t mFilteredNameTag = 0x2E0;
}
namespace BuiltInActorComponents {
inline constexpr std::size_t mAABBShapeComponent = 8;
}
namespace AABBShapeComponent {
inline constexpr std::size_t mAABB = 0;
}
namespace Dimension {
inline constexpr std::size_t mBlockSource = 208;
inline constexpr std::size_t mWeather = 0x1B8;
}
namespace Level {
inline constexpr std::size_t mActorManager = 0x478;
inline constexpr std::size_t mHitResultWrapper = 0x1D0;
}
}
template<class T> const T& field(const void* object,std::size_t offset) {
    return *reinterpret_cast<const T*>(static_cast<const unsigned char*>(object)+offset);
}
class BlockSource {};
class Dimension {
public:
    BlockSource* blockSource() const { return field<BlockSource*>(this,offsets::Dimension::mBlockSource); }
};
class Level {
public:
    void* actorManager() const { return field<void*>(this,offsets::Level::mActorManager); }
    ViewRay viewRay() const {
        // MC 1.26.60: Level stores a POINTER to HitResultWrapper at 0x1D0,
        // not an inline HitResult. The wrapper's HitResult member remains @0.
        const auto* hit=field<const void*>(this,offsets::Level::mHitResultWrapper);
        if(!hit) return {};
        return {field<Vec3>(hit,0),field<Vec3>(hit,12)};
    }
};
class Actor {
public:
    void* rotationComponent() const { return field<void*>(this,offsets::Actor::mActorRotationComponent); }
    Vec2 rotation() const { auto* p=rotationComponent(); return p?field<Vec2>(p,0):Vec2{}; }
    Level* level() const { return field<Level*>(this,offsets::Actor::mLevel); }
    Dimension* dimension() const { return field<Dimension*>(this,offsets::Actor::mDimension); }
    bool hasCategory(std::uint32_t mask) const { return (field<std::uint32_t>(this,offsets::Actor::mCategories)&mask)!=0; }
    AABB bounds() const {
        auto* p=field<void*>(this,offsets::Actor::mStateVectorComponent+offsets::BuiltInActorComponents::mAABBShapeComponent);
        return p?field<AABB>(p,offsets::AABBShapeComponent::mAABB):AABB{};
    }
};
using Player=Actor;
}
