#pragma once

#include "doc_model.hpp"
#include "shader_doc.hpp"

#include "core/package_meta.hpp"

#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

namespace slopdocs {

struct NavEntry {
    std::string label;
    std::vector<std::string> pages;
    bool group = false;
    std::vector<NavEntry> children;
};

struct SiteConfig {
    std::string title;
    std::vector<NavEntry> nav;
    std::vector<std::string> stylesheets;
    std::string engineDocs;
};

struct Page {
    std::string id;
    std::string title;
    std::string navTitle;
    DocNode root;
    std::filesystem::path source;
    bool generated = false;
};

struct Site {
    std::filesystem::path packageRoot;
    std::filesystem::path docsRoot;
    slopengine::PackageMeta meta;
    SiteConfig config;
    std::vector<Page> pages;
    std::vector<ShaderDoc> shaders;
    std::unordered_set<std::string> engineNames;

    const Page* findPage(const std::string& id) const;
    const ShaderDoc* findShader(const std::string& path) const;
};

bool loadSite(const std::filesystem::path& packageRoot, Site& out, std::string& error, bool verbose);

}
