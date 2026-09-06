#pragma once

#include "assets/audio_def.hpp"

#include <string>
#include <string_view>

namespace slopengine {

bool parseSaudioAsset(std::string_view source, AudioDef& def);
std::string serializeSaudioAsset(const AudioDef& def);

}
