#include "script/net_script.hpp"

#include "core/log.hpp"
#include "net/net_state.hpp"
#include "script/s7_read.hpp"
#include "script/script_scope.hpp"

#include <s7.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace slopengine {

namespace {

flecs::world* g_netWorld = nullptr;

s7_pointer netError(s7_scheme* sc, const std::string& message) {
    return s7_error(sc, s7_make_symbol(sc, "net"), s7_list(sc, 1, s7_make_string(sc, message.c_str())));
}

bool readTarget(s7_pointer value, NetTarget& out) {
    if (s7_is_symbol(value) && std::string_view(s7_symbol_name(value)) == "session") {
        out = NetTarget{NetScopeKind::Session, 0, {}};
        return true;
    }
    if (!s7_is_string(value)) {
        return false;
    }
    return resolveNetTarget(*g_netWorld, s7_string(value), out);
}

std::string targetText(s7_scheme* sc, s7_pointer value) {
    char* text = s7_object_to_c_string(sc, value);
    std::string out = text != nullptr ? text : "?";
    std::free(text);
    return out;
}

s7_pointer fieldAlist(s7_scheme* sc, const std::vector<std::pair<const NetField*, const NetValue*>>& fields) {
    s7_pointer list = s7_nil(sc);
    for (auto it = fields.rbegin(); it != fields.rend(); ++it) {
        if (!it->first->save) {
            continue;
        }
        list = s7_cons(
            sc, s7_cons(sc, s7_make_symbol(sc, it->first->name.c_str()), netValueToScheme(sc, *it->second)), list);
    }
    return list;
}

int restoreFields(s7_scheme* sc, const NetTarget& target, s7_pointer entries) {
    int applied = 0;
    for (s7_pointer cursor = entries; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        const s7_pointer entry = s7_car(cursor);
        if (!s7_is_pair(entry) || !s7_is_symbol(s7_car(entry))) {
            continue;
        }
        const NetField* field = nullptr;
        NetValue* slot = findNetValue(*g_netWorld, target, s7_symbol_name(s7_car(entry)), &field);
        if (slot == nullptr || !field->save) {
            continue;
        }
        NetValue value;
        std::string error;
        if (!netValueFromScheme(sc, s7_cdr(entry), field->type, value, error)) {
            Log::warning(LogCategory::Script, "NET: saved %s skipped, %s", field->name.c_str(), error.c_str());
            continue;
        }
        *slot = std::move(value);
        ++applied;
    }
    return applied;
}

s7_pointer g_net_get(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "net-get", args);
    const s7_pointer id = in.any("player id, thing id or 'session");
    const char* name = in.name("field symbol");
    if (!in.ok()) {
        return in.error();
    }
    NetTarget target;
    if (g_netWorld == nullptr || !readTarget(id, target)) {
        return s7_f(sc);
    }
    const NetValue* value = findNetValue(*g_netWorld, target, name, nullptr);
    if (value == nullptr) {
        return netError(sc, std::string("net-get: ") + targetText(sc, id) + " has no declared field " + name);
    }
    return netValueToScheme(sc, *value);
}

s7_pointer g_net_set(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "net-set!", args);
    const s7_pointer id = in.any("player id, thing id or 'session");
    const char* name = in.name("field symbol");
    const s7_pointer raw = in.any("value");
    if (!in.ok()) {
        return in.error();
    }
    NetTarget target;
    if (g_netWorld == nullptr || !readTarget(id, target)) {
        return s7_f(sc);
    }
    const NetField* field = nullptr;
    NetValue* slot = findNetValue(*g_netWorld, target, name, &field);
    if (slot == nullptr) {
        return netError(sc, std::string("net-set!: ") + targetText(sc, id) + " has no declared field " + name);
    }
    NetValue value;
    std::string error;
    if (!netValueFromScheme(sc, raw, field->type, value, error)) {
        return netError(sc, "net-set!: " + field->name + " " + error);
    }
    *slot = std::move(value);
    return s7_t(sc);
}

