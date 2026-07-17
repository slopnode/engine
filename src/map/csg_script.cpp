#include "map/csg_script.hpp"

#include "map/brush.hpp"
#include "map/bsp.hpp"
#include "map/bsp_analyze.hpp"
#include "map/bsp_io.hpp"
#include "map/csg_compile.hpp"
#include "map/lightmap.hpp"
#include "map/map_meta.hpp"

#include <raylib.h>
#include <s7.h>

#include <cstring>
#include <optional>
#include <string>
#include <unordered_map>
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
        } else if (std::strcmp(tag, "nodraw") == 0) {
            face.nodraw = true;
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

s7_pointer g_role(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "role", 1, args, "hull|detail");
    }
    return makeTaggedList(sc, "role", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_uv_shift(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args) || !s7_is_pair(s7_cdr(args))) {
        return s7_wrong_type_arg_error(sc, "uv-shift", 0, args, "x y");
    }
    return makeTaggedList(sc, "uv-shift", s7_list(sc, 2, s7_car(args), s7_cadr(args)));
}

s7_pointer g_nodraw(s7_scheme* sc, s7_pointer args) {
    (void)args;
    return makeTaggedList(sc, "nodraw", s7_nil(sc));
}

s7_pointer g_faces(s7_scheme* sc, s7_pointer args) {
    return makeTaggedList(sc, "faces", args);
}

s7_pointer g_face(s7_scheme* sc, s7_pointer args) {
    return makeTaggedList(sc, "face", args);
}

s7_pointer g_verts(s7_scheme* sc, s7_pointer args) {
    return makeTaggedList(sc, "verts", args);
}

s7_pointer g_v(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args) || !s7_is_pair(s7_cdr(args)) || !s7_is_pair(s7_cddr(args))) {
        return s7_wrong_type_arg_error(sc, "v", 0, args, "x y z");
    }
    return makeTaggedList(
        sc,
        "v",
        s7_list(sc, 3, s7_car(args), s7_cadr(args), s7_caddr(args)));
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
    BrushRole role = BrushRole::Hull;
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
        } else if (std::strcmp(tag, "role") == 0 && s7_is_pair(rest)) {
            std::string roleName;
            if (readString(sc, s7_car(rest), roleName)) {
                if (roleName == "detail") {
                    role = BrushRole::Detail;
                } else if (roleName == "hull") {
                    role = BrushRole::Hull;
                }
            }
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

    g_builder->brushes.push_back(
        makeBrushBox(std::move(id), mins, maxs, material, overrides, role));
    return s7_t(sc);
}

bool parseConvexFace(s7_scheme* sc, s7_pointer form, BrushFace& face) {
    if (!s7_is_pair(form) || !s7_is_symbol(s7_car(form))) {
        return false;
    }
    if (std::strcmp(s7_symbol_name(s7_car(form)), "face") != 0) {
        return false;
    }

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
        } else if (std::strcmp(tag, "nodraw") == 0) {
            face.nodraw = true;
        } else if (std::strcmp(tag, "verts") == 0) {
            for (s7_pointer vertCursor = rest; s7_is_pair(vertCursor); vertCursor = s7_cdr(vertCursor)) {
                s7_pointer vert = s7_car(vertCursor);
                if (!s7_is_pair(vert) || !s7_is_symbol(s7_car(vert))) {
                    continue;
                }
                if (std::strcmp(s7_symbol_name(s7_car(vert)), "v") != 0) {
                    continue;
                }
                s7_pointer coords = s7_cdr(vert);
                if (!s7_is_pair(coords) || !s7_is_pair(s7_cdr(coords)) || !s7_is_pair(s7_cddr(coords))) {
                    continue;
                }
                Vector3 point{};
                if (readVec3(sc, s7_car(coords), s7_cadr(coords), s7_caddr(coords), point)) {
                    face.vertices.push_back(point);
                }
            }
        }
    }
    return face.vertices.size() >= 3;
}

