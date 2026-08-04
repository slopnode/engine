#include "compile.hpp"

#include <raylib.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <sstream>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#include "core/win32.hpp"
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace slopmap {

namespace {

void upsertEnvVar(std::vector<std::string>& env, const char* key, const char* value) {
    const std::string prefix = std::string(key) + "=";
    for (std::string& entry : env) {
        if (entry.rfind(prefix, 0) == 0) {
            entry = prefix + value;
            return;
        }
    }
    env.push_back(prefix + value);
}

void copyEnviron(std::vector<std::string>& env) {
#if defined(_WIN32)
    extern char** _environ;
    if (_environ == nullptr) {
        return;
    }
    for (char** entry = _environ; *entry != nullptr; ++entry) {
        env.emplace_back(*entry);
    }
#else
    if (environ == nullptr) {
        return;
    }
    for (char** entry = environ; *entry != nullptr; ++entry) {
        env.emplace_back(*entry);
    }
#endif
}

void applyDiscreteGpuEnv(std::vector<std::string>& env) {
#if defined(_WIN32)
    upsertEnvVar(env, "SHIM_MCCOMPAT", "0x800000001");
#else
    upsertEnvVar(env, "DRI_PRIME", "1");
    upsertEnvVar(env, "__NV_PRIME_RENDER_OFFLOAD", "1");
    upsertEnvVar(env, "__GLX_VENDOR_LIBRARY_NAME", "nvidia");
    upsertEnvVar(env, "__VK_LAYER_NV_optimus", "NVIDIA_only");
#endif
}

#if defined(_WIN32)
std::string quoteWinArg(const std::string& arg) {
    if (arg.empty()) {
        return "\"\"";
    }
    bool needsQuotes = false;
    for (char c : arg) {
        if (c == ' ' || c == '\t' || c == '"') {
            needsQuotes = true;
            break;
        }
    }
    if (!needsQuotes) {
        return arg;
    }
    std::string out = "\"";
    for (char c : arg) {
        if (c == '"') {
            out += "\\\"";
        } else {
            out.push_back(c);
        }
    }
    out += "\"";
    return out;
}

std::string buildWindowsEnvBlock(const std::vector<std::string>& env) {
    std::string block;
    for (const std::string& entry : env) {
        block += entry;
        block.push_back('\0');
    }
    block.push_back('\0');
    return block;
}
#else
std::vector<char*> buildEnvPtrs(std::vector<std::string>& env) {
    std::vector<char*> ptrs;
    ptrs.reserve(env.size() + 1);
    for (std::string& entry : env) {
        ptrs.push_back(entry.data());
    }
    ptrs.push_back(nullptr);
    return ptrs;
}
#endif

} // namespace

const char* CompileController::stageToolName(CompileStage stage) {
    switch (stage) {
    case CompileStage::Bsp:
        return "slopbsp";
    case CompileStage::Fac:
        return "slopfac";
    case CompileStage::Vis:
        return "slopvis";
    case CompileStage::Rad:
        return "sloprad";
    }
    return "unknown";
}

std::filesystem::path CompileController::resolveToolPath(CompileStage stage) {
    const char* dir = GetApplicationDirectory();
    std::filesystem::path path = dir ? std::filesystem::path(dir) : std::filesystem::path();
#if defined(_WIN32)
    path /= std::string(stageToolName(stage)) + ".exe";
#else
    path /= stageToolName(stage);
#endif
    return path;
}

std::vector<std::string> CompileController::buildArgs(CompileStage stage) const {
    std::vector<std::string> args;
    args.emplace_back(stageToolName(stage));
    args.emplace_back("--base-game");
    args.push_back(mounts_.baseGame.string());
    for (const auto& mod : mounts_.mods) {
        args.emplace_back("--mod");
        args.push_back(mod.string());
    }
    args.emplace_back("--map");
    args.push_back(mounts_.mapName);
    if (stage == CompileStage::Rad) {
        args.emplace_back("--luxels-per-meter");
        args.push_back(std::to_string(radOptions.luxelsPerMeter));
        args.emplace_back("--bounces");
        args.push_back(std::to_string(radOptions.bounces));
        args.emplace_back("--samples");
        args.push_back(std::to_string(radOptions.samples));
        args.emplace_back("--emitter-direct-samples");
        args.push_back(std::to_string(radOptions.emitterDirectSamples));
        args.emplace_back("--emitter-grid-luxels-per-meter");
        args.push_back(std::to_string(radOptions.emitterGridLuxelsPerMeter));
        args.emplace_back("--emitter-grid-max-size");
        args.push_back(std::to_string(radOptions.emitterGridMaxSize));
        args.emplace_back("--sun-shadow-softness");
        args.push_back(std::to_string(radOptions.sunShadowSoftness));
        if (radOptions.preferGpu) {
            args.emplace_back("--gpu");
            if (radOptions.forceDiscreteGpu) {
                args.emplace_back("--gpu-fast");
            }
        } else {
            args.emplace_back("--cpu");
        }
    }
    return args;
}

void CompileController::setStatus(std::string status) {
    statusSummary_ = std::move(status);
    statusDirty_ = true;
}

void CompileController::appendLine(std::string line) {
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    logLines_.push_back(std::move(line));
    logDirty_ = true;
}

void CompileController::appendOutput(const char* data, std::size_t size) {
    if (data == nullptr || size == 0) {
        return;
    }
    lineBuffer_.append(data, size);
    std::size_t start = 0;
    for (std::size_t i = 0; i < lineBuffer_.size(); ++i) {
        if (lineBuffer_[i] == '\n') {
            appendLine(lineBuffer_.substr(start, i - start));
            start = i + 1;
        }
    }
    if (start > 0) {
        lineBuffer_.erase(0, start);
    }
}

void CompileController::flushLineBuffer() {
    if (!lineBuffer_.empty()) {
        appendLine(std::move(lineBuffer_));
        lineBuffer_.clear();
    }
}

void CompileController::clearLog() {
    logLines_.clear();
    lineBuffer_.clear();
    logDirty_ = true;
    logAutoScroll_ = true;
}

void CompileController::closeChildPipes() {
#if defined(_WIN32)
    if (child_.readPipe != nullptr) {
        CloseHandle(static_cast<HANDLE>(child_.readPipe));
        child_.readPipe = nullptr;
    }
#else
    if (child_.readFd >= 0) {
        ::close(child_.readFd);
        child_.readFd = -1;
    }
#endif
}

void CompileController::reapChild(int* outExitCode) {
#if defined(_WIN32)
    if (child_.process != nullptr) {
        DWORD code = 1;
        GetExitCodeProcess(static_cast<HANDLE>(child_.process), &code);
        if (outExitCode != nullptr) {
            *outExitCode = static_cast<int>(code);
        }
        CloseHandle(static_cast<HANDLE>(child_.process));
        child_.process = nullptr;
    }
    if (child_.thread != nullptr) {
        CloseHandle(static_cast<HANDLE>(child_.thread));
        child_.thread = nullptr;
    }
#else
    if (child_.pid > 0) {
        int status = 0;
        while (::waitpid(child_.pid, &status, 0) < 0 && errno == EINTR) {
        }
        if (outExitCode != nullptr) {
            if (WIFEXITED(status)) {
                *outExitCode = WEXITSTATUS(status);
            } else {
                *outExitCode = 1;
            }
        }
        child_.pid = -1;
    }
#endif
    child_.active = false;
}

bool CompileController::childExited(int* outExitCode) {
#if defined(_WIN32)
    if (child_.process == nullptr) {
        return true;
    }
    const DWORD wait = WaitForSingleObject(static_cast<HANDLE>(child_.process), 0);
    if (wait == WAIT_TIMEOUT) {
        return false;
    }
    DWORD code = 1;
    GetExitCodeProcess(static_cast<HANDLE>(child_.process), &code);
    if (outExitCode != nullptr) {
        *outExitCode = static_cast<int>(code);
    }
    CloseHandle(static_cast<HANDLE>(child_.process));
    child_.process = nullptr;
    if (child_.thread != nullptr) {
        CloseHandle(static_cast<HANDLE>(child_.thread));
        child_.thread = nullptr;
    }
    child_.active = false;
    return true;
#else
    if (child_.pid <= 0) {
        return true;
    }
    int status = 0;
    const pid_t result = ::waitpid(child_.pid, &status, WNOHANG);
    if (result == 0) {
        return false;
    }
    if (result < 0) {
        if (outExitCode != nullptr) {
            *outExitCode = 1;
        }
        child_.pid = -1;
        child_.active = false;
        return true;
    }
    if (outExitCode != nullptr) {
        if (WIFEXITED(status)) {
            *outExitCode = WEXITSTATUS(status);
        } else {
            *outExitCode = 1;
        }
    }
    child_.pid = -1;
    child_.active = false;
    return true;
#endif
}

bool CompileController::spawnStage(CompileStage stage) {
    const std::filesystem::path toolPath = resolveToolPath(stage);
    std::error_code ec;
    if (!std::filesystem::exists(toolPath, ec)) {
        appendLine(std::string("error: tool not found: ") + toolPath.string());
        return false;
    }

    const std::vector<std::string> args = buildArgs(stage);
    {
        std::ostringstream cmd;
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (i > 0) {
                cmd << ' ';
            }
            cmd << args[i];
        }
        appendLine(std::string("$ ") + cmd.str());
    }

    const bool useDiscreteGpu =
        stage == CompileStage::Rad && radOptions.preferGpu && radOptions.forceDiscreteGpu;
    std::vector<std::string> spawnEnvStorage;
    std::vector<char*> spawnEnvPtrs;
    std::string windowsEnvBlock;
    if (useDiscreteGpu) {
        copyEnviron(spawnEnvStorage);
        applyDiscreteGpuEnv(spawnEnvStorage);
        appendLine("note: requesting discrete GPU for sloprad");
#if !defined(_WIN32)
        spawnEnvPtrs = buildEnvPtrs(spawnEnvStorage);
#else
        windowsEnvBlock = buildWindowsEnvBlock(spawnEnvStorage);
#endif
    }

#if defined(_WIN32)
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        appendLine("error: CreatePipe failed");
        return false;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    std::string cmdline;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            cmdline.push_back(' ');
        }
        cmdline += quoteWinArg(args[i]);
    }
    std::vector<char> cmdlineMutable(cmdline.begin(), cmdline.end());
    cmdlineMutable.push_back('\0');

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;

    PROCESS_INFORMATION pi{};
    LPVOID processEnv = nullptr;
    if (!windowsEnvBlock.empty()) {
        processEnv = windowsEnvBlock.data();
    }
    const BOOL ok = CreateProcessA(
        toolPath.string().c_str(),
        cmdlineMutable.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        processEnv,
        nullptr,
        &si,
        &pi);
    CloseHandle(writePipe);
    if (!ok) {
        CloseHandle(readPipe);
        appendLine("error: CreateProcess failed");
        return false;
    }

    child_.process = pi.hProcess;
    child_.thread = pi.hThread;
    child_.readPipe = readPipe;
    child_.active = true;
    return true;
