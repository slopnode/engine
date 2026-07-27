#include "script/post_script.hpp"

#include "assets/asset_services.hpp"
#include "render/post_process.hpp"
#include "script/script_scope.hpp"

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

s7_pointer g_post_set_shader(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::FpPresent)) {
        return s7_f(sc);
    }
    PostProcessState* state = postState();
    AssetStore* assets = postAssets();
    if (state == nullptr || assets == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "post-set-shader", 1, args, "string path");
    }
    const char* path = s7_string(s7_car(args));
    if (path == nullptr || path[0] == '\0') {
        return s7_f(sc);
    }
    return loadPostProcessShader(*state, *assets, path) ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_post_clear_shader(s7_scheme* sc, s7_pointer) {
    if (!requireCap(sc, ScriptCap::FpPresent)) {
        return s7_f(sc);
    }
    PostProcessState* state = postState();
    if (state == nullptr) {
        return s7_f(sc);
    }
    clearPostProcessShader(*state);
    return s7_t(sc);
}

s7_pointer g_post_set_enabled(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::FpPresent)) {
        return s7_f(sc);
    }
    PostProcessState* state = postState();
    if (state == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_boolean(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "post-set-enabled", 1, args, "boolean");
    }
    state->enabled = s7_boolean(sc, s7_car(args));
    return s7_t(sc);
}

s7_pointer g_post_set_float(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::FpPresent)) {
        return s7_f(sc);
    }
    PostProcessState* state = postState();
    if (state == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "post-set-float", 1, args, "name");
    }
    std::string name;
    if (!parseUniformName(sc, s7_car(args), "post-set-float", name)) {
        return s7_f(sc);
    }
    s7_pointer rest = s7_cdr(args);
    if (!s7_is_pair(rest) || !s7_is_number(s7_car(rest))) {
        return s7_wrong_type_arg_error(sc, "post-set-float", 2, rest, "number");
    }
    PostUniformValue value{};
    value.kind = PostUniformKind::Float;
    value.data[0] = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
    state->uniforms[name] = value;
    return s7_t(sc);
}

s7_pointer g_post_set_vec2(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::FpPresent)) {
        return s7_f(sc);
    }
    PostProcessState* state = postState();
    if (state == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "post-set-vec2", 1, args, "name");
    }
    std::string name;
    if (!parseUniformName(sc, s7_car(args), "post-set-vec2", name)) {
        return s7_f(sc);
    }
    s7_pointer rest = s7_cdr(args);
    float comps[2]{};
    for (int i = 0; i < 2; ++i) {
        if (!s7_is_pair(rest) || !s7_is_number(s7_car(rest))) {
            return s7_wrong_type_arg_error(sc, "post-set-vec2", i + 2, rest, "number");
        }
        comps[i] = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
        rest = s7_cdr(rest);
    }
    PostUniformValue value{};
    value.kind = PostUniformKind::Vec2;
    value.data[0] = comps[0];
    value.data[1] = comps[1];
    state->uniforms[name] = value;
    return s7_t(sc);
}

s7_pointer g_post_set_vec3(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::FpPresent)) {
        return s7_f(sc);
    }
    PostProcessState* state = postState();
    if (state == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "post-set-vec3", 1, args, "name");
    }
    std::string name;
    if (!parseUniformName(sc, s7_car(args), "post-set-vec3", name)) {
        return s7_f(sc);
    }
    s7_pointer rest = s7_cdr(args);
    float comps[3]{};
    for (int i = 0; i < 3; ++i) {
        if (!s7_is_pair(rest) || !s7_is_number(s7_car(rest))) {
            return s7_wrong_type_arg_error(sc, "post-set-vec3", i + 2, rest, "number");
        }
        comps[i] = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
        rest = s7_cdr(rest);
    }
    PostUniformValue value{};
    value.kind = PostUniformKind::Vec3;
    value.data[0] = comps[0];
    value.data[1] = comps[1];
    value.data[2] = comps[2];
    state->uniforms[name] = value;
    return s7_t(sc);
}

s7_pointer g_post_set_vec4(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::FpPresent)) {
        return s7_f(sc);
    }
    PostProcessState* state = postState();
    if (state == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "post-set-vec4", 1, args, "name");
    }
    std::string name;
    if (!parseUniformName(sc, s7_car(args), "post-set-vec4", name)) {
        return s7_f(sc);
    }
    s7_pointer rest = s7_cdr(args);
    float comps[4]{};
    for (int i = 0; i < 4; ++i) {
        if (!s7_is_pair(rest) || !s7_is_number(s7_car(rest))) {
            return s7_wrong_type_arg_error(sc, "post-set-vec4", i + 2, rest, "number");
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
    state->uniforms[name] = value;
    return s7_t(sc);
}

} // namespace

void bindPostProcessApi(flecs::world& world, s7_scheme* scheme) {
    g_postWorld = &world;
    if (scheme == nullptr) {
        return;
    }

    s7_define_function(scheme, "post-set-shader", g_post_set_shader, 1, 0, false,
                       "(post-set-shader path)");
    s7_define_function(scheme, "post-clear-shader", g_post_clear_shader, 0, 0, false,
                       "(post-clear-shader)");
    s7_define_function(scheme, "post-set-enabled", g_post_set_enabled, 1, 0, false,
                       "(post-set-enabled enabled)");
    s7_define_function(scheme, "post-set-float", g_post_set_float, 2, 0, false,
                       "(post-set-float name x)");
    s7_define_function(scheme, "post-set-vec2", g_post_set_vec2, 3, 0, false,
                       "(post-set-vec2 name x y)");
    s7_define_function(scheme, "post-set-vec3", g_post_set_vec3, 4, 0, false,
                       "(post-set-vec3 name x y z)");
    s7_define_function(scheme, "post-set-vec4", g_post_set_vec4, 5, 0, false,
                       "(post-set-vec4 name x y z w)");
}

}
