#pragma once

#include "core/package.hpp"
#include "script/script_scope.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct s7_scheme;

namespace slopengine {

using ProcRoleSnapshot = std::unordered_map<std::string, void*>;

ProcRoleSnapshot snapshotProcRoles(s7_scheme* scheme);
void stampProcRoles(s7_scheme* scheme, const ProcRoleSnapshot& before, PackageRole role);
PackageRole roleForProc(std::string_view name);
void clearProcRoles();
PackageRole callerRole();
void bindRoleApi(s7_scheme* scheme);

struct ScriptPlayerStack {
    std::string current = "Player";
    std::vector<std::string> saved;
    std::size_t floor = 0;
};

ScriptPlayerStack& scriptPlayerStack();

class ScriptCallGuard {
public:
    ScriptCallGuard(s7_scheme* scheme, PackageRole role);
    ~ScriptCallGuard();

    ScriptCallGuard(const ScriptCallGuard&) = delete;
    ScriptCallGuard& operator=(const ScriptCallGuard&) = delete;

private:
    s7_scheme* scheme_;
    PackageRole role_;
    ScriptRoleGuard roleGuard_;
    std::string player_;
    std::size_t playerDepth_ = 0;
    std::size_t playerFloor_ = 0;
};

}
