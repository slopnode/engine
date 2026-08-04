#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace slopmap {

enum class CompileStage {
    Bsp,
    Fac,
    Vis,
    Rad,
};

struct RadCompileOptions {
    float luxelsPerMeter = 16.0f;
    int bounces = 2;
    int samples = 16;
    bool preferGpu = true;
    /** On hybrid systems, request the discrete GPU for sloprad (Linux DRI_PRIME / Windows shim). */
    bool forceDiscreteGpu = true;
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

    const std::vector<std::string>& logLines() const {
        return logLines_;
    }
    bool running() const {
        return running_;
    }
    bool logAutoScroll() const {
        return logAutoScroll_;
    }
    void setLogAutoScroll(bool enabled) {
        logAutoScroll_ = enabled;
    }
    bool logDirty() const {
        return logDirty_;
    }
    void clearLogDirty() {
        logDirty_ = false;
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
    std::vector<std::string> logLines_;
    std::string lineBuffer_;
    std::string statusSummary_;
    bool running_ = false;
    bool logAutoScroll_ = true;
    bool logDirty_ = false;
    bool statusDirty_ = false;
    CompileStage currentStage_ = CompileStage::Bsp;
    CompileStage completedStage_ = CompileStage::Bsp;
    bool completedStagePending_ = false;

    void setStatus(std::string status);
    void appendLine(std::string line);
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
