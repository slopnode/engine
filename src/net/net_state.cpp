#include "net/net_state.hpp"

#include "map/thing.hpp"
#include "map/thing_def_registry.hpp"

#include <algorithm>
#include <charconv>

namespace slopengine {

namespace {

std::vector<NetValue> defaultsFor(const NetScope& scope) {
    std::vector<NetValue> values;
    for (const NetField* field : netSchema().fieldsIn(scope)) {
        values.push_back(field->defaultValue);
    }
    return values;
}

std::vector<NetScope> thingScopes(flecs::entity thing) {
    std::vector<NetScope> scopes;
    if (!thing.has<ThingTypeRef>()) {
        return scopes;
    }
    const ThingDef* def = thingDefRegistry().find(thing.get<ThingTypeRef>().type);
    if (def == nullptr) {
        return scopes;
    }
    for (const std::string& tag : def->tags) {
        scopes.push_back(NetScope{NetScopeKind::Thing, tag});
    }
    return scopes;
}

const NetField* thingField(flecs::entity thing, std::string_view name) {
    for (const NetScope& scope : thingScopes(thing)) {
        if (const NetField* field = netSchema().find(scope, name)) {
            return field;
        }
    }
    return nullptr;
}

NetThingFields& thingFields(flecs::entity thing) {
    if (!thing.has<NetThingFields>()) {
        NetThingFields fields{};
        for (const NetScope& scope : thingScopes(thing)) {
            for (const NetField* field : netSchema().fieldsIn(scope)) {
                if (std::find(fields.fieldIds.begin(), fields.fieldIds.end(), field->id) != fields.fieldIds.end()) {
                    continue;
                }
                fields.fieldIds.push_back(field->id);
                fields.values.push_back(field->defaultValue);
            }
        }
        thing.set<NetThingFields>(std::move(fields));
    }
    return thing.get_mut<NetThingFields>();
}

flecs::entity lookupThingEntity(const flecs::world& world, std::string_view id) {
    const std::string key(id);
    flecs::entity entity = world.lookup(key.c_str());
    if (entity.is_valid()) {
        return entity;
    }
    std::uint64_t raw = 0;
    const auto [end, ec] = std::from_chars(id.data(), id.data() + id.size(), raw);
    if (ec != std::errc{} || end != id.data() + id.size() || raw == 0) {
        return {};
    }
    const flecs::entity byId = world.entity(static_cast<flecs::entity_t>(raw));
    return byId.is_alive() ? byId : flecs::entity{};
}

} // namespace

void registerNetState(flecs::world& world) {
    world.component<NetStore>();
    world.component<NetThingFields>();
    world.component<NetEventQueue>();
    world.set<NetEventQueue>({});
    NetStore store{};
    store.players.push_back(NetPlayerRecord{kLocalNetPlayerId, defaultsFor(NetScope{NetScopeKind::Player, {}})});
    store.session = defaultsFor(NetScope{NetScopeKind::Session, {}});
    world.set<NetStore>(std::move(store));
}

void resetNetState(flecs::world& world) {
    if (!world.has<NetStore>()) {
        return;
    }
    NetStore& store = world.get_mut<NetStore>();
    for (NetPlayerRecord& player : store.players) {
        player.values = defaultsFor(NetScope{NetScopeKind::Player, {}});
    }
    store.session = defaultsFor(NetScope{NetScopeKind::Session, {}});
}

void addNetPlayer(flecs::world& world, std::string_view id) {
    if (!world.has<NetStore>() || findNetPlayer(world, id) != nullptr) {
        return;
    }
    world.get_mut<NetStore>().players.push_back(
        NetPlayerRecord{std::string(id), defaultsFor(NetScope{NetScopeKind::Player, {}})});
}

void removeNetPlayer(flecs::world& world, std::string_view id) {
    if (!world.has<NetStore>() || id == kLocalNetPlayerId) {
        return;
    }
    std::vector<NetPlayerRecord>& players = world.get_mut<NetStore>().players;
    players.erase(
        std::remove_if(players.begin(), players.end(), [&](const NetPlayerRecord& record) { return record.id == id; }),
        players.end());
}

void resetNetPlayer(flecs::world& world, std::string_view id) {
    if (!world.has<NetStore>()) {
        return;
    }
    for (NetPlayerRecord& record : world.get_mut<NetStore>().players) {
        if (record.id == id) {
            record.values = defaultsFor(NetScope{NetScopeKind::Player, {}});
        }
    }
}

const NetPlayerRecord* findNetPlayer(const flecs::world& world, std::string_view id) {
    if (!world.has<NetStore>()) {
        return nullptr;
    }
    for (const NetPlayerRecord& record : world.get<NetStore>().players) {
        if (record.id == id) {
            return &record;
        }
    }
    return nullptr;
}

void resetNetMapFields(flecs::world& world) {
    if (!world.has<NetStore>()) {
        return;
    }
    NetStore& store = world.get_mut<NetStore>();
    const std::vector<const NetField*> fields = netSchema().fieldsIn(NetScope{NetScopeKind::Player, {}});
    for (NetPlayerRecord& player : store.players) {
        for (const NetField* field : fields) {
            if (!field->travel && static_cast<std::size_t>(field->slot) < player.values.size()) {
                player.values[static_cast<std::size_t>(field->slot)] = field->defaultValue;
            }
        }
    }
}

bool resolveNetTarget(const flecs::world& world, std::string_view id, NetTarget& out) {
    if (!world.has<NetStore>()) {
        return false;
    }
    const NetStore& store = world.get<NetStore>();
    for (std::size_t i = 0; i < store.players.size(); ++i) {
        if (store.players[i].id == id) {
            out = NetTarget{NetScopeKind::Player, i, {}};
            return true;
        }
    }
    const flecs::entity thing = lookupThingEntity(world, id);
    if (!thing.is_valid() || !thing.has<ThingTypeRef>()) {
        return false;
    }
    out = NetTarget{NetScopeKind::Thing, 0, thing};
    return true;
}

NetValue* findNetValue(flecs::world& world, const NetTarget& target, std::string_view name, const NetField** fieldOut) {
    if (!world.has<NetStore>()) {
        return nullptr;
    }
    NetStore& store = world.get_mut<NetStore>();
    const NetField* field = nullptr;
    NetValue* value = nullptr;
    switch (target.kind) {
    case NetScopeKind::Session:
        field = netSchema().find(NetScope{NetScopeKind::Session, {}}, name);
        if (field != nullptr && static_cast<std::size_t>(field->slot) < store.session.size()) {
            value = &store.session[static_cast<std::size_t>(field->slot)];
        }
        break;
    case NetScopeKind::Player:
        field = netSchema().find(NetScope{NetScopeKind::Player, {}}, name);
        if (field != nullptr && target.player < store.players.size()) {
            std::vector<NetValue>& values = store.players[target.player].values;
            if (static_cast<std::size_t>(field->slot) < values.size()) {
                value = &values[static_cast<std::size_t>(field->slot)];
            }
        }
        break;
    case NetScopeKind::Thing:
        if (!target.thing.is_alive()) {
            break;
        }
        field = thingField(target.thing, name);
        if (field != nullptr) {
            NetThingFields& fields = thingFields(target.thing);
            const auto it = std::find(fields.fieldIds.begin(), fields.fieldIds.end(), field->id);
            if (it != fields.fieldIds.end()) {
                value = &fields.values[static_cast<std::size_t>(it - fields.fieldIds.begin())];
            }
        }
        break;
    }
    if (fieldOut != nullptr) {
        *fieldOut = value != nullptr ? field : nullptr;
    }
    return value;
}

std::vector<std::pair<const NetField*, const NetValue*>> netTargetFields(flecs::world& world, const NetTarget& target) {
    std::vector<std::pair<const NetField*, const NetValue*>> out;
    std::vector<const NetField*> fields;
    if (target.kind == NetScopeKind::Thing) {
        for (const NetScope& scope : thingScopes(target.thing)) {
            for (const NetField* field : netSchema().fieldsIn(scope)) {
                fields.push_back(field);
            }
        }
    } else {
        fields = netSchema().fieldsIn(NetScope{target.kind, {}});
    }
    for (const NetField* field : fields) {
        const NetField* resolved = nullptr;
        const NetValue* value = findNetValue(world, target, field->name, &resolved);
        if (value != nullptr && resolved == field) {
            out.emplace_back(field, value);
        }
    }
    return out;
}

}
