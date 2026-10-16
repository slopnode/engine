#pragma once

#include "editor.hpp"
#include "thumb_cache.hpp"

#include "assets/asset_store.hpp"

#include <string>
#include <vector>

namespace slopmat {

struct BrowserEntry {
    std::string virtualPath;
    std::string packageId;
    DocKind kind = DocKind::Material;
};

struct BrowserTreeNode {
    std::string name;
    std::vector<BrowserTreeNode> folders;
    std::vector<const BrowserEntry*> leaves;
};

struct Browser {
    std::vector<BrowserEntry> entries;
    std::vector<std::string> packageOrder;
    std::string filter;
    DocKind activeTab = DocKind::Material;
    bool treeView = true;

    void rescan(const slopengine::AssetStore& assets);
    void draw(Editor& editor, slopengine::AssetStore& assets, ThumbCache& thumbs);
    std::vector<std::string> pathsOfKind(DocKind kind) const;

private:
    std::vector<BrowserTreeNode> packages_;
    std::string treeKey_;
    void rebuildTree();
};

bool containsIgnoreCase(const std::string& haystack, const std::string& needle);

}