s7_pointer g_net_reset(s7_scheme* sc, s7_pointer) {
    if (g_netWorld != nullptr) {
        resetNetState(*g_netWorld);
    }
    return s7_t(sc);
}

s7_pointer g_net_save_state(s7_scheme* sc, s7_pointer) {
    if (g_netWorld == nullptr || !g_netWorld->has<NetStore>()) {
        return s7_nil(sc);
    }
    s7_gc_on(sc, false);
    const NetTarget session{NetScopeKind::Session, 0, {}};
    s7_pointer players = s7_nil(sc);
    const std::size_t playerCount = g_netWorld->get<NetStore>().players.size();
    for (std::size_t i = playerCount; i > 0; --i) {
        const NetTarget player{NetScopeKind::Player, i - 1, {}};
        const std::string& id = g_netWorld->get<NetStore>().players[i - 1].id;
        players = s7_cons(
            sc,
            s7_cons(
                sc,
                s7_make_symbol(sc, "player"),
                s7_cons(sc, s7_make_string(sc, id.c_str()), fieldAlist(sc, netTargetFields(*g_netWorld, player)))),
            players);
    }
    const s7_pointer result = s7_cons(
        sc,
        s7_cons(sc, s7_make_symbol(sc, "session"), fieldAlist(sc, netTargetFields(*g_netWorld, session))),
        players);
    s7_gc_on(sc, true);
    return result;
}

s7_pointer g_net_restore_state(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "net-restore-state!", args);
    const s7_pointer form = in.list("state list from net-save-state");
    if (!in.ok()) {
        return in.error();
    }
    if (g_netWorld == nullptr) {
        return s7_f(sc);
    }
    int applied = 0;
    for (s7_pointer cursor = form; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        const s7_pointer entry = s7_car(cursor);
        if (!s7_is_pair(entry) || !s7_is_symbol(s7_car(entry))) {
            continue;
        }
        const std::string_view kind = s7_symbol_name(s7_car(entry));
        if (kind == "session") {
            applied += restoreFields(sc, NetTarget{NetScopeKind::Session, 0, {}}, s7_cdr(entry));
        } else if (kind == "player" && s7_is_pair(s7_cdr(entry)) && s7_is_string(s7_cadr(entry))) {
            NetTarget target;
            if (resolveNetTarget(*g_netWorld, s7_string(s7_cadr(entry)), target) &&
                target.kind == NetScopeKind::Player) {
                applied += restoreFields(sc, target, s7_cddr(entry));
            }
        }
    }
    return s7_make_integer(sc, applied);
}

s7_pointer g_net_schema_hash(s7_scheme* sc, s7_pointer) {
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(netSchema().hash()));
    return s7_make_string(sc, text);
}

constexpr SchemeBinding kNetBindings[] = {
    {"net-get", g_net_get, 2, 0, false, "(net-get id field)", ScriptCap::ReadWorld},
    {"net-set!", g_net_set, 3, 0, false, "(net-set! id field value)", ScriptCap::WorldMutate},
    {"net-reset!", g_net_reset, 0, 0, false, "(net-reset!)", ScriptCap::WorldMutate},
    {"net-save-state", g_net_save_state, 0, 0, false, "(net-save-state)", ScriptCap::ReadWorld},
    {"net-restore-state!", g_net_restore_state, 1, 0, false, "(net-restore-state! state)", ScriptCap::WorldMutate},
    {"net-schema-hash", g_net_schema_hash, 0, 0, false, "(net-schema-hash)", ScriptCap::ReadWorld},
};

} // namespace

void bindNetApi(flecs::world& world, s7_scheme* scheme) {
    g_netWorld = &world;
    if (scheme == nullptr) {
        return;
    }
    registerBindings(scheme, kNetBindings);
}

}