#else
    int pipefds[2] = {-1, -1};
    if (::pipe(pipefds) != 0) {
        appendLine("error: pipe failed");
        return false;
    }
    const int flags = ::fcntl(pipefds[0], F_GETFL, 0);
    if (flags >= 0) {
        ::fcntl(pipefds[0], F_SETFL, flags | O_NONBLOCK);
    }

    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        ::close(pipefds[0]);
        ::close(pipefds[1]);
        appendLine("error: posix_spawn_file_actions_init failed");
        return false;
    }
    posix_spawn_file_actions_addclose(&actions, pipefds[0]);
    posix_spawn_file_actions_adddup2(&actions, pipefds[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, pipefds[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipefds[1]);

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    std::vector<std::string> argsOwned = args;
    for (auto& arg : argsOwned) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    pid_t pid = -1;
    char** spawnEnv = spawnEnvPtrs.empty() ? environ : spawnEnvPtrs.data();
    const int spawnRc = ::posix_spawn(
        &pid, toolPath.string().c_str(), &actions, nullptr, argv.data(), spawnEnv);
    posix_spawn_file_actions_destroy(&actions);
    ::close(pipefds[1]);

    if (spawnRc != 0) {
        ::close(pipefds[0]);
        appendLine(std::string("error: posix_spawn failed: ") + std::strerror(spawnRc));
        return false;
    }

    child_.pid = static_cast<int>(pid);
    child_.readFd = pipefds[0];
    child_.active = true;
    return true;
#endif
}

void CompileController::abortQueue(const std::string& reason) {
    queue_.clear();
    running_ = false;
    setStatus(reason);
    appendLine(reason);
}

void CompileController::finishQueueSuccess() {
    running_ = false;
    setStatus("Compile finished");
    appendLine("=== compile finished ===");
}

void CompileController::startNextStage() {
    if (queue_.empty()) {
        finishQueueSuccess();
        return;
    }

    currentStage_ = queue_.front();
    queue_.erase(queue_.begin());
    const char* tool = stageToolName(currentStage_);
    appendLine(std::string("=== ") + tool + " ===");
    setStatus(std::string("Compile: ") + tool + " running…");

    if (!spawnStage(currentStage_)) {
        abortQueue(std::string("Compile failed (") + tool + " spawn error)");
    }
}

void CompileController::requestRun(std::vector<CompileStage> stages, const CompileMountArgs& mounts) {
    if (running_) {
        return;
    }
    if (stages.empty()) {
        return;
    }
    if (mounts.mapName.empty() || mounts.mapName == "untitled") {
        setStatus("Save the map before compiling");
        return;
    }
    if (mounts.baseGame.empty()) {
        setStatus("Compile failed (missing --base-game)");
        return;
    }

    mounts_ = mounts;
    queue_ = std::move(stages);
    clearLog();
    showOutputWindow = true;
    running_ = true;
    startNextStage();
}

void CompileController::tick() {
    if (!running_ || !child_.active) {
        return;
    }

#if defined(_WIN32)
    if (child_.readPipe != nullptr) {
        std::array<char, 4096> buf{};
        for (;;) {
            DWORD available = 0;
            if (!PeekNamedPipe(static_cast<HANDLE>(child_.readPipe), nullptr, 0, nullptr, &available, nullptr)) {
                break;
            }
            if (available == 0) {
                break;
            }
            const DWORD toRead = std::min<DWORD>(available, static_cast<DWORD>(buf.size()));
            DWORD got = 0;
            if (!ReadFile(static_cast<HANDLE>(child_.readPipe), buf.data(), toRead, &got, nullptr) || got == 0) {
                break;
            }
            appendOutput(buf.data(), static_cast<std::size_t>(got));
        }
    }
#else
    if (child_.readFd >= 0) {
        std::array<char, 4096> buf{};
        for (;;) {
            pollfd pfd{};
            pfd.fd = child_.readFd;
            pfd.events = POLLIN;
            const int pr = ::poll(&pfd, 1, 0);
            if (pr <= 0) {
                break;
            }
            const ssize_t n = ::read(child_.readFd, buf.data(), buf.size());
            if (n > 0) {
                appendOutput(buf.data(), static_cast<std::size_t>(n));
                continue;
            }
            break;
        }
    }
#endif

    int exitCode = 0;
    if (!childExited(&exitCode)) {
        return;
    }

#if defined(_WIN32)
    if (child_.readPipe != nullptr) {
        std::array<char, 4096> buf{};
        DWORD got = 0;
        while (ReadFile(static_cast<HANDLE>(child_.readPipe), buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr) &&
            got > 0) {
            appendOutput(buf.data(), static_cast<std::size_t>(got));
            got = 0;
        }
    }
#else
    if (child_.readFd >= 0) {
        std::array<char, 4096> buf{};
        for (;;) {
            const ssize_t n = ::read(child_.readFd, buf.data(), buf.size());
            if (n > 0) {
                appendOutput(buf.data(), static_cast<std::size_t>(n));
                continue;
            }
            break;
        }
    }
#endif

    closeChildPipes();
    flushLineBuffer();

    const char* tool = stageToolName(currentStage_);
    appendLine(std::string("=== exit ") + std::to_string(exitCode) + " ===");

    if (exitCode != 0) {
        abortQueue(std::string("Compile failed (") + tool + " exit " + std::to_string(exitCode) + ")");
        return;
    }

    completedStage_ = currentStage_;
    completedStagePending_ = true;
    startNextStage();
}

void CompileController::shutdown() {
    queue_.clear();
#if defined(_WIN32)
    if (child_.process != nullptr) {
        TerminateProcess(static_cast<HANDLE>(child_.process), 1);
        int code = 0;
        reapChild(&code);
    }
#else
    if (child_.pid > 0) {
        ::kill(child_.pid, SIGTERM);
        int code = 0;
        reapChild(&code);
    }
#endif
    closeChildPipes();
    flushLineBuffer();
    running_ = false;
    child_ = {};
}

bool launchGame(const CompileMountArgs& mounts, std::string& errorOut) {
    errorOut.clear();
    if (mounts.mapName.empty() || mounts.mapName == "untitled") {
        errorOut = "Save the map before playing";
        return false;
    }
    if (mounts.baseGame.empty()) {
        errorOut = "Missing --base-game";
        return false;
    }

    const char* dir = GetApplicationDirectory();
    std::filesystem::path toolPath = dir ? std::filesystem::path(dir) : std::filesystem::path();
#if defined(_WIN32)
    toolPath /= "slopengine.exe";
#else
    toolPath /= "slopengine";
#endif

    std::error_code ec;
    if (!std::filesystem::exists(toolPath, ec)) {
        errorOut = "slopengine not found: " + toolPath.string();
        return false;
    }

    std::vector<std::string> args;
    args.emplace_back("slopengine");
    args.emplace_back("--base-game");
    args.push_back(mounts.baseGame.string());
    for (const auto& mod : mounts.mods) {
        args.emplace_back("--mod");
        args.push_back(mod.string());
    }
    args.emplace_back("--map");
    args.push_back(mounts.mapName);

#if defined(_WIN32)
    std::string cmdline;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            cmdline.push_back(' ');
        }
        cmdline += quoteWinArg(args[i]);
    }
    std::vector<char> cmdlineMutable(cmdline.begin(), cmdline.end());
    cmdlineMutable.push_back('\0');

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessA(
        toolPath.string().c_str(),
        cmdlineMutable.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        nullptr,
        &si,
        &pi);
    if (!ok) {
        errorOut = "Failed to launch slopengine";
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    std::vector<std::string> argsOwned = args;
    for (auto& arg : argsOwned) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    pid_t pid = -1;
    const int spawnRc =
        ::posix_spawn(&pid, toolPath.string().c_str(), nullptr, nullptr, argv.data(), environ);
    if (spawnRc != 0) {
        errorOut = std::string("Failed to launch slopengine: ") + std::strerror(spawnRc);
        return false;
    }
    return true;
#endif
}

}
