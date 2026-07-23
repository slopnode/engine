#include "core/user_paths.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace slopengine {

namespace {

bool isUnsafeSegmentChar(char c) {
    return c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<'
        || c == '>' || c == '|';
}

std::string packageSegment(const Package& package) {
    return sanitizeSaveSegment(package.meta().id) + "_"
        + sanitizeSaveSegment(package.meta().version);
}

std::string escapeSexprString(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (char c : text) {
        if (c == '\\' || c == '"') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

void appendPackageForm(std::ostringstream& out, const Package& package) {
    out << "(id \"" << escapeSexprString(package.meta().id) << "\") "
        << "(version \"" << escapeSexprString(package.meta().version) << "\") "
        << "(root \"" << escapeSexprString(package.root().string()) << "\")";
}

} // namespace

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

std::filesystem::path userSavesDirectory() {
    return userConfigDirectory() / "saves";
}

std::string sanitizeSaveSegment(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        out.push_back(isUnsafeSegmentChar(c) ? '_' : c);
    }
    if (out.empty()) {
        out = "_";
    }
    return out;
}

std::filesystem::path buildSaveContextRoot(const std::vector<Package>& packages) {
    std::filesystem::path root = userSavesDirectory();
    if (packages.empty()) {
        return root / "unknown_engine" / "unknown_base" / "vanilla";
    }

    root /= packageSegment(packages[0]);
    if (packages.size() == 1) {
        return root / "unknown_base" / "vanilla";
    }

    root /= packageSegment(packages[1]);
    if (packages.size() == 2) {
        return root / "vanilla";
    }

    std::string mods;
    for (std::size_t i = 2; i < packages.size(); ++i) {
        if (i > 2) {
            mods.push_back('+');
        }
        mods += packageSegment(packages[i]);
    }
    return root / mods;
}

bool ensureSaveMountSidecar(
    const std::filesystem::path& contextRoot,
    const std::vector<Package>& packages) {
    const std::filesystem::path path = contextRoot / "mount.s7";
    std::error_code existsEc;
    if (std::filesystem::exists(path, existsEc)) {
        return true;
    }

    std::error_code dirEc;
    std::filesystem::create_directories(contextRoot, dirEc);
    if (dirEc) {
        return false;
    }

    std::ostringstream body;
    body << "(mount\n";
    if (!packages.empty()) {
        body << "  (engine ";
        appendPackageForm(body, packages[0]);
        body << ")\n";
    }
    if (packages.size() >= 2) {
        body << "  (base ";
        appendPackageForm(body, packages[1]);
        body << ")\n";
    }
    body << "  (mods";
    if (packages.size() <= 2) {
        body << ")\n";
    } else {
        body << "\n";
        for (std::size_t i = 2; i < packages.size(); ++i) {
            body << "    (";
            appendPackageForm(body, packages[i]);
            body << ")\n";
        }
        body << "  )\n";
    }
    body << ")\n";

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out << body.str();
    return static_cast<bool>(out);
}

bool resolveSaveRelativePath(
    const std::filesystem::path& contextRoot,
    std::string_view relative,
    std::filesystem::path& outAbsolute) {
    if (relative.empty()) {
        return false;
    }

    const std::filesystem::path relPath{std::string(relative)};
    if (relPath.is_absolute()) {
        return false;
    }

    for (const std::filesystem::path& part : relPath) {
        if (part == "..") {
            return false;
        }
    }

    const std::filesystem::path rootNormal = contextRoot.lexically_normal();
    const std::filesystem::path combined = (rootNormal / relPath).lexically_normal();
    const std::filesystem::path relativeToRoot = combined.lexically_relative(rootNormal);
    if (relativeToRoot.empty() || relativeToRoot == "." || *relativeToRoot.begin() == "..") {
        return false;
    }
    for (const std::filesystem::path& part : relativeToRoot) {
        if (part == "..") {
            return false;
        }
    }

    outAbsolute = combined;
    return true;
}

}
