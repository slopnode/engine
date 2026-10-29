#include "asset_counts.hpp"

#include "core/engine_package.hpp"
#include "script/scheme_harden.hpp"

#include <s7.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>

namespace slopdocs {

namespace fs = std::filesystem;

namespace {

struct KindSpec {
    const char* kind;
    const char* folder;
    const char* extension;
};

const KindSpec kKinds[] = {
    {"Textures", "textures", ".png"},
    {"Texture animations", "textures", ".texanim"},
    {"Texture graphs", "textures", ".texgraph"},
    {"Materials", "materials", ".mat"},
    {"Meshes", "meshes", ".glb"},
    {"Shaders", "shaders", ".glsl"},
    {"Scripts", "scripts", ".s7"},
    {"Skeletons", "skeletons", ".skel"},
    {"Skeleton binds", "skeletons", ".bind"},
    {"Geometry", "geometry", ".geo"},
    {"Geometry vertices", "geometry", ".vert"},
    {"Geometry weights", "geometry", ".weights"},
    {"Animations", "animations", ".anim"},
    {"Animation tracks", "animations", ".tracks"},
    {"Prefabs", "prefabs", ".map"},
    {"Prefab things", "prefabs", ".s7"},
    {"Sprites", "sprites", ".spr"},
    {"Sprite animations", "sprites", ".spanim"},
    {"Icon atlases", "icons", ".png"},
    {"Icon maps", "icons", ".iconmap"},
    {"Data files", "data", ".s7"},
    {"Fonts", "fonts", ".ttf"},
    {"Sounds", "sounds", ".ogg"},
    {"Audio definitions", "audio", ".saudio"},
    {"Procedural sounds", "sounds", ".sfxr"},
    {"Sound fonts", "sounds", ".sf2"},
    {"MIDI", "sounds", ".mid"},
    {"Particles", "particles", ".prt"},
};

struct CatalogSpec {
    const char* catalog;
    const char* symbol;
    const char* file;
    bool cliFlags;
};

const CatalogSpec kCatalogs[] = {
    {"Things", "*package-things*", "things", false},
    {"Thing folders", "*package-thing-folders*", "things", false},
    {"Actions", "*package-actions*", "actions", false},
    {"Map handlers", "*package-map-handlers*", "map-handlers", false},
    {"Nav profiles", "*package-nav-profiles*", "nav-profiles", false},
    {"CLI flags", "*package-cli*", "cli", true},
};

std::size_t countFiles(const fs::path& dir, const std::string& extension) {
    if (!fs::is_directory(dir)) {
        return 0;
    }
    std::size_t n = 0;
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == extension) {
            ++n;
        }
    }
    return n;
}

std::size_t countMaps(const fs::path& root, const char* compiledFile) {
    const fs::path maps = root / "maps";
    if (!fs::is_directory(maps)) {
        return 0;
    }
    std::size_t n = 0;
    for (const auto& entry : fs::directory_iterator(maps)) {
        if (!entry.is_directory()) {
            continue;
        }
        if (compiledFile == nullptr) {
            if (fs::is_regular_file(entry.path() / "map.meta")) {
                ++n;
            }
        } else if (fs::exists(entry.path() / "compiled" / compiledFile)) {
            ++n;
        }
    }
    return n;
}

std::size_t countDefinitions(const fs::path& root) {
    const fs::path scripts = root / "scripts";
    if (!fs::is_directory(scripts)) {
        return 0;
    }
    static const std::regex define(R"(^\s*\(define(?:\*|-macro|-constant)?\s)");
    std::size_t n = 0;
    for (const auto& entry : fs::recursive_directory_iterator(scripts)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".s7") {
            continue;
        }
        std::ifstream in(entry.path());
        std::string line;
        while (std::getline(in, line)) {
            if (std::regex_search(line, define)) {
                ++n;
            }
        }
    }
    return n;
}

