#pragma once

struct s7_scheme;

namespace slopengine {

/** Flecs world singleton that exposes the shared s7 Scheme runtime to systems. */
struct ScriptContext {
    s7_scheme* scheme = nullptr;
};

}
