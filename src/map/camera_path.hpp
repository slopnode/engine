#pragma once

#include "map/graph.hpp"

#include <raylib.h>

#include <vector>

namespace slopengine {

struct CameraPose {
    Vector3 position{0.0f, 0.0f, 0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    float fov = 0.0f;
};

Vector3 cameraPoseForward(const CameraPose& pose);
void yawPitchFromDirection(Vector3 direction, float& yaw, float& pitch);

int cameraPathSegmentCount(const CameraPath& path);
float cameraPathSegmentDuration(const CameraPath& path, int segment);
float cameraPathDuration(const CameraPath& path);
float cameraPathLength(const CameraPath& path);

CameraPose sampleCameraPathSegment(const CameraPath& path, int segment, float u);

CameraPose sampleCameraPath(const CameraPath& path, float time);

void tessellateCameraPath(const CameraPath& path, int perSegment, std::vector<Vector3>& out);

float applyPathEase(PathEase ease, float u);

}
