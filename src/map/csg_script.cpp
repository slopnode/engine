#include "map/csg_script.hpp"

#include "map/brush.hpp"
#include "map/csg_compile.hpp"

#include <raylib.h>
#include <s7.h>

#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace slopengine {

namespace {

struct CsgBuilder {
    std::vector<Brush> brushes;
};

CsgBuilder* g_builder = nullptr;

s7_pointer makeTaggedList(s7_scheme* sc, const char* tag, s7_pointer rest) {
    return s7_cons(sc, s7_make_symbol(sc, tag), rest);
}

bool readString(s7_scheme* sc, s7_pointer value, std::string& out) {
    if (s7_is_string(value)) {
        out = s7_string(value);
        return true;
    }
    if (s7_is_symbol(value)) {
        out = s7_symbol_name(value);
        return true;
    }
    (void)sc;
    return false;
}

bool readVec3(s7_scheme* sc, s7_pointer x, s7_pointer y, s7_pointer z, Vector3& out) {
    if (!s7_is_number(x) || !s7_is_number(y) || !s7_is_number(z)) {
        return false;
    }
    out.x = static_cast<float>(s7_number_to_real(sc, x));
    out.y = static_cast<float>(s7_number_to_real(sc, y));
    out.z = static_cast<float>(s7_number_to_real(sc, z));
    return true;
}

std::optional<BrushBoxSide> parseSide(s7_pointer value) {
    if (!s7_is_symbol(value)) {
        return std::nullopt;
    }
    const char* name = s7_symbol_name(value);
    if (std::strcmp(name, "top") == 0) {
        return BrushBoxSide::Top;
    }
    if (std::strcmp(name, "bottom") == 0) {
        return BrushBoxSide::Bottom;
    }
    if (std::strcmp(name, "north") == 0) {
        return BrushBoxSide::North;
    }
    if (std::strcmp(name, "south") == 0) {
        return BrushBoxSide::South;
    }
    if (std::strcmp(name, "east") == 0) {
        return BrushBoxSide::East;
    }
    if (std::strcmp(name, "west") == 0) {
        return BrushBoxSide::West;
    }
    return std::nullopt;
}

bool parseFaceOverride(s7_scheme* sc, s7_pointer form, BrushBoxSide& side, BrushFace& face) {
    if (!s7_is_pair(form)) {
        return false;
    }

    const auto parsedSide = parseSide(s7_car(form));
    if (!parsedSide) {
        return false;
    }
    side = *parsedSide;

    for (s7_pointer cursor = s7_cdr(form); s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        s7_pointer clause = s7_car(cursor);
        if (!s7_is_pair(clause) || !s7_is_symbol(s7_car(clause))) {
            continue;
        }
        const char* tag = s7_symbol_name(s7_car(clause));
        s7_pointer rest = s7_cdr(clause);
        if (std::strcmp(tag, "id") == 0 && s7_is_pair(rest)) {
            readString(sc, s7_car(rest), face.id);
        } else if (std::strcmp(tag, "material") == 0 && s7_is_pair(rest)) {
            readString(sc, s7_car(rest), face.material);
        } else if (std::strcmp(tag, "uv-shift") == 0 &&
                   s7_is_pair(rest) &&
                   s7_is_pair(s7_cdr(rest)) &&
                   s7_is_number(s7_car(rest)) &&
                   s7_is_number(s7_cadr(rest))) {
            face.uvShiftPixels.x = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
            face.uvShiftPixels.y = static_cast<float>(s7_number_to_real(sc, s7_cadr(rest)));
        }
    }

    return true;
}

s7_pointer g_id(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "id", 1, args, "value");
    }
    return makeTaggedList(sc, "id", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_mins(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args) || !s7_is_pair(s7_cdr(args)) || !s7_is_pair(s7_cddr(args))) {
        return s7_wrong_type_arg_error(sc, "mins", 0, args, "x y z");
    }
    return makeTaggedList(
        sc,
        "mins",
        s7_list(sc, 3, s7_car(args), s7_cadr(args), s7_caddr(args)));
}

