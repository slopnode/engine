#pragma once

#include "editor.hpp"

#include "assets/asset_store.hpp"

#include <string>
#include <vector>

namespace slopengine {
class AudioWorld;
}

namespace slopaudio {

struct AudioBrowserEntry {
    std::string virtualPath;
    std::string packageId;
    SourceKind kind = SourceKind::Definition;
};

struct Browser {
    std::vector<AudioBrowserEntry> entries;
    std::string filter;
    SourceKind activeTab = SourceKind::Definition;

    void rescan(const slopengine::AssetStore& assets);
    void draw(Editor& editor, slopengine::AssetStore& assets, slopengine::AudioWorld& audioWorld);
};

}
