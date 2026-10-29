#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace slopdocs {

struct DataValue {
    enum class Kind { Nil, String, Symbol, Number, Bool, List, Other };
    Kind kind = Kind::Nil;
    std::string text;
    std::vector<DataValue> items;
    bool dotted = false;
};

struct DataDef {
    std::string name;
    std::string doc;
    DataValue value;
    bool evaluated = false;
    int line = 0;
};

struct DataDoc {
    std::string path;
    std::filesystem::path file;
    std::string summary;
    std::vector<DataDef> defs;
    std::string error;
    bool evaluated = false;
};

std::vector<DataDoc> scanData(const std::filesystem::path& packageRoot, bool verbose);

std::string writeDataValue(const DataValue& v);

bool isAlist(const DataValue& v);
bool isCatalog(const DataValue& v);
bool isAlistList(const DataValue& v);

}
