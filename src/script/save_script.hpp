#pragma once

#include <flecs.h>

#include <string>
#include <string_view>
#include <unordered_map>

struct s7_scheme;

namespace slopengine {

class AssetStore;

/** Binds save I/O, map load, player pose, save-list, and package load helpers. */
void bindSaveApi(flecs::world& world, AssetStore& assets, s7_scheme* scheme);

/** Binds (startup-arg) / (startup-args) from the parsed package CLI map. */
void bindStartupApi(s7_scheme* scheme, const std::unordered_map<std::string, std::string>& args);

/** Calls (on-map-ready map-id reason) when defined in Scheme. */
void callOnMapReady(flecs::world& world, std::string_view mapId, std::string_view reason);

/** Calls (on-startup) when defined in Scheme. */
void callOnStartup(flecs::world& world);

}