s7_pointer g_maxs(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args) || !s7_is_pair(s7_cdr(args)) || !s7_is_pair(s7_cddr(args))) {
        return s7_wrong_type_arg_error(sc, "maxs", 0, args, "x y z");
    }
    return makeTaggedList(
        sc,
        "maxs",
        s7_list(sc, 3, s7_car(args), s7_cadr(args), s7_caddr(args)));
}

s7_pointer g_material(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "material", 1, args, "string");
    }
    return makeTaggedList(sc, "material", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_uv_shift(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args) || !s7_is_pair(s7_cdr(args))) {
        return s7_wrong_type_arg_error(sc, "uv-shift", 0, args, "x y");
    }
    return makeTaggedList(sc, "uv-shift", s7_list(sc, 2, s7_car(args), s7_cadr(args)));
}

s7_pointer g_faces(s7_scheme* sc, s7_pointer args) {
    return makeTaggedList(sc, "faces", args);
}

s7_pointer makeSideForm(s7_scheme* sc, const char* side, s7_pointer args) {
    return s7_cons(sc, s7_make_symbol(sc, side), args);
}

s7_pointer g_top(s7_scheme* sc, s7_pointer args) {
    return makeSideForm(sc, "top", args);
}

s7_pointer g_bottom(s7_scheme* sc, s7_pointer args) {
    return makeSideForm(sc, "bottom", args);
}

s7_pointer g_north(s7_scheme* sc, s7_pointer args) {
    return makeSideForm(sc, "north", args);
}

s7_pointer g_south(s7_scheme* sc, s7_pointer args) {
    return makeSideForm(sc, "south", args);
}

s7_pointer g_east(s7_scheme* sc, s7_pointer args) {
    return makeSideForm(sc, "east", args);
}

s7_pointer g_west(s7_scheme* sc, s7_pointer args) {
    return makeSideForm(sc, "west", args);
}

s7_pointer g_brush_box(s7_scheme* sc, s7_pointer args) {
    if (g_builder == nullptr) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "csg-error"),
            s7_list(sc, 1, s7_make_string(sc, "brush-box called outside map load")));
    }

    std::string id;
    std::string material = "default/cube";
    Vector3 mins{};
    Vector3 maxs{};
    bool haveMins = false;
    bool haveMaxs = false;
    std::vector<std::pair<BrushBoxSide, BrushFace>> overrides;

    for (s7_pointer cursor = args; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        s7_pointer clause = s7_car(cursor);
        if (!s7_is_pair(clause) || !s7_is_symbol(s7_car(clause))) {
            continue;
        }

        const char* tag = s7_symbol_name(s7_car(clause));
        s7_pointer rest = s7_cdr(clause);

        if (std::strcmp(tag, "id") == 0 && s7_is_pair(rest)) {
            readString(sc, s7_car(rest), id);
        } else if (std::strcmp(tag, "material") == 0 && s7_is_pair(rest)) {
            readString(sc, s7_car(rest), material);
        } else if (std::strcmp(tag, "mins") == 0 &&
                   s7_is_pair(rest) &&
                   s7_is_pair(s7_cdr(rest)) &&
                   s7_is_pair(s7_cddr(rest))) {
            haveMins = readVec3(sc, s7_car(rest), s7_cadr(rest), s7_caddr(rest), mins);
        } else if (std::strcmp(tag, "maxs") == 0 &&
                   s7_is_pair(rest) &&
                   s7_is_pair(s7_cdr(rest)) &&
                   s7_is_pair(s7_cddr(rest))) {
            haveMaxs = readVec3(sc, s7_car(rest), s7_cadr(rest), s7_caddr(rest), maxs);
        } else if (std::strcmp(tag, "faces") == 0) {
            for (s7_pointer faceCursor = rest; s7_is_pair(faceCursor); faceCursor = s7_cdr(faceCursor)) {
                BrushBoxSide side{};
                BrushFace face{};
                if (parseFaceOverride(sc, s7_car(faceCursor), side, face)) {
                    overrides.emplace_back(side, std::move(face));
                }
            }
        }
    }

    if (id.empty() || !haveMins || !haveMaxs) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "csg-error"),
            s7_list(sc, 1, s7_make_string(sc, "brush-box requires id, mins, and maxs")));
    }

    g_builder->brushes.push_back(makeBrushBox(std::move(id), mins, maxs, material, overrides));
    return s7_t(sc);
}

