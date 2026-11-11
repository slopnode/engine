#pragma once

#include <flecs.h>
#include <s7.h>

#include <string>

namespace slopengine {

class ScriptPlayerGuard {
public:
    explicit ScriptPlayerGuard(std::string id);
    ~ScriptPlayerGuard();

    ScriptPlayerGuard(const ScriptPlayerGuard&) = delete;
    ScriptPlayerGuard& operator=(const ScriptPlayerGuard&) = delete;

private:
    std::string previous_;
};

const std::string& currentScriptPlayer();
bool scriptPlayerIsLocal();
flecs::entity scriptPlayer(const flecs::world& world);
void bindScriptPlayerApi(flecs::world& world, s7_scheme* scheme);
flecs::entity scriptPlayer(const flecs::world& world, s7_pointer args);
bool scriptPlayerFeet(const flecs::world& world, flecs::entity player, float& x, float& y, float& z);

}