void loadHelper(s7_scheme* sc, const fs::path& file) {
    if (fs::is_regular_file(file)) {
        s7_load(sc, file.string().c_str());
    }
}

std::size_t listLength(s7_scheme* sc, s7_pointer v) {
    std::size_t n = 0;
    while (s7_is_pair(v)) {
        ++n;
        v = s7_cdr(v);
    }
    (void)sc;
    return n;
}

std::size_t cliFlagCount(s7_scheme* sc, s7_pointer v) {
    for (s7_pointer it = v; s7_is_pair(it); it = s7_cdr(it)) {
        const s7_pointer entry = s7_car(it);
        if (s7_is_pair(entry) && s7_is_symbol(s7_car(entry)) &&
            std::string(s7_symbol_name(s7_car(entry))) == "flags") {
            return listLength(sc, s7_cdr(entry));
        }
    }
    return 0;
}

void countCatalogs(const fs::path& root, AssetCounts& out, bool verbose) {
    const fs::path data = root / "data";
    for (const CatalogSpec& spec : kCatalogs) {
        CatalogCount c;
        c.catalog = spec.catalog;
        c.symbol = spec.symbol;
        c.file = std::string("data/") + spec.file + ".s7";
        out.catalogs.push_back(std::move(c));
    }
    if (!fs::is_directory(data)) {
        return;
    }
    s7_scheme* sc = s7_init();
    slopengine::hardenSchemeRuntime(sc);
    fs::path engine = root;
    if (!fs::is_regular_file(root / "scripts" / "lists.s7")) {
        const auto resolved = slopengine::resolveEnginePackage();
        if (resolved) {
            engine = *resolved;
        }
    }
    loadHelper(sc, engine / "scripts" / "lists.s7");
    loadHelper(sc, engine / "scripts" / "math.s7");

    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(data)) {
        if (entry.is_regular_file() && entry.path().extension() == ".s7") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    for (const fs::path& file : files) {
        slopengine::clearScriptingErrors();
        s7_load(sc, file.string().c_str());
        if (slopengine::scriptingErrorsOccurred()) {
            std::cerr << "slopdocs: warning: " << file.generic_string() << " did not evaluate; its catalogs are not counted\n";
        } else if (verbose) {
            std::cout << "slopdocs: evaluated " << file.generic_string() << "\n";
        }
    }
    for (std::size_t i = 0; i < out.catalogs.size(); ++i) {
        const CatalogSpec& spec = kCatalogs[i];
        CatalogCount& c = out.catalogs[i];
        const s7_pointer sym = s7_make_symbol(sc, spec.symbol);
        if (!s7_is_defined(sc, spec.symbol)) {
            continue;
        }
        const s7_pointer value = s7_symbol_value(sc, sym);
        c.present = true;
        c.count = spec.cliFlags ? cliFlagCount(sc, value) : listLength(sc, value);
    }
    slopengine::clearScriptingErrors();
    s7_quit(sc);
}

}

AssetCounts countAssets(const fs::path& root, bool verbose) {
    AssetCounts out;
    for (const KindSpec& spec : kKinds) {
        AssetCount c;
        c.kind = spec.kind;
        c.folder = spec.folder;
        c.extension = spec.extension;
        c.count = countFiles(root / spec.folder, spec.extension);
        out.assets.push_back(std::move(c));
    }
    out.assets.push_back({"Maps", "maps", "map.meta", countMaps(root, nullptr)});
    out.assets.push_back({"Compiled maps", "maps", "compiled/bsp", countMaps(root, "bsp")});
    out.assets.push_back({"Lit maps", "maps", "compiled/rad", countMaps(root, "rad")});
    out.assets.push_back({"Navigated maps", "maps", "compiled/nav", countMaps(root, "nav")});
    out.scriptFiles = countFiles(root / "scripts", ".s7");
    out.scriptDefinitions = countDefinitions(root);
    countCatalogs(root, out, verbose);
    return out;
}

}
