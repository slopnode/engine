#include "script/proc_role.hpp"

#include "core/log.hpp"
#include "script/package_load_context.hpp"

#include <s7.h>
#include <s7_slopengine.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace slopengine {

namespace {

std::unordered_map<std::string, PackageRole> g_procRoles;

std::vector<s7_pointer> g_baseSymbols;
bool g_baseSymbolsStale = true;

const char* kSchemeHooks[] = {
    "*error-hook*",
    "*load-hook*",
    "*missing-close-paren-hook*",
    "*read-error-hook*",
    "*rootlet-redefinition-hook*",
    "*unbound-variable-hook*",
};

constexpr std::size_t kSchemeHookCount = sizeof(kSchemeHooks) / sizeof(kSchemeHooks[0]);

struct BaseBindingSnapshot {
    std::vector<s7_pointer> symbols;
    s7_pointer values = nullptr;
    s7_pointer hookFunctions = nullptr;
    unsigned int gcLoc = 0;
    unsigned int hookGcLoc = 0;
};

int g_modCallDepth = 0;
BaseBindingSnapshot g_snapshot;
std::unordered_set<std::string> g_reportedRebinds;
int g_modProcType = -1;

s7_pointer globalProc(s7_scheme* scheme, const char* name) {
    const s7_pointer value = slopengine_s7_global_value(scheme, s7_make_symbol(scheme, name));
    return s7_is_procedure(value) || s7_is_macro(scheme, value) ? value : nullptr;
}

s7_pointer schemeHook(s7_scheme* scheme, std::size_t index) {
    const s7_pointer value =
        slopengine_s7_global_value(scheme, s7_make_symbol(scheme, kSchemeHooks[index]));
    return s7_is_procedure(value) ? value : nullptr;
}

s7_pointer copyList(s7_scheme* scheme, s7_pointer list) {
    return s7_is_pair(list) ? s7_reverse(scheme, s7_reverse(scheme, list)) : s7_nil(scheme);
}

bool sameList(s7_scheme* scheme, s7_pointer a, s7_pointer b) {
    while (s7_is_pair(a) && s7_is_pair(b)) {
        if (s7_car(a) != s7_car(b)) {
            return false;
        }
        a = s7_cdr(a);
        b = s7_cdr(b);
    }
    return s7_is_null(scheme, a) && s7_is_null(scheme, b);
}

void reportRebind(const char* name) {
    if (g_reportedRebinds.insert(name).second) {
        Log::warning(
            LogCategory::Script,
            "SCRIPT: mod code rebound base procedure '%s'; restored (extend hooks with hook-add)",
            name);
    }
}

void captureSchemeHooks(s7_scheme* scheme) {
    for (std::size_t i = 0; i < kSchemeHookCount; ++i) {
        const s7_pointer hook = schemeHook(scheme, i);
        s7_vector_set(
            scheme,
            g_snapshot.hookFunctions,
            static_cast<s7_int>(i),
            hook != nullptr ? copyList(scheme, s7_hook_functions(scheme, hook)) : s7_nil(scheme));
    }
}

struct CollectCtx {
    s7_scheme* scheme = nullptr;
    ProcRoleSnapshot* out = nullptr;
};

bool collectProc(const char* symbolName, void* data) {
    auto* ctx = static_cast<CollectCtx*>(data);
    if (symbolName == nullptr || ctx == nullptr || ctx->out == nullptr) {
        return false;
    }
    if (const s7_pointer proc = globalProc(ctx->scheme, symbolName)) {
        (*ctx->out)[symbolName] = proc;
    }
    return false;
}

struct StampCtx {
    s7_scheme* scheme = nullptr;
    const ProcRoleSnapshot* before = nullptr;
    PackageRole role = PackageRole::Base;
};

bool stampProc(const char* symbolName, void* data) {
    auto* ctx = static_cast<StampCtx*>(data);
    if (symbolName == nullptr || ctx == nullptr || ctx->before == nullptr) {
        return false;
    }
    const s7_pointer proc = globalProc(ctx->scheme, symbolName);
    if (proc == nullptr) {
        return false;
    }
    const auto it = ctx->before->find(symbolName);
    if (it == ctx->before->end() || it->second != proc) {
        g_procRoles[symbolName] = ctx->role;
    }
    return false;
}

struct BaseSymbolCtx {
    s7_scheme* scheme = nullptr;
};

bool collectBaseSymbol(const char* symbolName, void* data) {
    auto* ctx = static_cast<BaseSymbolCtx*>(data);
    if (symbolName == nullptr || ctx == nullptr) {
        return false;
    }
    if (globalProc(ctx->scheme, symbolName) != nullptr && roleForProc(symbolName) != PackageRole::Mod) {
        g_baseSymbols.push_back(s7_make_symbol(ctx->scheme, symbolName));
    }
    return false;
}

void refreshBaseSymbols(s7_scheme* scheme) {
    if (!g_baseSymbolsStale) {
        return;
    }
    g_baseSymbols.clear();
    BaseSymbolCtx ctx{};
    ctx.scheme = scheme;
    s7_for_each_symbol_name(scheme, collectBaseSymbol, &ctx);
    g_baseSymbolsStale = false;
}

void takeSnapshot(s7_scheme* scheme) {
    refreshBaseSymbols(scheme);
    g_snapshot.symbols = g_baseSymbols;
    const auto count = static_cast<s7_int>(g_snapshot.symbols.size());
    g_snapshot.values = s7_make_vector(scheme, count);
    g_snapshot.gcLoc = s7_gc_protect(scheme, g_snapshot.values);
    for (s7_int i = 0; i < count; ++i) {
        s7_vector_set(
            scheme,
            g_snapshot.values,
            i,
            slopengine_s7_global_value(scheme, g_snapshot.symbols[static_cast<std::size_t>(i)]));
    }
    g_snapshot.hookFunctions = s7_make_vector(scheme, static_cast<s7_int>(kSchemeHookCount));
    g_snapshot.hookGcLoc = s7_gc_protect(scheme, g_snapshot.hookFunctions);
    captureSchemeHooks(scheme);
}

void releaseSnapshot(s7_scheme* scheme) {
    if (g_snapshot.values != nullptr) {
        s7_gc_unprotect_at(scheme, g_snapshot.gcLoc);
    }
    if (g_snapshot.hookFunctions != nullptr) {
        s7_gc_unprotect_at(scheme, g_snapshot.hookGcLoc);
    }
    g_snapshot = BaseBindingSnapshot{};
}

void restoreBaseBindings(s7_scheme* scheme) {
    if (g_snapshot.values == nullptr) {
        return;
    }
    const s7_pointer rootlet = s7_rootlet(scheme);
    for (std::size_t i = 0; i < g_snapshot.symbols.size(); ++i) {
        const s7_pointer symbol = g_snapshot.symbols[i];
        const s7_pointer saved = s7_vector_ref(scheme, g_snapshot.values, static_cast<s7_int>(i));
        if (slopengine_s7_global_value(scheme, symbol) == saved) {
            continue;
        }
        s7_define(scheme, rootlet, symbol, saved);
        reportRebind(s7_symbol_name(symbol));
    }
    for (std::size_t i = 0; i < kSchemeHookCount; ++i) {
        const s7_pointer hook = schemeHook(scheme, i);
        if (hook == nullptr) {
            continue;
        }
        const s7_pointer saved = s7_vector_ref(scheme, g_snapshot.hookFunctions, static_cast<s7_int>(i));
        if (sameList(scheme, s7_hook_functions(scheme, hook), saved)) {
            continue;
        }
        s7_hook_set_functions(scheme, hook, copyList(scheme, saved));
        reportRebind(kSchemeHooks[i]);
    }
}

void recaptureBaseBindings(s7_scheme* scheme) {
    if (g_snapshot.values == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < g_snapshot.symbols.size(); ++i) {
        s7_vector_set(
            scheme,
            g_snapshot.values,
            static_cast<s7_int>(i),
            slopengine_s7_global_value(scheme, g_snapshot.symbols[i]));
    }
    captureSchemeHooks(scheme);
}

struct ModProc {
    s7_pointer proc = nullptr;
};

char* printModProc(s7_scheme*, void*) {
    const char text[] = "#<mod-procedure>";
    char* out = static_cast<char*>(std::malloc(sizeof(text)));
    std::memcpy(out, text, sizeof(text));
    return out;
}

void freeModProc(void* value) {
    delete static_cast<ModProc*>(value);
}

bool equalModProc(void* a, void* b) {
    return a == b;
}

void markModProc(void* value) {
    s7_mark_object(static_cast<ModProc*>(value)->proc);
}

s7_pointer applyModProc(s7_scheme* sc, s7_pointer obj, s7_pointer args) {
    const auto* wrapped = static_cast<ModProc*>(s7_object_value(obj));
    ScriptCallGuard guard(sc, PackageRole::Mod);
    return s7_call(sc, wrapped->proc, args);
}

s7_pointer g_as_caller_role(s7_scheme* sc, s7_pointer args) {
    const s7_pointer proc = s7_car(args);
    if (!s7_is_procedure(proc) || callerRole() != PackageRole::Mod) {
        return proc;
    }
    auto* wrapped = new ModProc{};
    wrapped->proc = proc;
    return s7_make_object(sc, g_modProcType, wrapped);
}

} // namespace