s7_pointer g_brush_convex(s7_scheme* sc, s7_pointer args) {
    if (g_builder == nullptr) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "csg-error"),
            s7_list(sc, 1, s7_make_string(sc, "brush-convex called outside map load")));
    }

    std::string id;
    std::string defaultMaterial = "default/cube";
    BrushRole role = BrushRole::Hull;
    std::vector<BrushFace> faces;

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
            readString(sc, s7_car(rest), defaultMaterial);
        } else if (std::strcmp(tag, "role") == 0 && s7_is_pair(rest)) {
            std::string roleName;
            if (readString(sc, s7_car(rest), roleName)) {
                if (roleName == "detail") {
                    role = BrushRole::Detail;
                } else if (roleName == "hull") {
                    role = BrushRole::Hull;
                }
            }
        } else if (std::strcmp(tag, "faces") == 0) {
            for (s7_pointer faceCursor = rest; s7_is_pair(faceCursor); faceCursor = s7_cdr(faceCursor)) {
                BrushFace face{};
                if (parseConvexFace(sc, s7_car(faceCursor), face)) {
                    if (face.material.empty()) {
                        face.material = defaultMaterial;
                    }
                    faces.push_back(std::move(face));
                }
            }
        }
    }

    if (id.empty() || faces.empty()) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "csg-error"),
            s7_list(sc, 1, s7_make_string(sc, "brush-convex requires id and faces")));
    }

    std::string error;
    auto brush = makeBrushConvex(std::move(id), std::move(faces), role, error);
    if (!brush) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "csg-error"),
            s7_list(sc, 1, s7_make_string(sc, error.c_str())));
    }
    g_builder->brushes.push_back(std::move(*brush));
    return s7_t(sc);
}

void bindCsgApi(s7_scheme* sc) {
    s7_define_function(sc, "id", g_id, 1, 0, false, "(id value)");
    s7_define_function(sc, "mins", g_mins, 3, 0, false, "(mins x y z)");
    s7_define_function(sc, "maxs", g_maxs, 3, 0, false, "(maxs x y z)");
    s7_define_function(sc, "material", g_material, 1, 0, false, "(material name)");
    s7_define_function(sc, "role", g_role, 1, 0, false, "(role hull|detail)");
    s7_define_function(sc, "uv-shift", g_uv_shift, 2, 0, false, "(uv-shift x y)");
    s7_define_function(sc, "nodraw", g_nodraw, 0, 0, false, "(nodraw)");
    s7_define_function(sc, "faces", g_faces, 0, 0, true, "(faces face...)");
    s7_define_function(sc, "face", g_face, 0, 0, true, "(face props...)");
    s7_define_function(sc, "verts", g_verts, 0, 0, true, "(verts (v x y z)...)");
    s7_define_function(sc, "v", g_v, 3, 0, false, "(v x y z)");
    s7_define_function(sc, "top", g_top, 0, 0, true, "(top props...)");
    s7_define_function(sc, "bottom", g_bottom, 0, 0, true, "(bottom props...)");
    s7_define_function(sc, "north", g_north, 0, 0, true, "(north props...)");
    s7_define_function(sc, "south", g_south, 0, 0, true, "(south props...)");
    s7_define_function(sc, "east", g_east, 0, 0, true, "(east props...)");
    s7_define_function(sc, "west", g_west, 0, 0, true, "(west props...)");
    s7_define_function(sc, "brush-box", g_brush_box, 0, 0, true, "(brush-box clauses...)");
    s7_define_function(sc, "brush-convex", g_brush_convex, 0, 0, true, "(brush-convex clauses...)");
}

Shader loadLightmapShader(AssetStore& assets, int& useLightmapLoc) {
    useLightmapLoc = -1;
    const std::string vert = assets.getShaderSource("default/lightmap_vert");
    const std::string frag = assets.getShaderSource("default/lightmap_frag");
    if (vert.empty() || frag.empty()) {
        TraceLog(LOG_WARNING, "MAP: missing lightmap shaders");
        return {};
    }
    Shader shader = LoadShaderFromMemory(vert.c_str(), frag.c_str());
    if (shader.id == 0) {
        TraceLog(LOG_WARNING, "MAP: failed to compile lightmap shaders");
        return {};
    }
    shader.locs[SHADER_LOC_MAP_ALBEDO] = GetShaderLocation(shader, "texture0");
    shader.locs[SHADER_LOC_MAP_METALNESS] = GetShaderLocation(shader, "texture1");
    shader.locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(shader, "colDiffuse");
    shader.locs[SHADER_LOC_COLOR_SPECULAR] = GetShaderLocation(shader, "colSpecular");
    useLightmapLoc = GetShaderLocation(shader, "useLightmap");
    int useLightmap = 1;
    if (useLightmapLoc >= 0) {
        SetShaderValue(shader, useLightmapLoc, &useLightmap, SHADER_UNIFORM_INT);
    }
    return shader;
}

MaterialUvInfo resolveMaterialUv(AssetStore& assets, std::string_view materialPath) {
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
}

} // namespace

std::optional<MapMeta> loadMapMeta(AssetStore& assets, std::string_view mapName) {
    const std::string metaPath = std::string(mapName) + "/map";
    if (!assets.hasMapMeta(metaPath)) {
        return std::nullopt;
    }
    MapMeta mapMeta;
    if (!parseMapMeta(assets.getMapMetaSource(metaPath), mapMeta)) {
        return std::nullopt;
    }
    if (!assets.hasPackageId(mapMeta.package)) {
        return std::nullopt;
    }
    for (const std::string& depend : mapMeta.depends) {
        if (!assets.hasPackageId(depend)) {
            return std::nullopt;
        }
    }
    return mapMeta;
}

