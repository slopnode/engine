#include "script/post_script.hpp"

#include "assets/asset_services.hpp"
#include "render/post_process.hpp"
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

bool parseUniformName(s7_scheme* sc, s7_pointer value, const char* caller, std::string& out) {
    if (s7_is_string(value)) {
        out.assign(s7_string(value));
        return !out.empty();
    }
    if (s7_is_symbol(value)) {
        const char* name = s7_symbol_name(value);
        if (name == nullptr || name[0] == '\0') {
            return false;
        }
        out.assign(name);
        return true;
    }
    s7_wrong_type_arg_error(sc, caller, 1, value, "string or symbol name");
    return false;
}

bool parseTarget(s7_scheme* sc, s7_pointer value, const char* caller, int argPos, PostPassTarget& out) {
    if (!s7_is_symbol(value)) {
        s7_wrong_type_arg_error(sc, caller, argPos, value, "target symbol ('scene, 'hud, or 'both)");
        return false;
    }
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
    s7_wrong_type_arg_error(sc, caller, argPos, value, "target symbol ('scene, 'hud, or 'both)");
    return false;
}

bool parseHandle(s7_scheme* sc, s7_pointer value, const char* caller, int argPos, PostPassHandle& out) {
    if (!s7_is_integer(value)) {
        s7_wrong_type_arg_error(sc, caller, argPos, value, "integer handle");
        return false;
    }
    out = static_cast<PostPassHandle>(s7_integer(value));
    return true;
}

s7_pointer g_post_push_shader(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::FpPresent)) {
        return s7_make_integer(sc, 0);
    }
    PostProcessState* state = postState();
    AssetStore* assets = postAssets();
    if (state == nullptr || assets == nullptr) {
        return s7_make_integer(sc, 0);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "post-push-shader", 1, args, "string path");
    }
    const char* path = s7_string(s7_car(args));
    if (path == nullptr || path[0] == '\0') {
        return s7_make_integer(sc, 0);
    }
    s7_pointer rest = s7_cdr(args);
    if (!s7_is_pair(rest)) {
        return s7_wrong_type_arg_error(sc, "post-push-shader", 2, rest, "target symbol");
    }
    PostPassTarget target = PostPassTarget::Scene;
    if (!parseTarget(sc, s7_car(rest), "post-push-shader", 2, target)) {
        return s7_make_integer(sc, 0);
    }
    const PostPassHandle handle = pushPostShader(*state, *assets, target, path);
    return s7_make_integer(sc, static_cast<s7_int>(handle));
}