ProcRoleSnapshot snapshotProcRoles(s7_scheme* scheme) {
    ProcRoleSnapshot snapshot;
    if (scheme == nullptr) {
        return snapshot;
    }
    CollectCtx ctx{};
    ctx.scheme = scheme;
    ctx.out = &snapshot;
    s7_for_each_symbol_name(scheme, collectProc, &ctx);
    return snapshot;
}

void stampProcRoles(s7_scheme* scheme, const ProcRoleSnapshot& before, PackageRole role) {
    if (scheme == nullptr) {
        return;
    }
    StampCtx ctx{};
    ctx.scheme = scheme;
    ctx.before = &before;
    ctx.role = role;
    s7_for_each_symbol_name(scheme, stampProc, &ctx);
    g_baseSymbolsStale = true;
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
    g_baseSymbols.clear();
    g_baseSymbolsStale = true;
    g_reportedRebinds.clear();
}

PackageRole callerRole() {
    if (currentPackageRole() == PackageRole::Mod || currentScriptRole() == PackageRole::Mod) {
        return PackageRole::Mod;
    }
    return currentScriptRole();
}

void bindRoleApi(s7_scheme* scheme) {
    if (scheme == nullptr) {
        return;
    }
    if (g_modProcType < 0) {
        g_modProcType = s7_new_type_x(
            scheme,
            "mod-procedure",
            printModProc,
            freeModProc,
            equalModProc,
            markModProc,
            applyModProc,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr);
    }
    s7_define_function(
        scheme,
        "as-caller-role",
        g_as_caller_role,
        1,
        0,
        false,
        "(as-caller-role proc) proc, wrapped to run with the Mod role when mod code calls this");
}

