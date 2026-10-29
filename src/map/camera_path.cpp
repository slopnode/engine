#include "map/camera_path.hpp"

#include <raymath.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace slopengine {

namespace {

constexpr float kPi = 3.14159265358979323846f;

int wrapIndex(int index, int count) {
    if (count <= 0) {
        return 0;
    }
    index %= count;
    if (index < 0) {
        index += count;
    }
    return index;
}

int knotIndexFor(const CameraPath& path, int index) {
    const int count = static_cast<int>(path.knots.size());
    if (count <= 0) {
        return 0;
    }
    if (path.closed) {
        return wrapIndex(index, count);
    }
    return std::clamp(index, 0, count - 1);
}

float unwrapYaw(float yaw, float reference) {
    float delta = yaw - reference;
    while (delta > kPi) {
        delta -= 2.0f * kPi;
    }
    while (delta < -kPi) {
        delta += 2.0f * kPi;
    }
    return reference + delta;
}

float wrapYaw(float yaw) {
    while (yaw > kPi) {
        yaw -= 2.0f * kPi;
    }
    while (yaw < -kPi) {
        yaw += 2.0f * kPi;
    }
    return yaw;
}

struct SegmentControl {
    Vector3 p0, p1, p2, p3;
    Vector3 o0, o1, o2, o3;
    float t0 = 0.0f, t1 = 1.0f, t2 = 2.0f, t3 = 3.0f;
};

Vector3 orientationVector(const CameraPath& path, int index, float yawReference) {
    const PathKnot& knot = path.knots[static_cast<std::size_t>(knotIndexFor(path, index))];
    float fov = knot.fov > 0.0f ? knot.fov : path.fov;
    return Vector3{unwrapYaw(knot.yaw, yawReference), knot.pitch, fov};
}

SegmentControl segmentControl(const CameraPath& path, int segment) {
    SegmentControl c{};
    const int i1 = knotIndexFor(path, segment);
    const int i2 = knotIndexFor(path, segment + 1);
    const int i0 = knotIndexFor(path, segment - 1);
    const int i3 = knotIndexFor(path, segment + 2);

    c.p0 = path.knots[static_cast<std::size_t>(i0)].at;
    c.p1 = path.knots[static_cast<std::size_t>(i1)].at;
    c.p2 = path.knots[static_cast<std::size_t>(i2)].at;
    c.p3 = path.knots[static_cast<std::size_t>(i3)].at;

    const float yawRef = path.knots[static_cast<std::size_t>(i1)].yaw;
    c.o1 = orientationVector(path, segment, yawRef);
    c.o0 = orientationVector(path, segment - 1, c.o1.x);
    c.o2 = orientationVector(path, segment + 1, c.o1.x);
    c.o3 = orientationVector(path, segment + 2, c.o2.x);

    auto knotTime = [](Vector3 a, Vector3 b) {
        const float d = std::sqrt(Vector3Distance(a, b));
        return d > 1e-4f ? d : 1e-4f;
    };
    c.t0 = 0.0f;
    c.t1 = c.t0 + knotTime(c.p0, c.p1);
    c.t2 = c.t1 + knotTime(c.p1, c.p2);
    c.t3 = c.t2 + knotTime(c.p2, c.p3);
    return c;
}

Vector3 barryGoldman(
    Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3,
    float t0, float t1, float t2, float t3,
    float t) {
    auto lerpAt = [](Vector3 a, Vector3 b, float ta, float tb, float tt) {
        const float span = tb - ta;
        if (std::fabs(span) < 1e-8f) {
            return a;
        }
        const float w = (tt - ta) / span;
        return Vector3Add(Vector3Scale(a, 1.0f - w), Vector3Scale(b, w));
    };
    const Vector3 a1 = lerpAt(p0, p1, t0, t1, t);
    const Vector3 a2 = lerpAt(p1, p2, t1, t2, t);
    const Vector3 a3 = lerpAt(p2, p3, t2, t3, t);
    const Vector3 b1 = lerpAt(a1, a2, t0, t2, t);
    const Vector3 b2 = lerpAt(a2, a3, t1, t3, t);
    return lerpAt(b1, b2, t1, t2, t);
}

Vector3 evalPosition(const CameraPath& path, const SegmentControl& c, float u) {
    if (path.interp == PathInterp::Linear || path.knots.size() < 3) {
        return Vector3Lerp(c.p1, c.p2, u);
    }
    const float t = c.t1 + (c.t2 - c.t1) * u;
    return barryGoldman(c.p0, c.p1, c.p2, c.p3, c.t0, c.t1, c.t2, c.t3, t);
}

Vector3 evalOrientation(const CameraPath& path, const SegmentControl& c, float u) {
    if (path.interp == PathInterp::Linear || path.knots.size() < 3) {
        return Vector3Lerp(c.o1, c.o2, u);
    }
    const float t = c.t1 + (c.t2 - c.t1) * u;
    return barryGoldman(c.o0, c.o1, c.o2, c.o3, c.t0, c.t1, c.t2, c.t3, t);
}

float segmentLength(const CameraPath& path, int segment, int steps = 16) {
    const SegmentControl c = segmentControl(path, segment);
    float length = 0.0f;
    Vector3 prev = evalPosition(path, c, 0.0f);
    for (int i = 1; i <= steps; ++i) {
        const Vector3 next = evalPosition(path, c, static_cast<float>(i) / static_cast<float>(steps));
        length += Vector3Distance(prev, next);
        prev = next;
    }
    return length;
}

PathEase segmentEase(const CameraPath& path, int segment) {
    const PathKnot& knot = path.knots[static_cast<std::size_t>(knotIndexFor(path, segment))];
    return knot.haveEase ? knot.ease : path.ease;
}

} // namespace

