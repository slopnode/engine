#pragma once

#include <raylib.h>

#include <vector>

namespace slopengine {

struct MsaaTarget {
    RenderTexture2D target{};
    unsigned int colorRenderbuffer = 0;
    int samples = 0;
    int width = 0;
    int height = 0;

    MsaaTarget() = default;
    MsaaTarget(const MsaaTarget&) = delete;
    MsaaTarget& operator=(const MsaaTarget&) = delete;
    ~MsaaTarget();

    void unload();
};

int maxMsaaSamples();
std::vector<int> msaaSampleOptions();

bool ensureMsaaTarget(MsaaTarget& msaa, int width, int height, int samples);
void resolveMsaaTarget(const MsaaTarget& msaa, const RenderTexture2D& destination);

}
