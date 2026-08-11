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

/**
 * Binds (package-load-data), (package-load-script), (current-package-id), and
 * (package-mounted?) only. Split out from bindSaveApi so it can be bound before any
 * script asset loads, letting scripts.s7/things.s7/etc (loaded during App::init_script,
 * before the rest of bindSaveApi is available) use (require ...) from lang.s7.
 */
void bindPackageApi(AssetStore& assets, s7_scheme* scheme);

/** Binds (startup-arg) / (startup-args) from the parsed package CLI map. */
void bindStartupApi(s7_scheme* scheme, const std::unordered_map<std::string, std::string>& args);

/** Calls (on-map-ready map-id reason) when defined in Scheme. */
void callOnMapReady(flecs::world& world, std::string_view mapId, std::string_view reason);

/** Calls (on-startup) when defined in Scheme. */
void callOnStartup(flecs::world& world);

}
