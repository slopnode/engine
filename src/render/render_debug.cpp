#include "render/render_debug.hpp"

#include "map/nav_graph.hpp"
#include "navigation/nav_components.hpp"
#include "physics/components.hpp"
#include "render/components.hpp"
#include "render/debug_line_pool.hpp"
#include "render/sprite_billboard.hpp"

#include <rlgl.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace slopengine {

void drawSkeletonOverlay(const Model& model, const AnimationPlayer* animationPlayer) {
    if (model.skeleton.boneCount <= 0 || model.skeleton.bones == nullptr) {
        return;
    }

    const Transform* jointPoses = model.skeleton.bindPose;
    if (animationPlayer != nullptr && animationPlayer->playing && model.currentPose != nullptr) {
        jointPoses = model.currentPose;
    }

    if (jointPoses == nullptr) {
        return;
    }

    const float jointRadius = 0.12f;
    const Color boneColor = {255, 220, 0, 255};
    const Color jointColor = {255, 64, 64, 255};

    rlDisableDepthTest();
    rlDisableDepthMask();

    for (int boneIndex = 0; boneIndex < model.skeleton.boneCount; ++boneIndex) {
        const Vector3 jointPosition = jointPoses[boneIndex].translation;
        DrawSphereWires(jointPosition, jointRadius, 6, 8, jointColor);

        const int parentIndex = model.skeleton.bones[boneIndex].parent;
        if (parentIndex >= 0 && parentIndex < model.skeleton.boneCount) {
            const Vector3 parentPosition = jointPoses[parentIndex].translation;
            DrawLine3D(parentPosition, jointPosition, boneColor);
        }
    }

    rlEnableDepthMask();
    rlEnableDepthTest();
}

Color bspLeafDebugColor(std::int32_t leafIndex, bool solid, unsigned char alpha) {
    const std::uint32_t h = static_cast<std::uint32_t>(leafIndex) * 2654435761u;
    Color color{
        static_cast<unsigned char>(40 + (h & 0x7Fu)),
        static_cast<unsigned char>(40 + ((h >> 8) & 0x7Fu)),
        static_cast<unsigned char>(40 + ((h >> 16) & 0x7Fu)),
        alpha,
    };
    if (solid) {
        color.r = static_cast<unsigned char>(std::min(255, static_cast<int>(color.r) + 80));
        color.g = static_cast<unsigned char>(color.g / 2);
        color.b = static_cast<unsigned char>(color.b / 2);
    }
    return color;
}

void drawDebugPolygon(const std::vector<Vector3>& verts, Color color) {
    if (verts.size() < 3) {
        return;
    }
    for (std::size_t i = 1; i + 1 < verts.size(); ++i) {
        DrawTriangle3D(verts[0], verts[i], verts[i + 1], color);
        DrawTriangle3D(verts[0], verts[i + 1], verts[i], color);
    }
}

void drawDebugPolygonOutline(const std::vector<Vector3>& verts, Color color) {
    if (verts.size() < 2) {
        return;
    }
    for (std::size_t i = 0; i < verts.size(); ++i) {
        DrawLine3D(verts[i], verts[(i + 1) % verts.size()], color);
    }
}

void drawBspLeafFaces(const BspLeaf& leaf, Color color) {
    for (const auto& face : leaf.faces) {
        drawDebugPolygon(face, color);
    }
}

