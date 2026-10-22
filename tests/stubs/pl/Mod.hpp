#pragma once
// Host-only lifecycle stand-in. ModMenu.hpp and BedrockTools headers remain real.
#include <string>
namespace pl::mod {
struct TestLogger { template<class... T> void warn(T&&...) {} };
class ModContext {
    TestLogger log;
public:
    std::string id() const { return "adaptiveaim"; }
    TestLogger& logger() { return log; }
};
}
#define PL_REGISTER_MOD(...)
