#include "core/user_paths.hpp"

#include <cstdlib>

namespace slopengine {

std::filesystem::path userConfigDirectory() {
#if defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA"); appdata != nullptr && appdata[0] != '\0') {
        return std::filesystem::path(appdata) / "slopengine";
    }
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
        return std::filesystem::path(home) / "Library" / "Application Support" / "slopengine";
    }
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && xdg[0] != '\0') {
        return std::filesystem::path(xdg) / "slopengine";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
        return std::filesystem::path(home) / ".config" / "slopengine";
    }
#endif
    return std::filesystem::path("slopengine-config");
}

std::filesystem::path userSettingsPath() {
    return userConfigDirectory() / "settings.cfg";
}

std::filesystem::path userScreenshotDirectory() {
    return userConfigDirectory() / "screenshots";
}

}
