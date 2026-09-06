#pragma once

#include "assets/audio_def.hpp"

#include <string>
#include <string_view>

namespace slopengine {

bool parseSaudioAsset(std::string_view source, AudioDef& def);
std::string serializeSaudioAsset(const AudioDef& def);

bool parseProceduralAsset(std::string_view source, ProceduralDef& def);
std::string serializeProceduralAsset(const ProceduralDef& def);

bool parseBusConfig(std::string_view source, BusConfig& config);
std::string serializeBusConfig(const BusConfig& config);

}
