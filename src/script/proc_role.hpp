#pragma once

#include "core/package.hpp"

#include <string>
#include <string_view>
#include <unordered_map>

struct s7_scheme;

namespace slopengine {

using ProcRoleSnapshot = std::unordered_map<std::string, void*>;

ProcRoleSnapshot snapshotProcRoles(s7_scheme* scheme);
void stampProcRoles(s7_scheme* scheme, const ProcRoleSnapshot& before, PackageRole role);
PackageRole roleForProc(std::string_view name);
void clearProcRoles();

}
