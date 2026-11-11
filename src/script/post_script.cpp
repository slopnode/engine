#include "script/post_script.hpp"

#include "assets/asset_services.hpp"
#include "render/post_process.hpp"
#include "script/s7_read.hpp"
#include "script/script_scope.hpp"

#include <cstring>
#include <string>

#include <s7.h>

namespace slopengine {

namespace {

flecs::world* g_postWorld = nullptr;

PostProcessState* postState() {
    if (g_postWorld == nullptr) {
        return nullptr;
    }
    return &ensurePostProcessState(*g_postWorld);
}

AssetStore* postAssets() {
    if (g_postWorld == nullptr || !g_postWorld->has<AssetServices>() ||
        g_postWorld->get<AssetServices>().store == nullptr) {
        return nullptr;
    }
    return g_postWorld->get_mut<AssetServices>().store;
}

bool readTarget(ArgReader& in, PostPassTarget& out) {
    const char* descr = "target symbol ('scene, 'hud, or 'both)";
    if (!in.nextIsSymbol()) {
        in.wrongType(descr);
        return false;
    }
    const int index = in.index();
    const s7_pointer value = in.any();
    const char* name = s7_symbol_name(value);
    if (name != nullptr && std::strcmp(name, "scene") == 0) {
        out = PostPassTarget::Scene;
        return true;
    }
    if (name != nullptr && std::strcmp(name, "hud") == 0) {
        out = PostPassTarget::Hud;
        return true;
    }
    if (name != nullptr && std::strcmp(name, "both") == 0) {
        out = PostPassTarget::Both;
        return true;
    }
    in.wrongTypeAt(index, value, descr);
    return false;
}

PostPassHandle readHandle(ArgReader& in) {
    return static_cast<PostPassHandle>(in.exactInteger("integer handle"));
}

s7_pointer g_post_push_shader(s7_scheme* sc, s7_pointer args) {
    PostProcessState* state = postState();
    AssetStore* assets = postAssets();
    if (state == nullptr || assets == nullptr) {
        return s7_make_integer(sc, 0);
    }
    ArgReader in(sc, "post-push-shader", args);
    const char* path = in.string("string path");
    if (path == nullptr || path[0] == '\0') {
        return s7_make_integer(sc, 0);
    }
    PostPassTarget target = PostPassTarget::Scene;
    if (!readTarget(in, target)) {
        return s7_make_integer(sc, 0);
    }
    const PostPassHandle handle = pushPostShader(*state, *assets, target, path);
    return s7_make_integer(sc, static_cast<s7_int>(handle));
}

s7_pointer g_post_remove_shader(s7_scheme* sc, s7_pointer args) {
    PostProcessState* state = postState();
    if (state == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "post-remove-shader", args);
    const PostPassHandle handle = readHandle(in);
    return s7_make_boolean(sc, removePostShader(*state, handle));
}

s7_pointer g_post_clear_shaders(s7_scheme* sc, s7_pointer args) {
    PostProcessState* state = postState();
    if (state == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "post-clear-shaders", args);
    PostPassTarget target = PostPassTarget::Scene;
    if (!readTarget(in, target)) {
        return s7_f(sc);
    }
    return s7_make_boolean(sc, clearPostShaders(*state, target));
}

s7_pointer g_post_set_enabled(s7_scheme* sc, s7_pointer args) {
    PostProcessState* state = postState();
    if (state == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "post-set-enabled", args);
    const PostPassHandle handle = readHandle(in);
    const bool enabled = in.boolean("boolean");
    return s7_make_boolean(sc, setPostPassEnabled(*state, handle, enabled));
}

s7_pointer setUniform(s7_scheme* sc, s7_pointer args, const char* caller, PostUniformKind kind, int count) {
    PostProcessState* state = postState();
    if (state == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, caller, args);
    const PostPassHandle handle = readHandle(in);
    const std::string name = in.name("string or symbol name");
    if (name.empty()) {
        return s7_f(sc);
    }
    PostUniformValue value{};
    value.kind = kind;
    for (int i = 0; i < count; ++i) {
        value.data[i] = in.realf("number");
    }
    return s7_make_boolean(sc, setPostPassUniform(*state, handle, name, value));
}

s7_pointer g_post_set_float(s7_scheme* sc, s7_pointer args) {
    return setUniform(sc, args, "post-set-float", PostUniformKind::Float, 1);
}

s7_pointer g_post_set_vec2(s7_scheme* sc, s7_pointer args) {
    return setUniform(sc, args, "post-set-vec2", PostUniformKind::Vec2, 2);
}

s7_pointer g_post_set_vec3(s7_scheme* sc, s7_pointer args) {
    return setUniform(sc, args, "post-set-vec3", PostUniformKind::Vec3, 3);
}

s7_pointer g_post_set_vec4(s7_scheme* sc, s7_pointer args) {
    return setUniform(sc, args, "post-set-vec4", PostUniformKind::Vec4, 4);
}

constexpr SchemeBinding kPostBindings[] = {
    {"post-push-shader", g_post_push_shader, 2, 0, false, "(post-push-shader path target) target is 'scene, 'hud, or 'both", ScriptCap::FpPresent},
    {"post-remove-shader", g_post_remove_shader, 1, 0, false, "(post-remove-shader handle)", ScriptCap::FpPresent},
    {"post-clear-shaders", g_post_clear_shaders, 1, 0, false, "(post-clear-shaders target)", ScriptCap::FpPresent},
    {"post-set-enabled", g_post_set_enabled, 2, 0, false, "(post-set-enabled handle enabled)", ScriptCap::FpPresent},
    {"post-set-float", g_post_set_float, 3, 0, false, "(post-set-float handle name x)", ScriptCap::FpPresent},
    {"post-set-vec2", g_post_set_vec2, 4, 0, false, "(post-set-vec2 handle name x y)", ScriptCap::FpPresent},
    {"post-set-vec3", g_post_set_vec3, 5, 0, false, "(post-set-vec3 handle name x y z)", ScriptCap::FpPresent},
    {"post-set-vec4", g_post_set_vec4, 6, 0, false, "(post-set-vec4 handle name x y z w)", ScriptCap::FpPresent},
};

} // namespace

void bindPostProcessApi(flecs::world& world, s7_scheme* scheme) {
    g_postWorld = &world;
    if (scheme == nullptr) {
        return;
    }
    registerBindings(scheme, kPostBindings);
}

}
