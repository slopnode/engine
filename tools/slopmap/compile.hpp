#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace slopmap {

enum class CompileStage {
    Bsp,
    Vis,
    Rad,
    Nav,
};

inline int compileStageIndex(CompileStage stage) {
    return static_cast<int>(stage);
}

inline const char* compileStageLabel(CompileStage stage) {
    switch (stage) {
    case CompileStage::Bsp:
        return "BSP";
    case CompileStage::Vis:
        return "VIS";
    case CompileStage::Rad:
        return "RAD";
    case CompileStage::Nav:
        return "NAV";
    }
    return "?";
}

inline constexpr int kCompileStageCount = 4;

/** Mirrors sloprad's --gpu-safe/--gpu-fast: how conservatively GPU batches are sized.
 *  Auto leaves it to sloprad, which enables safe mode on integrated GPUs. */
enum class GpuSafetyMode {
    Auto,
    Fast,
    Safe,
};

struct RadCompileOptions {
    float luxelsPerMeter = 16.0f;
    int bounces = 2;
    int samples = 16;
    int emitterDirectSamples = 4;
    float emitterGridLuxelsPerMeter = 8.0f;
    int emitterGridMaxSize = 32;
    int exactEmissionGridMaxSize = 256;
    int exactEmissionMaxSamples = 8192;
    float sunShadowSoftness = 0.0f;
    float seamStitchRadiusLuxels = 1.5f;
    /** Coarse light-probe grid spacing (world units), covering all open space. */
    float probeCellSize = 4.0f;
    /** Fine light-probe grid spacing (world units), placed only near geometry. */
    float probeFineCellSize = 2.0f;
    /** Sphere samples gathered per probe for the SH lighting projection. */
    int probeSampleCount = 32;
    bool preferGpu = true;
    /** On hybrid systems, request the discrete GPU for sloprad (Linux DRI_PRIME / Windows shim). */
    bool forceDiscreteGpu = true;
    /** How conservatively sloprad sizes GPU dispatch batches; independent of which physical
     *  GPU is used. */
    GpuSafetyMode gpuSafetyMode = GpuSafetyMode::Auto;
    /** Seconds a single unsynced GPU dispatch can run before the platform driver watchdog
     *  resets it. Windows TDR defaults to 2s; raise to match a higher TDR delay configured
     *  on the machine. */
    float gpuWatchdogLimitSeconds = 2.0f;
    /** Hard ceiling on luxels per GPU dispatch group. 0 = auto (sloprad picks 1024, or 2048
     *  for large luxel counts); ignored in Safe batch mode, which always caps at 256. */
    int gpuMaxLuxelBatch = 0;
};

struct CompileMountArgs {
    std::filesystem::path baseGame;
    std::vector<std::filesystem::path> mods;
    std::string mapName;
};

class CompileController {
public:
    RadCompileOptions radOptions{};
    bool showOptionsModal = false;
    bool showOutputWindow = false;

    const std::vector<std::string>& logLines(CompileStage stage) const {
        return stageLogs_[static_cast<std::size_t>(compileStageIndex(stage))];
    }
    std::string& logText(CompileStage stage) {
        return stageLogText_[static_cast<std::size_t>(compileStageIndex(stage))];
    }
    bool running() const {
        return running_;
    }
    CompileStage selectedOutputTab() const {
        return selectedOutputTab_;
    }
    void setSelectedOutputTab(CompileStage tab) {
        selectedOutputTab_ = tab;
    }
    bool outputTabFocusPending() const {
        return outputTabFocusPending_;
    }
    void clearOutputTabFocusPending() {
        outputTabFocusPending_ = false;
    }
    bool logAutoScroll(CompileStage stage) const {
        return stageLogAutoScroll_[static_cast<std::size_t>(compileStageIndex(stage))];
    }
    void setLogAutoScroll(CompileStage stage, bool enabled) {
        stageLogAutoScroll_[static_cast<std::size_t>(compileStageIndex(stage))] = enabled;
    }
    bool logDirty(CompileStage stage) const {
        return stageLogDirty_[static_cast<std::size_t>(compileStageIndex(stage))];
    }
    void clearLogDirty(CompileStage stage) {
        stageLogDirty_[static_cast<std::size_t>(compileStageIndex(stage))] = false;
    }
    const std::string& statusSummary() const {
        return statusSummary_;
    }
    bool takeStatusUpdate(std::string& out) {
        if (!statusDirty_) {
            return false;
        }
        out = statusSummary_;
        statusDirty_ = false;
        return true;
    }
    bool takeCompletedStage(CompileStage& out) {
        if (!completedStagePending_) {
            return false;
        }
        out = completedStage_;
        completedStagePending_ = false;
        return true;
    }

    void requestRun(std::vector<CompileStage> stages, const CompileMountArgs& mounts);
    void tick();
    void cancel();
    void shutdown();

private:
    struct ChildProcess {
#if defined(_WIN32)
        void* process = nullptr;
        void* thread = nullptr;
        void* readPipe = nullptr;
#else
        int pid = -1;
        int readFd = -1;
#endif
        bool active = false;
    };

    std::vector<CompileStage> queue_;
    CompileMountArgs mounts_{};
    ChildProcess child_{};
    std::array<std::vector<std::string>, kCompileStageCount> stageLogs_{};
    std::array<std::string, kCompileStageCount> stageLogText_{};
    std::array<bool, kCompileStageCount> stageLogDirty_{};
    std::array<bool, kCompileStageCount> stageLogAutoScroll_{true, true, true, true};
    std::string lineBuffer_;
    std::string statusSummary_;
    bool running_ = false;
    bool statusDirty_ = false;
    CompileStage currentStage_ = CompileStage::Bsp;
    CompileStage selectedOutputTab_ = CompileStage::Bsp;
    bool outputTabFocusPending_ = false;
    CompileStage completedStage_ = CompileStage::Bsp;
    bool completedStagePending_ = false;

    void setStatus(std::string status);
    void appendLine(std::string line);
    void appendLine(std::string line, CompileStage stage);
    void appendOutput(const char* data, std::size_t size);
    void flushLineBuffer();
    void clearLog();
    void abortQueue(const std::string& reason);
    void finishQueueSuccess();
    void startNextStage();
    bool spawnStage(CompileStage stage);
    void closeChildPipes();
    void reapChild(int* outExitCode);
    bool childExited(int* outExitCode);
    static const char* stageToolName(CompileStage stage);
    static std::filesystem::path resolveToolPath(CompileStage stage);
    std::vector<std::string> buildArgs(CompileStage stage) const;
};

bool launchGame(const CompileMountArgs& mounts, std::string& errorOut);

}
