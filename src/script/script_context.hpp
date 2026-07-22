#pragma once

struct s7_scheme;

namespace slopengine {

/** Flecs world singleton that exposes the shared s7 Scheme runtime to systems.
 *  @ingroup script_components
 */
struct ScriptContext {
    s7_scheme* scheme = nullptr;
};

}
