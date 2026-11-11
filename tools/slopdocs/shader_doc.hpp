#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace slopdocs {

struct ShaderDecl {
    std::string name;
    std::string type;
    std::string qualifier;
    std::string doc;
};

struct ShaderBuffer {
    std::string name;
    std::string binding;
    std::string access;
    std::string doc;
};

struct ShaderDoc {
    std::string path;
    std::filesystem::path file;
    std::string stage;
    std::string version;
    std::string description;
    std::string usedBy;
    std::vector<std::string> see;
    std::vector<std::string> includes;
    std::string localSize;
    std::vector<ShaderDecl> inputs;
    std::vector<ShaderDecl> outputs;
    std::vector<ShaderDecl> uniforms;
    std::vector<ShaderBuffer> buffers;
    std::string source;
};

std::vector<ShaderDoc> scanShaders(const std::filesystem::path& packageRoot);

std::string shaderStageFromPath(const std::string& path);

}
