#include "fx/tracer.hpp"

#include "game/game_state.hpp"

#include <algorithm>
#include <raymath.h>
#include <rlgl.h>

namespace slopengine {

namespace {

constexpr std::size_t kMaxTracers = 512;

struct RibbonEnd {
    Vector3 center{};
    Vector3 offset{};
    float alpha = 0.0f;
};

void emitVertex(Vector3 p, Color color, float alpha) {
    rlColor4ub(
        color.r,
        color.g,
        color.b,
        static_cast<unsigned char>(std::clamp(alpha, 0.0f, 1.0f) * static_cast<float>(color.a)));
    rlVertex3f(p.x, p.y, p.z);
}

void drawRibbonLayer(const RibbonEnd& tail, const RibbonEnd& tip, Color color) {
    const Vector3 tailLeft = Vector3Subtract(tail.center, tail.offset);
    const Vector3 tailRight = Vector3Add(tail.center, tail.offset);
    const Vector3 tipLeft = Vector3Subtract(tip.center, tip.offset);
    const Vector3 tipRight = Vector3Add(tip.center, tip.offset);
    rlBegin(RL_QUADS);
    rlTexCoord2f(0.5f, 0.5f);
    emitVertex(tailLeft, color, 0.0f);
    emitVertex(tail.center, color, tail.alpha);
    emitVertex(tip.center, color, tip.alpha);
    emitVertex(tipLeft, color, 0.0f);
    emitVertex(tail.center, color, tail.alpha);
    emitVertex(tailRight, color, 0.0f);
    emitVertex(tipRight, color, 0.0f);
    emitVertex(tip.center, color, tip.alpha);
    rlEnd();
}

} // namespace

void spawnTracer(flecs::world& world, Vector3 from, Vector3 to, const TracerStyle& style, float lead) {
    if (!world.has<TracerPool>()) {
        return;
    }
    TracerPool& pool = world.get_mut<TracerPool>();
    if (pool.tracers.size() >= kMaxTracers) {
        pool.tracers.erase(pool.tracers.begin());
    }
    pool.tracers.push_back(makeTracer(from, to, style, lead));
}

void clearTracers(flecs::world& world) {
    if (world.has<TracerPool>()) {
        world.get_mut<TracerPool>().tracers.clear();
    }
}

void registerTracerModule(flecs::world& world) {
    world.component<TracerPool>();
    world.set<TracerPool>({});

    world.system("TracerUpdate")
        .kind(flecs::OnUpdate)
        .run([](flecs::iter& it) {
            flecs::world world = it.world();
            if (!isPlaying(world) || !world.has<TracerPool>()) {
                return;
            }
            const float dt = GetFrameTime();
            std::vector<Tracer>& tracers = world.get_mut<TracerPool>().tracers;
            tracers.erase(
                std::remove_if(
                    tracers.begin(),
                    tracers.end(),
                    [dt](Tracer& tracer) { return !advanceTracer(tracer, dt); }),
                tracers.end());
        });
}

void drawTracers(flecs::world& world, const Camera3D& camera) {
    if (!world.has<TracerPool>() || world.get<TracerPool>().tracers.empty()) {
        return;
    }
    const float screenHeight = static_cast<float>(GetRenderHeight());
    const Matrix matView = MatrixLookAt(camera.position, camera.target, camera.up);
    const Vector3 fallbackRight = Vector3Normalize(Vector3{matView.m0, matView.m4, matView.m8});

    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    BeginBlendMode(BLEND_ADDITIVE);
    rlSetTexture(rlGetTextureIdDefault());

    for (const Tracer& tracer : world.get<TracerPool>().tracers) {
        TracerSpan span{};
        if (!tracerSpan(tracer, span)) {
            continue;
        }
        const TracerStyle& style = tracer.style;
        const Vector3 middle = Vector3Lerp(span.tail, span.tip, 0.5f);
        const Vector3 toCam = Vector3Subtract(camera.position, middle);
        Vector3 right = Vector3CrossProduct(tracer.direction, toCam);
        right = Vector3LengthSqr(right) < 1.0e-8f ? fallbackRight : Vector3Normalize(right);

        const float tailDistance = Vector3Distance(camera.position, span.tail);
        const float tipDistance = Vector3Distance(camera.position, span.tip);
        const float tailCore =
            tracerWidthAt(style.width, style.minPixels, tailDistance, camera.fovy, screenHeight);
        const float tipCore =
            tracerWidthAt(style.width, style.minPixels, tipDistance, camera.fovy, screenHeight);
        const float tailAlpha = style.tailAlpha + (1.0f - style.tailAlpha) * span.tailT;
        const float tipAlpha = style.tailAlpha + (1.0f - style.tailAlpha) * span.tipT;

        if (style.glowWidth > 0.0f && style.glow.a > 0) {
            const float glowScale = style.width > 0.0f ? style.glowWidth / style.width : 1.0f;
            drawRibbonLayer(
                {span.tail, Vector3Scale(right, std::max(style.glowWidth, tailCore * glowScale)), tailAlpha},
                {span.tip, Vector3Scale(right, std::max(style.glowWidth, tipCore * glowScale)), tipAlpha},
                style.glow);
        }
        drawRibbonLayer(
            {span.tail, Vector3Scale(right, tailCore), tailAlpha},
            {span.tip, Vector3Scale(right, tipCore), tipAlpha},
            style.core);
    }

    rlSetTexture(0);
    rlDrawRenderBatchActive();
    EndBlendMode();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
}

}
