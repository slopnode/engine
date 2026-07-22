#include "core/screenshot.hpp"

#include "core/user_paths.hpp"

#include <raylib.h>

#include <chrono>
#include <ctime>
#include <system_error>

namespace slopengine {

namespace {

std::filesystem::path uniqueScreenshotPath(const std::filesystem::path& dir) {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &local);

    std::filesystem::path path = dir / (std::string("screenshot_") + stamp + ".png");
    if (!std::filesystem::exists(path)) {
        return path;
    }
    for (int i = 2; i < 1000; ++i) {
        path = dir / (std::string("screenshot_") + stamp + "_" + std::to_string(i) + ".png");
        if (!std::filesystem::exists(path)) {
            return path;
        }
    }
    return dir / (std::string("screenshot_") + stamp + "_x.png");
}

}

bool saveScreenshotPng(std::filesystem::path& outPath, std::string& outError) {
    const std::filesystem::path dir = userScreenshotDirectory();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        outError = "Failed to create screenshot directory: " + dir.string();
        return false;
    }

    outPath = uniqueScreenshotPath(dir);
    Image image = LoadImageFromScreen();
    if (image.data == nullptr) {
        outError = "Failed to capture framebuffer";
        return false;
    }

    const bool ok = ExportImage(image, outPath.string().c_str());
    UnloadImage(image);
    if (!ok) {
        outError = "Failed to write " + outPath.string();
        return false;
    }
    outError.clear();
    return true;
}

}
