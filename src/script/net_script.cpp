#include "script/net_script.hpp"

#include "core/log.hpp"
#include "net/net_state.hpp"
#include "script/proc_role.hpp"
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
    NetGcPause pause(sc);
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

s7_pointer whereValue(s7_scheme* sc, const NetEventMessage& event) {
    switch (event.where) {
    case NetEventWhere::Point:
        return s7_list(
            sc,
            3,
            s7_make_real(sc, event.point[0]),
            s7_make_real(sc, event.point[1]),
            s7_make_real(sc, event.point[2]));
    case NetEventWhere::Player:
        return s7_make_string(sc, event.player.c_str());
    case NetEventWhere::None:
        break;
    }
    return s7_f(sc);
}

bool readWhere(s7_scheme* sc, s7_pointer value, NetEventMessage& out) {
    if (value == s7_f(sc)) {
        out.where = NetEventWhere::None;
        return true;
    }
    if (s7_is_string(value)) {
        out.where = NetEventWhere::Player;
        out.player = s7_string(value);
        return out.player.size() <= 128;
    }
    if (!s7_is_pair(value)) {
        return false;
    }
    out.where = NetEventWhere::Point;
    s7_pointer cursor = value;
    for (int i = 0; i < 3; ++i) {
        if (!s7_is_pair(cursor) || !s7_is_number(s7_car(cursor)) || !s7_is_real(s7_car(cursor))) {
            return false;
        }
        out.point[i] = static_cast<float>(s7_number_to_real(sc, s7_car(cursor)));
        cursor = s7_cdr(cursor);
    }
    return s7_is_null(sc, cursor);
}

s7_pointer g_net_event(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "net-event!", args);
    const char* name = in.name("event symbol");
    const s7_pointer where = in.any("point (x y z), player id or #f");
    if (!in.ok()) {
        return in.error();
    }
    const NetEventDef* def = netSchema().findEvent(name);
    if (def == nullptr) {
        return netError(sc, std::string("net-event!: no declared event ") + name);
    }
    NetEventMessage event;
    event.eventId = static_cast<std::uint32_t>(def->id);
    if (!readWhere(sc, where, event)) {
        return netError(sc, "net-event!: " + def->name + " needs a point (x y z), a player id or #f");
    }
    s7_pointer cursor = in.rest();
    for (const NetEventArg& arg : def->args) {
        if (!s7_is_pair(cursor)) {
            return netError(sc, "net-event!: " + def->name + " is missing argument " + arg.name);
        }
        NetValue value;
        std::string error;
        if (!netValueFromScheme(sc, s7_car(cursor), arg.type, value, error)) {
            return netError(sc, "net-event!: " + def->name + " " + arg.name + " " + error);
        }
        event.args.push_back(std::move(value));
        cursor = s7_cdr(cursor);
    }
    if (!s7_is_null(sc, cursor)) {
        return netError(sc, "net-event!: " + def->name + " takes " + std::to_string(def->args.size()) + " arguments");
    }
    if (def->to != NetEventAudience::Owner || event.where != NetEventWhere::Player ||
        event.player == kLocalNetPlayerId) {
        deliverNetEvent(sc, event);
    }
    if (g_netWorld != nullptr && g_netWorld->has<NetEventQueue>()) {
        NetEventQueue& queue = g_netWorld->get_mut<NetEventQueue>();
        if (queue.forward) {
            queue.pending.push_back(std::move(event));
        }
    }
    return s7_t(sc);
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
    {"net-event!", g_net_event, 2, 0, true, "(net-event! event where arg ...)", ScriptCap::WorldMutate},
};

} // namespace

void deliverNetEvent(s7_scheme* scheme, const NetEventMessage& event) {
    if (scheme == nullptr || event.eventId >= netSchema().events().size()) {
        return;
    }
    const NetEventDef& def = netSchema().events()[event.eventId];
    const std::string procName = "on-event-" + def.name;
    const s7_pointer proc = s7_name_to_value(scheme, procName.c_str());
    if (!s7_is_procedure(proc)) {
        return;
    }
    s7_pointer list = s7_nil(scheme);
    unsigned int protectedAt = 0;
    {
        NetGcPause pause(scheme);
        for (auto it = event.args.rbegin(); it != event.args.rend(); ++it) {
            list = s7_cons(scheme, netValueToScheme(scheme, *it), list);
        }
        list = s7_cons(scheme, whereValue(scheme, event), list);
        protectedAt = s7_gc_protect(scheme, list);
    }
    ScriptScopeGuard scope(ScriptScope::World);
    ScriptCallGuard guard(scheme, roleForProc(procName));
    s7_call(scheme, proc, list);
    s7_gc_unprotect_at(scheme, protectedAt);
}

void bindNetApi(flecs::world& world, s7_scheme* scheme) {
    g_netWorld = &world;
    if (scheme == nullptr) {
        return;
    }
    registerBindings(scheme, kNetBindings);
}

}
