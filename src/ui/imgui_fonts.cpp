#include "ui/imgui_fonts.hpp"

#include "core/user_paths.hpp"

#include "rlImGui.h"

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace slopengine {

namespace {

float defaultFontSizePixels() {
    static constexpr float kDefaultFontSize = 13.0f;
    float size = kDefaultFontSize;
#if !defined(__APPLE__)
    if (!IsWindowState(FLAG_WINDOW_HIGHDPI)) {
        size = std::ceil(size * GetWindowScaleDPI().y);
    }
#endif
    return size;
}

} // namespace

void setImGuiIniPath(std::string_view toolName) {
    // ImGui keeps the pointer, not a copy, so the backing string must outlive the context.
    static std::string iniPath;

    const std::filesystem::path path = userImguiIniPath(toolName);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    iniPath = path.string();
    ImGui::GetIO().IniFilename = iniPath.c_str();
}

ImFont* loadImGuiFont(AssetStore& assets, std::string_view path, float sizePixels) {
    if (!assets.hasFont(path)) {
        TraceLog(LOG_WARNING, "FONT: not found: %.*s", static_cast<int>(path.size()), path.data());
        return nullptr;
    }

    const std::vector<std::byte> bytes = assets.readBinary(path, AssetKind::Font);
    if (bytes.empty()) {
        TraceLog(LOG_WARNING, "FONT: empty: %.*s", static_cast<int>(path.size()), path.data());
        return nullptr;
    }

    void* owned = std::malloc(bytes.size());
    if (owned == nullptr) {
        TraceLog(LOG_WARNING, "FONT: alloc failed: %.*s", static_cast<int>(path.size()), path.data());
        return nullptr;
    }
    std::memcpy(owned, bytes.data(), bytes.size());

    ImFontConfig config;
    config.FontDataOwnedByAtlas = true;
    config.PixelSnapH = true;
#if !defined(__APPLE__)
    if (!IsWindowState(FLAG_WINDOW_HIGHDPI)) {
        config.RasterizerMultiply = GetWindowScaleDPI().y;
    }
#endif

    const float size = sizePixels > 0.0f ? sizePixels : defaultFontSizePixels();
    ImGuiIO& io = ImGui::GetIO();
    ImFont* font = io.Fonts->AddFontFromMemoryTTF(
        owned, static_cast<int>(bytes.size()), size, &config);
    if (font == nullptr) {
        std::free(owned);
        TraceLog(LOG_WARNING, "FONT: AddFontFromMemoryTTF failed: %.*s",
            static_cast<int>(path.size()), path.data());
    }
    return font;
}

bool setupImGuiWithUiFont(
    AssetStore& assets,
    std::string_view uiFontPath,
    bool darkTheme,
    std::string_view toolName) {
    rlImGuiBeginInitImGui();
    setImGuiIniPath(toolName);

    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->ClearFonts();

    if (loadImGuiFont(assets, uiFontPath) == nullptr) {
        ImFontConfig defaultConfig;
        defaultConfig.SizePixels = defaultFontSizePixels();
        defaultConfig.PixelSnapH = true;
#if !defined(__APPLE__)
        if (!IsWindowState(FLAG_WINDOW_HIGHDPI)) {
            defaultConfig.RasterizerMultiply = GetWindowScaleDPI().y;
        }
#endif
        io.Fonts->AddFontDefault(&defaultConfig);
    }

    if (darkTheme) {
        ImGui::StyleColorsDark();
    } else {
        ImGui::StyleColorsLight();
    }

    rlImGuiEndInitImGui();
    return true;
}

ImFont* setupImGuiWithUiAndMonoFont(
    AssetStore& assets,
    std::string_view uiFontPath,
    std::string_view monoFontPath,
    bool darkTheme,
    std::string_view toolName) {
    rlImGuiBeginInitImGui();
    setImGuiIniPath(toolName);

    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->ClearFonts();

    if (loadImGuiFont(assets, uiFontPath) == nullptr) {
        ImFontConfig defaultConfig;
        defaultConfig.SizePixels = defaultFontSizePixels();
        defaultConfig.PixelSnapH = true;
#if !defined(__APPLE__)
        if (!IsWindowState(FLAG_WINDOW_HIGHDPI)) {
            defaultConfig.RasterizerMultiply = GetWindowScaleDPI().y;
        }
#endif
        io.Fonts->AddFontDefault(&defaultConfig);
    }

    ImFont* mono = loadImGuiFont(assets, monoFontPath);

    if (darkTheme) {
        ImGui::StyleColorsDark();
    } else {
        ImGui::StyleColorsLight();
    }

    rlImGuiEndInitImGui();
    return mono;
}

}
