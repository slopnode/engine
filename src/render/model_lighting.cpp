#include "render/model_lighting.hpp"

#include "map/light_components.hpp"
#include "map/light_sample.hpp"
#include "render/components.hpp"
#include "render/dynamic_light.hpp"
#include "render/fx_local_light.hpp"
#include "render/render_frustum.hpp"

#include <raymath.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace slopengine {

namespace {

constexpr float kModelLightFadeRate = 6.0f;
constexpr double kModelTrackStaleSeconds = 1.0;
constexpr double kModelTrackPruneInterval = 2.0;
constexpr int kModelLightVisibilityCandidates = kMaxModelStaticLights + 2;

float smoothstepRange(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float luminance(Vector3 rgb) {
    return rgb.x * 0.2126f + rgb.y * 0.7152f + rgb.z * 0.0722f;
}

Vector3 inverseTonemap(Color display) {
    auto channel = [](unsigned char value) {
        const float v = std::min(static_cast<float>(value) / 255.0f, 0.98f);
        return v / (1.0f - v);
    };
    return {channel(display.r), channel(display.g), channel(display.b)};
}

float staticLightVisibility(
    const MapLighting& lighting,
    const StaticModelLight& light,
    Vector3 center,
    Vector3 top) {
    const QuadBvh* bvh = lighting.surfaceBvh.empty() ? nullptr : &lighting.surfaceBvh;
    const std::vector<char>* skip =
        lighting.faceTransparentSkip.empty() ? nullptr : &lighting.faceTransparentSkip;
    float visible = 0.0f;
    if (!lightSegmentOccluded(bvh, light.position, center, skip)) {
        visible += 0.5f;
    }
    if (!lightSegmentOccluded(bvh, light.position, top, skip)) {
        visible += 0.5f;
    }
    return visible;
}

void updateTracks(
    ModelLightEntityTrack& track,
    const std::vector<std::pair<flecs::entity_t, float>>& targets,
    const ModelLightingState& state,
    bool firstUpdate,
    float dt) {
    for (ModelLightTrack& tracked : track.lights) {
        float target = 0.0f;
        for (const auto& [id, visibility] : targets) {
            if (id == tracked.light) {
                target = visibility;
                break;
            }
        }
        const float step = kModelLightFadeRate * dt;
        if (tracked.fade < target) {
            tracked.fade = std::min(target, tracked.fade + step);
        } else {
            tracked.fade = std::max(target, tracked.fade - step);
        }
    }
    for (const auto& [id, visibility] : targets) {
        const bool known = std::any_of(
            track.lights.begin(), track.lights.end(), [id](const ModelLightTrack& tracked) {
                return tracked.light == id;
            });
        if (!known) {
            track.lights.push_back({id, firstUpdate ? visibility : 0.0f});
        }
    }
    track.lights.erase(
        std::remove_if(
            track.lights.begin(),
            track.lights.end(),
            [&](const ModelLightTrack& tracked) {
                if (state.lightIndexById.find(tracked.light) == state.lightIndexById.end()) {
                    return true;
                }
                if (tracked.fade > 0.0f) {
                    return false;
                }
                return std::none_of(targets.begin(), targets.end(), [&](const auto& target) {
                    return target.first == tracked.light;
                });
            }),
        track.lights.end());
}

void pruneStaleTracks(ModelLightingState& state, double now) {
    if (now - state.lastPruneTime < kModelTrackPruneInterval) {
        return;
    }
    state.lastPruneTime = now;
    for (auto it = state.tracks.begin(); it != state.tracks.end();) {
        if (now - it->second.lastTime > kModelTrackStaleSeconds) {
            it = state.tracks.erase(it);
        } else {
            ++it;
        }
    }
}

ModelLightingShaderLocs& shaderLocs(Shader shader) {
    static ModelLightingShaderLocs locs{};
    if (locs.shaderId != shader.id) {
        locs = {};
        locs.shaderId = shader.id;
        locs.modelLit = GetShaderLocation(shader, "modelLit");
        locs.probeCube = GetShaderLocation(shader, "modelProbeCube");
        locs.lightCount = GetShaderLocation(shader, "modelLightCount");
        locs.lightPosRange = GetShaderLocation(shader, "modelLightPosRange");
        locs.lightColor = GetShaderLocation(shader, "modelLightColor");
        locs.lightDirCone = GetShaderLocation(shader, "modelLightDirCone");
        locs.lightWrap = GetShaderLocation(shader, "modelLightWrap");
        locs.viewSpace = GetShaderLocation(shader, "modelViewSpace");
        locs.worldFromView = GetShaderLocation(shader, "modelWorldFromView");
    }
    return locs;
}

}

float staticLightScoreAtPoint(const StaticModelLight& light, Vector3 point, float radius) {
    const Vector3 delta = Vector3Subtract(light.position, point);
    const float dist = Vector3Length(delta);
    const float range = std::max(light.range, 1e-4f);
    const float nearDist = std::max(dist - radius, 0.0f);
    if (nearDist > range) {
        return 0.0f;
    }
    const float t = nearDist / range;
    float atten = std::max(0.0f, 1.0f - t * t);
    atten *= atten;
    float spot = 1.0f;
    if (light.spot && dist > 1e-4f) {
        const Vector3 toLight = Vector3Scale(delta, 1.0f / dist);
        const float cosTheta = Vector3DotProduct(Vector3Scale(toLight, -1.0f), light.direction);
        spot = smoothstepRange(std::cos(light.coneAngle), std::cos(light.coneAngle * 0.8f), cosTheta);
    }
    const float dist2 = std::max(nearDist * nearDist, 0.25f);
    return luminance(light.radiance) * atten * spot / dist2;
}

void gatherStaticModelLights(flecs::world& world, ModelLightingState& state) {
    state.lights.clear();
    state.lightIndexById.clear();
    auto push = [&](flecs::entity entity, StaticModelLight light) {
        if (light.range <= 0.0f || luminance(light.radiance) <= 0.0f) {
            return;
        }
        state.lightIndexById[entity.id()] = state.lights.size();
        state.lights.push_back(light);
    };
    world.each([&](flecs::entity entity, const PointLight& point, const LocalTransformation& local) {
        push(entity, StaticModelLight{
            .id = entity.id(),
            .position = local.position,
            .radiance = Vector3Scale(point.color, point.intensity),
            .range = point.range,
        });
    });
    world.each([&](flecs::entity entity, const SpotLight& spot, const LocalTransformation& local) {
        push(entity, StaticModelLight{
            .id = entity.id(),
            .position = local.position,
            .direction = dynamicLightDirectionFromRotation(local.rotation),
            .radiance = Vector3Scale(spot.color, spot.intensity),
            .range = spot.range,
            .coneAngle = spot.coneAngle,
            .spot = true,
        });
    });
}

bool computeModelLighting(
    flecs::world& world,
    ModelLightingState& state,
    flecs::entity entity,
    const Model& model,
    const Matrix& globalMatrix,
    ModelLightingUniforms& out) {
    if (!world.has<MapLighting>()) {
        return false;
    }
    const MapLighting& lighting = world.get<MapLighting>();
    out = {};

    const BoundingBox localBounds = GetModelBoundingBox(model);
    const BoundingBox bounds = transformAabb(localBounds, globalMatrix);
    const Vector3 center = Vector3Scale(Vector3Add(bounds.min, bounds.max), 0.5f);
    const Vector3 extent = Vector3Subtract(bounds.max, bounds.min);
    const float radius = Vector3Length(extent) * 0.5f;
    const Vector3 top{center.x, center.y + extent.y * 0.4f, center.z};

    std::optional<ProbeCube> cube = sampleLightProbeCube(lighting, center);
    if (!cube) {
        cube = sampleLightProbeCube(lighting, {center.x, bounds.min.y + 0.25f, center.z});
    }
    if (cube) {
        for (std::size_t i = 0; i < kProbeCubeFaces; ++i) {
            out.probeCube[i] = cube->face[i];
        }
    } else {
        const Vector3 flat = inverseTonemap(sampleBakeTintColorForModel(world, model, globalMatrix, false));
        for (Vector3& face : out.probeCube) {
            face = flat;
        }
    }

    struct Candidate {
        flecs::entity_t id = 0;
        float score = 0.0f;
        float visibility = 0.0f;
    };
    std::vector<Candidate> candidates;
    for (const StaticModelLight& light : state.lights) {
        const float score = staticLightScoreAtPoint(light, center, radius);
        if (score > 0.0f) {
            candidates.push_back({light.id, score, 0.0f});
        }
    }
    const auto byScore = [](const Candidate& a, const Candidate& b) {
        return a.score * std::max(a.visibility, 1e-3f) > b.score * std::max(b.visibility, 1e-3f);
    };
    const std::size_t probeCount =
        std::min(candidates.size(), static_cast<std::size_t>(kModelLightVisibilityCandidates));
    std::partial_sort(
        candidates.begin(),
        candidates.begin() + static_cast<std::ptrdiff_t>(probeCount),
        candidates.end(),
        [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
    candidates.resize(probeCount);
    for (Candidate& candidate : candidates) {
        const StaticModelLight& light = state.lights[state.lightIndexById.at(candidate.id)];
        candidate.visibility = staticLightVisibility(lighting, light, center, top);
    }
    std::sort(candidates.begin(), candidates.end(), byScore);

    std::vector<std::pair<flecs::entity_t, float>> targets;
    for (const Candidate& candidate : candidates) {
        if (candidate.visibility <= 0.0f ||
            static_cast<int>(targets.size()) >= kMaxModelStaticLights) {
            continue;
        }
        targets.emplace_back(candidate.id, candidate.visibility);
    }

    const double now = GetTime();
    auto [trackIt, inserted] = state.tracks.try_emplace(entity.id());
    ModelLightEntityTrack& track = trackIt->second;
    const float dt = inserted
        ? 0.0f
        : static_cast<float>(std::clamp(now - track.lastTime, 0.0, 0.25));
    track.lastTime = now;
    updateTracks(track, targets, state, inserted, dt);
    pruneStaleTracks(state, now);

    std::vector<std::pair<const StaticModelLight*, float>> active;
    for (const ModelLightTrack& tracked : track.lights) {
        if (tracked.fade <= 0.0f) {
            continue;
        }
        active.emplace_back(&state.lights[state.lightIndexById.at(tracked.light)], tracked.fade);
    }
    std::sort(active.begin(), active.end(), [&](const auto& a, const auto& b) {
        return staticLightScoreAtPoint(*a.first, center, radius) * a.second >
            staticLightScoreAtPoint(*b.first, center, radius) * b.second;
    });
    for (const auto& [light, fade] : active) {
        if (out.lightCount >= kMaxModelStaticLights) {
            break;
        }
        const int slot = out.lightCount++;
        out.lightPosRange[slot] = {light->position.x, light->position.y, light->position.z, light->range};
        out.lightColor[slot] = {
            light->radiance.x * fade, light->radiance.y * fade, light->radiance.z * fade, 0.0f};
        out.lightDirCone[slot] = {
            light->direction.x,
            light->direction.y,
            light->direction.z,
            light->spot ? light->coneAngle : 0.0f};
    }
    return true;
}

Matrix firstPersonWorldFromView(const Camera3D& camera) {
    Vector3 forward = Vector3Subtract(camera.target, camera.position);
    if (Vector3LengthSqr(forward) < 1e-10f) {
        forward = {0.0f, 0.0f, 1.0f};
    }
    forward = Vector3Normalize(forward);
    Vector3 right = Vector3CrossProduct(forward, camera.up);
    if (Vector3LengthSqr(right) < 1e-10f) {
        right = Vector3CrossProduct(forward, {0.0f, 0.0f, 1.0f});
    }
    right = Vector3Normalize(right);
    const Vector3 up = Vector3CrossProduct(right, forward);
    Matrix m = MatrixIdentity();
    m.m0 = right.x;
    m.m1 = right.y;
    m.m2 = right.z;
    m.m4 = up.x;
    m.m5 = up.y;
    m.m6 = up.z;
    m.m8 = forward.x;
    m.m9 = forward.y;
    m.m10 = forward.z;
    m.m12 = camera.position.x;
    m.m13 = camera.position.y;
    m.m14 = camera.position.z;
    return m;
}

bool modelLightingSupported(Shader shader) {
    if (shader.id == 0) {
        return false;
    }
    const ModelLightingShaderLocs& locs = shaderLocs(shader);
    return locs.modelLit >= 0 && locs.probeCube >= 0;
}

void uploadModelLighting(Shader shader, const ModelLightingUniforms* uniforms) {
    if (shader.id == 0) {
        return;
    }
    const ModelLightingShaderLocs& locs = shaderLocs(shader);
    if (locs.modelLit < 0) {
        return;
    }
    const int enabled = uniforms != nullptr ? 1 : 0;
    SetShaderValue(shader, locs.modelLit, &enabled, SHADER_UNIFORM_INT);
    const int viewSpace = (uniforms != nullptr && uniforms->viewSpace) ? 1 : 0;
    if (locs.viewSpace >= 0) {
        SetShaderValue(shader, locs.viewSpace, &viewSpace, SHADER_UNIFORM_INT);
    }
    if (viewSpace != 0 && locs.worldFromView >= 0) {
        SetShaderValueMatrix(shader, locs.worldFromView, uniforms->worldFromView);
    }
    if (uniforms == nullptr) {
        return;
    }
    if (locs.probeCube >= 0) {
        SetShaderValueV(shader, locs.probeCube, uniforms->probeCube, SHADER_UNIFORM_VEC3, 6);
    }
    if (locs.lightCount >= 0) {
        SetShaderValue(shader, locs.lightCount, &uniforms->lightCount, SHADER_UNIFORM_INT);
    }
    if (locs.lightWrap >= 0) {
        const float wrap = kModelStaticLightWrap;
        SetShaderValue(shader, locs.lightWrap, &wrap, SHADER_UNIFORM_FLOAT);
    }
    if (uniforms->lightCount <= 0) {
        return;
    }
    if (locs.lightPosRange >= 0) {
        SetShaderValueV(
            shader, locs.lightPosRange, uniforms->lightPosRange, SHADER_UNIFORM_VEC4, uniforms->lightCount);
    }
    if (locs.lightColor >= 0) {
        SetShaderValueV(
            shader, locs.lightColor, uniforms->lightColor, SHADER_UNIFORM_VEC4, uniforms->lightCount);
    }
    if (locs.lightDirCone >= 0) {
        SetShaderValueV(
            shader, locs.lightDirCone, uniforms->lightDirCone, SHADER_UNIFORM_VEC4, uniforms->lightCount);
    }
}

}
