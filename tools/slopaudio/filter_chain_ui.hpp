#pragma once

#include "assets/audio_def.hpp"

#include <array>
#include <functional>
#include <string_view>
#include <vector>

namespace slopaudio {

// Index 0 ("none") is a placeholder skipped by the Add Filter popup; the rest match
// AudioWorld::createBuiltinFilter's names.
inline constexpr const char* kFilterNames[] =
    {"none", "echo", "biquad", "eq", "lofi", "flanger", "bassboost", "freeverb", "robotize", "waveshaper", "dcremoval"};

// Builds a fresh instance just to read the filter's own constructor defaults (SoLoud always
// defaults WET to 1 and everything else to whatever the concrete filter's fields say), so a
// newly attached filter starts at sane values instead of all-zero.
std::vector<float> defaultFilterParams(std::string_view name);

using FilterParamChangedFn = std::function<void(unsigned int slot, unsigned int paramIndex, float value)>;

// Draws Add/Remove/Reorder controls plus per-param sliders for a filter chain of up to
// kMaxFiltersPerBus slots, shared by the Bus tab and the definition inspector's per-def
// filter chain. Mutates `slots` in place, including param values (written directly by the
// sliders). Calls `onParamChanged` as the live-audio side effect of a param edit; add/
// remove/reorder are structural and are the caller's job to react to via the return value,
// since bus and def playback apply a structural change very differently.
// Returns true if the chain's structure (which filters are attached, in what order) changed
// this frame -- as opposed to just a param value.
bool drawFilterChain(
    std::array<slopengine::BusFilterSlot, slopengine::kMaxFiltersPerBus>& slots,
    const FilterParamChangedFn& onParamChanged);

}