const char* pathInterpName(PathInterp interp) {
    switch (interp) {
    case PathInterp::Linear:
        return "linear";
    case PathInterp::CatmullRom:
        return "catmull-rom";
    }
    return "catmull-rom";
}

const char* pathOrientName(PathOrient orient) {
    switch (orient) {
    case PathOrient::Knots:
        return "knots";
    case PathOrient::Tangent:
        return "tangent";
    case PathOrient::LookAt:
        return "look-at";
    }
    return "knots";
}

const char* pathEaseName(PathEase ease) {
    switch (ease) {
    case PathEase::Linear:
        return "linear";
    case PathEase::Smooth:
        return "smooth";
    case PathEase::In:
        return "in";
    case PathEase::Out:
        return "out";
    }
    return "linear";
}

bool parsePathInterp(const char* name, PathInterp& out) {
    if (name == nullptr) {
        return false;
    }
    if (std::strcmp(name, "linear") == 0) {
        out = PathInterp::Linear;
        return true;
    }
    if (std::strcmp(name, "catmull-rom") == 0 || std::strcmp(name, "smooth") == 0) {
        out = PathInterp::CatmullRom;
        return true;
    }
    return false;
}

bool parsePathOrient(const char* name, PathOrient& out) {
    if (name == nullptr) {
        return false;
    }
    if (std::strcmp(name, "knots") == 0) {
        out = PathOrient::Knots;
        return true;
    }
    if (std::strcmp(name, "tangent") == 0) {
        out = PathOrient::Tangent;
        return true;
    }
    if (std::strcmp(name, "look-at") == 0) {
        out = PathOrient::LookAt;
        return true;
    }
    return false;
}

bool parsePathEase(const char* name, PathEase& out) {
    if (name == nullptr) {
        return false;
    }
    if (std::strcmp(name, "linear") == 0) {
        out = PathEase::Linear;
        return true;
    }
    if (std::strcmp(name, "smooth") == 0) {
        out = PathEase::Smooth;
        return true;
    }
    if (std::strcmp(name, "in") == 0) {
        out = PathEase::In;
        return true;
    }
    if (std::strcmp(name, "out") == 0) {
        out = PathEase::Out;
        return true;
    }
    return false;
}

Vector3 cameraPoseForward(const CameraPose& pose) {
    const float cosPitch = std::cos(pose.pitch);
    return Vector3Normalize({
        std::sin(pose.yaw) * cosPitch,
        std::sin(pose.pitch),
        std::cos(pose.yaw) * cosPitch,
    });
}

void yawPitchFromDirection(Vector3 direction, float& yaw, float& pitch) {
    const float lengthSq = Vector3LengthSqr(direction);
    if (lengthSq < 1e-12f) {
        return;
    }
    const Vector3 d = Vector3Scale(direction, 1.0f / std::sqrt(lengthSq));
    yaw = std::atan2(d.x, d.z);
    pitch = std::asin(std::clamp(d.y, -1.0f, 1.0f));
}

int cameraPathSegmentCount(const CameraPath& path) {
    const int count = static_cast<int>(path.knots.size());
    if (count < 2) {
        return 0;
    }
    return path.closed ? count : count - 1;
}

float cameraPathSegmentDuration(const CameraPath& path, int segment) {
    const int segments = cameraPathSegmentCount(path);
    if (segment < 0 || segment >= segments) {
        return 0.0f;
    }
    const PathKnot& knot = path.knots[static_cast<std::size_t>(segment)];
    const float duration = knot.duration > 0.0f ? knot.duration : path.duration;
    return duration > 1e-4f ? duration : 1e-4f;
}

float cameraPathDuration(const CameraPath& path) {
    float total = 0.0f;
    const int segments = cameraPathSegmentCount(path);
    for (int i = 0; i < segments; ++i) {
        total += cameraPathSegmentDuration(path, i);
    }
    return total;
}

float cameraPathLength(const CameraPath& path) {
    float total = 0.0f;
    const int segments = cameraPathSegmentCount(path);
    for (int i = 0; i < segments; ++i) {
        total += segmentLength(path, i);
    }
    return total;
}