std::optional<std::vector<Brush>> loadMapBrushes(
    s7_scheme* scheme,
    AssetStore& assets,
    std::string_view mapName) {
    if (scheme == nullptr) {
        return std::nullopt;
    }

    const std::string virtualPath = std::string(mapName) + "/static";
    if (!assets.hasMapCsg(virtualPath)) {
        return std::nullopt;
    }
    if (!loadMapMeta(assets, mapName)) {
        return std::nullopt;
    }

    CsgBuilder builder;
    g_builder = &builder;
    bindCsgApi(scheme);
    const bool loaded = assets.loadMapCsg(scheme, virtualPath);
    g_builder = nullptr;
    if (!loaded || builder.brushes.empty()) {
        return std::nullopt;
    }
    return std::move(builder.brushes);
}

std::optional<LoadedMap> loadAndCompileMap(
    s7_scheme* scheme,
    AssetStore& assets,
    std::string_view mapName) {
    if (scheme == nullptr) {
        TraceLog(LOG_WARNING, "MAP: no scheme runtime");
        return std::nullopt;
    }

    const std::string virtualPath = std::string(mapName) + "/static";
    const std::string radVirtualPath = std::string(mapName) + "/rad/static";
    const std::string metaPath = std::string(mapName) + "/map";
    if (!assets.hasMapCsg(virtualPath)) {
        TraceLog(LOG_WARNING, "MAP: missing maps/%s.csg", virtualPath.c_str());
        return std::nullopt;
    }
    if (!assets.hasMapMeta(metaPath)) {
        TraceLog(LOG_WARNING, "MAP: missing maps/%s.meta", metaPath.c_str());
        return std::nullopt;
    }
    if (!assets.hasMapBsp(virtualPath)) {
        TraceLog(LOG_WARNING, "MAP: missing maps/%s.bsp (run slopbsp)", virtualPath.c_str());
        return std::nullopt;
    }

    auto mapMeta = loadMapMeta(assets, mapName);
    if (!mapMeta) {
        TraceLog(LOG_WARNING, "MAP: invalid maps/%s.meta", metaPath.c_str());
        return std::nullopt;
    }

    TraceLog(
        LOG_INFO,
        "MAP: meta id='%s' name='%s' package='%s' depends=%d ambient=(%.3f %.3f %.3f)",
        mapMeta->id.c_str(),
        mapMeta->name.c_str(),
        mapMeta->package.c_str(),
        static_cast<int>(mapMeta->depends.size()),
        mapMeta->ambient.x,
        mapMeta->ambient.y,
        mapMeta->ambient.z);

    auto brushes = loadMapBrushes(scheme, assets, mapName);
    if (!brushes) {
        TraceLog(LOG_WARNING, "MAP: failed to load maps/%s.csg", virtualPath.c_str());
        return std::nullopt;
    }

    const auto bspPath = assets.resolvePath(AssetKind::MapBsp, virtualPath);
    if (!bspPath) {
        TraceLog(LOG_WARNING, "MAP: failed to resolve bsp path");
        return std::nullopt;
    }

    auto bsp = readBspFile(*bspPath);
    if (!bsp) {
        TraceLog(LOG_WARNING, "MAP: failed to read bsp for '%s'", virtualPath.c_str());
        return std::nullopt;
    }

    const MapHullAnalysis analysis = analyzeMapHull(*bsp, *brushes);
    if (!analysis.sealed) {
        TraceLog(
            LOG_WARNING,
            "MAP: hull is not sealed; skipping auto-nodraw (authored nodraw only)");
    } else {
        applyInferredNodraw(*brushes, analysis);
        TraceLog(
            LOG_INFO,
            "MAP: auto-nodraw faces=%d",
            static_cast<int>(analysis.inferredNodrawFaceIds.size()));
        for (const std::string& warning : analysis.detailOutsideWarnings) {
            TraceLog(LOG_WARNING, "MAP: %s", warning.c_str());
        }
    }

    RadFile rad{};
    const bool hasRad = assets.hasMapRad(radVirtualPath);
    if (hasRad) {
        const auto radPath = assets.resolvePath(AssetKind::MapRad, radVirtualPath);
        if (radPath) {
            if (auto loadedRad = readRadFile(*radPath)) {
                rad = std::move(*loadedRad);
            } else {
                TraceLog(LOG_WARNING, "MAP: failed to read rad; rendering unlit");
            }
        }
    } else {
        TraceLog(LOG_INFO, "MAP: no rad bake present; rendering unlit");
    }

    int hullCount = 0;
    int detailCount = 0;
    for (const Brush& brush : *brushes) {
        if (brush.role == BrushRole::Detail) {
            ++detailCount;
        } else {
            ++hullCount;
        }
    }

    int emptyLeaves = 0;
    int solidLeaves = 0;
    for (const BspLeaf& leaf : bsp->leaves) {
        if (leaf.solid) {
            ++solidLeaves;
        } else {
            ++emptyLeaves;
        }
    }

    TraceLog(
        LOG_INFO,
        "MAP: BSP hull=%d detail=%d nodes=%d emptyLeaves=%d solidLeaves=%d surfaceFaces=%d charts=%d",
        hullCount,
        detailCount,
        static_cast<int>(bsp->nodes.size()),
        emptyLeaves,
        solidLeaves,
        static_cast<int>(bsp->surfaceFaces.size()),
        static_cast<int>(rad.charts.size()));

    LoadedMap result;
    result.hasLightmaps = !rad.charts.empty() && !rad.atlases.empty();
    if (result.hasLightmaps) {
        result.lightmapShader = loadLightmapShader(assets, result.useLightmapLoc);
        if (result.lightmapShader.id == 0) {
            result.hasLightmaps = false;
        }
    }

    if (result.hasLightmaps) {
        result.lightmapAtlases.reserve(rad.atlases.size());
        for (const LightmapAtlasInfo& atlas : rad.atlases) {
            const std::string atlasPath = std::string(mapName) + "/rad/" + atlas.texturePath;
            const auto resolved = assets.resolvePath(AssetKind::MapLightmap, atlasPath);
            Texture2D texture{};
            if (resolved) {
                texture = LoadTexture(resolved->string().c_str());
                if (texture.id != 0) {
                    SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
                    SetTextureWrap(texture, TEXTURE_WRAP_CLAMP);
                }
            }
            result.lightmapAtlases.push_back(texture);
        }
    }

    const RadFile* lightmaps = result.hasLightmaps ? &rad : nullptr;
    const CsgCompileResult compiled = compileBrushesToGeo(
        *brushes,
        [&assets](std::string_view materialPath) { return resolveMaterialUv(assets, materialPath); },
        lightmaps);

    std::unordered_map<std::string, std::int32_t> faceAtlasById;
    for (const LightmapChart& chart : rad.charts) {
        faceAtlasById[chart.faceId] = chart.atlasIndex;
    }

    Model model = buildModelFromGeo(
        compiled.asset,
        compiled.buffer,
        [&](std::string_view path) {
            Material material = assets.resolveMaterial(path);
            if (result.hasLightmaps) {
                material.shader = result.lightmapShader;
                if (!result.lightmapAtlases.empty() && result.lightmapAtlases[0].id != 0) {
                    SetMaterialTexture(&material, MATERIAL_MAP_METALNESS, result.lightmapAtlases[0]);
                }
            }
            return material;
        });

    if (model.meshCount > 0 && result.hasLightmaps) {
        for (int meshIndex = 0; meshIndex < model.meshCount; ++meshIndex) {
            const std::string& faceId = compiled.asset.primitives[static_cast<std::size_t>(meshIndex)].name;
            std::int32_t atlasIndex = 0;
            const auto atlasIt = faceAtlasById.find(faceId);
            if (atlasIt != faceAtlasById.end()) {
                atlasIndex = atlasIt->second;
            }
            if (atlasIndex >= 0 && atlasIndex < static_cast<std::int32_t>(result.lightmapAtlases.size())) {
                const Texture2D lightmap = result.lightmapAtlases[static_cast<std::size_t>(atlasIndex)];
                if (lightmap.id != 0) {
                    SetMaterialTexture(&model.materials[meshIndex], MATERIAL_MAP_METALNESS, lightmap);
                }
            }
            model.materials[meshIndex].shader = result.lightmapShader;
        }
    }

    if (model.meshCount <= 0) {
        TraceLog(LOG_WARNING, "MAP: compile produced empty model for '%s'", virtualPath.c_str());
        return std::nullopt;
    }

    TraceLog(
        LOG_INFO,
        "MAP: loaded '%s' (%d brushes, %d meshes, lightmaps=%s)",
        virtualPath.c_str(),
        static_cast<int>(brushes->size()),
        model.meshCount,
        result.hasLightmaps ? "yes" : "no");

    result.model = model;
    result.brushes = std::move(*brushes);
    result.bsp = std::move(*bsp);
    result.rad = std::move(rad);
    result.meta = std::move(*mapMeta);
    return result;
}

}