bool portalPolygonBetweenLeaves(const BspLeaf& a, const BspLeaf& b, std::vector<Vector3>& out) {
    constexpr float kEps = 1e-3f;
    auto normalOf = [](const std::vector<Vector3>& verts) -> Vector3 {
        if (verts.size() < 3) {
            return {};
        }
        Vector3 accum{};
        for (std::size_t i = 1; i + 1 < verts.size(); ++i) {
            const Vector3 e0{
                verts[i].x - verts[0].x,
                verts[i].y - verts[0].y,
                verts[i].z - verts[0].z,
            };
            const Vector3 e1{
                verts[i + 1].x - verts[0].x,
                verts[i + 1].y - verts[0].y,
                verts[i + 1].z - verts[0].z,
            };
            accum.x += e0.y * e1.z - e0.z * e1.y;
            accum.y += e0.z * e1.x - e0.x * e1.z;
            accum.z += e0.x * e1.y - e0.y * e1.x;
        }
        const float len = std::sqrt(accum.x * accum.x + accum.y * accum.y + accum.z * accum.z);
        if (len < 1e-8f) {
            return {};
        }
        return {accum.x / len, accum.y / len, accum.z / len};
    };

    for (const auto& fa : a.faces) {
        const Vector3 na = normalOf(fa);
        if (na.x == 0.0f && na.y == 0.0f && na.z == 0.0f) {
            continue;
        }
        for (const auto& fb : b.faces) {
            const Vector3 nb = normalOf(fb);
            const float align = na.x * nb.x + na.y * nb.y + na.z * nb.z;
            if (align > -0.99f) {
                continue;
            }
            Vector3 ca{};
            for (const Vector3& v : fa) {
                ca.x += v.x;
                ca.y += v.y;
                ca.z += v.z;
            }
            ca.x /= static_cast<float>(fa.size());
            ca.y /= static_cast<float>(fa.size());
            ca.z /= static_cast<float>(fa.size());
            Vector3 cb{};
            for (const Vector3& v : fb) {
                cb.x += v.x;
                cb.y += v.y;
                cb.z += v.z;
            }
            cb.x /= static_cast<float>(fb.size());
            cb.y /= static_cast<float>(fb.size());
            cb.z /= static_cast<float>(fb.size());
            const float dx = ca.x - cb.x;
            const float dy = ca.y - cb.y;
            const float dz = ca.z - cb.z;
            if (std::fabs(dx * na.x + dy * na.y + dz * na.z) > kEps * 8.0f) {
                continue;
            }
            out = fa;
            return true;
        }
    }
    return false;
}

void drawDebugQuadOutline(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color color) {
    DrawLine3D(a, b, color);
    DrawLine3D(b, c, color);
    DrawLine3D(c, d, color);
    DrawLine3D(d, a, color);
}

void drawGraphDebugOverlays(const GraphDocument& document) {
    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthMask();

    const Color nodeColor{80, 220, 120, 255};
    const Color edgeColor{80, 180, 255, 220};

    for (const NamedGraph& graph : document.graphs) {
        for (const GraphNode& node : graph.nodes) {
            DrawSphereWires(node.at, 0.12f, 8, 8, nodeColor);
        }

        for (const GraphEdge& edge : graph.edges) {
            const GraphNode* from = nullptr;
            const GraphNode* to = nullptr;
            for (const GraphNode& node : graph.nodes) {
                if (from == nullptr && node.id == edge.from) {
                    from = &node;
                }
                if (to == nullptr && node.id == edge.to) {
                    to = &node;
                }
            }
            if (from == nullptr || to == nullptr) {
                continue;
            }
            DrawLine3D(from->at, to->at, edgeColor);
        }
    }

    rlEnableDepthMask();
    EndBlendMode();
}

void drawNavDebugOverlays(flecs::world& world, const DebugUiState& debugUi) {
    if (!debugUi.showNavPaths) {
        return;
    }

    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthMask();

    const Color pathColor{255, 180, 40, 220};
    const Color currentColor{255, 60, 60, 255};

    world.each([&](flecs::entity, NavigationAgent& agent, LocalTransformation& local) {
        if (!agent.hasGoal || agent.waypoints.empty()) {
            return;
        }

        Vector3 prev = local.position;
        prev.y += 0.15f;
        for (std::size_t i = 0; i < agent.waypoints.size(); ++i) {
            Vector3 wp = agent.waypoints[i];
            wp.y += 0.15f;
            const Color color =
                static_cast<int>(i) == agent.waypointIndex ? currentColor : pathColor;
            DrawLine3D(prev, wp, color);
            DrawSphereWires(wp, 0.1f, 6, 6, color);
            prev = wp;
        }
    });

    rlEnableDepthMask();
    EndBlendMode();
}