CameraPose sampleCameraPathSegment(const CameraPath& path, int segment, float u) {
    CameraPose pose{};
    if (path.knots.empty()) {
        return pose;
    }
    const int segments = cameraPathSegmentCount(path);
    if (segments == 0) {
        const PathKnot& knot = path.knots.front();
        pose.position = knot.at;
        pose.yaw = knot.yaw;
        pose.pitch = knot.pitch;
        pose.fov = knot.fov > 0.0f ? knot.fov : path.fov;
        if (path.orient == PathOrient::LookAt) {
            yawPitchFromDirection(Vector3Subtract(path.lookAt, pose.position), pose.yaw, pose.pitch);
        }
        return pose;
    }
    segment = std::clamp(segment, 0, segments - 1);
    u = std::clamp(u, 0.0f, 1.0f);

    const SegmentControl c = segmentControl(path, segment);
    pose.position = evalPosition(path, c, u);
    const Vector3 orient = evalOrientation(path, c, u);
    pose.yaw = wrapYaw(orient.x);
    pose.pitch = orient.y;
    pose.fov = orient.z;

    if (path.orient == PathOrient::Tangent) {
        const float du = 1.0f / 64.0f;
        const float ua = std::max(0.0f, u - du);
        const float ub = std::min(1.0f, u + du);
        const Vector3 tangent =
            Vector3Subtract(evalPosition(path, c, ub), evalPosition(path, c, ua));
        yawPitchFromDirection(tangent, pose.yaw, pose.pitch);
    } else if (path.orient == PathOrient::LookAt) {
        yawPitchFromDirection(Vector3Subtract(path.lookAt, pose.position), pose.yaw, pose.pitch);
    }
    return pose;
}

CameraPose sampleCameraPath(const CameraPath& path, float time) {
    const int segments = cameraPathSegmentCount(path);
    if (segments == 0) {
        return sampleCameraPathSegment(path, 0, 0.0f);
    }
    const float total = cameraPathDuration(path);
    time = std::clamp(time, 0.0f, total);

    if (path.constantSpeed) {
        const float length = cameraPathLength(path);
        if (length <= 1e-6f) {
            return sampleCameraPathSegment(path, 0, 0.0f);
        }
        float target = (time / total) * length;
        for (int i = 0; i < segments; ++i) {
            const float segLength = segmentLength(path, i);
            if (target <= segLength || i == segments - 1) {
                const SegmentControl c = segmentControl(path, i);
                constexpr int kSteps = 32;
                float walked = 0.0f;
                Vector3 prev = evalPosition(path, c, 0.0f);
                for (int s = 1; s <= kSteps; ++s) {
                    const float u = static_cast<float>(s) / static_cast<float>(kSteps);
                    const Vector3 next = evalPosition(path, c, u);
                    const float step = Vector3Distance(prev, next);
                    if (walked + step >= target || s == kSteps) {
                        const float frac = step > 1e-8f ? (target - walked) / step : 1.0f;
                        const float uPrev = static_cast<float>(s - 1) / static_cast<float>(kSteps);
                        return sampleCameraPathSegment(
                            path, i, uPrev + (u - uPrev) * std::clamp(frac, 0.0f, 1.0f));
                    }
                    walked += step;
                    prev = next;
                }
                return sampleCameraPathSegment(path, i, 1.0f);
            }
            target -= segLength;
        }
        return sampleCameraPathSegment(path, segments - 1, 1.0f);
    }

    float start = 0.0f;
    for (int i = 0; i < segments; ++i) {
        const float duration = cameraPathSegmentDuration(path, i);
        if (time <= start + duration || i == segments - 1) {
            const float u = std::clamp((time - start) / duration, 0.0f, 1.0f);
            return sampleCameraPathSegment(path, i, applyPathEase(segmentEase(path, i), u));
        }
        start += duration;
    }
    return sampleCameraPathSegment(path, segments - 1, 1.0f);
}

void tessellateCameraPath(const CameraPath& path, int perSegment, std::vector<Vector3>& out) {
    const int segments = cameraPathSegmentCount(path);
    if (segments == 0) {
        for (const PathKnot& knot : path.knots) {
            out.push_back(knot.at);
        }
        return;
    }
    perSegment = std::max(1, perSegment);
    for (int i = 0; i < segments; ++i) {
        const SegmentControl c = segmentControl(path, i);
        const int first = i == 0 ? 0 : 1;
        for (int s = first; s <= perSegment; ++s) {
            out.push_back(evalPosition(path, c, static_cast<float>(s) / static_cast<float>(perSegment)));
        }
    }
}

float applyPathEase(PathEase ease, float u) {
    u = std::clamp(u, 0.0f, 1.0f);
    switch (ease) {
    case PathEase::Linear:
        return u;
    case PathEase::Smooth:
        return u * u * (3.0f - 2.0f * u);
    case PathEase::In:
        return u * u;
    case PathEase::Out:
        return 1.0f - (1.0f - u) * (1.0f - u);
    }
    return u;
}

}
