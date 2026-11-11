#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace slopdocs {

struct ScriptParam {
    std::string name;
    std::string type;
    std::string doc;
};

struct SigParam {
    std::string name;
    std::string defaultValue;
    bool optional = false;
    bool rest = false;
};

struct ScriptDef {
    std::string name;
    std::string kind;
    std::string signature;
    std::vector<SigParam> sigParams;
    std::string doc;
    std::vector<ScriptParam> params;
    std::string returnType;
    std::string returnDoc;
    std::vector<std::string> see;
    int line = 0;
};

struct ScriptDoc {
    std::string path;
    std::filesystem::path file;
    std::string summary;
    std::vector<std::string> requiredScripts;
    std::vector<std::string> requiresData;
    std::vector<ScriptDef> defs;
    std::string error;
};

std::vector<ScriptDoc> scanScripts(const std::filesystem::path& packageRoot);

}
