#pragma once

#include "physics/motor_rules.hpp"

#include <flecs.h>

#include <string>
#include <vector>

struct s7_scheme;
typedef struct s7_cell* s7_pointer;

namespace slopengine {

class AssetStore;

bool parseMotorRules(s7_scheme* sc, s7_pointer list, std::vector<MotorRule>& out, std::vector<std::string>& messages);
void loadPackageMotorRules(s7_scheme* sc, AssetStore& assets);
void bindMotorRuleApi(flecs::world& world, s7_scheme* sc);

}