void drawNavPolyDebugOverlays(const MapNavigation& nav, const DebugUiState& debugUi) {
    if (!debugUi.showNavPolys || nav.leafCount <= 0) {
        return;
    }

    // Fill color distinct from the live BSP-leaf nav overlay (drawNavDebugOverlays'
    // orange/red) so the two are never confused when both are toggled on at once.
    const Color fillColor{60, 220, 140, 90};
    const Color outlineColor{60, 220, 140, 220};
    const bool hasBoundary = nav.leafBoundary.size() == static_cast<std::size_t>(nav.leafCount);

    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthMask();

    for (int i = 0; i < nav.leafCount; ++i) {
        if (!nav.walkable[static_cast<std::size_t>(i)]) {
            continue;
        }
        if (hasBoundary && nav.leafBoundary[static_cast<std::size_t>(i)].size() >= 3) {
            std::vector<Vector3> verts = nav.leafBoundary[static_cast<std::size_t>(i)];
            for (Vector3& v : verts) {
                v.y += 0.1f;
            }
            drawDebugPolygon(verts, fillColor);
            drawDebugPolygonOutline(verts, outlineColor);
        } else {
            Vector3 centroid = nav.leafCentroids[static_cast<std::size_t>(i)];
            centroid.y = nav.leafFloorY[static_cast<std::size_t>(i)] + 0.1f;
            DrawSphereWires(centroid, 0.15f, 6, 6, outlineColor);
        }
    }

    rlEnableDepthMask();
    EndBlendMode();
}

void drawBspDebugOverlays(const BspTree& tree, const DebugUiState& debugUi, std::int32_t currentLeaf) {
    const bool any = debugUi.showBspOutlines || debugUi.showBspLeafFaces || debugUi.showBspPortals
        || debugUi.showBspSurfaceFaces;
    if (!any) {
        return;
    }

    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthMask();

    const std::int32_t leafCount = static_cast<std::int32_t>(tree.leaves.size());
    for (std::int32_t i = 0; i < leafCount; ++i) {
        if (debugUi.showBspCurrentLeafOnly && i != currentLeaf) {
            continue;
        }
        const BspLeaf& leaf = tree.leaves[static_cast<std::size_t>(i)];
        if (debugUi.showBspOutlines) {
            const bool blocked = leafBlocksFlood(leaf.contents);
            Color color = blocked ? Color{180, 60, 60, 255} : Color{60, 180, 220, 255};
            if (i == currentLeaf) {
                color = blocked ? Color{255, 220, 40, 255} : Color{40, 255, 120, 255};
            }
            DrawBoundingBox(BoundingBox{leaf.mins, leaf.maxs}, color);
        }
        if (debugUi.showBspLeafFaces) {
            const unsigned char alpha = i == currentLeaf ? static_cast<unsigned char>(140)
                                                        : static_cast<unsigned char>(70);
            drawBspLeafFaces(leaf, bspLeafDebugColor(i, leafBlocksFlood(leaf.contents), alpha));
        }
    }

    if (debugUi.showBspPortals) {
        for (const BspPortal& portal : tree.portals) {
            if (debugUi.showBspCurrentLeafOnly
                && portal.leafA != currentLeaf
                && portal.leafB != currentLeaf) {
                continue;
            }
            const bool involvesCurrent =
                portal.leafA == currentLeaf || portal.leafB == currentLeaf;
            const Color portalColor = involvesCurrent ? Color{255, 200, 40, 160}
                                                     : Color{255, 80, 220, 100};
            drawDebugPolygon(portal.vertices, portalColor);
        }
    }

    if (debugUi.showBspSurfaceFaces) {
        for (std::size_t faceIndex = 0; faceIndex < tree.surfaceFaces.size(); ++faceIndex) {
            const BspSurfaceFace& face = tree.surfaceFaces[faceIndex];
            if (debugUi.showBspCurrentLeafOnly && face.emptyLeaf != currentLeaf) {
                continue;
            }
            const Color fill = bspLeafDebugColor(static_cast<std::int32_t>(faceIndex), false, 90);
            const Color outline = face.emptyLeaf == currentLeaf ? Color{40, 255, 120, 255}
                                                               : Color{255, 255, 255, 220};
            drawDebugPolygon(face.vertices, fill);
            drawDebugPolygonOutline(face.vertices, outline);
        }
    }

    rlEnableDepthMask();
    EndBlendMode();
}

Ray spriteAimRay(const Lens& lens) {
    Ray ray{};
    ray.position = lens.camera.position;
    ray.direction = Vector3Normalize(Vector3Subtract(lens.camera.target, lens.camera.position));
    return ray;
}

constexpr float kSpriteAimMaxDistance = 100.0f;

