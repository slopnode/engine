#include "map/face_triggers.hpp"

#include "interact/components.hpp"
#include "map/bsp.hpp"
#include "render/components.hpp"

#include <string>

#include <raylib.h>
#include <raymath.h>

namespace slopengine {

namespace {

std::string faceEntityName(const std::string& faceId) {
    return "face:" + faceId;
}

std::string resolveFaceId(const Brush& brush, const BrushFace& face, std::size_t faceIndex) {
    if (!face.id.empty()) {
        return face.id;
    }
    return brush.id + "/" + std::to_string(faceIndex);
}

Vector3 normalizeOrZero(Vector3 v) {
    const float len = Vector3Length(v);
    if (len < 1e-6f) {
        return {};
    }
    return Vector3Scale(v, 1.0f / len);
}

} // namespace

void spawnFaceUseSurfaces(flecs::world& world, const std::vector<Brush>& brushes) {
    for (const Brush& brush : brushes) {
        for (std::size_t i = 0; i < brush.faces.size(); ++i) {
            const BrushFace& face = brush.faces[i];
            if (face.onUse.empty() || face.vertices.size() < 3) {
                continue;
            }

            const std::string faceId = resolveFaceId(brush, face, i);
            const std::string entityName = faceEntityName(faceId);

            Vector3 normal = face.normal;
            if (Vector3Length(normal) < 1e-6f) {
                normal = normalizeOrZero(faceNormalFromVertices(face.vertices));
            } else {
                normal = normalizeOrZero(normal);
            }

            FaceUseSurface surface{};
            surface.vertices = face.vertices;
            surface.normal = normal;

            flecs::entity entity = world.entity(entityName.c_str());
            entity.add<MapOwned>()
                .add<WorldSpace>()
                .set<LocalTransformation>({
                    .position = {0.0f, 0.0f, 0.0f},
                    .scale = {1.0f, 1.0f, 1.0f},
                    .rotation = {0.0f, 0.0f, 0.0f, 1.0f},
                })
                .set<Interactable>({
                    .prompt = "Interact",
                    .eventName = face.onUse,
                    .maxDistance = 5.0f,
                })
                .set<FaceUseSurface>(std::move(surface));
        }
    }
}

}
