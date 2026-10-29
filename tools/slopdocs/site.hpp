#pragma once

#include "doc_model.hpp"

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
    DocNode root;
    std::filesystem::path source;
};

struct Site {
    std::filesystem::path packageRoot;
    std::filesystem::path docsRoot;
    slopengine::PackageMeta meta;
    SiteConfig config;
    std::vector<Page> pages;
    std::unordered_set<std::string> engineNames;

    const Page* findPage(const std::string& id) const;
};

bool loadSite(const std::filesystem::path& packageRoot, Site& out, std::string& error, bool verbose);

}