std::string drawSpriteDebugOverlays(
    const Lens& lens,
    AssetStore& assets,
    const DebugUiState& debugUi,
    flecs::query<SpriteInstance, GlobalTransformation>& spriteQuery) {
    std::string aimStatus;
    if (!debugUi.showSpriteMasks && !debugUi.showSpriteAim) {
        return aimStatus;
    }

    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthTest();
    rlDisableDepthMask();

    if (debugUi.showSpriteMasks) {
        spriteQuery.each(
            [&](flecs::entity entity, SpriteInstance& sprite, GlobalTransformation& global) {
                if (!entity.has<WorldSpace>() || entity.has<ViewSprite>()) {
                    return;
                }
                if (const auto billboard = resolveSpriteBillboard(sprite, global, lens, assets)) {
                    drawSpriteMaskDebug(*billboard);
                }
            });
    }

    if (debugUi.showSpriteAim) {
        const Ray ray = spriteAimRay(lens);
        std::optional<SpriteBillboardHit> bestHit;
        std::string hitSprite;
        std::string hitFrame;
        float bestDistance = kSpriteAimMaxDistance;

        spriteQuery.each(
            [&](flecs::entity entity, SpriteInstance& sprite, GlobalTransformation& global) {
                if (!entity.has<WorldSpace>()) {
                    return;
                }
                const auto billboard = resolveSpriteBillboard(sprite, global, lens, assets);
                if (!billboard) {
                    return;
                }
                if (const auto hit = raycastSpriteBillboard(ray, *billboard, bestDistance)) {
                    bestDistance = hit->distance;
                    bestHit = *hit;
                    hitSprite = sprite.sprite;
                    hitFrame = sprite.frame;
                }
            });

        const Vector3 rayEnd = bestHit
            ? bestHit->point
            : Vector3Add(ray.position, Vector3Scale(ray.direction, kSpriteAimMaxDistance));
        DrawLine3D(ray.position, rayEnd, bestHit ? Color{80, 255, 120, 255} : Color{255, 220, 40, 255});

        if (bestHit) {
            DrawSphereWires(bestHit->point, 0.04f, 6, 6, Color{80, 255, 120, 255});
            const Vector3 tickEnd = Vector3Add(bestHit->point, Vector3Scale(ray.direction, -0.12f));
            DrawLine3D(bestHit->point, tickEnd, Color{255, 255, 255, 255});
            char buffer[256];
            std::snprintf(
                buffer,
                sizeof(buffer),
                "Sprite aim: %s frame=%s part=%s px=(%d,%d) dist=%.2f",
                hitSprite.c_str(),
                hitFrame.c_str(),
                bestHit->partName.c_str(),
                bestHit->pixelX,
                bestHit->pixelY,
                bestHit->distance);
            aimStatus = buffer;
        } else {
            aimStatus = "Sprite aim: no hit";
        }
    }

    rlEnableDepthMask();
    rlEnableDepthTest();
    EndBlendMode();
    return aimStatus;
}

constexpr float kLightProbeDebugMaxDistance = 4.0f;
constexpr float kLightProbeDebugMaxDistanceSq = kLightProbeDebugMaxDistance * kLightProbeDebugMaxDistance;
// Probes closer than this sit inside/at the camera's own position; at that range even a small
// world-space sphere radius fills the screen, so skip them rather than draw a giant blob.
constexpr float kLightProbeDebugMinDistance = 0.2f;
constexpr float kLightProbeDebugMinDistanceSq = kLightProbeDebugMinDistance * kLightProbeDebugMinDistance;

