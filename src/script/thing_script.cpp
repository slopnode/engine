#include "script/thing_script.hpp"

#include <cstring>
#include <string>
#include <vector>

#include <s7.h>

namespace slopengine {

namespace {

flecs::world* g_thingWorld = nullptr;

struct ThingDespawnQueue {
    std::vector<std::string> ids;
};

bool isProtectedThingId(const char* id) {
    return std::strcmp(id, "Player") == 0 || std::strcmp(id, "MapStatic") == 0;
}

s7_pointer g_thing_despawn(s7_scheme* sc, s7_pointer args) {
    if (g_thingWorld == nullptr || !s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "thing-despawn", 1, args, "thing-id string");
    }

    const char* id = s7_string(s7_car(args));
    if (isProtectedThingId(id)) {
        return s7_f(sc);
    }

    flecs::entity entity = g_thingWorld->lookup(id);
    if (!entity.is_alive()) {
        return s7_f(sc);
    }

    if (!g_thingWorld->has<ThingDespawnQueue>()) {
        g_thingWorld->set<ThingDespawnQueue>({});
    }
    g_thingWorld->get_mut<ThingDespawnQueue>().ids.emplace_back(id);
    return s7_t(sc);
}

void registerFlushThingDespawns(flecs::world& world) {
    world.system("FlushThingDespawns")
        .kind(flecs::OnUpdate)
        .run([](flecs::iter& it) {
            flecs::world world = it.world();
            if (!world.has<ThingDespawnQueue>()) {
                return;
            }

            ThingDespawnQueue& queue = world.get_mut<ThingDespawnQueue>();
            if (queue.ids.empty()) {
                return;
            }

            for (const std::string& id : queue.ids) {
                if (isProtectedThingId(id.c_str())) {
                    continue;
                }
                flecs::entity entity = world.lookup(id.c_str());
                if (entity.is_alive()) {
                    entity.destruct();
                }
            }
            queue.ids.clear();
        });
}

} // namespace

void bindThingRuntimeApi(flecs::world& world, s7_scheme* scheme) {
    g_thingWorld = &world;
    if (!world.has<ThingDespawnQueue>()) {
        world.set<ThingDespawnQueue>({});
    }
    registerFlushThingDespawns(world);

    if (scheme == nullptr) {
        return;
    }

    s7_define_function(
        scheme,
        "thing-despawn",
        g_thing_despawn,
        1,
        0,
        false,
        "(thing-despawn id)");
}

}
