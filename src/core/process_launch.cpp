#include "core/process_launch.hpp"

#include <raylib.h>

#include <cerrno>
#include <cstring>
#include <system_error>

#if defined(_WIN32)
#include "core/win32.hpp"
#else
#include <spawn.h>
#include <unistd.h>

extern char** environ;
#endif

namespace slopengine {

namespace {

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
    unsigned backslashes = 0;
    for (char c : arg) {
        if (c == '\\') {
            ++backslashes;
            continue;
        }
        if (c == '"') {
            out.append(backslashes * 2 + 1, '\\');
            backslashes = 0;
            out.push_back('"');
            continue;
        }
        out.append(backslashes, '\\');
        backslashes = 0;
        out.push_back(c);
    }
    out.append(backslashes * 2, '\\');
    out.push_back('"');
    return out;
}
#endif

} // namespace

std::filesystem::path resolveSiblingExecutable(std::string_view exeNameNoExt) {
    const char* dir = GetApplicationDirectory();
    std::filesystem::path toolPath = dir ? std::filesystem::path(dir) : std::filesystem::path();
    toolPath /= std::string(exeNameNoExt);
#if defined(_WIN32)
    toolPath += ".exe";
#endif

    std::error_code ec;
    if (!std::filesystem::exists(toolPath, ec)) {
        return {};
    }
    return toolPath;
}

bool spawnDetached(
    const std::filesystem::path& exePath,
    const std::vector<std::string>& args,
    std::string& errorOut) {
    errorOut.clear();

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
        exePath.string().c_str(),
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
        errorOut = "Failed to launch " + exePath.string();
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
        ::posix_spawn(&pid, exePath.string().c_str(), nullptr, nullptr, argv.data(), environ);
    if (spawnRc != 0) {
        errorOut = "Failed to launch " + exePath.string() + ": " + std::strerror(spawnRc);
        return false;
    }
    return true;
#endif
}

}