s7_pointer g_post_remove_shader(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::FpPresent)) {
        return s7_f(sc);
    }
    PostProcessState* state = postState();
    if (state == nullptr) {
        return s7_f(sc);
    }
    PostPassHandle handle = 0;
    if (!parseHandle(sc, s7_car(args), "post-remove-shader", 1, handle)) {
        return s7_f(sc);
    }
    return removePostShader(*state, handle) ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_post_clear_shaders(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::FpPresent)) {
        return s7_f(sc);
    }
    PostProcessState* state = postState();
    if (state == nullptr) {
        return s7_f(sc);
    }
    PostPassTarget target = PostPassTarget::Scene;
    if (!parseTarget(sc, s7_car(args), "post-clear-shaders", 1, target)) {
        return s7_f(sc);
    }
    return clearPostShaders(*state, target) ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_post_set_enabled(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::FpPresent)) {
        return s7_f(sc);
    }
    PostProcessState* state = postState();
    if (state == nullptr) {
        return s7_f(sc);
    }
    PostPassHandle handle = 0;
    if (!parseHandle(sc, s7_car(args), "post-set-enabled", 1, handle)) {
        return s7_f(sc);
    }
    s7_pointer rest = s7_cdr(args);
    if (!s7_is_pair(rest) || !s7_is_boolean(s7_car(rest))) {
        return s7_wrong_type_arg_error(sc, "post-set-enabled", 2, rest, "boolean");
    }
    return setPostPassEnabled(*state, handle, s7_boolean(sc, s7_car(rest))) ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_post_set_float(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::FpPresent)) {
        return s7_f(sc);
    }
    PostProcessState* state = postState();
    if (state == nullptr) {
        return s7_f(sc);
    }
    PostPassHandle handle = 0;
    if (!parseHandle(sc, s7_car(args), "post-set-float", 1, handle)) {
        return s7_f(sc);
    }
    s7_pointer rest = s7_cdr(args);
    std::string name;
    if (!s7_is_pair(rest) || !parseUniformName(sc, s7_car(rest), "post-set-float", name)) {
        return s7_f(sc);
    }
    rest = s7_cdr(rest);
    if (!s7_is_pair(rest) || !s7_is_number(s7_car(rest))) {
        return s7_wrong_type_arg_error(sc, "post-set-float", 3, rest, "number");
    }
    PostUniformValue value{};
    value.kind = PostUniformKind::Float;
    value.data[0] = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
    return setPostPassUniform(*state, handle, name, value) ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_post_set_vec2(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::FpPresent)) {
        return s7_f(sc);
    }
    PostProcessState* state = postState();
    if (state == nullptr) {
        return s7_f(sc);
    }
    PostPassHandle handle = 0;
    if (!parseHandle(sc, s7_car(args), "post-set-vec2", 1, handle)) {
        return s7_f(sc);
    }
    s7_pointer rest = s7_cdr(args);
    std::string name;
    if (!s7_is_pair(rest) || !parseUniformName(sc, s7_car(rest), "post-set-vec2", name)) {
        return s7_f(sc);
    }
    rest = s7_cdr(rest);
    float comps[2]{};
    for (int i = 0; i < 2; ++i) {
        if (!s7_is_pair(rest) || !s7_is_number(s7_car(rest))) {
            return s7_wrong_type_arg_error(sc, "post-set-vec2", i + 3, rest, "number");
        }
        comps[i] = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
        rest = s7_cdr(rest);
    }
    PostUniformValue value{};
    value.kind = PostUniformKind::Vec2;
    value.data[0] = comps[0];
    value.data[1] = comps[1];
    return setPostPassUniform(*state, handle, name, value) ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_post_set_vec3(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::FpPresent)) {
        return s7_f(sc);
    }
    PostProcessState* state = postState();
    if (state == nullptr) {
        return s7_f(sc);
    }
    PostPassHandle handle = 0;
    if (!parseHandle(sc, s7_car(args), "post-set-vec3", 1, handle)) {
        return s7_f(sc);
    }
    s7_pointer rest = s7_cdr(args);
    std::string name;
    if (!s7_is_pair(rest) || !parseUniformName(sc, s7_car(rest), "post-set-vec3", name)) {
        return s7_f(sc);
    }
    rest = s7_cdr(rest);
    float comps[3]{};
    for (int i = 0; i < 3; ++i) {
        if (!s7_is_pair(rest) || !s7_is_number(s7_car(rest))) {
            return s7_wrong_type_arg_error(sc, "post-set-vec3", i + 3, rest, "number");
        }
        comps[i] = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
        rest = s7_cdr(rest);
    }
    PostUniformValue value{};
    value.kind = PostUniformKind::Vec3;
    value.data[0] = comps[0];
    value.data[1] = comps[1];
    value.data[2] = comps[2];
    return setPostPassUniform(*state, handle, name, value) ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_post_set_vec4(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::FpPresent)) {
        return s7_f(sc);
    }
    PostProcessState* state = postState();
    if (state == nullptr) {
        return s7_f(sc);
    }
    PostPassHandle handle = 0;
    if (!parseHandle(sc, s7_car(args), "post-set-vec4", 1, handle)) {
        return s7_f(sc);
    }
    s7_pointer rest = s7_cdr(args);
    std::string name;
    if (!s7_is_pair(rest) || !parseUniformName(sc, s7_car(rest), "post-set-vec4", name)) {
        return s7_f(sc);
    }
    rest = s7_cdr(rest);
    float comps[4]{};
    for (int i = 0; i < 4; ++i) {
        if (!s7_is_pair(rest) || !s7_is_number(s7_car(rest))) {
            return s7_wrong_type_arg_error(sc, "post-set-vec4", i + 3, rest, "number");
        }
        comps[i] = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
        rest = s7_cdr(rest);
    }
    PostUniformValue value{};
    value.kind = PostUniformKind::Vec4;
    value.data[0] = comps[0];
    value.data[1] = comps[1];
    value.data[2] = comps[2];
    value.data[3] = comps[3];
    return setPostPassUniform(*state, handle, name, value) ? s7_t(sc) : s7_f(sc);
}

} // namespace

void bindPostProcessApi(flecs::world& world, s7_scheme* scheme) {
    g_postWorld = &world;
    if (scheme == nullptr) {
        return;
    }

    s7_define_function(scheme, "post-push-shader", g_post_push_shader, 2, 0, false,
                       "(post-push-shader path target) target is 'scene, 'hud, or 'both");
    s7_define_function(scheme, "post-remove-shader", g_post_remove_shader, 1, 0, false,
                       "(post-remove-shader handle)");
    s7_define_function(scheme, "post-clear-shaders", g_post_clear_shaders, 1, 0, false,
                       "(post-clear-shaders target)");
    s7_define_function(scheme, "post-set-enabled", g_post_set_enabled, 2, 0, false,
                       "(post-set-enabled handle enabled)");
    s7_define_function(scheme, "post-set-float", g_post_set_float, 3, 0, false,
                       "(post-set-float handle name x)");
    s7_define_function(scheme, "post-set-vec2", g_post_set_vec2, 4, 0, false,
                       "(post-set-vec2 handle name x y)");
    s7_define_function(scheme, "post-set-vec3", g_post_set_vec3, 5, 0, false,
                       "(post-set-vec3 handle name x y z)");
    s7_define_function(scheme, "post-set-vec4", g_post_set_vec4, 6, 0, false,
                       "(post-set-vec4 handle name x y z w)");
}

}