void bindCsgApi(s7_scheme* sc) {
    s7_define_function(sc, "id", g_id, 1, 0, false, "(id value)");
    s7_define_function(sc, "mins", g_mins, 3, 0, false, "(mins x y z)");
    s7_define_function(sc, "maxs", g_maxs, 3, 0, false, "(maxs x y z)");
    s7_define_function(sc, "material", g_material, 1, 0, false, "(material name)");
    s7_define_function(sc, "uv-shift", g_uv_shift, 2, 0, false, "(uv-shift x y)");
    s7_define_function(sc, "faces", g_faces, 0, 0, true, "(faces face...)");
    s7_define_function(sc, "top", g_top, 0, 0, true, "(top props...)");
    s7_define_function(sc, "bottom", g_bottom, 0, 0, true, "(bottom props...)");
    s7_define_function(sc, "north", g_north, 0, 0, true, "(north props...)");
    s7_define_function(sc, "south", g_south, 0, 0, true, "(south props...)");
    s7_define_function(sc, "east", g_east, 0, 0, true, "(east props...)");
    s7_define_function(sc, "west", g_west, 0, 0, true, "(west props...)");
    s7_define_function(sc, "brush-box", g_brush_box, 0, 0, true, "(brush-box clauses...)");
}

} // namespace

std::optional<LoadedMap> loadAndCompileMap(
    s7_scheme* scheme,
    AssetStore& assets,
    std::string_view mapName) {
    if (scheme == nullptr) {
        TraceLog(LOG_WARNING, "MAP: no scheme runtime");
        return std::nullopt;
    }

    const std::string virtualPath = std::string(mapName) + "/static";
    if (!assets.hasMapCsg(virtualPath)) {
        TraceLog(LOG_WARNING, "MAP: missing maps/%s.csg", virtualPath.c_str());
        return std::nullopt;
    }

    CsgBuilder builder;
    g_builder = &builder;
    bindCsgApi(scheme);

    const bool loaded = assets.loadMapCsg(scheme, virtualPath);
    g_builder = nullptr;

    if (!loaded) {
        TraceLog(LOG_WARNING, "MAP: failed to load maps/%s.csg", virtualPath.c_str());
        return std::nullopt;
    }

    if (builder.brushes.empty()) {
        TraceLog(LOG_WARNING, "MAP: no brushes in maps/%s.csg", virtualPath.c_str());
        return std::nullopt;
    }

    const CsgCompileResult compiled = compileBrushesToGeo(
        builder.brushes,
        [&assets](std::string_view materialPath) {
            MaterialUvInfo info{};
            const MaterialAsset* asset = assets.getMaterialAsset(materialPath);
            if (asset != nullptr) {
                info.pixelsPerMeter = asset->pixelsPerMeter;
                if (!asset->albedoTexture.empty()) {
                    const Texture2D texture = assets.getTexture(asset->albedoTexture);
                    if (texture.id != 0 && texture.width > 0 && texture.height > 0) {
                        info.textureWidth = static_cast<float>(texture.width);
                        info.textureHeight = static_cast<float>(texture.height);
                    }
                }
            }
            return info;
        });

    for (const GeoPrimitive& primitive : compiled.asset.primitives) {
        TraceLog(LOG_INFO, "MAP: face '%s' material '%s'", primitive.name.c_str(), primitive.material.c_str());
    }

    Model model = buildModelFromGeo(
        compiled.asset,
        compiled.buffer,
        [&assets](std::string_view path) { return assets.resolveMaterial(path); });

    if (model.meshCount <= 0) {
        TraceLog(LOG_WARNING, "MAP: compile produced empty model for '%s'", virtualPath.c_str());
        return std::nullopt;
    }

    TraceLog(
        LOG_INFO,
        "MAP: loaded '%s' (%d brushes, %d meshes)",
        virtualPath.c_str(),
        static_cast<int>(builder.brushes.size()),
        model.meshCount);

    LoadedMap result;
    result.model = model;
    result.brushes = std::move(builder.brushes);
    return result;
}

}
