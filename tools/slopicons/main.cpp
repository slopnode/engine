#include <raylib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct IconSource {
    std::string id;
    std::filesystem::path path;
    int width = 0;
    int height = 0;
};

int nextPowerOfTwo(int value) {
    int result = 1;
    while (result < value) {
        result <<= 1;
    }
    return result;
}

std::string relativeId(const std::filesystem::path& root, const std::filesystem::path& file) {
    std::filesystem::path relative = std::filesystem::relative(file, root);
    relative.replace_extension();
    std::string id = relative.generic_string();
    return id;
}

bool collectIcons(const std::filesystem::path& sourceDir, std::vector<IconSource>& out) {
    if (!std::filesystem::is_directory(sourceDir)) {
        std::fprintf(stderr, "slopicons: source directory not found: %s\n", sourceDir.string().c_str());
        return false;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(sourceDir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() != ".png") {
            continue;
        }
        IconSource icon{};
        icon.id = relativeId(sourceDir, entry.path());
        icon.path = entry.path();
        Image image = LoadImage(entry.path().string().c_str());
        if (image.data == nullptr) {
            std::fprintf(stderr, "slopicons: failed to load %s\n", entry.path().string().c_str());
            return false;
        }
        icon.width = image.width;
        icon.height = image.height;
        UnloadImage(image);
        out.push_back(std::move(icon));
    }

    std::sort(out.begin(), out.end(), [](const IconSource& a, const IconSource& b) {
        if (a.height != b.height) {
            return a.height > b.height;
        }
        if (a.width != b.width) {
            return a.width > b.width;
        }
        return a.id < b.id;
    });
    return true;
}

bool packIcons(
    const std::string& setName,
    const std::vector<IconSource>& icons,
    const std::filesystem::path& atlasPath,
    const std::filesystem::path& mapPath) {
    if (icons.empty()) {
        std::fprintf(stderr, "slopicons: no icons to pack\n");
        return false;
    }

    int maxW = 0;
    int maxH = 0;
    long long area = 0;
    for (const IconSource& icon : icons) {
        maxW = std::max(maxW, icon.width);
        maxH = std::max(maxH, icon.height);
        area += static_cast<long long>(icon.width) * icon.height;
    }

    int atlasW = nextPowerOfTwo(std::max(maxW, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(area))))));
    atlasW = std::max(atlasW, maxW);

    struct PackedIcon {
        std::string id;
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
    };

    for (;;) {
        const int atlasH = std::max(
            maxH,
            nextPowerOfTwo(static_cast<int>((area + atlasW - 1) / atlasW) + maxH));
        Image atlas = GenImageColor(atlasW, atlasH, BLANK);
        ImageFormat(&atlas, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        std::vector<PackedIcon> packed;
        packed.reserve(icons.size());

        int cursorX = 0;
        int cursorY = 0;
        int rowH = 0;
        bool fits = true;

        for (const IconSource& icon : icons) {
            if (cursorX + icon.width > atlasW) {
                cursorX = 0;
                cursorY += rowH;
                rowH = 0;
            }
            if (cursorY + icon.height > atlasH) {
                fits = false;
                break;
            }

            Image image = LoadImage(icon.path.string().c_str());
            if (image.data == nullptr) {
                UnloadImage(atlas);
                std::fprintf(stderr, "slopicons: failed to load %s\n", icon.path.string().c_str());
                return false;
            }
            ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
            ImageDrawImage(&atlas, image, cursorX, cursorY, WHITE);
            UnloadImage(image);

            PackedIcon entry{};
            entry.id = icon.id;
            entry.x = cursorX;
            entry.y = cursorY;
            entry.width = icon.width;
            entry.height = icon.height;
            packed.push_back(std::move(entry));

            cursorX += icon.width;
            rowH = std::max(rowH, icon.height);
        }

        if (!fits) {
            UnloadImage(atlas);
            atlasW *= 2;
            if (atlasW > 8192) {
                std::fprintf(stderr, "slopicons: atlas exceeded 8192px\n");
                return false;
            }
            continue;
        }

        int usedW = 0;
        int usedH = 0;
        for (const PackedIcon& entry : packed) {
            usedW = std::max(usedW, entry.x + entry.width);
            usedH = std::max(usedH, entry.y + entry.height);
        }
        if (usedW < atlas.width || usedH < atlas.height) {
            ImageCrop(
                &atlas,
                Rectangle{0.0f, 0.0f, static_cast<float>(usedW), static_cast<float>(usedH)});
        }

        if (!ExportImage(atlas, atlasPath.string().c_str())) {
            UnloadImage(atlas);
            std::fprintf(stderr, "slopicons: failed to write %s\n", atlasPath.string().c_str());
            return false;
        }
        UnloadImage(atlas);

        std::ofstream mapFile{mapPath};
        if (!mapFile) {
            std::fprintf(stderr, "slopicons: failed to write %s\n", mapPath.string().c_str());
            return false;
        }
        mapFile << "(iconmap\n";
        mapFile << "  (atlas \"" << setName << "\")\n";
        mapFile << "  (size " << usedW << ' ' << usedH << ")\n";
        for (const PackedIcon& entry : packed) {
            mapFile << "  (icon \"" << entry.id << "\" " << entry.x << ' ' << entry.y << ' '
                    << entry.width << ' ' << entry.height << ")\n";
        }
        mapFile << ")\n";

        std::printf(
            "slopicons: packed %zu icons into %s (%dx%d)\n",
            packed.size(),
            atlasPath.string().c_str(),
            usedW,
            usedH);
        return true;
    }
}

void printUsage(const char* argv0) {
    std::fprintf(
        stderr,
        "usage: %s pack <set-name> <icons-root>\n"
        "  Expects sources at <icons-root>/<set-name>/*.png\n"
        "  Writes <icons-root>/<set-name>.png and <icons-root>/<set-name>.iconmap\n",
        argv0);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    const std::string command = argv[1];
    if (command != "pack") {
        printUsage(argv[0]);
        return 1;
    }
    if (argc != 4) {
        printUsage(argv[0]);
        return 1;
    }

    const std::string setName = argv[2];
    const std::filesystem::path iconsRoot = argv[3];
    const std::filesystem::path sourceDir = iconsRoot / setName;
    const std::filesystem::path atlasPath = iconsRoot / (setName + ".png");
    const std::filesystem::path mapPath = iconsRoot / (setName + ".iconmap");

    SetTraceLogLevel(LOG_WARNING);

    std::vector<IconSource> icons;
    if (!collectIcons(sourceDir, icons)) {
        return 1;
    }
    if (!packIcons(setName, icons, atlasPath, mapPath)) {
        return 1;
    }
    return 0;
}
