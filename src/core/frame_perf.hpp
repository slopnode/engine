#pragma once

namespace slopengine {

inline constexpr int kFramePerfHistorySize = 240;

struct FramePerfStats {
    float frameMs = 0.0f;
    float physicsMs = 0.0f;
    float renderMs = 0.0f;
    float uiMs = 0.0f;

    float frameHistory[kFramePerfHistorySize]{};
    float physicsHistory[kFramePerfHistorySize]{};
    float renderHistory[kFramePerfHistorySize]{};
    float uiHistory[kFramePerfHistorySize]{};
    int historyOffset = 0;
    int historyCount = 0;

    void pushSample();
};

double perfNow();
float perfElapsedMs(double start);
float processRssMb();

}