ScriptPlayerStack& scriptPlayerStack() {
    static ScriptPlayerStack stack;
    return stack;
}

ScriptCallGuard::ScriptCallGuard(s7_scheme* scheme, PackageRole role)
    : scheme_(scheme)
    , role_(role)
    , roleGuard_(role)
    , player_(scriptPlayerStack().current)
    , playerDepth_(scriptPlayerStack().saved.size())
    , playerFloor_(scriptPlayerStack().floor) {
    scriptPlayerStack().floor = playerDepth_;
    if (scheme_ == nullptr) {
        return;
    }
    if (role_ == PackageRole::Mod) {
        if (g_modCallDepth++ == 0) {
            takeSnapshot(scheme_);
        }
    } else {
        restoreBaseBindings(scheme_);
    }
}

ScriptCallGuard::~ScriptCallGuard() {
    ScriptPlayerStack& players = scriptPlayerStack();
    players.saved.resize(std::min(players.saved.size(), playerDepth_));
    players.current = std::move(player_);
    players.floor = playerFloor_;
    if (scheme_ == nullptr) {
        return;
    }
    if (role_ == PackageRole::Mod) {
        restoreBaseBindings(scheme_);
        if (--g_modCallDepth == 0) {
            releaseSnapshot(scheme_);
        }
    } else {
        recaptureBaseBindings(scheme_);
    }
}

}