void drawProbeGridDots(const ProbeGrid& grid, Vector3 cameraPosition, bool fine) {
    const float radius = fine ? 0.035f : 0.06f;
    for (const auto& [cell, sh] : grid.probesByCell) {
        const Vector3 worldPos{
            static_cast<float>(cell.x) * grid.cellSize,
            static_cast<float>(cell.y) * grid.cellSize,
            static_cast<float>(cell.z) * grid.cellSize,
        };
        const float dx = worldPos.x - cameraPosition.x;
        const float dy = worldPos.y - cameraPosition.y;
        const float dz = worldPos.z - cameraPosition.z;
        const float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq > kLightProbeDebugMaxDistanceSq || distSq < kLightProbeDebugMinDistanceSq) {
            continue;
        }
        // Reconstruct toward straight up, matching the {0,1,0} "eye" query used for actual
        // sprite lighting (see sampleLightProbe callers) — showing only coeff[0] (the DC/average
        // term) hides the sun-facing lobe entirely and makes sunlit probes look dull.
        const float r = std::max(0.0f, sh.coeff[0].x + sh.coeff[2].x);
        const float g = std::max(0.0f, sh.coeff[0].y + sh.coeff[2].y);
        const float b = std::max(0.0f, sh.coeff[0].z + sh.coeff[2].z);
        const Color color = linearIrradianceToDisplayColor(r, g, b);
        DrawSphere(worldPos, radius, color);
    }
}

void drawSpriteLightSampleTaps(
    const MapLighting& lighting,
    const Lens& lens,
    AssetStore& assets,
    flecs::query<SpriteInstance, GlobalTransformation>& spriteQuery) {
    const float radius = 0.05f;
    spriteQuery.each(
        [&](flecs::entity entity, SpriteInstance& sprite, GlobalTransformation& global) {
            if (!entity.has<WorldSpace>() || entity.has<ViewSprite>()) {
                return;
            }
            const auto billboard = resolveSpriteBillboard(sprite, global, lens, assets);
            if (!billboard) {
                return;
            }

            const Vector3 feetOrigin{
                billboard->position.x,
                billboard->position.y + 0.05f,
                billboard->position.z};
            Color colorFeet = WHITE;
            if (auto feet = sampleLightProbe(lighting, feetOrigin, {0.0f, -1.0f, 0.0f})) {
                colorFeet = *feet;
            } else if (auto feetRay =
                           sampleMapLight(lighting, feetOrigin, {0.0f, -1.0f, 0.0f}, 2.0f)) {
                colorFeet = *feetRay;
            }
            DrawSphere(feetOrigin, radius, colorFeet);

            const Vector3 headPos{
                billboard->position.x,
                billboard->position.y + billboard->size.y,
                billboard->position.z};
            Vector3 headDir{
                lens.camera.position.x - headPos.x,
                0.0f,
                lens.camera.position.z - headPos.z,
            };
            Color colorHead = colorFeet;
            const float headLenSq = Vector3LengthSqr(headDir);
            if (headLenSq > 1e-8f) {
                headDir = Vector3Scale(headDir, 1.0f / std::sqrt(headLenSq));
                if (auto head = sampleLightProbe(lighting, headPos, headDir)) {
                    colorHead = *head;
                } else if (auto headRay = sampleMapLight(lighting, headPos, headDir, 4.0f)) {
                    colorHead = *headRay;
                }
            }
            DrawSphere(headPos, radius, colorHead);
        });
}

void drawLightProbeDebugOverlays(
    const MapLighting* lighting,
    const DebugUiState& debugUi,
    const Lens& lens,
    AssetStore& assets,
    flecs::query<SpriteInstance, GlobalTransformation>& spriteQuery) {
    const bool any = debugUi.showLightProbesFine || debugUi.showLightProbesCoarse
        || debugUi.showLightProbeSampleTaps;
    if (!any || lighting == nullptr || !lighting->available) {
        return;
    }

    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthMask();

    if (debugUi.showLightProbesFine) {
        drawProbeGridDots(lighting->probeGridFine, lens.camera.position, true);
    }
    if (debugUi.showLightProbesCoarse) {
        drawProbeGridDots(lighting->probeGridCoarse, lens.camera.position, false);
    }
    if (debugUi.showLightProbeSampleTaps) {
        drawSpriteLightSampleTaps(*lighting, lens, assets, spriteQuery);
    }

    rlEnableDepthMask();
    EndBlendMode();
}

void drawDebugLinePool(const DebugLinePool& pool) {
    if (pool.lines.empty()) {
        return;
    }

    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthTest();
    rlDisableDepthMask();

    for (const DebugLine& line : pool.lines) {
        DrawLine3D(line.from, line.to, line.color);
        DrawSphereWires(line.from, 0.04f, 4, 4, line.color);
        DrawSphereWires(line.to, 0.04f, 4, 4, line.color);
    }

    rlEnableDepthMask();
    rlEnableDepthTest();
    EndBlendMode();
}

}
