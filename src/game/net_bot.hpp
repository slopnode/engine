#pragma once

#include <flecs.h>

#include <cstdint>
#include <random>
#include <string>
#include <string_view>

namespace slopengine {

enum class NetBotKind {
    Still,
    Walk,
    Fight,
};

bool parseNetBotKind(std::string_view text, NetBotKind& out);

class NetBot {
public:
    NetBot(NetBotKind kind, std::uint32_t seed);

    void drive(flecs::world& world, float frameDt);

private:
    void press(flecs::world& world, std::string_view action);
    void hold(flecs::world& world, std::string_view action, bool down);
    void pickCourse();

    NetBotKind kind_;
    std::mt19937 random_;
    float courseLeft_ = 0.0f;
    float forward_ = 0.0f;
    float strafe_ = 0.0f;
    float turnRate_ = 0.0f;
    bool sprint_ = false;
    float burstLeft_ = 0.0f;
    float nextBurst_ = 0.0f;
    float nextJump_ = 0.0f;
    float nextSwitch_ = 0.0f;
    float time_ = 0.0f;
};

}
