#include "script/proc_role.hpp"

#include <s7.h>

#include <string>
#include <unordered_map>

namespace slopengine {

namespace {

std::unordered_map<std::string, PackageRole> g_procRoles;

struct CollectCtx {
    ProcRoleSnapshot* out = nullptr;
};

bool collectProc(const char* symbolName, s7_pointer value, void* data) {
    if (symbolName == nullptr || data == nullptr || !s7_is_procedure(value)) {
        return false;
    }
    auto* ctx = static_cast<CollectCtx*>(data);
    if (ctx->out != nullptr) {
        (*ctx->out)[symbolName] = value;
    }
    return false;
}

struct StampCtx {
    const ProcRoleSnapshot* before = nullptr;
    PackageRole role = PackageRole::Base;
};

bool stampProc(const char* symbolName, s7_pointer value, void* data) {
    if (symbolName == nullptr || data == nullptr || !s7_is_procedure(value)) {
        return false;
    }
    auto* ctx = static_cast<StampCtx*>(data);
    if (ctx->before == nullptr) {
        return false;
    }
    const auto it = ctx->before->find(symbolName);
    if (it == ctx->before->end() || it->second != value) {
        g_procRoles[symbolName] = ctx->role;
    }
    return false;
}

} // namespace

ProcRoleSnapshot snapshotProcRoles(s7_scheme* scheme) {
    ProcRoleSnapshot snapshot;
    if (scheme == nullptr) {
        return snapshot;
    }
    CollectCtx ctx{};
    ctx.out = &snapshot;
    s7_for_each_symbol(scheme, collectProc, &ctx);
    return snapshot;
}

void stampProcRoles(s7_scheme* scheme, const ProcRoleSnapshot& before, PackageRole role) {
    if (scheme == nullptr) {
        return;
    }
    StampCtx ctx{};
    ctx.before = &before;
    ctx.role = role;
    s7_for_each_symbol(scheme, stampProc, &ctx);
}

PackageRole roleForProc(std::string_view name) {
    const auto it = g_procRoles.find(std::string(name));
    if (it == g_procRoles.end()) {
        return PackageRole::Base;
    }
    return it->second;
}

void clearProcRoles() {
    g_procRoles.clear();
}

}
